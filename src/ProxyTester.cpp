#include "ProxyTester.h"
#include "XrayManager.h"
#include <chrono>

ProxyTester::ProxyTester(XrayManager* manager, const std::string& testUrl, int timeoutMs)
    : manager_(manager), testUrl_(testUrl), timeoutMs_(timeoutMs) {}

ProxyTester::~ProxyTester() {}

TestResult ProxyTester::test(int socksPort) {
    TestResult result;
    result.success = false;
    result.latencyMs = -1;
    result.errorMsg = "";
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.errorMsg = "curl_init_failed";
        return result;
    }
    
    std::string proxyUrl = "http://127.0.0.1:" + std::to_string(socksPort);
    curl_easy_setopt(curl, CURLOPT_PROXY, proxyUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, testUrl_.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs_);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    
    double totalTime = 0;
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &totalTime);
    result.latencyMs = static_cast<long>(totalTime * 1000);
    
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        result.errorMsg = curl_easy_strerror(res);
    } else if (responseCode != 200 && responseCode != 204) {
        result.errorMsg = "http_" + std::to_string(responseCode);
    }
    
    result.success = (res == CURLE_OK && (responseCode == 200 || responseCode == 204));
    return result;
}

XrayManager* ProxyTester::getManager() const {
    return manager_;
}