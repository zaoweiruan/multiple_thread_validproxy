#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "XrayManager.h"
#include "PortManager.h"
#include "Logger.h"
#include <thread>
#include <chrono>

XrayManager* XrayManager::instance_ = nullptr;
std::mutex XrayManager::instanceMutex_;

/**
 * Poll the API port of a newly-started Xray instance until it accepts
 * connections (bounded up to ~5 s in 100 ms steps).
 * Returns true on readiness, false on timeout.
 */
static bool pollApiPortReady(int apiPort, int timeoutMs = 5000, int stepMs = 100) {
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(static_cast<uint16_t>(apiPort));

    for (int elapsed = 0; elapsed < timeoutMs; elapsed += stepMs) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int64_t>(stepMs)));
            continue;
        }

        int rc = connect(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        closesocket(sock);

        if (rc == 0) {
            return true;
        }

        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
            // WSAECONNREFUSED (10061), WSAETIMEDOUT (10060), or similar
            // — port not yet listening, retry after a short sleep.
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int64_t>(stepMs)));
            continue;
        }

        // Asynchronous in-progress — retry without sleeping.
    }
    return false;
}

XrayManager* XrayManager::getInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    return instance_;
}

XrayManager* XrayManager::getInstance(const std::string& xrayPath, const std::string& configDir, int workers) {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = new XrayManager(xrayPath, configDir, workers);
    }
    return instance_;
}

void XrayManager::release() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (instance_) {
        int count = instance_->getInstanceCount();
        Logger::write("[XrayManager][release] shutting down " + std::to_string(count) + " instance(s)", LogLevel::REPORT);
        instance_->stopAll();
        // release() calls stopAll() then deletes the singleton;
        // the destructor also calls stopAll(), so if already shut down, defensively mark to skip.
        instance_->stopped_ = true;
        delete instance_;
        instance_ = nullptr;
        Logger::write("[XrayManager][release] done", LogLevel::REPORT);
    }
}

XrayManager::XrayManager(const std::string& xrayPath, const std::string& configDir, int workers)
    : xrayPath_(xrayPath), configDir_(configDir), workers_(workers) {}

XrayManager::~XrayManager() {
    if (!stopped_) {
        stopAll();
    }
}

int XrayManager::start(int testCount) {
    return start(testCount, 1080, 10080);
}

int XrayManager::start(int count, int startPort, int apiPort) {
    if (count > workers_) {
        count = workers_;
    }
    
    Logger::write("XrayManager::start: count=" + std::to_string(count) + ", startPort=" + std::to_string(startPort) + ", apiPort=" + std::to_string(apiPort) + ", configDir=" + configDir_, LogLevel::REPORT);
    
    std::vector<std::unique_ptr<XrayInstance>> startedInstances;
    int actualCount = 0;
    
    for (int i = 0; i < count; ++i) {
        // Reuse existing instance at this index if it is already running
        // to avoid unnecessary kill+restart.
        XrayInstance* existing = nullptr;
        {
            std::lock_guard<std::mutex> lock(instancesMutex_);
            if (i < static_cast<int>(instances_.size()) && instances_[i]->isRunning()) {
                existing = instances_[i].get();
            }
        }

        if (existing != nullptr) {
            Logger::write("XrayManager::start: reusing instance " + std::to_string(i)
                          + " (socks=" + std::to_string(existing->getSocksPort())
                          + ", api=" + std::to_string(existing->getApiPort()) + ")", LogLevel::INFO);
            actualCount++;
            continue;
        }

        int socksPort = PortManager::findAvailable(startPort + i, 1000);
        int apiPortAddr = PortManager::findAvailable(apiPort + i, 1000);

        if (socksPort <= 0 || apiPortAddr <= 0) {
            Logger::write("XrayManager: failed to find available ports", LogLevel::ERR);
            break;
        }

        std::unique_ptr<XrayInstance> instance = std::make_unique<XrayInstance>(xrayPath_, socksPort, apiPortAddr, configDir_);
        if (instance->start()) {
            if (!pollApiPortReady(apiPortAddr)) {
                Logger::write("XrayManager: API port " + std::to_string(apiPortAddr)
                              + " not ready within timeout for instance " + std::to_string(i), LogLevel::ERR);
                instance->stop();
                PortManager::freePort(socksPort);
                PortManager::freePort(apiPortAddr);
                break;
            }
            startedInstances.push_back(std::move(instance));
            actualCount++;
        } else {
            Logger::write("XrayManager: failed to start instance " + std::to_string(i), LogLevel::WARN);
            PortManager::freePort(socksPort);
            PortManager::freePort(apiPortAddr);
            break;
        }
    }
    
    // Commit started instances under lock. XrayInstance::start() (2s sleep)
    // runs above without instancesMutex_ held; only the final move-in is locked.
    {
        std::lock_guard<std::mutex> lock(instancesMutex_);
        for (std::unique_ptr<XrayInstance>& inst : startedInstances) {
            instances_.push_back(std::move(inst));
        }
    }
    
    Logger::write("XrayManager::start: actualCount=" + std::to_string(actualCount), LogLevel::REPORT);
    return actualCount;
}

