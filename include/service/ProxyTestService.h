#ifndef SERVICE_PROXY_TEST_SERVICE_H
#define SERVICE_PROXY_TEST_SERVICE_H

#include <string>
#include <sqlite3.h>
#include <atomic>
#include "ConfigReader.h"
#include "TestResult.h"

namespace service {

class ProxyTestService {
public:
    ProxyTestService(sqlite3* db, const config::AppConfig& config);

    // Core testing operations
    bool runSubscription(const std::string& subId, std::atomic<bool>* cancel = nullptr);
    bool runSingleProxy(const std::string& indexId, std::atomic<bool>* cancel = nullptr);
    bool runAllProxies(std::atomic<bool>* cancel = nullptr);

    // Find operations
    TestResult findFirstProxy(std::atomic<bool>* cancel = nullptr);
    TestResult findBestProxy(std::atomic<bool>* cancel = nullptr);

private:
    sqlite3* db_;
    config::AppConfig config_;
};

} // namespace service

#endif // SERVICE_PROXY_TEST_SERVICE_H