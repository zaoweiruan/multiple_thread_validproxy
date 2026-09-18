#include "ProxyHealthEvaluator.h"
#include "ProxyProbePool.h"
#include "CurlEasyHandle.h"

#include <curl/curl.h>
#include <stdexcept>

namespace proxy {

ProxyHealthEvaluator::ProxyHealthEvaluator() {
}

ProxyHealthEvaluator::~ProxyHealthEvaluator() {
}

void ProxyHealthEvaluator::setProbePool(ProxyProbePool* pool) {
    probePool_ = pool;
}

std::string ProxyHealthEvaluator::proxyUrlFor(const MemberProbeTarget& t) {
    std::string cred;
    if (!t.username.empty() && !t.password.empty()) {
        cred = t.username + ":" + t.password + "@";
    } else if (!t.username.empty()) {
        cred = t.username + "@";
    }
    if (t.configtype == 4) {  // SOCKS (mirrors SOCKSOutboundBuilder)
        return "socks5h://" + cred + t.address + ":" + t.port;
    } else if (t.configtype == 10) {  // HTTP (mirrors HTTPOutboundBuilder)
        return "http://" + cred + t.address + ":" + t.port;
    }
    return "";
}

MemberHealth ProxyHealthEvaluator::probeSocksHttp(const MemberProbeTarget& t,
                                                  const std::string& testUrl,
                                                  long connectTimeoutMs,
                                                  long totalTimeoutMs) {
    MemberHealth h;
    h.tag = t.tag;
    h.tested = true;

    std::string proxyUrl = proxyUrlFor(t);
    if (proxyUrl.empty()) {
        h.alive = false;
        h.lastError = "unsupported protocol for direct probe";
        return h;
    }

    try {
        CurlEasyHandle curl;
        curl.setProxy(proxyUrl)
            .setUrl(testUrl)
            .setTimeoutMs(totalTimeoutMs)
            .setConnectTimeoutMs(connectTimeoutMs)
            .setSslVerifyPeer(false)
            .setSslVerifyHost(false)
            .setNoBody(true)        // HEAD: we judge by status, not body
            .setFollowLocation(true);
        curl.perform();
        double total = curl.getTotalTime();
        long code = curl.getResponseCode();
        h.delayMs = static_cast<long long>(total * 1000.0);
        h.alive = (code >= 200 && code < 400);
        if (!h.alive) {
            h.lastError = "http status " + std::to_string(code);
        }
    } catch (const std::exception& e) {
        h.alive = false;
        h.delayMs = -1;
        h.lastError = e.what();
    }
    return h;
}

std::vector<MemberHealth> ProxyHealthEvaluator::probe(const std::vector<MemberProbeTarget>& targets,
                                                      const std::string& testUrl,
                                                      long connectTimeoutMs,
                                                      long totalTimeoutMs,
                                                      std::atomic<bool>* stopFlag) {
    std::vector<MemberHealth> result;
    result.reserve(targets.size());
    size_t i = 0;
    for (; i < targets.size(); ++i) {
        const MemberProbeTarget& t = targets[i];
        // Cancellation: stopFlag 生效时立即跳出主循环，跳到尾部补占位。
        // 2026-09-18 Spec §3.1：让 evaluator 的 join() 不再阻塞 30-120s。
        if (stopFlag != nullptr && stopFlag->load(std::memory_order_acquire)) {
            break;
        }
        if (t.configtype == 4 || t.configtype == 10) {
            result.push_back(probeSocksHttp(t, testUrl, connectTimeoutMs, totalTimeoutMs));
        } else {
            // Protocols that require xray to speak the wire protocol cannot be
            // measured with a bare cURL proxy URL. When a resident probe pool
            // is attached and running, route the member to its xray workers;
            // otherwise leave tested=false so the pool does not overwrite the
            // member's state with a false negative.
            MemberHealth h;
            h.tag = t.tag;
            h.tested = false;
            if (probePool_ != nullptr && probePool_->isRunning()) {
                bool ok = probePool_->probeMember(t, testUrl, connectTimeoutMs,
                                                  totalTimeoutMs, h);
                if (!ok) {
                    h.tested = false;
                    h.lastError = "xray probe worker unavailable";
                }
            } else {
                h.lastError = "protocol requires xray-side probe (not directly probeable)";
            }
            result.push_back(h);
        }
    }
    // 补占位：stopFlag 触发后，未探测的 target 保持 API 契约（一个 MemberHealth
    // 对应一个 target）。占位结果 tested=false，mergeHealth 不会写入 lastDelayMs/
    // lastAlive/failStreak，因此未探测的成员保持上一次 probe 的状态。
    for (; i < targets.size(); ++i) {
        MemberHealth h;
        h.tag = targets[i].tag;
        h.tested = false;
        h.alive = false;
        h.delayMs = -1;
        h.lastError = "probe interrupted by stop request";
        result.push_back(h);
    }
    return result;
}

} // namespace proxy
