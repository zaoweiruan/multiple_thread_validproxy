#ifndef URL_FETCHER_H
#define URL_FETCHER_H

#include <optional>
#include <string>
#include <curl/curl.h>

class ProxyTester;

class UrlFetcher {
public:
    UrlFetcher(ProxyTester* tester = nullptr);
    ~UrlFetcher();
    
    std::optional<std::string> fetch(const std::string& url, int connectTimeoutMs = 10000, int totalTimeoutMs = 10000);
    std::optional<std::string> fetchViaProxy(const std::string& url, int socksPort, int connectTimeoutMs = 10000, int totalTimeoutMs = 10000);
    // Same proxy path as fetchViaProxy but returns only the HTTP status code
    // (no body download). Used by ConnectivityVerifier so that success is
    // judged by status (200/204) instead of a non-empty body: the configured
    // test URL (https://www.google.com/generate_204) answers 204 No Content.
    std::optional<long> fetchViaProxyStatus(const std::string& url, int socksPort, int connectTimeoutMs = 10000, int totalTimeoutMs = 10000);
    
private:
    ProxyTester* tester_;
    
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
};

#endif // URL_FETCHER_H