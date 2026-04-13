#ifndef PROXY_BATCH_TESTER_H
#define PROXY_BATCH_TESTER_H

#include <string>
#include <vector>
#include <sqlite3.h>
#include "Profileitem.h"
#include "Profileexitem.h"
#include "ConfigReader.h"
#include "XrayManager.h"
#include "ProxyTester.h"

class ProxyBatchTester {
public:
    ProxyBatchTester(sqlite3* db, const config::AppConfig& config);
    ~ProxyBatchTester();
    
    bool run();
    bool runWithSubId(const std::string& subId);

private:
    std::vector<db::models::Profileitem> loadProxies(const std::string& subId = "");
    int calculateXrayInstanceCount(int proxyCount);
    bool startXrayInstances(int count);
    void testProxiesMultiThreaded();
    void printSummary();
    
    sqlite3* db_;
    config::AppConfig config_;
    XrayManager* xrayManager_;
    ProxyTester* proxyTester_;
    int totalProxies_;
    int successCount_;
    int failedCount_;
};

#endif // PROXY_BATCH_TESTER_H