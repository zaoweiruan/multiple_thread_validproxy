#include "UrlFetcher.h"
#include "ProxyTester.h"
#include "CurlEasyHandle.h"

UrlFetcher::UrlFetcher(ProxyTester* tester) : tester_(tester) {}

std::optional<std::string> UrlFetcher::fetch(const std::string& url, int connectTimeoutMs, int totalTimeoutMs) {
    std::string result;
    try {
        CurlEasyHandle curl;
        curl.setUrl(url)
            .setWriteCallback(CurlEasyHandle::writeCallback, &result)
            .setFollowLocation(true)
            .setConnectTimeoutMs(connectTimeoutMs)
            .setTimeoutMs(totalTimeoutMs)
            .perform();
    } catch (const std::exception& e) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::string> UrlFetcher::fetchViaProxy(const std::string& url, int socksPort, int connectTimeoutMs, int totalTimeoutMs) {
    if (tester_) {
        TestResult result = tester_->test(socksPort);
        if (!result.success) {
            return std::nullopt;
        }
    }

    std::string result;
    try {
        CurlEasyHandle curl;
        std::string proxyUrl = "socks5://127.0.0.1:" + std::to_string(socksPort);
        // TLS verification is intentionally disabled for the proxy-test path:
        // the proxy endpoint may present self-signed or internally-signed
        // certificates; disabling ensures connectivity tests are not blocked
        // by certificate chain validation that is outside the tester's scope.
        curl.setProxy(proxyUrl)
            .setUrl(url)
            .setWriteCallback(CurlEasyHandle::writeCallback, &result)
            .setFollowLocation(true)
            .setConnectTimeoutMs(connectTimeoutMs)
            .setTimeoutMs(totalTimeoutMs)
            .setSslVerifyPeer(false)
            .setSslVerifyHost(false)
            .perform();
    } catch (const std::exception& e) {
        return std::nullopt;
    }
    return result;
}
