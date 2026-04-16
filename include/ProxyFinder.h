#ifndef PROXY_FINDER_H
#define PROXY_FINDER_H

#include <string>
#include <vector>
#include <utility>
#include <sqlite3.h>
#include <curl/curl.h>
#include <iostream>

class XrayManager;
class ConfigGenerator;

class ProxyFinder {
public:
    ProxyFinder(sqlite3* db, XrayManager* manager, const std::string& xrayPath, 
                const std::string& testUrl = "https://www.google.com/generate_204", 
                const std::string& targetUrl = "",
                int timeoutMs = 5000, std::ostream* logOut = nullptr);
    ~ProxyFinder();
    
    std::pair<int, int> findFirstWorkingProxy(const std::string& targetUrl = "");
    std::pair<int, int> findWorkingProxy(const std::string& targetUrl = "");
    void release();
    
    struct TestResult {
        bool success;
        long latencyMs;
        std::string errorMsg;
        std::string indexId;
        std::string address;
        int port;
        int delay;
    };
    
    TestResult getLastResult() const { return lastResult_; }
    
    TestResult testProxyConnectivity(int socksPort, const std::string& targetUrl = "");

struct FallbackProxy {
    std::string indexId;
    std::string address;
    int socksPort;
    int delay;
};

private:
    std::vector<FallbackProxy> loadFallbackProxies(int maxCount = 100);
    bool injectProxyToXray(const std::string& indexId);
    void removeProxyFromXray();
    void log(const std::string& msg);
    
    sqlite3* db_;
    XrayManager* manager_;
    std::string xrayPath_;
    std::string testUrl_;
    std::string targetUrl_;
    int timeoutMs_;
    int currentSocksPort_;
    int currentApiPort_;
    TestResult lastResult_;
    std::ostream* logOut_;
};

#endif // PROXY_FINDER_H