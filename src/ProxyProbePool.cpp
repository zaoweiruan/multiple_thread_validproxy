#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <boost/json.hpp>
#include <chrono>
#include <thread>

#include "ProxyProbePool.h"
#include "CurlEasyHandle.h"
#include "Logger.h"
#include "PortManager.h"
#include "config/OutboundBuilderFactory.h"

namespace {

// Bounded, crash-aware readiness check copied from XrayManager.cpp (file-local
// static there, so duplicated here). Succeeds as soon as the gRPC API port
// accepts a TCP connection; fails immediately if the process already exited.
constexpr int PROBE_INSTANCE_TIMEOUT_MS = 5000;
constexpr int PROBE_INSTANCE_STEP_MS = 100;

bool waitInstanceReady(int apiPort) {
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(static_cast<uint16_t>(apiPort));

    for (int elapsed = 0; elapsed < PROBE_INSTANCE_TIMEOUT_MS;
         elapsed += PROBE_INSTANCE_STEP_MS) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock != INVALID_SOCKET) {
            int rc = connect(sock, reinterpret_cast<const sockaddr*>(&addr),
                             sizeof(addr));
            closesocket(sock);
            if (rc == 0) {
                return true;
            }
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(PROBE_INSTANCE_STEP_MS));
    }
    return false;
}

constexpr int PROBE_INJECT_RETRIES = 3;