void XrayManager::stopAll() {
    std::lock_guard<std::mutex> lock(instancesMutex_);
    int prevSize = static_cast<int>(instances_.size());
    for (std::unique_ptr<XrayInstance>& inst : instances_) {
        inst->stop();
    }
    instances_.clear();
    PortManager::clearPorts();  // Release ports for reuse on next start
    Logger::write("[XrayManager][stopAll] cleared " + std::to_string(prevSize) + " instance(s)", LogLevel::INFO);
}

bool XrayManager::evaluateInstanceHealth(int apiPort) {
    // Phase 1 — locate the instance bound to apiPort, release its resources
    // (stop the process, free both ports) and remove it from the pool.
    // The heavy stop() call runs OUTSIDE instancesMutex_ to keep the critical
    // section short (stop() waits up to ~1s for process exit).
    int socksPort = -1;
    {
        std::lock_guard<std::mutex> lock(instancesMutex_);
        for (std::vector<std::unique_ptr<XrayInstance>>::iterator it = instances_.begin();
             it != instances_.end(); ++it) {
            if ((*it)->getApiPort() != apiPort) continue;

            socksPort = (*it)->getSocksPort();
            bool alive = (*it)->isRunning();
            if (!alive) {
                Logger::write("[XrayManager][evaluateInstanceHealth] instance (socks=" +
                              std::to_string(socksPort) + ", api=" + std::to_string(apiPort) +
                              ") is DEAD — releasing resources and relaunching with original config", LogLevel::ERR);
            } else {
                Logger::write("[XrayManager][evaluateInstanceHealth] instance (socks=" +
                              std::to_string(socksPort) + ", api=" + std::to_string(apiPort) +
                              ") process alive but API port unresponsive (possible hang) — releasing resources and relaunching with original config", LogLevel::WARN);
            }

            std::unique_ptr<XrayInstance> stale = std::move(*it);
            instances_.erase(it);
            stale->stop();
            PortManager::freePort(socksPort);
            PortManager::freePort(apiPort);
            break;
        }
    }

    if (socksPort < 0) {
        return false;  // no instance bound to apiPort
    }

    // Phase 2 — relaunch with the original config. The same ports yield the
    // same config file path (configDir + "/xray_config_<socksPort>.json"),
    // which XrayInstance::start() regenerates via createConfigFile().
    // start() sleeps ~2s, so it must NOT run under instancesMutex_.
    std::unique_ptr<XrayInstance> replacement =
        std::make_unique<XrayInstance>(xrayPath_, socksPort, apiPort, configDir_);
    if (replacement->start() && pollApiPortReady(apiPort)) {
        Logger::write("[XrayManager][evaluateInstanceHealth] relaunched instance with original config (socks=" +
                      std::to_string(socksPort) + ", api=" + std::to_string(apiPort) + ")", LogLevel::WARN);
        std::lock_guard<std::mutex> lock(instancesMutex_);
        instances_.push_back(std::move(replacement));
        return true;
    }

    Logger::write("[XrayManager][evaluateInstanceHealth] relaunch FAILED (socks=" +
                  std::to_string(socksPort) + ", api=" + std::to_string(apiPort) +
                  ") — ports freed; instance will be re-created on next start()", LogLevel::ERR);
    replacement->stop();
    PortManager::freePort(socksPort);
    PortManager::freePort(apiPort);
    return false;  // relaunch failed — ports freed; re-created on next start()
}

XrayInstance* XrayManager::getInstance(int index) {
    std::lock_guard<std::mutex> lock(instancesMutex_);
    if (index >= 0 && index < static_cast<int>(instances_.size())) {
        return instances_[index].get();
    }
    return nullptr;
}

int XrayManager::getInstanceCount() const {
    std::lock_guard<std::mutex> lock(instancesMutex_);
    return static_cast<int>(instances_.size());
}

std::vector<std::pair<int, int>> XrayManager::getPortPairs() {
    std::lock_guard<std::mutex> lock(instancesMutex_);
    std::vector<std::pair<int, int>> pairs;
    for (std::unique_ptr<XrayInstance>& inst : instances_) {
        pairs.push_back({inst->getSocksPort(), inst->getApiPort()});
    }
    return pairs;
}