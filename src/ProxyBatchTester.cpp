#include "ProxyBatchTester.h"
#include "ConfigGenerator.h"
#include "Utils.h"
#include "Logger.h"
#include "XrayApi.h"
#include "NetworkMonitor.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <thread>
#include <windows.h>

ProxyBatchTester::ProxyBatchTester(sqlite3* db, const config::AppConfig& config, const std::string& baseDir,
                                    std::atomic<bool>* externalCancel, const NetworkMonitor* netMon)
    : db_(db), config_(config), totalProxies_(0), externalCancel_(externalCancel), netMon_(netMon)
    , resultQueue_([this](const std::vector<TestResultQueue::ResultTuple>& batch) -> bool {
          std::lock_guard<std::mutex> lock(dbMutex_);
          db::models::ProfileExItemDAO dao(db_);
          return dao.updateTestResultBatch(batch);
      }) {
    
    std::string exeBaseDir = baseDir.empty() ? utils::getExecutableDir() : baseDir;
    std::string configDir = exeBaseDir + "/config";
    
    xrayManager_ = XrayManager::getInstance(config_.proxy.xray_executable, configDir, config_.xray_workers);
    proxyTester_ = std::make_shared<ProxyTester>(xrayManager_, config_.test_url, config_.test_timeout_ms);
    
    lastResult_ = TestResult{};
    lastIndexId_.clear();
}

