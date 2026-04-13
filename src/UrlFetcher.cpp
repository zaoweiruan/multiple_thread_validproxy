#include "UrlFetcher.h"
#include "ProxyTester.h"
#include <curl/curl.h>

UrlFetcher::UrlFetcher(ProxyTester* tester) : tester_(tester) {}

size_t UrlFetcher::writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string UrlFetcher::fetch(const std::string& url) {
    std::string result;
    
    CURL* curl = curl_easy_init();
    if (!curl) return "";
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
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
    
    CURL* curl = curl_easy_init();
    if (!curl) return "";
    
    std::string proxyUrl = "socks5://127.0.0.1:" + std::to_string(socksPort);
    curl_easy_setopt(curl, CURLOPT_PROXY, proxyUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    return result;
}