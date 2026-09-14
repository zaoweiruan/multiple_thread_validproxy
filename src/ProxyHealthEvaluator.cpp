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
                                                      long totalTimeoutMs) {
    std::vector<MemberHealth> result;
    result.reserve(targets.size());
    for (const auto& t : targets) {
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
    return result;
}

} // namespace proxy
