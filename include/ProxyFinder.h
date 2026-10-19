#ifndef PROXY_FINDER_H
#define PROXY_FINDER_H

#include <string>
#include <vector>
#include <utility>
#include <atomic>
#include <sqlite3.h>

#include "TestResult.h"

// Forward declaration only: networkDown() is defined in the .cpp so this header
// stays free of a NetworkMonitor link dependency for every consumer.
class NetworkMonitor;

class XrayManager;

class ProxyFinder {
public:
    ProxyFinder(sqlite3* db, XrayManager* manager, const std::string& xrayPath, 
                const std::string& testUrl = "https://www.google.com/generate_204", 
                const std::string& targetUrl = "",
                int timeoutMs = 5000,
                std::atomic<bool>* cancelFlag = nullptr,
                const NetworkMonitor* netMon = nullptr);
    ~ProxyFinder();
    
    bool isCancelled() const {
        return cancelRequested_ && cancelRequested_->load();
    }

    /// F7 (2026-10-19): true when a NetworkMonitor was supplied and reports the
    /// network as down. Null monitor (CLI/tests without one) means "never down",
    /// preserving the pre-F7 behaviour where these paths had no monitor at all.
    /// Defined in the .cpp.
    bool networkDown() const;

    std::pair<int, int> findFirstWorkingProxy(const std::string& targetUrl = "");
    std::pair<int, int> findWorkingProxy(const std::string& targetUrl = "");
    void release();
    
    TestResult getLastResult() const { return lastResult_; }

    TestResult testProxyConnectivity(int socksPort, const std::string& targetUrl = "");

struct FallbackProxy {
    std::string indexId;
    std::string address;
    int socksPort;
    int delay;
    std::string configType;
};

private:
    std::vector<FallbackProxy> loadFallbackProxies(int maxCount = -1);
    bool injectProxyToXray(const std::string& indexId);
    
    sqlite3* db_;
    XrayManager* manager_;
    std::string xrayPath_;
    std::string testUrl_;
    std::string targetUrl_;
    int timeoutMs_;
    int currentSocksPort_;
    int currentApiPort_;
    TestResult lastResult_;
    std::atomic<bool>* cancelRequested_{nullptr};
    // F7 (2026-10-19): the probe-on-disconnect cancel path was removed from
    // NetworkMonitor, so this class can no longer rely on the application cancel
    // flag being raised on a network outage. It checks the monitor directly.
    const NetworkMonitor* netMon_{nullptr};
};

#endif // PROXY_FINDER_H