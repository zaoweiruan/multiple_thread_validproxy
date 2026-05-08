#include "UrlFetcher.h"
#include "ProxyTester.h"
#include "CurlEasyHandle.h"

UrlFetcher::UrlFetcher(ProxyTester* tester) : tester_(tester) {}

std::string UrlFetcher::fetch(const std::string& url) {
    std::string result;
    try {
        CurlEasyHandle curl;
        curl.setUrl(url)
            .setWriteCallback(CurlEasyHandle::writeCallback, &result)
            .setFollowLocation(true)
            .setTimeoutMs(10000)
            .setSslVerifyPeer(false)
            .setSslVerifyHost(false)
            .perform();
    } catch (const std::exception& e) {
        return "";
    }
    return result;
}

std::string UrlFetcher::fetchViaProxy(const std::string& url, int socksPort) {
    if (tester_) {
        auto result = tester_->test(socksPort);
        if (!result.success) {
            return "";
        }
    }

    std::string result;
    try {
        CurlEasyHandle curl;
        std::string proxyUrl = "socks5://127.0.0.1:" + std::to_string(socksPort);
        curl.setProxy(proxyUrl)
            .setUrl(url)
            .setWriteCallback(CurlEasyHandle::writeCallback, &result)
            .setFollowLocation(true)
            .setTimeoutMs(10000)
            .setSslVerifyPeer(false)
            .setSslVerifyHost(false)
            .perform();
    } catch (const std::exception& e) {
        return "";
    }
    return result;
}