// Transient gRPC errors worth retrying when injecting an outbound (mirrors
// ProxyBatchTester's worker retry table).
bool isTransientInjectError(const std::string& msg) {
    static const char* keywords[] = {
        "not ready", "not found", "unavailable", "connection refused",
        "timed out", "refused", "network unreachable", "host unreachable",
        "WSA10060", "WSA10061", "WSA10051", "WSA10056"};
    for (const char* kw : keywords) {
        if (msg.find(kw) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace proxy {

ProxyProbePool::ProxyProbePool(const std::string& xrayPath, int workerCount,
                               const std::string& configDir)
    : xrayPath_(xrayPath),
      configDir_(configDir),
      workerCount_(workerCount),
      running_(false) {}

ProxyProbePool::~ProxyProbePool() {
    stop();
}

bool ProxyProbePool::start() {
    if (running_.load()) {
        return true;
    }
    if (workerCount_ <= 0) {
        Logger::write("[ProxyProbePool] workerCount <= 0, probe pool disabled",
                      LogLevel::WARN);
        return false;
    }

    int started = 0;
    for (int i = 0; i < workerCount_; ++i) {
        int socksPort = PortManager::findAvailable(20000 + i * 2, 1000);
        if (socksPort <= 0) {
            Logger::write("[ProxyProbePool] failed to allocate socks port for worker "
                          + std::to_string(i), LogLevel::ERR);
            break;
        }
        int apiPort = PortManager::findAvailable(20001 + i * 2, 1000);
        if (apiPort <= 0) {
            PortManager::freePort(socksPort);
            Logger::write("[ProxyProbePool] failed to allocate api port for worker "
                          + std::to_string(i), LogLevel::ERR);
            break;
        }
        if (apiPort == socksPort) {
            PortManager::freePort(apiPort);
            apiPort = PortManager::findAvailable(socksPort + 1, 1000);
            if (apiPort <= 0) {
                PortManager::freePort(socksPort);
                Logger::write("[ProxyProbePool] failed to reallocate api port for worker "
                              + std::to_string(i), LogLevel::ERR);
                break;
            }
        }

        std::shared_ptr<XrayInstance> inst =
            std::make_shared<XrayInstance>(xrayPath_, socksPort, apiPort, configDir_);
        // No setExplicitConfig: the default config template is the probe-worker
        // config (socks-in mixed + api + direct/proxy outbounds + TCP->proxy).
        if (!inst->start()) {
            Logger::write("[ProxyProbePool] worker " + std::to_string(i)
                          + " xray start failed", LogLevel::ERR);
            inst->stop();
            PortManager::freePort(socksPort);
            PortManager::freePort(apiPort);
            break;
        }
        if (!waitInstanceReady(apiPort)) {
            Logger::write("[ProxyProbePool] worker " + std::to_string(i)
                          + " api port not ready", LogLevel::ERR);
            inst->stop();
            PortManager::freePort(socksPort);
            PortManager::freePort(apiPort);
            break;
        }

        Worker w;
        w.instance = inst;
        w.socksPort = socksPort;
        w.apiPort = apiPort;
        w.busy = false;
        w.api.reset(new xray::XrayApi(xrayPath_, "127.0.0.1:" + std::to_string(apiPort)));
        {
            std::lock_guard<std::mutex> lock(workersMutex_);
            workers_.push_back(std::move(w));
        }
        ++started;
    }

    if (started == 0) {
        Logger::write("[ProxyProbePool] no probe worker started", LogLevel::ERR);
        return false;
    }
    running_.store(true);
    Logger::write("[ProxyProbePool] started " + std::to_string(started)
                  + " probe worker(s)", LogLevel::INFO);
    return true;
}

void ProxyProbePool::stop() {
    if (!running_.load() && workers_.empty()) {
        return;
    }
    running_.store(false);
    std::vector<Worker> workers;
    {
        std::lock_guard<std::mutex> lock(workersMutex_);
        workers.swap(workers_);
    }
    for (Worker& w : workers) {
        if (w.instance) {
            w.instance->stop();
        }
        PortManager::freePort(w.socksPort);
        PortManager::freePort(w.apiPort);
    }
    Logger::write("[ProxyProbePool] stopped " + std::to_string(workers.size())
                  + " probe worker(s)", LogLevel::INFO);
}

bool ProxyProbePool::isRunning() const {
    return running_.load();
}

int ProxyProbePool::size() const {
    std::lock_guard<std::mutex> lock(workersMutex_);
    return static_cast<int>(workers_.size());
}

int ProxyProbePool::acquireWorker() {
    std::lock_guard<std::mutex> lock(workersMutex_);
    for (size_t i = 0; i < workers_.size(); ++i) {
        if (!workers_[i].busy) {
            workers_[i].busy = true;
            return static_cast<int>(i);
        }
    }
    return -1;
}

void ProxyProbePool::releaseWorker(int index) {
    std::lock_guard<std::mutex> lock(workersMutex_);
    if (index >= 0 && index < static_cast<int>(workers_.size())) {
        workers_[index].busy = false;
    }
}

bool ProxyProbePool::probeMember(const MemberProbeTarget& target,
                                 const std::string& testUrl,
                                 long connectTimeoutMs,
                                 long totalTimeoutMs,
                                 MemberHealth& out) {
    if (!running_.load()) {
        return false;
    }
    int idx = acquireWorker();
    if (idx < 0) {
        return false;
    }

    // Hold the lock for the whole probe so stop() cannot destroy the worker's
    // api while we are injecting/removing outbounds.
    std::lock_guard<std::mutex> lock(workersMutex_);
    if (idx >= static_cast<int>(workers_.size())) {
        return false;
    }
    Worker& w = workers_[idx];
    int socksPort = w.socksPort;
    xray::XrayApi* api = w.api.get();
    if (api == nullptr) {
        w.busy = false;
        return false;
    }

    out.tag = target.tag;
    out.tested = false;
    out.alive = false;
    out.delayMs = -1;
    out.lastError.clear();

    // Clean any leftover outbound from a previous probe.
    std::string ignore;
    api->removeOutboundDirect("proxy");

    // Build and inject the member's outbound.
    config::OutboundBuilderFactory factory;
    boost::json::object ob = factory.create(target.profile, "proxy");
    ob["tag"] = "proxy";
    boost::json::object root;
    root["outbounds"] = boost::json::array{ob};
    std::string json = boost::json::serialize(root);

    bool injected = false;
    std::string injectError;
    for (int attempt = 1; attempt <= PROBE_INJECT_RETRIES; ++attempt) {
        std::string resultOut;
        if (api->addOutboundDirect(json, "proxy", resultOut)) {
            injected = true;
            break;
        }
        injectError = api->getLastError();
        if (attempt < PROBE_INJECT_RETRIES && isTransientInjectError(injectError)) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(200 * attempt));
        } else {
            break;
        }
    }
    if (!injected) {
        // Fall back to the subprocess-based addOutbound.
        std::string resultOut;
        if (api->addOutbound(json, "proxy", resultOut)) {
            injected = true;
        } else {
            injectError = api->getLastError();
        }
    }
    if (!injected) {
        out.lastError = "inject failed: " + injectError;
        api->removeOutboundDirect("proxy");
        w.busy = false;
        return true; // probe flow executed; member not tested
    }

    // Probe through the worker's local socks port.
    try {
        CurlEasyHandle curl;
        curl.setProxy("http://127.0.0.1:" + std::to_string(socksPort));
        curl.setUrl(testUrl);
        curl.setTimeoutMs(totalTimeoutMs);
        curl.setConnectTimeoutMs(connectTimeoutMs);
        curl.setSslVerifyPeer(false);
        curl.setSslVerifyHost(false);
        curl.setNoBody(true);
        curl.setFollowLocation(true);
        curl.perform();
        long code = curl.getResponseCode();
        out.tested = true;
        out.delayMs = static_cast<long long>(curl.getTotalTime() * 1000.0);
        out.alive = (code >= 200 && code < 400);
        if (!out.alive) {
            out.lastError = "http status " + std::to_string(code);
        }
    } catch (const std::exception& e) {
        out.tested = true;
        out.alive = false;
        out.delayMs = -1;
        out.lastError = e.what();
    }

    api->removeOutboundDirect("proxy");
    w.busy = false;
    return true;
}

} // namespace proxy