ProxyBatchTester::~ProxyBatchTester() {
    // Signal workers to stop and wake any interruptible wait.
    cancelRequested_ = true;
    cancelCv_.notify_all();

    // Unconditionally join all workers BEFORE releasing proxyTester_ they may be
    // using. Workers exit promptly via cooperative cancellation (interruptible
    // waits + curl progress-callback cancel + bounded curl/Xray timeouts).
    for (std::thread& t : workerThreads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    workerThreads_.clear();

    // Now safe to release proxyTester_
    proxyTester_.reset();

    // C4: Idempotent safety-net — stopAll() clears empty instances_ without side-effects.
    if (xrayManager_) {
        xrayManager_->stopAll();
    }
}

TestResult ProxyBatchTester::getLastResult() const {
    std::lock_guard<std::mutex> lock(workerStateMutex_);
    return lastResult_;
}

std::vector<db::models::Profileitem> ProxyBatchTester::loadProxies(const std::string& subId) {
    config::ConfigGenerator configGen(db_);
    std::string sql;
    
    if (!subId.empty() && !config_.sql_by_subid.empty()) {
        sql = config_.sql_by_subid;
        // Replace ALL occurrences of {subid} in the SQL template
        size_t pos = 0;
        while ((pos = sql.find("{subid}", pos)) != std::string::npos) {
            sql.replace(pos, 7, subId);
            pos += subId.length();
        }
    } else {
        sql = config_.sql_query;
    }
    
    // Replace ALL occurrences of {blacklist_threshold} in the SQL template (both queries)
    std::string thresholdStr = std::to_string(config_.blacklist_threshold);
    size_t btPos = 0;
    while ((btPos = sql.find("{blacklist_threshold}", btPos)) != std::string::npos) {
        sql.replace(btPos, 21, thresholdStr);
        btPos += thresholdStr.length();
    }
    
    Logger::write("Executing SQL: " + sql, LogLevel::DEBUG);
    return configGen.loadProfiles(sql);
}

int ProxyBatchTester::calculateXrayInstanceCount(int proxyCount) {
    int hardwareCores = static_cast<int>(std::thread::hardware_concurrency());
    if (hardwareCores < 1) hardwareCores = 1;
    int configMaxWorkers = config_.xray_workers;
    const int HARD_CAP = 16;
    int maxWorkers = std::min({proxyCount, hardwareCores, configMaxWorkers, HARD_CAP});
    return maxWorkers;
}

    bool ProxyBatchTester::startXrayInstances(int count) {
    int actual = xrayManager_->start(count, config_.xray_start_port, config_.xray_api_port);
    if (actual <= 0) {
        return false;
    }

    return true;
}

void ProxyBatchTester::workerThreadFunc(int workerId, int socksPort, int apiPort) {
std::string xrayApiAddr = "127.0.0.1:" + std::to_string(apiPort);
    xray::XrayApi xrayApi(config_.proxy.xray_executable, xrayApiAddr);
    
    while (true) {
        // Check for cancellation
        if (isCancelled()) {
            break;
        }
        if (netMon_ && !netMon_->IsConnected()) {
            if (!waitForNetworkRecovery()) break;
            continue;
        }
        
        int profileIdx = -1;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (proxiesQueue_.empty()) break;
            profileIdx = proxiesQueue_.front();
            proxiesQueue_.pop();
        }
        
        if (profileIdx < 0 || profileIdx >= static_cast<int>(proxies_.size()) ||
            profileIdx >= static_cast<int>(preGenConfigs_.size())) {
            processedCount_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // E7: Skip proxies whose pre-generation failed (outbound_json is empty).
        if (pregenFailedFlags_[profileIdx]) {
            failedCount_.fetch_add(1, std::memory_order_relaxed);
            processedCount_.fetch_add(1, std::memory_order_relaxed);
            resultQueue_.enqueue(proxies_[profileIdx].indexid, -1, false, "PREGEN_FAILED");
            teardownWorkerResult(workerId, "PREGEN_FAILED");
            continue;
        }

        // Track which proxy this worker is currently handling (best-effort)
        if (workerId < static_cast<int>(workerCurrentProxyIndex_.size())) {
            std::lock_guard<std::mutex> lock(workerStateMutex_);
            workerCurrentProxyIndex_[workerId] = profileIdx;
        }

        const db::models::Profileitem& profile = proxies_[profileIdx];
 db::models::Profileitem configProfile = profile;
        
        try {
            configProfile.checkRequired();
        } catch (const std::exception& e) {
            std::string errorDetail = profile.address + ":" + profile.port + " (" + profile.configtype + ") - " + e.what();
            Logger::write("CONFIG_ERROR: " + profile.indexid + " - " + errorDetail, LogLevel::INFO);
            {
                std::lock_guard<std::mutex> lock(dbMutex_);
                db::models::ProfileitemDAO dao(db_);
                dao.deleteByIndexId(profile.indexid);
            }
            failedCount_.fetch_add(1, std::memory_order_relaxed);
            processedCount_.fetch_add(1, std::memory_order_relaxed);
            teardownWorkerResult(workerId, "CONFIG_ERROR");
            continue;
        }
        
        try {
            const config::XrayConfig& config = preGenConfigs_[profileIdx];
            std::string tag = "proxy";

#ifdef USE_GRPC_API
            xrayApi.removeOutboundDirect(tag);
#else
            xrayApi.removeOutbound(tag);
#endif

            // Interruptible settle wait (20ms total) before adding the outbound
            if (waitInterruptible(20)) return;
            
            std::string addResult;
            int retryCount = 0;
            bool addSuccess = false;
            while (retryCount < 3) {
#ifdef USE_GRPC_API
                if (xrayApi.addOutboundDirect(config.outbound_json, tag, addResult)) {
#else
                if (xrayApi.addOutbound(config.outbound_json, tag, addResult)) {
#endif
                    addSuccess = true;
                    break;
                }
                retryCount++;
                if (retryCount < 3) {
#ifdef USE_GRPC_API
                    xrayApi.removeOutboundDirect(tag);
#else
                    xrayApi.removeOutbound(tag);
#endif
                    // Exponential backoff only for transient/not-ready errors
                    std::string lastErr = xrayApi.getLastError();
                    bool transientErr = (lastErr.find("not ready") != std::string::npos ||
                                         lastErr.find("not found") != std::string::npos ||
                                         lastErr.find("unavailable") != std::string::npos ||
                                         lastErr.find("connection refused") != std::string::npos ||
                                         lastErr.find("timed out") != std::string::npos ||
                                         lastErr.find("refused") != std::string::npos ||
                                         lastErr.find("network unreachable") != std::string::npos ||
                                         lastErr.find("host unreachable") != std::string::npos ||
                                         lastErr.find("WSA10060") != std::string::npos ||  // WSAETIMEDOUT
                                         lastErr.find("WSA10061") != std::string::npos ||  // WSAECONNREFUSED
                                         lastErr.find("WSA10051") != std::string::npos ||  // WSAENETUNREACH
                                         lastErr.find("WSA10056") != std::string::npos);   // WSAEISCONN
                    if (transientErr) {
                        int backoffMs = 50 * (1 << (retryCount - 1));
                        if (waitInterruptible(backoffMs)) return;
                    }
                }
            }

            if (!addSuccess) {
                // Per-proxy config errors are reported at DEBUG level to avoid
                // flooding the log during batch tests; the batch summary
                // (REPORT Total/Success/Failed) still reflects the failures.
                Logger::write("[Worker-" + std::to_string(workerId) + "] 注入xray outbound 错误: " + xrayApi.getLastError(), LogLevel::DEBUG);
                Logger::write("[Worker-" + std::to_string(workerId) + "] XRAY_ERROR - " + profile.indexid + " (tag=" + tag + ") - " + xrayApi.getLastError(), LogLevel::DEBUG);
                if (addResult.length() > 300) {
                    Logger::write("  Xray output: " + addResult.substr(0, 300) + "...", LogLevel::DEBUG);
                } else {
                    Logger::write("  Xray output: " + addResult, LogLevel::DEBUG);
                }
                failedCount_.fetch_add(1, std::memory_order_relaxed);
                processedCount_.fetch_add(1, std::memory_order_relaxed);
                resultQueue_.enqueue(profile.indexid, -1, false, "XRAY_ERROR");
                teardownWorkerResult(workerId, "XRAY_ERROR");
                continue;
            }

            // External cancellation (if provided) also aborts the blocking curl test
            TestResult result = proxyTester_->test(socksPort, &cancelRequested_, externalCancel_);
            
            int currentNum;
            if (result.success) {
                currentNum = static_cast<int>(processedCount_.fetch_add(1, std::memory_order_relaxed) + 1);
                successCount_.fetch_add(1, std::memory_order_relaxed);
                Logger::write(std::string("[Worker-" + std::to_string(workerId) + "] [") + std::to_string(currentNum) + "/" + std::to_string(totalProxies_) + "] " + profile.address + ":" + profile.port +
                          " (" + utils::getProtocolName(profile.configtype) + ") OK " + std::to_string(result.latencyMs) + "ms", LogLevel::INFO);
            } else {
                currentNum = static_cast<int>(processedCount_.fetch_add(1, std::memory_order_relaxed) + 1);
                failedCount_.fetch_add(1, std::memory_order_relaxed);
                Logger::write(std::string("[Worker-" + std::to_string(workerId) + "] [") + std::to_string(currentNum) + "/" + std::to_string(totalProxies_) + "] " + profile.address + ":" + profile.port +
                          " (" + utils::getProtocolName(profile.configtype) + ") FAIL " + result.errorMsg, LogLevel::INFO);
            }
            
            resultQueue_.enqueue(profile.indexid, result.latencyMs, result.success, result.errorMsg);
            
            // Store result for callers of runWithIndexId() to read after this worker finishes
            {
                std::lock_guard<std::mutex> lock(workerStateMutex_);
                lastResult_ = result;
            }

        } catch (const std::exception& e) {
            Logger::write("[Worker-" + std::to_string(workerId) + "] Exception: " + e.what(), LogLevel::ERR);
            Logger::write("[Worker-" + std::to_string(workerId) + "] failed to build conf: " + e.what(), LogLevel::ERR);
            Logger::write("[Worker-" + std::to_string(workerId) + "] EXCEPTION - " + profile.indexid + " - " + e.what(), LogLevel::ERR);
            failedCount_.fetch_add(1, std::memory_order_relaxed);
            processedCount_.fetch_add(1, std::memory_order_relaxed);
            resultQueue_.enqueue(profile.indexid, -1, false, e.what());
            teardownWorkerResult(workerId, e.what());
        } catch (...) {
            Logger::write("[Worker-" + std::to_string(workerId) + "] Unknown exception for " + profile.indexid, LogLevel::ERR);
            failedCount_.fetch_add(1, std::memory_order_relaxed);
            processedCount_.fetch_add(1, std::memory_order_relaxed);
            resultQueue_.enqueue(profile.indexid, -1, false, "UNKNOWN_EXCEPTION");
            teardownWorkerResult(workerId, "UNKNOWN_EXCEPTION");
            try {
#ifdef USE_GRPC_API
                xrayApi.removeOutboundDirect("proxy");
#else
                xrayApi.removeOutbound("proxy");
#endif
            } catch (...) {}
        }
    }
}

void ProxyBatchTester::testProxiesMultiThreaded() {
    // C3: RAII scope guard — stopAll() runs on any exit path (thread creation failure,
    // exception, or normal return), preventing leaked Xray instances.
    struct XrayGuard {
        XrayManager* mgr;
        ~XrayGuard() { if (mgr) mgr->stopAll(); }
    } xrayGuard{xrayManager_};

    // E14: Reset shared network-down state at start of each batch.
    networkDown_.store(false, std::memory_order_relaxed);
    networkCv_.notify_all();

    std::vector<std::pair<int, int>> portPairs = xrayManager_->getPortPairs();
    int numWorkers = static_cast<int>(portPairs.size());
    {
        std::lock_guard<std::mutex> lock(workerStateMutex_);
        workerCurrentProxyIndex_.assign(numWorkers, -1);
    }

    for (int i = 0; i < totalProxies_; ++i) {
        proxiesQueue_.push(i);
    }

    // Phase D: Start flush thread for batched DB writes
    resultQueue_.start();

    std::vector<std::thread> threads;
    try {
        for (int i = 0; i < numWorkers; ++i) {
            int socksPort = portPairs[i].first;
            int apiPort = portPairs[i].second;
            threads.emplace_back(&ProxyBatchTester::workerThreadFunc, this, i, socksPort, apiPort);
        }
    } catch (...) {
        // Thread-creation failure: join any already-started threads then rethrow.
        for (std::thread& t : threads) {
            if (t.joinable()) t.join();
        }
        throw;
    }

    // Unconditionally join all workers. Workers exit via cooperative cancellation:
    // interruptible waits wake on cancelRequested_, and the blocking curl test is
    // aborted by the progress callback combined with bounded curl/Xray timeouts.
    for (std::thread& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // Phase D: Stop flush thread, drain any remaining queued results
    resultQueue_.stop();
}

void ProxyBatchTester::printSummary() {
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Total: " + std::to_string(totalProxies_), LogLevel::REPORT);
    Logger::write("Success: " + std::to_string(successCount_.load(std::memory_order_relaxed)), LogLevel::REPORT);
    Logger::write("Failed: " + std::to_string(failedCount_.load(std::memory_order_relaxed)), LogLevel::REPORT);
    Logger::write("========================================", LogLevel::REPORT);
}

void ProxyBatchTester::teardownWorkerResult(int workerId, const std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(workerStateMutex_);
    lastResult_ = TestResult{};
    lastResult_.errorMsg = errorMsg;
    (void)workerId;
}

bool ProxyBatchTester::waitForNetworkRecovery() {
    Logger::write("[ProxyBatchTester] network disconnected — pausing, waiting for recovery", LogLevel::WARN);
    // Bounded polling instead of an unbounded condition-variable wait:
    // NetworkMonitor never notifies networkCv_, so once ALL workers sleep on
    // the CV nobody re-checks IsConnected() and a transient probe failure
    // would hang the whole batch forever (or, after maxProbes failed cycles,
    // spuriously cancel it). Poll every 250ms and give up after 30s (longer
    // than one full probe cycle: up to 3 urls x checkTimeoutMs + interval).
    const int totalWaitMs = 30000;
    int elapsedMs = 0;
    while (!isCancelled() && elapsedMs < totalWaitMs) {
        {
            std::unique_lock<std::mutex> lock(networkStateMutex_);
            if (!netMon_ || netMon_->IsConnected()) {
                networkDown_ = false;
                networkCv_.notify_all();
                Logger::write("[ProxyBatchTester] network restored — resuming", LogLevel::WARN);
                return true;
            }
            networkDown_ = true;
            networkCv_.wait_for(lock, std::chrono::milliseconds(250),
                                [this]() { return cancelRequested_.load(); });
        }
        elapsedMs += 250;
    }
    if (isCancelled()) {
        Logger::write("[ProxyBatchTester] network wait cancelled — exiting", LogLevel::WARN);
    } else {
        Logger::write("[ProxyBatchTester] network wait timed out after 30s — exiting", LogLevel::WARN);
    }
    return false;
}

bool ProxyBatchTester::waitInterruptible(int totalMs) {
    const int stepMs = 10;
    int elapsed = 0;
    while (elapsed < totalMs) {
        if (isCancelled()) return true;
        if (netMon_ && !netMon_->IsConnected()) {
            if (!waitForNetworkRecovery()) return true;
            continue;
        }
        {
            std::unique_lock<std::mutex> lock(cancelMutex_);
            cancelCv_.wait_for(lock, std::chrono::milliseconds(stepMs),
                               [this]() { return cancelRequested_.load(); });
        }
        elapsed += stepMs;
    }
    return isCancelled();
}

bool ProxyBatchTester::preGenerateConfigs(int count) {
    std::chrono::steady_clock::time_point preGenStart = std::chrono::steady_clock::now();
    preGenConfigs_.clear();
    preGenConfigs_.reserve(count);
    pregenFailedFlags_.resize(count, false);
    config::ConfigGenerator configGen(db_);
    for (int i = 0; i < count; ++i) {
        try {
            config::XrayConfig cfg = configGen.generateConfig(proxies_[i]);
            preGenConfigs_.push_back(cfg);
        } catch (const std::exception& e) {
            Logger::write("[ProxyBatchTester] Pre-gen config failed for " + proxies_[i].indexid + ": " + e.what(), LogLevel::WARN);
            config::XrayConfig failCfg;
            failCfg.configFailed = true;
            preGenConfigs_.push_back(failCfg);
            pregenFailedFlags_[i] = true;
        }
    }
    std::chrono::steady_clock::time_point preGenEnd = std::chrono::steady_clock::now();
    int preGenMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(preGenEnd - preGenStart).count());
    Logger::write("Pre-generated " + std::to_string(count) + " configs in " + std::to_string(preGenMs) + "ms", LogLevel::INFO);
    return true;
}

bool ProxyBatchTester::runInternal() {
    testProxiesMultiThreaded();
    printSummary();
    xrayManager_->stopAll();
    return true;
}

bool ProxyBatchTester::run() {
    // B1: Reset all mutable state at start of each run
    successCount_.store(0, std::memory_order_relaxed);
    failedCount_.store(0, std::memory_order_relaxed);
    processedCount_.store(0, std::memory_order_relaxed);
    pregenFailedFlags_.clear();
    proxiesQueue_ = std::queue<int>();
    {
        std::lock_guard<std::mutex> lock(workerStateMutex_);
        lastResult_ = TestResult{};
    }

    proxies_ = loadProxies();
    totalProxies_ = static_cast<int>(proxies_.size());

    if (totalProxies_ == 0) {
        Logger::write("No proxies to test", LogLevel::WARN);
        printSummary();
        return true;
    }

    Logger::write("Testing " + std::to_string(totalProxies_) + " proxies total", LogLevel::REPORT);

    int instanceCount = calculateXrayInstanceCount(totalProxies_);
    if (!startXrayInstances(instanceCount)) {
        Logger::write("Failed to start xray instances", LogLevel::WARN);
        return false;
    }

    if (config_.log_network_failures) {
        Logger::write("Started " + std::to_string(instanceCount) + " xray instances", LogLevel::INFO);
    }

    if (!preGenerateConfigs(totalProxies_)) {
        return false;
    }

    return runInternal();
}

bool ProxyBatchTester::runWithSubId(const std::string& subId) {
    // B1: Reset all mutable state at start of each run
    successCount_.store(0, std::memory_order_relaxed);
    failedCount_.store(0, std::memory_order_relaxed);
    processedCount_.store(0, std::memory_order_relaxed);
    pregenFailedFlags_.clear();
    proxiesQueue_ = std::queue<int>();
    {
        std::lock_guard<std::mutex> lock(workerStateMutex_);
        lastResult_ = TestResult{};
    }

    proxies_ = loadProxies(subId);
    totalProxies_ = static_cast<int>(proxies_.size());

    if (totalProxies_ == 0) {
        Logger::write("No proxies to test for subscription: " + subId, LogLevel::WARN);
        printSummary();
        return false;
    }

    Logger::write("Testing " + std::to_string(totalProxies_) + " proxies total", LogLevel::REPORT);
    if (config_.log_network_failures) {
        Logger::write("Testing " + std::to_string(totalProxies_) + " proxies from subscription: " + subId, LogLevel::INFO);
    }

    int instanceCount = calculateXrayInstanceCount(totalProxies_);
    if (!startXrayInstances(instanceCount)) {
        Logger::write("Failed to start xray instances", LogLevel::WARN);
        return false;
    }

    if (config_.log_network_failures) {
        Logger::write("Started " + std::to_string(instanceCount) + " xray instances", LogLevel::INFO);
    }

    if (!preGenerateConfigs(totalProxies_)) {
        return false;
    }

    return runInternal();
}

bool ProxyBatchTester::runWithIndexId(const std::string& indexId) {
    // B1: Reset all mutable state at start of each run
    successCount_.store(0, std::memory_order_relaxed);
    failedCount_.store(0, std::memory_order_relaxed);
    processedCount_.store(0, std::memory_order_relaxed);
    pregenFailedFlags_.clear();
    proxiesQueue_ = std::queue<int>();
    {
        std::lock_guard<std::mutex> lock(workerStateMutex_);
        lastResult_ = TestResult{};
    }

    db::models::ProfileitemDAO dao(db_);
    std::vector<db::models::Profileitem> profiles = dao.getAll();
    std::vector<db::models::Profileitem> filtered;
    std::copy_if(profiles.begin(), profiles.end(), std::back_inserter(filtered),
        [&indexId](const db::models::Profileitem& p) { return p.indexid == indexId; });

    proxies_ = std::move(filtered);
    totalProxies_ = static_cast<int>(proxies_.size());

    if (totalProxies_ == 0) {
        Logger::write("Proxy not found: " + indexId, LogLevel::WARN);
        {
            std::lock_guard<std::mutex> lock(workerStateMutex_);
            lastIndexId_ = indexId;
            lastResult_ = TestResult{};
            lastResult_.errorMsg = "NOTFOUND";
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(workerStateMutex_);
        lastIndexId_ = proxies_[0].indexid;
        lastResult_ = TestResult{};
    }

    Logger::write("Testing single proxy: " + indexId, LogLevel::REPORT);

    if (!startXrayInstances(1)) {
        Logger::write("Failed to start xray instance", LogLevel::WARN);
        return false;
    }

    if (!preGenerateConfigs(totalProxies_)) {
        return false;
    }

    return runInternal();
}
