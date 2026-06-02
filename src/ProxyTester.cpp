#include "ProxyTester.h"
#include "XrayManager.h"
#include "CurlEasyHandle.h"
#include <chrono>

ProxyTester::ProxyTester(XrayManager* manager, const std::string& testUrl, int timeoutMs)
    : manager_(manager), testUrl_(testUrl), timeoutMs_(timeoutMs) {}

ProxyTester::~ProxyTester() {}

TestResult ProxyTester::test(int socksPort) {
    TestResult result; // test event bridging via mediator can be emitted from here if needed
    result.success = false;
    result.latencyMs = -1;
    result.errorMsg = "";

    try {
        CurlEasyHandle curl;
        std::string proxyUrl = "http://127.0.0.1:" + std::to_string(socksPort);
        
        curl.setProxy(proxyUrl)
            .setUrl(testUrl_)
            .setNoBody(true)
            .setTimeoutMs(timeoutMs_)
            .setFollowLocation(true)
            .perform();

        result.latencyMs = static_cast<long>(curl.getTotalTime() * 1000);
        long responseCode = curl.getResponseCode();
        result.success = (responseCode == 200 || responseCode == 204);

    } catch (const std::exception& e) {
        result.errorMsg = e.what();
    }

    return result;
}

XrayManager* ProxyTester::getManager() const {
    return manager_;
}
