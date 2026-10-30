#include "ProxyBatchTester.h"
#include "SqlQuery.h"
#include "ConfigGenerator.h"
#include "Utils.h"
#include "Logger.h"
#include "XrayApi.h"
#include "NetworkMonitor.h"
#include "BatchWorkerPolicy.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <windows.h>

ProxyBatchTester::ProxyBatchTester(sqlite3* db, const config::AppConfig& config, const std::string& baseDir,
                                    std::atomic<bool>* externalCancel, const NetworkMonitor* netMon)
    : db_(db), config_(config), totalProxies_(0), externalCancel_(externalCancel), netMon_(netMon)
    , resultQueue_([this](const std::vector<TestResultQueue::ResultTuple>& batch) -> bool {
          std::lock_guard<std::mutex> lock(dbMutex_);
          db::models::ProfileExItemDAO dao(db_);
          bool ok = dao.updateTestResultBatch(batch);
          if (!ok) {
              return false;
          }
          // Real-time UI refresh: hand the flushed indexIds to the UI thread.
          // Called while holding dbMutex_ only to keep ordering; the callback
          // itself must be non-blocking (AppController only queues an event).
          if (rowsUpdatedCb_) {
              std::vector<std::string> ids;
              ids.reserve(batch.size());
              for (std::size_t i = 0; i < batch.size(); ++i) {
                  ids.push_back(std::get<0>(batch[i]));
              }
              rowsUpdatedCb_(ids);
          }
          return true;
      }) {
    
    std::string exeBaseDir = baseDir.empty() ? utils::getExecutableDir() : baseDir;
    std::string configDir = exeBaseDir + "/config";
    
    xrayManager_ = XrayManager::getInstance(config_.proxy.xray_executable, configDir, config_.xray_workers, db_);
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

    // Phase C (ProxyTestUnified): print unified summary on destruction if not
    // already printed (covers force-terminated / crash / shutdown paths).
    // Exit reason: if the cancel flag is set → Cancelled; else if summary was
    // already printed (normal path) → skip; else → Shutdown (destructor while
    // still running, e.g. app exit or exception).
    if (!summaryPrinted_.load(std::memory_order_acquire)) {
        ExitReason r = ExitReason::Shutdown;
        if (cancelRequested_.load(std::memory_order_acquire)) {
            r = ExitReason::Cancelled;
        }
        printSummary(r);
    }

    // C4: Idempotent safety-net — stopAll() clears empty instances_ without side-effects.
    // Skip when keepXrayAlive_ is set (single-proxy optimization: reuse the Xray
    // instance across consecutive single-proxy tests instead of restarting).
    if (xrayManager_ && !keepXrayAlive_) {
        xrayManager_->stopAll();
    }
}

TestResult ProxyBatchTester::getLastResult() const {
    std::lock_guard<std::mutex> lock(workerStateMutex_);
    return lastResult_;
}

std::string ProxyBatchTester::resolveSql(int id, const std::string& fallback) const {
    if (id > 0 && db_) {
        db::SqlQueryDAO dao(db_);
        db::SqlQueryItem item = dao.getById(id);
        if (item.id > 0 && !item.sql_text.empty()) {
            return item.sql_text;
        }
    }
    if (!fallback.empty()) return fallback;
    return "SELECT * FROM ProfileItem;";
}

