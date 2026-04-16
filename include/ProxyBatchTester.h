#ifndef PROXY_BATCH_TESTER_H
#define PROXY_BATCH_TESTER_H

#include <string>
#include <vector>
#include <sqlite3.h>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include "Profileitem.h"
#include "Profileexitem.h"
#include "ConfigReader.h"
#include "XrayManager.h"
#include "ProxyTester.h"
#include "XrayApi.h"
#include "ConfigGenerator.h"

class ProxyBatchTester {
public:
    ProxyBatchTester(sqlite3* db, const config::AppConfig& config, const std::string& baseDir = "", std::ofstream* logOut = nullptr);
    ~ProxyBatchTester();
    
    bool run();
    bool runWithSubId(const std::string& subId);
    XrayManager* getXrayManager() { return xrayManager_; }

private:
    std::vector<db::models::Profileitem> loadProxies(const std::string& subId = "");
    int calculateXrayInstanceCount(int proxyCount);
    bool startXrayInstances(int count);
    void testProxiesMultiThreaded();
    void printSummary();
    
    void workerThreadFunc(int workerId, int socksPort, int apiPort);
    void writeLog(const std::string& msg);
    void logToConsole(const std::string& msg);
    
    sqlite3* db_;
    config::AppConfig config_;
    XrayManager* xrayManager_;
    ProxyTester* proxyTester_;
    std::ofstream* logOut_;
    int totalProxies_;
    int successCount_;
    int failedCount_;
    std::vector<db::models::Profileitem> proxies_;
    std::queue<int> proxiesQueue_;
    std::mutex queueMutex_;
    std::atomic<int> processedCount_;
};

#endif // PROXY_BATCH_TESTER_H