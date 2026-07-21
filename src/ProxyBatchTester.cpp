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
#include <future>

ProxyBatchTester::ProxyBatchTester(sqlite3* db, const config::AppConfig& config, const std::string& baseDir,
                                    std::atomic<bool>* externalCancel, const NetworkMonitor* netMon)
    : db_(db), config_(config), totalProxies_(0), successCount_(0), failedCount_(0), processedCount_(0)
    , externalCancel_(externalCancel), netMon_(netMon) {
    
    std::string exeBaseDir = baseDir.empty() ? utils::getExecutableDir() : baseDir;
    std::string configDir = exeBaseDir + "/config";
    
    xrayManager_ = XrayManager::getInstance(config_.proxy.xray_executable, configDir, config_.xray_workers);
    proxyTester_ = new ProxyTester(xrayManager_, config_.test_url, config_.test_timeout_ms);
    
    lastResult_ = TestResult{};
    lastIndexId_.clear();
}

ProxyBatchTester::~ProxyBatchTester() {
    cancelRequested_ = true;  // Signal workers to stop
    // Note: xrayManager_ is a singleton managed by XrayManager::release(), not deleted here
    
    // Wait for worker threads with timeout FIRST (before deleting proxyTester_ they're using)
    if (!workerThreads_.empty()) {
        for (std::thread& t : workerThreads_) {
            if (t.joinable()) {
                std::future<void> fut = std::async(std::launch::async, [&t]() {
                    if (t.joinable()) t.join();
                });
                if (fut.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
                    t.detach();
                    Logger::write("[ProxyBatchTester] Destructor: detach due to timeout", LogLevel::WARN);
                }
            }
        }
        workerThreads_.clear();
    }
    
    // Now safe to delete proxyTester_ after threads are done/detached
    delete proxyTester_;
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
    int maxWorkers = config_.xray_workers;
    return std::min(proxyCount, maxWorkers);
}

bool ProxyBatchTester::startXrayInstances(int count) {
    int actual = xrayManager_->start(count, config_.xray_start_port, config_.xray_api_port);
    if (actual <= 0) {
        return false;
    }

    // Brief warm-up: give Xray processes time to open their gRPC API ports.
    // Worker threads call removeOutbound/addOutbound immediately, and each
    // xray api subprocess can block up to 5s if gRPC isn't ready yet.
    // Use a minimalist wait - launch subprocess per ping is too expensive.
    std::vector<std::pair<int, int>> portPairs = xrayManager_->getPortPairs();
    const int warmupMs = 2000;
    std::this_thread::sleep_for(std::chrono::milliseconds(warmupMs));

    return true;
}

