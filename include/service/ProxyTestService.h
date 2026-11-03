#ifndef SERVICE_PROXY_TEST_SERVICE_H
#define SERVICE_PROXY_TEST_SERVICE_H

#include <string>
#include <sqlite3.h>
#include <atomic>
#include "ConfigReader.h"
#include "TestResult.h"
#include "Profileitem.h"

// Forward declaration
class NetworkMonitor;

namespace service {

class ProxyTestService {
public:
    ProxyTestService(sqlite3* db, const config::AppConfig& config, const ::NetworkMonitor* netMon = nullptr);

    // Core testing operations
    bool runSubscription(const std::string& subId, std::atomic<bool>* cancel = nullptr);
    bool runSingleProxy(const std::string& indexId, std::atomic<bool>* cancel = nullptr);
    bool runAllProxies(std::atomic<bool>* cancel = nullptr);

    // Load proxies from DB (for fine-grained splitting)
    std::vector<db::models::Profileitem> loadProxies();

    // Find operations
    TestResult findFirstProxy(std::atomic<bool>* cancel = nullptr);
    TestResult findBestProxy(std::atomic<bool>* cancel = nullptr);

    TestResult getLastResult() const { return lastResult_; }
    int getTotalProxies() const { return lastTotalProxies_; }

private:
    sqlite3* db_;
    config::AppConfig config_;
    const ::NetworkMonitor* netMon_;
    TestResult lastResult_;
    int lastTotalProxies_ = 0;
};

} // namespace service

#endif // SERVICE_PROXY_TEST_SERVICE_H