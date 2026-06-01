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
#include "ProfileExItem.h"
#include "ConfigReader.h"
#include "XrayManager.h"
#include "ProxyTester.h"
#include "XrayApi.h"
#include "ConfigGenerator.h"

class ProxyBatchTester {
public:
    ProxyBatchTester(sqlite3* db, const config::AppConfig& config, const std::string& baseDir = "",
                     std::atomic<bool>* externalCancel = nullptr);
    ~ProxyBatchTester();

    bool run();
    bool runWithSubId(const std::string& subId);
    bool runWithIndexId(const std::string& indexId);
    XrayManager* getXrayManager() { return xrayManager_; }
    TestResult getLastResult() const { return lastResult_; }

    // Cancel support
    void cancel() { cancelRequested_ = true; }
    bool isCancelled() const {
        if (externalCancel_ && externalCancel_->load()) return true;
        return cancelRequested_.load();
    }

 private:

    std::vector<db::models::Profileitem> loadProxies(const std::string& subId = "");
    int calculateXrayInstanceCount(int proxyCount);
    bool startXrayInstances(int count);
    void testProxiesMultiThreaded();
    void printSummary();

    void workerThreadFunc(int workerId, int socksPort, int apiPort);
    void logToConsole(const std::string& msg);

    sqlite3* db_;
    config::AppConfig config_;
    XrayManager* xrayManager_;
    ProxyTester* proxyTester_;
    int totalProxies_;
    int successCount_;
    int failedCount_;
    std::vector<db::models::Profileitem> proxies_;
    std::queue<int> proxiesQueue_;
    std::mutex queueMutex_;
    std::atomic<int> processedCount_;
    std::atomic<bool> cancelRequested_{false};

    TestResult lastResult_;
    std::string lastIndexId_;
    std::atomic<bool>* externalCancel_{nullptr};
    std::vector<std::thread> workerThreads_;
};

#endif // PROXY_BATCH_TESTER_H