void ProxyBatchTester::workerThreadFunc(int workerId, int socksPort, int apiPort) {
std::string xrayApiAddr = "127.0.0.1:" + std::to_string(apiPort);
    xray::XrayApi xrayApi(config_.proxy.xray_executable, xrayApiAddr);
    db::models::ProfileExItemDAO exItemDao(db_);
    config::ConfigGenerator configGen(db_);
    
    while (true) {
        // Check for cancellation
        if (isCancelled()) {
            // DIAGNOSTIC INSTRUMENTATION (systematic-debugging Phase 3/4)
            // Captures when inner worker actually observes the flag vs. when it was set.
            // REMOVE after verification.
            Logger::write("[ProxyBatchTester] Worker " + std::to_string(workerId) + 
                          " observed cancelRequested_ (will exit after current proxy or immediately)", LogLevel::WARN);
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
        
        if (profileIdx < 0 || profileIdx >= static_cast<int>(proxies_.size())) {
            std::lock_guard<std::mutex> lock(queueMutex_);
            processedCount_++;
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
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                failedCount_++;
                processedCount_++;
            }
            lastResult_ = TestResult{};
            lastResult_.errorMsg = "CONFIG_ERROR";
            continue;
        }
        
        try {
            config::XrayConfig config = configGen.generateConfig(profile);
            std::string tag = "proxy";
            
            xrayApi.removeOutbound(tag);
            for (int i = 0; i < 10; ++i) {  // 10 * 10ms = 100ms total
                if (isCancelled()) return;
                if (netMon_ && !netMon_->IsConnected()) { if (!waitForNetworkRecovery()) return; }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            xrayApi.removeOutbound(tag);
            for (int i = 0; i < 40; ++i) {  // 40 * 10ms = 400ms total
                if (isCancelled()) return;
                if (netMon_ && !netMon_->IsConnected()) { if (!waitForNetworkRecovery()) return; }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            std::string addResult;
            int retryCount = 0;
            bool addSuccess = false;
            while (retryCount < 3) {
                if (xrayApi.addOutbound(config.outbound_json, tag, addResult)) {
                    addSuccess = true;
                    break;
                }
                retryCount++;
                if (retryCount < 3) {
                    xrayApi.removeOutbound(tag);
                    for (int i = 0; i < 20; ++i) {  // 20 * 10ms = 200ms total
                        if (isCancelled()) return;
                        if (netMon_ && !netMon_->IsConnected()) { if (!waitForNetworkRecovery()) return; }
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
            }
            
            if (!addSuccess) {
                Logger::write("[Worker-" + std::to_string(workerId) + "] 注入xray outbound 错误: " + xrayApi.getLastError(), LogLevel::ERR);
                Logger::write("[Worker-" + std::to_string(workerId) + "] XRAY_ERROR - " + profile.indexid + " (tag=" + tag + ") - " + xrayApi.getLastError(), LogLevel::ERR);
                Logger::write("  Xray output: " + addResult, LogLevel::ERR);
                Logger::write("  Outbound JSON: " + config.outbound_json, LogLevel::ERR);
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    failedCount_++;
                    processedCount_++;
                }
                {
                    std::lock_guard<std::mutex> lock(dbMutex_);
                    exItemDao.updateTestResult(profile.indexid, -1, false, "XRAY_ERROR");
                }
                lastResult_ = TestResult{};
                lastResult_.errorMsg = "XRAY_ERROR";
                continue;
            }
            
            // sleep in 10ms increments for cancellation responsiveness
            for (int i = 0; i < 30; ++i) {  // 30 * 10ms = 300ms total
                if (isCancelled()) return;
                if (netMon_ && !netMon_->IsConnected()) { if (!waitForNetworkRecovery()) return; }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            // Phase 4 fix: check cancellation before blocking curl test
            if (isCancelled()) {
                Logger::write("[Worker-" + std::to_string(workerId) + "] cancel before blocking test, skipping", LogLevel::WARN);
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    failedCount_++;
                    processedCount_++;
                }
                lastResult_ = TestResult{};
                lastResult_.errorMsg = "CANCELLED";
                continue;
            }
            
            TestResult result = proxyTester_->test(socksPort, &cancelRequested_);
            
            int currentNum;
            if (result.success) {
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    currentNum = ++processedCount_;
                    successCount_++;
                }
                Logger::write(std::string("[Worker-" + std::to_string(workerId) + "] [") + std::to_string(currentNum) + "/" + std::to_string(totalProxies_) + "] " + profile.address + ":" + profile.port +
                          " (" + utils::getProtocolName(profile.configtype) + ") OK " + std::to_string(result.latencyMs) + "ms", LogLevel::INFO);
            } else {
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    currentNum = ++processedCount_;
                    failedCount_++;
                }
                Logger::write(std::string("[Worker-" + std::to_string(workerId) + "] [") + std::to_string(currentNum) + "/" + std::to_string(totalProxies_) + "] " + profile.address + ":" + profile.port +
                          " (" + utils::getProtocolName(profile.configtype) + ") FAIL " + result.errorMsg, LogLevel::INFO);
            }
            
            {
                std::lock_guard<std::mutex> lock(dbMutex_);
                exItemDao.updateTestResult(profile.indexid, result.latencyMs, result.success, result.errorMsg);
            }
            
            // Store result for callers of runWithIndexId() to read after this worker finishes
            lastResult_ = result;

        } catch (const std::exception& e) {
            Logger::write("[Worker-" + std::to_string(workerId) + "] Exception: " + e.what(), LogLevel::ERR);
            Logger::write("[Worker-" + std::to_string(workerId) + "] failed to build conf: " + e.what(), LogLevel::ERR);
            Logger::write("[Worker-" + std::to_string(workerId) + "] EXCEPTION - " + profile.indexid + " - " + e.what(), LogLevel::ERR);
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                failedCount_++;
                processedCount_++;
            }
            {
                std::lock_guard<std::mutex> lock(dbMutex_);
                exItemDao.updateTestResult(profile.indexid, -1, false, e.what());
            }
            lastResult_ = TestResult{};
            lastResult_.errorMsg = e.what();
        }
    }
}

