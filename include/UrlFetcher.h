#ifndef URL_FETCHER_H
#define URL_FETCHER_H

#include <string>

class ProxyTester;

class UrlFetcher {
public:
    UrlFetcher(ProxyTester* tester);
    
    std::string fetch(const std::string& url);
    std::string fetchViaProxy(const std::string& url, int socksPort);

private:
    ProxyTester* tester_;
    
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
};

#endif // URL_FETCHER_H