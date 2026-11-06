#ifndef PROXY_BATCH_TESTER_H
#define PROXY_BATCH_TESTER_H

#include <string>
#include <vector>
#include <cstddef>
#include <sqlite3.h>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <memory>
#include <functional>
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

    // Phase A (ProxyTestUnified): load a specific set of proxies (by indexId)
    // into proxies_ so a multi-select test can create one count=N pool instead
    // of N count=1 pools. Returns true if at least one proxy was loaded.
    bool loadProxies(const std::vector<std::string>& indexIds);

    // Insert a high-priority proxy into the running pool (Scheme B).
    // Can be called while run()/runWithSubId() is executing.
    bool insertProxy(const std::string& indexId);
    std::string resolveSql(int id, const std::string& fallback) const;
    XrayManager* getXrayManager() { return xrayManager_; }
    TestResult getLastResult() const;

    int getTotalProxies() const { return totalProxies_.load(); }

    // Queue/progress snapshot for the monitoring panel (spec 2026-11-03 v1.2 §3.3).
    // NOT const: reading the two queue sizes requires holding queueMutex_/
    // insertedMutex_, which are not mutable. Callers are UI-thread only.
    struct BatchQueueStats {
        int total = 0;
        int processed = 0;
        int success = 0;
        int failed = 0;
        int remaining = 0;          // max(0, total - processed); counts the
                                    // in-flight proxies, not just queue sizes
        int batchQueueSize = 0;     // proxiesQueue_.size()
        int insertedQueueSize = 0;  // insertedQueue_.size()
        bool running = false;
        double percent = 0.0;       // processed/total*100, 0 when total==0
        // True when these numbers describe a subscription-update task rather
        // than a proxy batch test (owner 2026-11-06: 更新订阅复用队列进度条).
        bool updating = false;
    };
    BatchQueueStats getQueueStats();

    // One proxy still waiting in the batch queues, for the "任务进度" panel
    // (was labelled "测试队列", owner 2026-11-06 rename).
    struct PendingProxy {
        std::string indexId;
        std::string address;
        std::string port;
        std::string configType;
        bool inserted = false;   // true = high-priority insertion queue
    };

    // Snapshot of the queued-but-not-yet-tested proxies in WORKER PICKUP ORDER
    // (high-priority inserted queue first, then proxiesQueue_ FIFO), capped at
    // `limit`. Empty while the run has not sealed its proxy vector yet, so the
    // UI never reads proxies_ while a run is still loading it.
    // NOT const: needs insertedMutex_/queueMutex_ (not mutable). UI thread only.
    std::vector<PendingProxy> getPendingProxies(std::size_t limit = 500);

    // Real-time progress: invoked (on the flush thread) with the indexIds of
    // rows that were just written to the DB, so the UI can refresh only them.
    using RowsUpdatedCallback = std::function<void(const std::vector<std::string>&)>;
    void setRowsUpdatedCallback(RowsUpdatedCallback cb) { rowsUpdatedCb_ = std::move(cb); }

    // Cancel support
    void cancel() {
        cancelRequested_ = true;
        cancelCv_.notify_all();
    }
    bool isCancelled() const {
        if (externalCancel_ && externalCancel_->load()) return true;
        return cancelRequested_.load();
    }

 public:
    std::vector<db::models::Profileitem> loadProxies(const std::string& subId = "");

 private:
    int calculateXrayInstanceCount(int proxyCount);
    bool startXrayInstances(int count);
    void testProxiesMultiThreaded();
    // Phase C (ProxyTestUnified): unified completion summary exit reason.
    enum class ExitReason { Normal, Cancelled, Crash, Shutdown };
    void printSummary(ExitReason reason = ExitReason::Normal);

    void workerThreadFunc(int workerId, int socksPort, int apiPort);
    void logToConsole(const std::string& msg);

    bool preGenerateConfigs(int count);
    bool runInternal();

    /// Wait for network recovery or cancellation.
    /// Returns true if network is connected (resume testing), false if cancelled (exit).
    bool waitForNetworkRecovery();

    /// Interruptible sleep: waits up to totalMs in small steps, returning early
    /// when cancellation is requested. Returns true **only** when the batch was
    /// cancelled -- never merely because the delay was cut short by a bounded
    /// network wait (D1b, 2026-10-19).
    bool waitInterruptible(int totalMs);

    /// D1 (2026-10-19): raise a batch-wide cancellation because `reason` left this
    /// worker unable to make progress. Called only when a bounded network wait is
    /// exhausted (never on user cancellation, which exits quietly). Setting the
    /// external flag stops sibling workers and the owning operation, so the batch
    /// ends as a whole instead of degrading to 1/N concurrency.
    ///
    /// Idempotent: only the first caller logs, because several workers run their
    /// own 30s timer independently and would otherwise report the same event once
    /// per worker.
    void requestBatchCancellation(int workerId, int apiPort, const std::string& reason);

    /// E13: Teardown a worker's lastResult_ after a non-success path.
    void teardownWorkerResult(int workerId, const std::string& errorMsg);

    /// P2-1: single sink for every "the backing Xray instance looks
    /// unreachable" report (XrayApi::instanceHealthHook_). Fires from both the
    /// CLI-spawn failure site (src/XrayApi.cpp:171-175) and the 2nd
    /// consecutive gRPC connect failure (src/XrayApi.cpp:1952), plus the
    /// ProxyBatchTester gate at "3 failed injections". Throttled per apiPort
    /// inside XrayManager::evaluateInstanceHealthThrottled, so callers do not
    /// need their own rate limiting.
    void onXrayEndpointUnreachable(int apiPort);

    sqlite3* db_;
    config::AppConfig config_;
    XrayManager* xrayManager_;
    std::shared_ptr<ProxyTester> proxyTester_;
    std::atomic<int> totalProxies_;   // grows when proxies are inserted mid-run
    std::atomic<int> processedCount_{0};
    std::atomic<int> successCount_{0};
    std::atomic<int> failedCount_{0};
    std::vector<db::models::Profileitem> proxies_;
    std::queue<int> proxiesQueue_;
    std::mutex queueMutex_;
    // High-priority insertion queue (Scheme B: pool as infrastructure)
    std::vector<db::models::Profileitem> insertedProxies_;
    // Per-inserted-proxy pre-generated config, indexed in lockstep with
    // insertedProxies_ (guarded by insertedMutex_).
    std::vector<config::XrayConfig> insertedConfigs_;
    std::vector<bool> insertedConfigFailed_;
    std::queue<int> insertedQueue_;
    std::mutex insertedMutex_;
    std::condition_variable insertedCv_;
    // Real-time UI refresh hook (set before a run starts; invoked from the
    // result-flush thread with the indexIds just written to the DB).
    RowsUpdatedCallback rowsUpdatedCb_;
    // Protects proxies_ and pregenFailedFlags_ during concurrent insertion
    std::mutex proxiesMutex_;
    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool> running_{false};
    // True only while proxies_ is immutable for the current run (set right
    // after the queue is filled in runInternal(), cleared when a new run
    // starts loading). Guards getPendingProxies() against reading proxies_
    // while run()/runWithSubId() is assigning it.
    std::atomic<bool> proxiesSealed_{false};
    // Phase C: whether printSummary() has already been called for this run,
    // so the destructor doesn't double-print on early-exit paths.
    std::atomic<bool> summaryPrinted_{false};
    // When true, testProxiesMultiThreaded() keeps the Xray instance alive
    // (does not stopAll on teardown) so consecutive single-proxy tests can
    // reuse it instead of restarting the Xray process each time.
    bool keepXrayAlive_ = false;
    // D1: each worker runs its own 30s network-wait timer, so several of them can
    // exhaust it within microseconds of each other. This latch keeps the
    // "whole batch is stopping" report to a single line.
    std::atomic<bool> batchStopReported_{false};

    TestResult lastResult_;
    std::string lastIndexId_;
    std::atomic<bool>* externalCancel_{nullptr};
    std::vector<std::thread> workerThreads_;
    std::vector<int> workerCurrentProxyIndex_;
    // One flag per worker; cleared by the worker wrapper when workerThreadFunc()
    // returns. The teardown uses it to bound the join (a worker stuck in a hung
    // gRPC addOutboundDirect never returns, so an unconditional join would hang
    // the whole batch task forever). shared_ptr (not vector<atomic<bool>>, which
    // is non-movable) so the worker wrapper can own its flag by value copy.
    std::vector<std::shared_ptr<std::atomic<bool>>> workerAlive_;
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
