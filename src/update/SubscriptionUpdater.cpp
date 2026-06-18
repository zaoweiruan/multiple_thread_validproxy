#include "update/SubscriptionUpdater.h"
#include "update/SubscriptionParser.h"
#include "Utils.h"
#include "Logger.h"
#include "CurlEasyHandle.h"

namespace update {

SubscriptionUpdater::SubscriptionUpdater(sqlite3* db,
                                      const std::string& xrayPath,
                                      const config::AppConfig& config,
                                      std::atomic<bool>* externalCancel,
                                      const NetworkMonitor* netMon)
    : db_(db), xrayPath_(xrayPath), config_(config), externalCancel_(externalCancel), netMon_(netMon) {
}

std::vector<SubscriptionUpdater::UpdateMethod> SubscriptionUpdater::parseUpdateMethods(const std::vector<std::string>& methods) {
    std::vector<UpdateMethod> result;
    for (const std::string& m : methods) {
        if (m == "accelerator" && std::find(result.begin(), result.end(), UpdateMethod::Accelerator) == result.end()) {
            result.push_back(UpdateMethod::Accelerator);
        } else if (m == "proxy" && std::find(result.begin(), result.end(), UpdateMethod::Proxy) == result.end()) {
            result.push_back(UpdateMethod::Proxy);
        } else if (m == "direct" && std::find(result.begin(), result.end(), UpdateMethod::Direct) == result.end()) {
            result.push_back(UpdateMethod::Direct);
        }
    }
    if (result.empty()) {
        result.push_back(UpdateMethod::Accelerator);
    }
    return result;
}

std::string SubscriptionUpdater::fetchUrlViaAccelerator(const std::string& url) {
    if (config_.accelerator_url.empty()) {
        Logger::write("accelerator_url empty, falling back to direct fetch", LogLevel::ERR);
        return fetchUrl(url);
    }
    std::string joinedUrl = utils::joinUrl(config_.accelerator_url, url);
    Logger::write("INFO: Fetching via accelerator: " + joinedUrl, LogLevel::INFO);
    return fetchUrl(joinedUrl);
}

std::string SubscriptionUpdater::fetchUrl(const std::string& url) {
    try {
        std::string response;
        CurlEasyHandle curl;
        curl.setUrl(url)
            .setWriteCallback(CurlEasyHandle::writeCallback, &response)
            .setFollowLocation()
            .setConnectTimeoutMs(config_.subscription_connect_timeout_ms)
            .setTimeoutMs(config_.subscription_timeout_ms)
            .setSslVerifyPeer(false)
            .setSslVerifyHost(false);

        curl.perform();
        return response;

    } catch (const std::exception& e) {
        Logger::write("fetchUrl failed - " + std::string(e.what()), LogLevel::ERR);
        return "";
    }
}

std::string SubscriptionUpdater::fetchUrlViaProxy(const std::string& url, int socksPort) {
    try {
        std::string response;
        CurlEasyHandle curl;
        std::string proxyStr = "socks5h://127.0.0.1:" + std::to_string(socksPort);
        
        curl.setProxy(proxyStr)
            .setUrl(url)
            .setWriteCallback(CurlEasyHandle::writeCallback, &response)
            .setFollowLocation()
            .setConnectTimeoutMs(config_.subscription_connect_timeout_ms)
            .setTimeoutMs(config_.subscription_timeout_ms)
            .setSslVerifyPeer(false)
            .setSslVerifyHost(false);

        curl.perform();
        return response;

    } catch (const std::exception& e) {
        Logger::write("fetchUrlViaProxy failed - " + std::string(e.what()), LogLevel::INFO);
        return "";
    }
}

} // namespace update