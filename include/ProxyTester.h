#ifndef PROXY_TESTER_H
#define PROXY_TESTER_H

#include <string>
#include <curl/curl.h>

class XrayManager;

struct TestResult {
    bool success;
    long latencyMs;
    std::string errorMsg;
};

class ProxyTester {
public:
    ProxyTester(XrayManager* manager, const std::string& testUrl, int timeoutMs);
    ~ProxyTester();
    
    TestResult test(int socksPort);
    XrayManager* getManager() const;

private:
    XrayManager* manager_;
    std::string testUrl_;
    int timeoutMs_;
};

#endif // PROXY_TESTER_H