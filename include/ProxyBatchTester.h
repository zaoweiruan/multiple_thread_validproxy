#ifndef PROXY_BATCH_TESTER_H
#define PROXY_BATCH_TESTER_H

#include <string>
#include <vector>
#include <sqlite3.h>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <memory>
#include "Profileitem.h"
#include "ProfileExItem.h"
#include "ConfigReader.h"
#include "XrayManager.h"
#include "ProxyTester.h"
#include "XrayApi.h"
#include "ConfigGenerator.h"
#include "TestResultQueue.h"

class NetworkMonitor;

class ProxyBatchTester {
public:
    ProxyBatchTester(sqlite3* db, const config::AppConfig& config, const std::string& baseDir = "",
                     std::atomic<bool>* externalCancel = nullptr, const NetworkMonitor* netMon = nullptr);
    ~ProxyBatchTester();

    bool run();
    bool runWithSubId(const std::string& subId);
    bool runWithIndexId(const std::string& indexId);
    XrayManager* getXrayManager() { return xrayManager_; }
    TestResult getLastResult() const;

    int getTotalProxies() const { return totalProxies_; }

    // Cancel support
    void cancel() {
        cancelRequested_ = true;
        cancelCv_.notify_all();
    }
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

    bool preGenerateConfigs(int count);
    bool runInternal();

    /// Wait for network recovery or cancellation.
    /// Returns true if network is connected (resume testing), false if cancelled (exit).
    bool waitForNetworkRecovery();

    /// Interruptible sleep: waits up to totalMs in small steps, returning early
    /// when cancellation is requested. Returns true if cancelled.
    bool waitInterruptible(int totalMs);

    /// E13: Teardown a worker's lastResult_ after a non-success path.
    void teardownWorkerResult(int workerId, const std::string& errorMsg);

    sqlite3* db_;
    config::AppConfig config_;
    XrayManager* xrayManager_;
    std::shared_ptr<ProxyTester> proxyTester_;
    int totalProxies_;
    std::atomic<int> processedCount_{0};
    std::atomic<int> successCount_{0};
    std::atomic<int> failedCount_{0};
    std::vector<db::models::Profileitem> proxies_;
    std::queue<int> proxiesQueue_;
    std::mutex queueMutex_;
    std::atomic<bool> cancelRequested_{false};

    TestResult lastResult_;
    std::string lastIndexId_;
    std::atomic<bool>* externalCancel_{nullptr};
    std::vector<std::thread> workerThreads_;
    std::vector<int> workerCurrentProxyIndex_;
    // Protects lastResult_, lastIndexId_ and workerCurrentProxyIndex_
    mutable std::mutex workerStateMutex_;
    // Cooperative cancellation: cancelCv_ pairs with cancelRequested_, notified by
    // cancel()/destructor so interruptible waits wake promptly.
    std::mutex cancelMutex_;
    std::condition_variable cancelCv_;
    // Serialize all SQLite operations on shared db_ handle
    std::mutex dbMutex_;
    const NetworkMonitor* netMon_{nullptr};
    // Phase F: pre-generated XrayConfig per proxy (indexed by proxy index)
    std::vector<config::XrayConfig> preGenConfigs_;
    std::vector<bool> pregenFailedFlags_;
    // Phase D: thread-safe result queue with flush thread for batched DB writes
    TestResultQueue resultQueue_;
    // Network state for E14: single-point network-down detection
    std::mutex networkStateMutex_;
    std::atomic<bool> networkDown_{false};
    std::condition_variable networkCv_;
};

#endif // PROXY_BATCH_TESTER_H