std::vector<db::models::Profileitem> ProxyBatchTester::loadProxies(const std::string& subId) {
    config::ConfigGenerator configGen(db_);
    std::string sql;

    std::string sqlQueryStr = resolveSql(config_.sql_query_id, config_.sql_query);
    std::string sqlBySubidStr = resolveSql(config_.sql_by_subid_id, config_.sql_by_subid);

    if (!subId.empty() && !sqlBySubidStr.empty()) {
        sql = sqlBySubidStr;
        // Replace ALL occurrences of {subid} in the SQL template
        size_t pos = 0;
        while ((pos = sql.find("{subid}", pos)) != std::string::npos) {
            sql.replace(pos, 7, subId);
            pos += subId.length();
        }
    } else {
        sql = sqlQueryStr;
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

    // P2-1: XrayApi reports "the backing instance looks unreachable" from every
    // failure site (CLI spawn failure at src/XrayApi.cpp:171-175, and the second
    // consecutive gRPC connect failure at :1952). Registered OUTSIDE the
    // USE_GRPC_API guard on purpose: the CLI fallback (addOutbound, XrayApi.h:31)
    // is compiled in BOTH configurations and is a hot path in the default build
    // (XrayApi.cpp:2481/:2508 return addOutbound(...)), so it must be able to
    // report too — before this, only the gRPC path could trigger a relaunch and
    // a dead instance silently absorbed 50000+ CLI spawns.
    // Rate limiting is the hook owner's job (XrayManager::
    // evaluateInstanceHealthThrottled), so XrayApi stays a dumb reporter.
    xrayApi.setInstanceHealthHook([this, apiPort] { onXrayEndpointUnreachable(apiPort); });

    // Lifecycle management: when grpcConnect fails twice consecutively, evaluate
    // the backing Xray instance process health. If the instance was successfully
    // relaunched (evaluateInstanceHealth returned true), instanceDead is cleared
    // at the loop top so this worker keeps serving the remaining shared queue
    // items. Only a failed relaunch stops the worker (its queue items are then
    // picked up by the other workers of the shared queue).
    std::atomic<bool> instanceDead{false};
    // D3: three-state outcome of the last health evaluation (see
    // XrayManager::InstanceHealthOutcome). Stored as int because <atomic> has no
    // lock-free guaranteed form for enum class.
    std::atomic<int> relaunchDecision{
        static_cast<int>(XrayManager::InstanceHealthOutcome::RelaunchFailed)};
#ifdef USE_GRPC_API
    xrayApi.setConnectFailureHook([this, &instanceDead, &relaunchDecision, apiPort]() {
        instanceDead.store(true, std::memory_order_relaxed);
        // P2-1: throttled variant — this hook and instanceHealthHook_ (registered
        // above) can both fire for the same gRPC failure, and the 30s per-port
        // cooldown makes the second one a no-op instead of a second restart.
        //
        // D3: store the three-state outcome, not a bool. Reporting "hit the
        // cooldown" as false used to look like a failed relaunch and retired the
        // worker even though the instance was healthy.
        relaunchDecision.store(
            static_cast<int>(xrayManager_->evaluateInstanceHealthThrottledEx(apiPort)),
            std::memory_order_relaxed);
    });
#endif

    while (true) {
        // Check for cancellation
        if (isCancelled()) {
            break;
        }
        if (instanceDead.load()) {
            // D3: route the outcome through the shared pure policy so production
            // and the unit tests cannot drift apart.
            const XrayManager::InstanceHealthOutcome outcome =
                static_cast<XrayManager::InstanceHealthOutcome>(
                    relaunchDecision.load(std::memory_order_relaxed));
            const batch_policy::InstanceDeadDecision decision =
                batch_policy::decideInstanceDead(outcome);
            if (decision == batch_policy::InstanceDeadDecision::StopWorkerAndLog) {
                // D3: this branch used to `break` with zero logging, which is why
                // a worker could vanish mid-batch with nothing in the log to
                // explain it.
                Logger::write("[ProxyBatchTester] worker "
                              + std::to_string(workerId)
                              + ": instance relaunch FAILED (api="
                              + std::to_string(apiPort)
                              + ") — stopping this worker",
                              LogLevel::ERR);
                break;
            }
            Logger::write("[ProxyBatchTester] worker "
                          + std::to_string(workerId)
                          + ": instance " +
                          (decision == batch_policy::InstanceDeadDecision::ClearAndContinueRelaunched
                               ? "relaunched"
                               : "healthy (health check throttled)") +
                          " (api=" + std::to_string(apiPort)
                          + ") — continuing to serve remaining queue items",
                          LogLevel::WARN);
            instanceDead.store(false);
        }
        if (netMon_ && !netMon_->IsConnected()) {
            if (!waitForNetworkRecovery()) {
                // false means EITHER "cancelled" OR "30s bounded wait exhausted".
                // Only the latter is an outage, and an outage must stop the whole
                // batch (owner policy, 2026-10-19) instead of silently retiring
                // single workers until the batch limps along at 1/N concurrency.
                if (!isCancelled()) {
                    requestBatchCancellation(
                        workerId, apiPort,
                        "network unreachable for 30s (bounded wait exhausted)");
                }
                break;
            }
            continue;
        }
        
        int profileIdx = -1;
        bool isInserted = false;
        {
            std::unique_lock<std::mutex> lock(insertedMutex_);
            if (insertedQueue_.empty()) {
                insertedCv_.wait_for(lock, std::chrono::milliseconds(100),
                    [this] { return !insertedQueue_.empty() || !running_.load(); });
            }
            if (!insertedQueue_.empty()) {
                profileIdx = insertedQueue_.front();
                insertedQueue_.pop();
                isInserted = true;
                Logger::write("[ProxyBatchTester] worker " + std::to_string(workerId)
                              + " picked up inserted proxy idx=" + std::to_string(profileIdx)
                              + " (insertedQueue remaining=" + std::to_string(insertedQueue_.size())
                              + ", isCancelled=" + (isCancelled() ? "true" : "false") + ")",
                              LogLevel::INFO);
            } else {
                Logger::write("[ProxyBatchTester] worker " + std::to_string(workerId)
                              + " insertedQueue empty after wait (running=" + (running_.load() ? "true" : "false") + ")",
                              LogLevel::DEBUG);
            }
        }
        if (!isInserted) {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (proxiesQueue_.empty()) {
                // Both queues empty: exit only if pool is shutting down
                if (!running_.load()) break;
                // Pool still running: wait briefly for insertions
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            profileIdx = proxiesQueue_.front();
            proxiesQueue_.pop();
        }
        
        if (isInserted) {
            // Lock insertedMutex_ when reading sizes: insertedProxies_/insertedConfigs_
            // are mutated by insertProxy() on the UI thread concurrently.
            std::lock_guard<std::mutex> lock(insertedMutex_);
            if (profileIdx < 0 ||
                profileIdx >= static_cast<int>(insertedProxies_.size()) ||
                profileIdx >= static_cast<int>(insertedConfigs_.size())) {
                Logger::write("[ProxyBatchTester] inserted proxy profileIdx=" + std::to_string(profileIdx)
                              + " out of bounds (proxies=" + std::to_string(insertedProxies_.size())
                              + ", configs=" + std::to_string(insertedConfigs_.size()) + ") — skipping",
                              LogLevel::WARN);
                processedCount_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
        } else if (profileIdx < 0 ||
                   profileIdx >= static_cast<int>(proxies_.size()) ||
                   profileIdx >= static_cast<int>(preGenConfigs_.size())) {
            processedCount_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // E7: Skip proxies whose pre-generation failed (outbound_json is empty).
        if (!isInserted && profileIdx < static_cast<int>(pregenFailedFlags_.size()) && pregenFailedFlags_[profileIdx]) {
            failedCount_.fetch_add(1, std::memory_order_relaxed);
            processedCount_.fetch_add(1, std::memory_order_relaxed);
            std::string pregenIdx;
            if (isInserted) {
                std::lock_guard<std::mutex> l(insertedMutex_);
                pregenIdx = insertedProxies_[profileIdx].indexid;
            } else {
                pregenIdx = proxies_[profileIdx].indexid;
            }
            resultQueue_.enqueue(pregenIdx, -1, false, "PREGEN_FAILED");
            teardownWorkerResult(workerId, "PREGEN_FAILED");
            continue;
        }

        // Track which proxy this worker is currently handling (best-effort)
        if (workerId < static_cast<int>(workerCurrentProxyIndex_.size())) {
            std::lock_guard<std::mutex> lock(workerStateMutex_);
            workerCurrentProxyIndex_[workerId] = profileIdx;
        }

        db::models::Profileitem profile;
        if (isInserted) {
            std::lock_guard<std::mutex> lock(insertedMutex_);
            profile = insertedProxies_[profileIdx];
        } else {
            profile = proxies_[profileIdx];
        }
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
        
        // Select the config that actually belongs to this item: inserted
        // proxies carry their own generated config, batch proxies use the
        // pre-generated vector. Never cross the two.
        config::XrayConfig selectedConfig;
        bool selectedConfigFailed = false;
        if (isInserted) {
            std::lock_guard<std::mutex> lock(insertedMutex_);
            selectedConfig = insertedConfigs_[profileIdx];
            selectedConfigFailed = insertedConfigFailed_[profileIdx];
        } else {
            selectedConfig = preGenConfigs_[profileIdx];
        }
        if (selectedConfigFailed || selectedConfig.configFailed) {
            failedCount_.fetch_add(1, std::memory_order_relaxed);
            processedCount_.fetch_add(1, std::memory_order_relaxed);
            Logger::write("[ProxyBatchTester] PREGEN_FAILED for " + profile.indexid, LogLevel::WARN);
            resultQueue_.enqueue(profile.indexid, -1, false, "PREGEN_FAILED");
            teardownWorkerResult(workerId, "PREGEN_FAILED");
            continue;
        }

        try {
            const config::XrayConfig& config = selectedConfig;
            std::string tag = "proxy";

#ifdef USE_GRPC_API
            xrayApi.removeOutboundDirect(tag);
#else
            xrayApi.removeOutbound(tag);
#endif

            // Interruptible settle wait (20ms total) before adding the outbound
            if (waitInterruptible(20)) return;
            
            std::string addResult;
            std::string injectError;
            int retryCount = 0;
            bool addSuccess = false;
            while (retryCount < 3 && !instanceDead.load() && !isCancelled()) {
#ifdef USE_GRPC_API
                if (xrayApi.addOutboundDirect(config.outbound_json, tag, addResult)) {
#else
                if (xrayApi.addOutbound(config.outbound_json, tag, addResult)) {
#endif
                    addSuccess = true;
                    break;
                }
                // Save the failure reason NOW: the removeOutboundDirect below may
                // succeed against a freshly relaunched instance and clear
                // lastError_ (XrayApi removeOutboundDirect success path), which
                // would otherwise wipe the error before it reaches the final log.
                injectError = xrayApi.getLastError();
                retryCount++;
                if (retryCount < 3) {
#ifdef USE_GRPC_API
                    xrayApi.removeOutboundDirect(tag);
#else
                    xrayApi.removeOutbound(tag);
#endif
                    // Exponential backoff only for transient/not-ready errors
                    std::string lastErr = injectError;
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
                        int backoffMs = 200 * (1 << (retryCount - 1));
                        if (waitInterruptible(backoffMs)) return;
                    }
                }
            }

            // In-flight proxy retry: when this worker's own connect failures
            // triggered a health evaluation that successfully relaunched the
            // instance on the same ports, give this SAME proxy item one more
            // chance instead of discarding it — the relaunched instance is
            // ready and can run the injection + connectivity test.
            //
            // D3: only Relaunched qualifies, which is exactly what the old boolean
            // "relaunch succeeded" check meant. Throttled must NOT retry: we
            // have no evidence that the instance finished coming back up.
            const bool relaunchedForThisItem =
                instanceDead.load() &&
                static_cast<XrayManager::InstanceHealthOutcome>(
                    relaunchDecision.load(std::memory_order_relaxed)) ==
                    XrayManager::InstanceHealthOutcome::Relaunched;
            if (!addSuccess && relaunchedForThisItem && !isCancelled()) {
                Logger::write("[Worker-" + std::to_string(workerId) + "] instance relaunched (api=" + std::to_string(apiPort) + ") — retrying proxy " + profile.indexid + " once", LogLevel::WARN);
#ifdef USE_GRPC_API
                xrayApi.removeOutboundDirect(tag);
                if (xrayApi.addOutboundDirect(config.outbound_json, tag, addResult)) {
#else
                xrayApi.removeOutbound(tag);
                if (xrayApi.addOutbound(config.outbound_json, tag, addResult)) {
#endif
                    addSuccess = true;
                } else {
                    injectError = xrayApi.getLastError();
                }
            }

            // P2-1: all three injection attempts failed and no relaunch rescued
            // this item. Report the endpoint so XrayManager can evaluate/relaunch
            // the backing instance before the next queue item hits the same dead
            // process. Gated on the retry count so a single bad outbound JSON
            // (configuration-level failure) never triggers an instance restart;
            // the 30s per-apiPort cooldown inside Throttled absorbs the rest.
            //
            // F5: a leftover/duplicate tag is a gRPC semantic error — it proves the
            // instance is reachable, so restarting it would be pure churn. Route
            // the decision through the shared pure policy (unit-tested) instead of
            // the bare `retryCount >= 3`.
            const std::string lastInjectionError =
                injectError.empty() ? xrayApi.getLastError() : injectError;
            if (!addSuccess &&
                batch_policy::shouldReportEndpointUnreachable(lastInjectionError, retryCount)) {
                onXrayEndpointUnreachable(apiPort);
            }

            if (!addSuccess) {
                // requirement); the raw Xray output dump below stays at DEBUG.
                std::string displayErr = injectError.empty() ? xrayApi.getLastError() : injectError;
                Logger::write("[Worker-" + std::to_string(workerId) + "] 注入xray outbound 错误: " + displayErr, LogLevel::ERR);
                Logger::write("[Worker-" + std::to_string(workerId) + "] XRAY_ERROR - " + profile.indexid + " (tag=" + tag + ") - " + displayErr, LogLevel::ERR);
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
            Logger::write("[ProxyBatchTester] worker " + std::to_string(workerId)
                          + " about to test " + (isInserted ? "INSERTED" : "batch")
                          + " proxy " + profile.address + ":" + profile.port,
                          LogLevel::DEBUG);
            TestResult result = proxyTester_->test(socksPort, &cancelRequested_, externalCancel_);
            
            int currentNum;
            if (result.success) {
                currentNum = static_cast<int>(processedCount_.fetch_add(1, std::memory_order_relaxed) + 1);
                successCount_.fetch_add(1, std::memory_order_relaxed);
                Logger::write(std::string("[Testing] [") + std::to_string(currentNum) + "/" + std::to_string(totalProxies_.load()) + "] " + profile.address + ":" + profile.port +
                          " (" + utils::getProtocolName(profile.configtype) + ") OK " + std::to_string(result.latencyMs) + "ms", LogLevel::INFO);
            } else {
                currentNum = static_cast<int>(processedCount_.fetch_add(1, std::memory_order_relaxed) + 1);
                failedCount_.fetch_add(1, std::memory_order_relaxed);
                Logger::write(std::string("[Testing] [") + std::to_string(currentNum) + "/" + std::to_string(totalProxies_.load()) + "] " + profile.address + ":" + profile.port +
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

void ProxyBatchTester::onXrayEndpointUnreachable(int apiPort) {
    // P2-1: single sink for every "instance looks unreachable" report (XrayApi
    // instanceHealthHook_). Throttled per apiPort, so both the CLI-spawn site
    // and the gRPC site can call this on every failure without risking a restart
    // storm. Deliberately wording-neutral: this fires after 1 CLI spawn failure
    // as well as after 3 failed injections, so claiming a specific attempt count
    // here would log something untrue.
    const bool relaunched = xrayManager_->evaluateInstanceHealthThrottled(apiPort);
    if (relaunched) {
        Logger::write("[ProxyBatchTester] instance relaunch attempted after injection failures (api="
                      + std::to_string(apiPort) + ")",
                      LogLevel::WARN);
    }
}

void ProxyBatchTester::testProxiesMultiThreaded() {
    // C3: RAII scope guard — stopAll() runs on any exit path (thread creation failure,
    // exception, or normal return), preventing leaked Xray instances.
    struct XrayGuard {
        XrayManager* mgr;
        bool keepAlive;
        ~XrayGuard() { if (mgr && !keepAlive) mgr->stopAll(); }
    } xrayGuard{xrayManager_, keepXrayAlive_};

    // E14: Reset shared network-down state at start of each batch.
    networkDown_.store(false, std::memory_order_relaxed);
    networkCv_.notify_all();
    running_.store(true, std::memory_order_release);

    std::vector<std::pair<int, int>> portPairs = xrayManager_->getPortPairs();
    int numWorkers = static_cast<int>(portPairs.size());
    {
        std::lock_guard<std::mutex> lock(workerStateMutex_);
        workerCurrentProxyIndex_.assign(numWorkers, -1);
    }
    workerAlive_.clear();
    for (int i = 0; i < numWorkers; ++i) {
        workerAlive_.push_back(std::make_shared<std::atomic<bool>>(true));
    }

    // Enqueue the original batch only; inserted proxies have their own queue
    // and must not be mixed in here (totalProxies_ may already have grown).
    for (int i = 0; i < static_cast<int>(proxies_.size()); ++i) {
        proxiesQueue_.push(i);
    }

    // Phase D: Start flush thread for batched DB writes
    resultQueue_.start();

    std::vector<std::thread> threads;
    try {
        for (int i = 0; i < numWorkers; ++i) {
            int socksPort = portPairs[i].first;
            int apiPort = portPairs[i].second;
            // Wrapper clears workerAlive_[i] on every exit path so the teardown
            // can detect a stuck worker (hung gRPC) and bound its join. The flag
            // is captured by value (shared_ptr) so it stays valid even if the
            // tester is destroyed while a detached worker is still running.
            std::shared_ptr<std::atomic<bool>> aliveFlag = workerAlive_[i];
            threads.emplace_back([this, aliveFlag, i, socksPort, apiPort]() {
                workerThreadFunc(i, socksPort, apiPort);
                aliveFlag->store(false, std::memory_order_release);
            });
        }
    } catch (...) {
        // Thread-creation failure: join any already-started threads then rethrow.
        for (std::thread& t : threads) {
            if (t.joinable()) t.join();
        }
        throw;
    }

    // Watcher: wait for batch queue to drain, then grace period for insertions
    std::atomic<bool> watcherDone{false};
    std::thread watcher([this, &watcherDone]() {
        // RAII: clear watcherDone on every exit path (normal, early return,
        // or exception) so the teardown's bounded join can detect completion.
        struct WatcherDoneGuard {
            std::atomic<bool>* flag;
            ~WatcherDoneGuard() { flag->store(true); }
        } watcherDoneGuard{&watcherDone};
        // Wait until batch queue is empty, or cancellation is requested.
        // On cancel the workers exit cooperatively but leave the queue
        // partially full (the remaining items are abandoned), so the queue
        // will never drain on its own — without this check the watcher
        // would spin here forever and the bounded join would detach it,
        // delaying the batch stop by the full 30s timeout.
        while (true) {
            if (externalCancel_ && externalCancel_->load()) {
                running_.store(false, std::memory_order_release);
                insertedCv_.notify_all();
                return;
            }
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                if (proxiesQueue_.empty()) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!running_.load()) return;
        }
        // Grace period: stay alive while insertions keep arriving, so a user
        // clicking "test this proxy" right at the end of the batch still lands
        // in the running pool instead of waiting for a new task.
        std::size_t seenInsertions = 0;
        {
            std::lock_guard<std::mutex> lock(insertedMutex_);
            seenInsertions = insertedProxies_.size();
        }
        for (int i = 0; i < 20 && running_.load(); ++i) {
            if (externalCancel_ && externalCancel_->load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            bool newWork = false;
            {
                std::lock_guard<std::mutex> lock(insertedMutex_);
                newWork = insertedProxies_.size() != seenInsertions;
                if (newWork) seenInsertions = insertedProxies_.size();
            }
            if (newWork) i = 0;
        }
        running_.store(false, std::memory_order_release);
        insertedCv_.notify_all();
    });

    // Join all workers. Workers exit via cooperative cancellation: interruptible
    // waits wake on cancelRequested_, and the blocking curl test is aborted by the
    // progress callback combined with bounded curl/Xray timeouts. BUT a worker can
    // still hang in a non-cooperative gRPC addOutboundDirect (no timeout, ignores
    // the cancel flag), which would make an unconditional join hang the batch task
    // forever and block the whole TaskQueue. So each join is bounded: if a worker
    // has not cleared workerAlive_[i] within 30s, detach it (the XrayGuard stopAll
    // below will tear down the instance, causing the hung gRPC to fail and the
    // detached worker to exit on its own).
    // A cancelled batch uses the 30s bound below (escape hatch so a hung worker
    // cannot block the batch task forever). A batch that was NOT cancelled must
    // never be force-killed by the bound: it may legitimately run for a long time
    // (e.g. a large/slow subscription), and workers drain the queue cooperatively
    // with hung-gRPC handled by BatchWorkerPolicy (instance relaunch / stop-worker).
    const bool batchCancelled = (externalCancel_ != nullptr && externalCancel_->load());
    for (int i = 0; i < static_cast<int>(threads.size()); ++i) {
        std::thread& t = threads[i];
        if (!t.joinable()) continue;
        if (i < static_cast<int>(workerAlive_.size())) {
            if (batchCancelled) {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                while (workerAlive_[i]->load() && std::chrono::steady_clock::now() < deadline) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                if (workerAlive_[i]->load()) {
                    Logger::write("[ProxyBatchTester] worker " + std::to_string(i)
                                  + " did not exit within 30s; detaching (stuck worker may hold a hung gRPC)",
                                  LogLevel::WARN);
                    t.detach();
                    continue;
                }
            } else {
                // Not cancelled: wait for natural completion (workers drain the
                // queue). The bound is intentionally NOT applied here so a
                // legitimately running batch is never cut short.
                while (workerAlive_[i]->load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
        }
        t.join();
    }

    // Ensure watcher is done. Bounded only when cancelled (the watcher waits for
    // proxiesQueue_ to empty, which can take a long time for a large batch).
    if (watcher.joinable()) {
        if (batchCancelled) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (!watcherDone.load() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (!watcherDone.load()) {
                Logger::write("[ProxyBatchTester] watcher did not exit within 30s; detaching", LogLevel::WARN);
                watcher.detach();
            } else {
                watcher.join();
            }
        } else {
            // Not cancelled: wait for the watcher to finish naturally (queue drained).
            watcher.join();
        }
    }

    // Phase D: Stop flush thread, drain any remaining queued results
    resultQueue_.stop();
    running_.store(false, std::memory_order_release);
}

void ProxyBatchTester::printSummary(ExitReason reason) {
    if (summaryPrinted_.exchange(true, std::memory_order_acq_rel)) return;  // avoid double-print
    const char* reasonStr = "normal";
    switch (reason) {
        case ExitReason::Cancelled: reasonStr = "cancelled"; break;
        case ExitReason::Crash: reasonStr = "crash"; break;
        case ExitReason::Shutdown: reasonStr = "shutdown"; break;
        default: reasonStr = "normal"; break;
    }
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("[Summary] Total: " + std::to_string(totalProxies_.load())
                 + " / Success: " + std::to_string(successCount_.load(std::memory_order_relaxed))
                 + " / Failed: " + std::to_string(failedCount_.load(std::memory_order_relaxed))
                 + " / Remaining: " + std::to_string(totalProxies_.load()
                     - successCount_.load(std::memory_order_relaxed)
                     - failedCount_.load(std::memory_order_relaxed))
                 + " (exit: " + reasonStr + ")", LogLevel::REPORT);
    Logger::write("========================================", LogLevel::REPORT);
}

void ProxyBatchTester::teardownWorkerResult(int workerId, const std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(workerStateMutex_);
    lastResult_ = TestResult{};
    lastResult_.errorMsg = errorMsg;
    (void)workerId;
}

void ProxyBatchTester::requestBatchCancellation(int workerId, int apiPort,
                                                const std::string& reason) {
    // Flag first, log second: a sibling worker may observe the cancellation and
    // exit before this line runs, so the latch must not depend on the log.
    const bool firstReport = !batchStopReported_.exchange(true);

    // externalCancel_ is the owning operation's flag (AppController::cancelRequested_
    // in GUI/CLI). Without it the batch would only lose THIS worker.
    if (externalCancel_) {
        externalCancel_->store(true);
    }
    cancelRequested_.store(true);
    // Notified without holding the mutex: both CVs use timed waits with an atomic
    // predicate, so this is a wake-up optimisation, never a correctness requirement.
    cancelCv_.notify_all();
    networkCv_.notify_all();

    if (firstReport) {
        Logger::write("[ProxyBatchTester] worker " + std::to_string(workerId) +
                      " (api=" + std::to_string(apiPort) + "): " + reason +
                      " — requesting whole-batch cancellation",
                      LogLevel::ERR);
    }
}

bool ProxyBatchTester::waitForNetworkRecovery() {
    Logger::write("[ProxyBatchTester] network disconnected — pausing, waiting for recovery", LogLevel::WARN);
    // Bounded polling instead of an unbounded condition-variable wait:
    // NetworkMonitor never notifies networkCv_, so once ALL workers sleep on
    // the CV nobody re-checks IsConnected() and a transient probe failure
    // would hang the whole batch forever. Poll every 250ms and give up after
    // 30s. Exceeding that budget raises a batch-wide cancellation (D1), it is
    // not a per-worker retirement.
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

// D1b (2026-10-19): the return value means "the batch was cancelled", NOT
// "the requested delay was interrupted". Callers do `if (waitInterruptible(n))
// return;`, which abandons the whole worker, so conflating a bounded network-wait
// timeout with cancellation is what silently killed workers. A timeout now ends
// only the delay; the batch-wide stop is raised by requestBatchCancellation().
bool ProxyBatchTester::waitInterruptible(int totalMs) {
    const int stepMs = 10;
    int elapsed = 0;
    while (elapsed < totalMs) {
        if (isCancelled()) return true;
        if (netMon_ && !netMon_->IsConnected()) {
            if (!waitForNetworkRecovery()) {
                // Timed out. requestBatchCancellation() already raised the batch-wide
                // stop, so isCancelled() is true here; returning it keeps the
                // "cancellation only" contract without assuming that.
                return isCancelled();
            }
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
    ExitReason reason = ExitReason::Normal;
    try {
        testProxiesMultiThreaded();
    } catch (...) {
        reason = ExitReason::Crash;
        printSummary(reason);
        if (!keepXrayAlive_) {
            xrayManager_->stopAll();
        }
        throw;
    }
    if (externalCancel_ && externalCancel_->load()) {
        reason = ExitReason::Cancelled;
    }
    printSummary(reason);
    if (!keepXrayAlive_) {
        xrayManager_->stopAll();
    }
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

    // Phase A (ProxyTestUnified): if proxies already loaded (e.g. by
    // loadProxies() for a multi-select test), do NOT reload from the default
    // SQL — otherwise the selected subset is replaced by the full DB.
    if (proxies_.empty()) {
        proxies_ = loadProxies();
        totalProxies_ = static_cast<int>(proxies_.size());

        if (totalProxies_ == 0) {
            Logger::write("No proxies to test", LogLevel::WARN);
            printSummary(ExitReason::Normal);
            return true;
        }

        Logger::write("[Testing] " + std::to_string(totalProxies_.load()) + " proxies total", LogLevel::REPORT);
    } else {
        totalProxies_ = static_cast<int>(proxies_.size());
        Logger::write("[Testing] " + std::to_string(totalProxies_.load()) + " selected proxies total", LogLevel::REPORT);
    }

    int instanceCount = calculateXrayInstanceCount(totalProxies_);
    if (!startXrayInstances(instanceCount)) {
        Logger::write("Failed to start xray instances", LogLevel::WARN);
        return false;
    }

    // Phase A: mark pool as running early so insertProxy() (multi-select fast
    // path) works during pre-generation, not only after workers are spawned.
    running_.store(true, std::memory_order_release);

    Logger::write("Started " + std::to_string(instanceCount) + " xray instances", LogLevel::DEBUG);

    if (!preGenerateConfigs(totalProxies_)) {
        running_.store(false, std::memory_order_release);
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
        printSummary(ExitReason::Normal);
        return false;
    }

    Logger::write("[Testing] " + std::to_string(totalProxies_.load()) + " proxies total", LogLevel::REPORT);
    Logger::write("Testing " + std::to_string(totalProxies_.load()) + " proxies from subscription: " + subId, LogLevel::DEBUG);

    int instanceCount = calculateXrayInstanceCount(totalProxies_);
    if (!startXrayInstances(instanceCount)) {
        Logger::write("Failed to start xray instances", LogLevel::WARN);
        return false;
    }

    // Phase A: mark pool as running early (see run() for rationale).
    running_.store(true, std::memory_order_release);

    Logger::write("Started " + std::to_string(instanceCount) + " xray instances", LogLevel::DEBUG);

    if (!preGenerateConfigs(totalProxies_)) {
        running_.store(false, std::memory_order_release);
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

    Logger::write("[Testing] single proxy: " + indexId, LogLevel::REPORT);

    if (!startXrayInstances(1)) {
        Logger::write("Failed to start xray instance", LogLevel::WARN);
        return false;
    }

    // Phase A: mark pool as running early (see run() for rationale).
    running_.store(true, std::memory_order_release);

    if (!preGenerateConfigs(totalProxies_)) {
        running_.store(false, std::memory_order_release);
        return false;
    }

    // Keep the Xray instance alive across consecutive single-proxy tests so
    // queued single-proxy tests can reuse it instead of restarting Xray each
    // time (optimization for "多次测试单个代理时，排队后每次都重启xray实例").
    keepXrayAlive_ = true;

    return runInternal();
}

bool ProxyBatchTester::loadProxies(const std::vector<std::string>& indexIds) {
    // Phase A (ProxyTestUnified): load a specific set of proxies (by indexId)
    // into proxies_ so a multi-select test can create one count=N pool instead
    // of N count=1 pools.
    db::models::ProfileitemDAO dao(db_);
    std::vector<db::models::Profileitem> profiles = dao.getAll();
    std::vector<db::models::Profileitem> filtered;
    for (std::size_t i = 0; i < indexIds.size(); ++i) {
        const std::string& id = indexIds[i];
        for (std::size_t j = 0; j < profiles.size(); ++j) {
            const db::models::Profileitem& p = profiles[j];
            if (p.indexid == id) {
                filtered.push_back(p);
                break;
            }
        }
    }
    proxies_ = std::move(filtered);
    totalProxies_ = static_cast<int>(proxies_.size());
    if (totalProxies_ == 0) {
        Logger::write("loadProxies: no matching proxies found", LogLevel::WARN);
        return false;
    }
    Logger::write("Loaded " + std::to_string(totalProxies_.load()) + " proxies for multi-select test", LogLevel::REPORT);
    return true;
}

bool ProxyBatchTester::insertProxy(const std::string& indexId) {
    if (!running_.load(std::memory_order_acquire)) {
        Logger::write("insertProxy: pool not running, cannot insert: " + indexId, LogLevel::WARN);
        return false;
    }
    db::models::ProfileitemDAO dao(db_);
    std::vector<db::models::Profileitem> profiles = dao.getAll();
    std::vector<db::models::Profileitem> filtered;
    std::copy_if(profiles.begin(), profiles.end(), std::back_inserter(filtered),
        [&indexId](const db::models::Profileitem& p) { return p.indexid == indexId; });

    if (filtered.empty()) {
        Logger::write("insertProxy: proxy not found: " + indexId, LogLevel::WARN);
        return false;
    }

    // Scheme B correctness: the inserted proxy needs its OWN pre-generated
    // Xray config. Without this the worker would fall back to a batch config
    // (preGenConfigs_[n]) that belongs to a completely different proxy and
    // would produce a meaningless verdict.
    config::XrayConfig insertedCfg;
    bool insertedCfgFailed = false;
    try {
        config::ConfigGenerator configGen(db_);
        insertedCfg = configGen.generateConfig(filtered[0]);
    } catch (const std::exception& e) {
        Logger::write("[ProxyBatchTester] insertProxy: config generation failed for "
                          + indexId + ": " + e.what(),
                      LogLevel::WARN);
        insertedCfg = config::XrayConfig{};
        insertedCfg.configFailed = true;
        insertedCfgFailed = true;
    }

    {
        std::lock_guard<std::mutex> lock(insertedMutex_);
        insertedProxies_.push_back(filtered[0]);
        insertedConfigs_.push_back(insertedCfg);
        insertedConfigFailed_.push_back(insertedCfgFailed);
        insertedQueue_.push(static_cast<int>(insertedProxies_.size()) - 1);
    }
    totalProxies_.fetch_add(1, std::memory_order_relaxed);
    insertedCv_.notify_all();

    Logger::write("Inserted high-priority proxy: " + indexId, LogLevel::DEBUG);
    return true;
}