void ProxyBatchTester::testProxiesMultiThreaded() {
    std::vector<std::pair<int, int>> portPairs = xrayManager_->getPortPairs();
    int numWorkers = static_cast<int>(portPairs.size());
    {
        std::lock_guard<std::mutex> lock(workerStateMutex_);
        workerCurrentProxyIndex_.assign(numWorkers, -1);
    }

    for (int i = 0; i < totalProxies_; ++i) {
        proxiesQueue_.push(i);
    }
    
    std::vector<std::thread> threads;
    for (int i = 0; i < numWorkers; ++i) {
        int socksPort = portPairs[i].first;
        int apiPort = portPairs[i].second;
        threads.emplace_back(&ProxyBatchTester::workerThreadFunc, this, i, socksPort, apiPort);
    }
    
    // Store threads for destructor to join with timeout
    workerThreads_ = std::move(threads);
    
    // Wait for all threads with timeout to prevent hang on curl blocking.
    // Each worker processes a share of the total proxies sequentially.
    // Calculate a timeout proportional to the expected workload so that
    // normal completion does not trigger the detach fallback.
    int proxiesPerWorker = (totalProxies_ + numWorkers - 1) / numWorkers;
    int perProxyBudgetMs = config_.test_timeout_ms + 5000; // curl test + API overhead
    int joinTimeoutMs = proxiesPerWorker * perProxyBudgetMs;
    const int MIN_JOIN_TIMEOUT = 60000;    // 1 min floor
    const int MAX_JOIN_TIMEOUT = 300000;   // 5 min cap
    if (joinTimeoutMs < MIN_JOIN_TIMEOUT) joinTimeoutMs = MIN_JOIN_TIMEOUT;
    if (joinTimeoutMs > MAX_JOIN_TIMEOUT) joinTimeoutMs = MAX_JOIN_TIMEOUT;

    for (size_t idx = 0; idx < workerThreads_.size(); ++idx) {
        std::thread& t = workerThreads_[idx];
        if (t.joinable()) {
            if (isCancelled()) {
                t.detach();  // Detach to avoid deadlock on shutdown
            } else {
                // Use async with timeout to prevent indefinite blocking on join
                std::future<void> fut = std::async(std::launch::async, [&t]() {
                    if (t.joinable()) t.join();
                });
                if (fut.wait_for(std::chrono::milliseconds(joinTimeoutMs)) != std::future_status::ready) {
                    int proxyIdx = -1;
                    {
                        std::lock_guard<std::mutex> lock(workerStateMutex_);
                        if (static_cast<size_t>(idx) < workerCurrentProxyIndex_.size()) {
                            proxyIdx = workerCurrentProxyIndex_[idx];
                        }
                    }
                    std::string proxyTag = "[unknown proxy]";
                    if (proxyIdx >= 0 && proxyIdx < static_cast<int>(proxies_.size())) {
                        const db::models::Profileitem& p = proxies_[proxyIdx];
                        proxyTag = "indexid=" + p.indexid + " " + p.address + ":" + p.port;
                    }
                    Logger::write("[Worker-" + std::to_string(idx) + "][WARN] Wait timeout, detaching thread to prevent hang - " + proxyTag, LogLevel::WARN);
                    t.detach();
                }
            }
        }
    }
    workerThreads_.clear();
}

void ProxyBatchTester::printSummary() {
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Total: " + std::to_string(totalProxies_), LogLevel::REPORT);
    Logger::write("Success: " + std::to_string(successCount_), LogLevel::REPORT);
    Logger::write("Failed: " + std::to_string(failedCount_), LogLevel::REPORT);
    Logger::write("========================================", LogLevel::REPORT);
}

bool ProxyBatchTester::waitForNetworkRecovery() {
    Logger::write("[ProxyBatchTester] network disconnected — pausing, waiting for recovery", LogLevel::WARN);
    while (!isCancelled()) {
        if (!netMon_ || netMon_->IsConnected()) {
            Logger::write("[ProxyBatchTester] network restored — resuming", LogLevel::WARN);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    Logger::write("[ProxyBatchTester] network probe exhausted or cancelled — exiting", LogLevel::WARN);
    return false;
}

bool ProxyBatchTester::run() {
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

    testProxiesMultiThreaded();
    printSummary();

    xrayManager_->stopAll();
    return true;
}

bool ProxyBatchTester::runWithSubId(const std::string& subId) {
     proxies_ = loadProxies(subId);
     totalProxies_ = static_cast<int>(proxies_.size());
     
     if (totalProxies_ == 0) {
         Logger::write("No proxies to test for subscription: " + subId, LogLevel::WARN);
         printSummary();
         return false;
     }

     Logger::write("Testing " + std::to_string(totalProxies_) + " proxies from subscription: " + subId, LogLevel::INFO);
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
    
    testProxiesMultiThreaded();
    printSummary();
    
    xrayManager_->stopAll();
    return true;
}

bool ProxyBatchTester::runWithIndexId(const std::string& indexId) {
    db::models::ProfileitemDAO dao(db_);
    std::vector<db::models::Profileitem> profiles = dao.getAll();
    std::vector<db::models::Profileitem> filtered;
    std::copy_if(profiles.begin(), profiles.end(), std::back_inserter(filtered),
        [&indexId](const db::models::Profileitem& p) { return p.indexid == indexId; });
    
    proxies_ = std::move(filtered);
    totalProxies_ = static_cast<int>(proxies_.size());
    
    if (totalProxies_ == 0) {
        Logger::write("Proxy not found: " + indexId, LogLevel::WARN);
        lastIndexId_ = indexId;
        lastResult_ = TestResult{};
        lastResult_.errorMsg = "NOTFOUND";
        return false;
    }
    
    lastIndexId_ = proxies_[0].indexid;
    lastResult_ = TestResult{};
    
    Logger::write("Testing single proxy: " + indexId, LogLevel::REPORT);
    
    if (!startXrayInstances(1)) {
        Logger::write("Failed to start xray instance", LogLevel::WARN);
        return false;
    }
    
    testProxiesMultiThreaded();
    printSummary();
    
    xrayManager_->stopAll();
    return true;
}
