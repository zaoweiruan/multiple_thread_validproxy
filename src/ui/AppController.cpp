#include "AppController.h"
#include "config/StreamSettingsBuilder.h"
#include "BatchWorkerPolicy.h"
#include "Events.h"
#include "RefreshCommands.h"
#include "UnifiedMonitorRows.h"
#include "StandaloneAdoptionPolicy.h"
#include "ui/AsyncOperationGuard.h"
#include "ui/ScopeGuard.h"
#include "service/DatabaseConnectionService.h"

using ui::AsyncOperationGuard;
using ui::ScopeGuard;

#include "SubitemUpdaterV2.h"
#include "Profileexitem.h"
#include "ProxyBatchTester.h"
#include "ProxyTester.h"
#include "ConfigGenerator.h"
#include "config/ConfigBuilder.h"
#include "config/OutboundBuilderFactory.h"
#include "config/SingBoxOutboundBuilderFactory.h"
#include "ShareLink.h"
#include "PortManager.h"
#include "AutoTaskManager.h"
#include "Utils.h"
#include "Logger.h"
#include "RegionBatchResolver.h"
#include "ProcessInspector.h"
#include "ProcessExitListener.h"
#include "UrlFetcher.h"
#include "ProxyConnectivityVerifier.h"
#include "ProxyScorer.h"
#include "StandaloneConfigPort.h"

#include <wx/app.h>
#include <wx/event.h>
#include <wx/window.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <future>
#include <chrono>
#include <thread>
#include <atomic>
#include <boost/json.hpp>

// ---------------------------------------------------------------
// AppController implementation
// ---------------------------------------------------------------
AppController::AppController(sqlite3* db, const config::AppConfig& cfg)
    : db_(db), config_(cfg) {

    // P0-1: publish the TIME_WAIT rollback switch once, at startup. ConfigReader::load()
    // is disk IO per call, so the probing hot path must never read the config file —
    // it reads this atomic instead.
    utils::setTimeWaitBlocking(config_.port.time_wait_blocking);
    if (!config_.network_monitor.enabled) {
        netMon_.setEnabled(false);
    } else {
        netMon_.Start(config_.network_monitor.checkUrls,
                     config_.network_monitor.checkIntervalMs,
                     config_.network_monitor.checkTimeoutMs);
        // D2 was removed on 2026-10-19: the monitor no longer raises the
        // application cancel flag. Stopping a batch is ProxyBatchTester's own
        // decision, taken when one of its workers exhausts its 30s wait.
    }
    exitListener_ = new proc::ProcessExitListener();
}

AppController::~AppController() {
    // The proxy pool's evaluator thread calls back into this controller
    // (onMembersChanged captures `this` and posts wx events to topWindow_).
    // It MUST be stopped and joined before anything else in this destructor
    // tears down state the evaluator depends on (DB handle, topWindow_,
    // XrayManager). stopProxyPool() is idempotent (mutex + reset + null check).
    stopProxyPool();

    netMon_.Stop();

    // Standalone proxy processes run independently — NOT terminated here,
    // so they keep running when the main program exits.
    // However, we MUST shut down the exit listener to stop watching threads.
    shutdownRequested_ = true;
    if (exitListener_) {
        exitListener_->shutdown();
        delete exitListener_;
        exitListener_ = nullptr;
    }

    // Signal cancellation first so any in-flight async work can observe the flag
    cancelRequested_ = true;

    // Wait for worker thread to finish before releasing XrayManager.
    // WRONG order (release first, detach later) leaves a detached thread
    // running until the OS kills it — the worker may still access XrayManager
    // or xray instances after release() destroys the singleton, causing zombies.
    if (workerThread_.joinable()) {
        // Wait up to 10 seconds for thread to finish gracefully.
        // Worker threads may be stuck in network operations (curl, Sleep) that
        // cannot observe cancelRequested_ until the operation completes.
        // 10s covers typical curl timeouts (30s test_timeout / 2 attempts).
        std::future<void> fut = std::async(std::launch::async, [this]() {
            if (workerThread_.joinable()) {
                workerThread_.join();
            }
        });
        if (fut.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
            Logger::write("[AppController] Destructor: worker thread join timed out after 10s, detaching", LogLevel::WARN);
            workerThread_.detach();
        }
    }

    // Signal cancellation for any in-flight periodic probe, then join it
    // with the same bounded-wait pattern as workerThread_.
    // (cancelRequested_ was already set true at the top of this destructor,
    // so the probe loop exits quickly; no need to touch it here.)
    if (probeThread_.joinable()) {
        std::future<void> fut = std::async(std::launch::async, [this]() {
            if (probeThread_.joinable()) {
                probeThread_.join();
            }
        });
        if (fut.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
            Logger::write("[AppController] Destructor: probe thread join timed out after 10s, detaching", LogLevel::WARN);
            probeThread_.detach();
        }
    }

    // Now it is safe to shut down XrayManager — no thread still holds a ref
    XrayManager::release();
}

// ---------------------------------------------------------------
// Config
// ---------------------------------------------------------------
bool AppController::saveConfig(const config::AppConfig& cfg) {
    // Guard config_ against the background silent probe's config snapshot
    // (doTestOnlineProxies reads test_url / test_timeout_ms / pruneFailStreak
    // under this mutex). Ordinary config saves are allowed while the probe
    // runs; only DB-path swaps require full idle (see MainFrame::onMenuConfig).
    std::lock_guard<std::mutex> lock(configMutex_);
    config_ = cfg;

    // P0-1: keep the published rollback switch in sync after a config save.
    utils::setTimeWaitBlocking(config_.port.time_wait_blocking);
    // 同步 ECH 开关：否则「配置窗口勾选 ECH → 立即启动独立代理」用的仍是旧值
    // （setEchEnabled 只在 ConfigReader::load() 里下发，即只在进程启动时）。
    config::setEchEnabled(config_.proxy.ech_enabled);
    // Persist to config.json so changes survive restart
    std::string configPath = config::ConfigReader::getDefaultConfigPath();
    return config::ConfigReader::save(configPath, cfg);
}

// ---------------------------------------------------------------
// Database switching
// ---------------------------------------------------------------
sqlite3* AppController::switchDatabase(const std::string& newPath) {
    // Save the old handle; only close it after the new one opens successfully
    sqlite3* oldDb = db_;
    db_ = nullptr;

    // Open the new database into a local variable first
    sqlite3* newDb = nullptr;
    int rc = sqlite3_open_v2(newPath.c_str(), &newDb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        Logger::write("Failed to switch database: " + std::string(sqlite3_errmsg(newDb)), LogLevel::ERR);
        if (newDb) sqlite3_close(newDb);
        // Restore the old handle so the controller stays operational
        db_ = oldDb;
        return nullptr;
    }

    // Open succeeded — now it is safe to close the old handle
    if (oldDb) {
        sqlite3_close(oldDb);
    }
    db_ = newDb;

    service::DatabaseConnectionService::applyPragmas(db_);
    config_.database_path = newPath;

    // Reset monitoring state for the new database:
    // 1) Update the DAO handles to point at the new database.  Both DAOs
    //    were constructed with the original sqlite3*; without re-binding,
    //    exDao_ keeps referencing the (now-closed) old handle on every
    //    subsequent updateTestResult()/updateStartupTime() call.
    historyDao_.setDb(db_);
    exDao_.setDb(db_);
    // setDb() is a plain assignment: it does NOT re-run migrations. Without
    // this, a switched-in older database lacks the socks_port column and
    // lookupSocksPort() would fail for every dangling process.
    db::models::ProxyRuntimeHistoryDAO::migrateTable(db_);

    // 2) Clear stale standalone proxy state — old entries reference the
    //    previous database and may contain invalid runtimeHistoryId values.
    //    proxyWatchKeys_ is cleared inside the same critical section: the map
    //    is shared with the adoption sweep, which reads and writes it under
    //    standaloneMutex_ - clearing it outside the lock is a data race.
    {
        std::lock_guard<std::mutex> lock(standaloneMutex_);
        standaloneProxies_.clear();
        proxyWatchKeys_.clear();
    }

    // 3) Re-run dangling proxy adoption against the new database so any
    //    orphaned xray/sing-box processes are picked up and tracked.
    std::thread([this]() {
        adoptDanglingStandaloneProxies();
    }).detach();

    Logger::write("[AppController] Database switched to: " + newPath, LogLevel::INFO);
    return db_;
}

// ---------------------------------------------------------------
// Subscription operations
// ---------------------------------------------------------------
std::vector<db::models::Subitem> AppController::loadSubscriptions() {
    db::models::SubitemDAO dao(db_);
    return dao.getAll();
}

void AppController::loadSubscriptionsAsync(wxEvtHandler* handler) {
    std::string dbPath = config_.database_path;

    std::thread([dbPath, handler]() {
        sqlite3* readerDb = nullptr;
        if (sqlite3_open(dbPath.c_str(), &readerDb) != SQLITE_OK) {
            Logger::write("sqlite3_open failed for loadSubscriptionsAsync: " + dbPath, LogLevel::ERR);
            if (readerDb) sqlite3_close(readerDb);
            wxQueueEvent(handler, new SubListLoadedEvent({}, {}));
            return;
        }
        if (sqlite3_exec(readerDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            Logger::write("PRAGMA journal_mode=WAL failed for async reader: " + dbPath, LogLevel::WARN);
        }
        sqlite3_busy_timeout(readerDb, 5000);
        sqlite3_exec(readerDb, "PRAGMA cache_size=-8000;", nullptr, nullptr, nullptr);
        sqlite3_exec(readerDb, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(readerDb, "PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);
        sqlite3_exec(readerDb, "PRAGMA mmap_size=268435456;", nullptr, nullptr, nullptr);
        sqlite3_exec(readerDb, "PRAGMA journal_size_LIMIT=1073741824;", nullptr, nullptr, nullptr);

        db::models::SubitemDAO subDao(readerDb);
        std::vector<db::models::Subitem> subs = subDao.getAll();

        db::models::ProfileitemDAO proxyDao(readerDb);
        std::unordered_map<std::string, int> proxyCounts = proxyDao.countBySubId();

        sqlite3_close(readerDb);

        wxQueueEvent(handler, new SubListLoadedEvent(std::move(subs), std::move(proxyCounts)));
    }).detach();
}

bool AppController::updateSubscriptionEnabled(const std::string& id, bool enabled) {
    db::models::SubitemDAO dao(db_);
    return dao.updateEnabled(id, enabled);
}

bool AppController::updateSubitem(const db::models::Subitem& sub) {
    db::models::SubitemDAO dao(db_);
    return dao.updateSubitem(sub);
}

bool AppController::deleteProxiesBySubId(const std::string& subId) {
    db::models::ProfileitemDAO proxyDao(db_);
    return proxyDao.deleteBySubId(subId);
}

bool AppController::deleteSubscription(const std::string& subId) {
    // Delete associated proxies first
    db::models::ProfileitemDAO proxyDao(db_);
    proxyDao.deleteBySubId(subId);
    
    // Then delete the subscription itself
    db::models::SubitemDAO subDao(db_);
    return subDao.deleteById(subId);
}

void AppController::updateSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler) {
        AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, wxHandler};
        if (!guard.isAllowed()) return;
    workerThread_ = std::thread(&AppController::doUpdateSubscription, this, subId, wxHandler);
}

void AppController::updateAllSubscriptionsAsync(wxEvtHandler* wxHandler) {
        AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, wxHandler};
        if (!guard.isAllowed()) return;
    workerThread_ = std::thread(&AppController::doUpdateAllSubscriptions, this, wxHandler);
}

bool AppController::importSubscription(const std::string& url, wxEvtHandler* wxHandler) {
    try {
        update::SubitemUpdaterV2 updater(db_, config_.proxy.xray_executable, config_, nullptr, "");
        bool ok = updater.importSingleUrl(url);
        if (ok && wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0,
                std::string(ui::refresh_cmd::kRowsChanged)));
        }
        return ok;
    } catch (const std::exception& e) {
        Logger::write(std::string("Import error: ") + e.what(), LogLevel::ERR);
        return false;
    }
}

// ---------------------------------------------------------------
// Proxy operations
// ---------------------------------------------------------------
std::unordered_map<std::string, int> AppController::countProxiesBySubId() {
    db::models::ProfileitemDAO dao(db_);
    return dao.countBySubId();
}

std::unordered_map<std::string, int> AppController::countValidProxiesBySubId() {
    db::models::ProfileitemDAO dao(db_);
    return dao.countValidBySubId();
}

std::vector<db::models::Profileitem> AppController::loadProxies(const std::string& subId) {
    db::models::ProfileitemDAO dao(db_);
    if (subId.empty()) {
        std::vector<db::models::Profileitem> result = dao.getAll();
        return result;
    }
    std::vector<db::models::Profileitem> all = dao.getAll();
    std::vector<db::models::Profileitem> filtered;
    std::copy_if(all.begin(), all.end(), std::back_inserter(filtered),
        [&subId](const db::models::Profileitem& p) { return p.subid == subId; });
    return filtered;
}

void AppController::loadProxiesAsync(const std::string& subId, wxEvtHandler* handler) {
    std::string dbPath = config_.database_path;
    std::string subIdCopy = subId;

    std::thread([dbPath, subIdCopy, handler]() {
        sqlite3* readerDb = nullptr;
        if (sqlite3_open(dbPath.c_str(), &readerDb) != SQLITE_OK) {
            Logger::write("sqlite3_open failed for loadProxiesAsync: " + dbPath, LogLevel::ERR);
            if (readerDb) sqlite3_close(readerDb);
            wxQueueEvent(handler, new ProxyListLoadedEvent(subIdCopy, {}, {}, utils::ProxyListMaps()));
            return;
        }
        if (sqlite3_exec(readerDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            Logger::write("PRAGMA journal_mode=WAL failed for async reader: " + dbPath, LogLevel::WARN);
        }
        sqlite3_busy_timeout(readerDb, 5000);
        sqlite3_exec(readerDb, "PRAGMA cache_size=-8000;", nullptr, nullptr, nullptr);
        sqlite3_exec(readerDb, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(readerDb, "PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);
        sqlite3_exec(readerDb, "PRAGMA mmap_size=268435456;", nullptr, nullptr, nullptr);
        sqlite3_exec(readerDb, "PRAGMA journal_size_LIMIT=1073741824;", nullptr, nullptr, nullptr);

        db::models::ProfileitemDAO dao(readerDb);
        // Post the FULL unfiltered profile table: the panel caches it and
        // derives each subscription's subset in memory (instant switching).
        std::vector<db::models::Profileitem> proxies = dao.getAll();

        db::models::ProfileExItemDAO exDao(readerDb);
        std::vector<db::models::ProfileExItem> exItems = exDao.getAll();

        // Build lookup maps in the background thread so the UI thread only
        // assigns them into the model instead of iterating 50k+ items.
        utils::ProxyListMaps maps = utils::buildProxyListMaps(exItems);

        sqlite3_close(readerDb);

        wxQueueEvent(handler, new ProxyListLoadedEvent(subIdCopy,
                     std::move(proxies), std::move(exItems), std::move(maps)));
    }).detach();
}

std::optional<db::models::Profileitem> AppController::getProxyByIndexId(const std::string& indexId) {
    db::models::ProfileitemDAO dao(db_);
    return dao.getByIndexId(indexId);
}

std::string AppController::getSubIdByProxyIndexId(const std::string& indexId) {
    if (!db_ || indexId.empty()) return "";
    db::models::ProfileitemDAO dao(db_);
    std::optional<db::models::Profileitem> proxy = dao.getByIndexId(indexId);
    if (!proxy.has_value()) return "";
    return proxy->subid;
}

std::vector<pool_candidate::PoolCandidateItem> AppController::getPoolCandidateProfiles(int limit) {
    std::vector<pool_candidate::PoolCandidateItem> out;
    if (!db_) return out;
    const int cap = (limit > 0 && limit <= 1000) ? limit : 300;
    sqlite3_stmt* stmt = nullptr;
    // Join ProfileExItem to (a) filter to valid proxies only (delay > 0) and
    // (b) carry the extended columns the picker displays (delay / message /
    // start_count / crash_count). p.* keeps the first 36 columns identical to
    // Profileitem::fromStmt; the extra columns sit at 36+ and are read manually.
    std::string sql =
        "SELECT p.*, e.Delay, e.Message, e.start_count, e.crash_count "
        "FROM ProfileItem p "
        "INNER JOIN ProfileExItem e ON p.IndexId = e.IndexId "
        "WHERE CAST(e.delay AS INTEGER) > 0 "
        "LIMIT " + std::to_string(cap);
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        db::models::Profileitem p = db::models::Profileitem::fromStmt(stmt);
        pool_candidate::PoolCandidateItem item;
        item.indexid = p.indexid;
        item.configtype = p.configtype;
        item.address = p.address;
        item.remarks = p.remarks;
        item.region = p.region;
        const char* delay = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 36));
        item.delay = delay ? delay : "";
        const char* msg = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 37));
        item.message = msg ? msg : "";
        item.start_count = sqlite3_column_int(stmt, 38);
        item.crash_count = sqlite3_column_int(stmt, 39);
        out.push_back(item);
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<db::models::ProfileExItem> AppController::loadProxyResults() {
    db::models::ProfileExItemDAO dao(db_);
    std::vector<db::models::ProfileExItem> items = dao.getAll();

    // Compute history scores for UI display
    if (!items.empty()) {
        auto scores = scoring::computeBatch(items,
            config_.proxy.scoring_delay_weight * 5000.0,  // speed min ~0ms
            config_.proxy.scoring_delay_weight * 5000.0 + 3000.0); // speed max ~5000ms
        // Scores are already baked into exItems_ via rebuildMaps; no per-item mutation needed.
        // The model's healthMap_ is computed directly from start_count/crash_count in rebuildMaps().
    }

    return items;
}

// -------------------------------------------------------------------
// Small indexed query: load only the ProfileExItem rows for the given
// indexIds.  Used by ProxyListPanel::refreshResultsFor() so a single
// probe-completion event does not trigger a full 53k-row re-read.
// -------------------------------------------------------------------
std::vector<db::models::ProfileExItem> AppController::loadProxyResultsFor(
        const std::vector<std::string>& indexIds) {
    std::vector<db::models::ProfileExItem> rows;
    rows.reserve(indexIds.size());
    db::models::ProfileExItemDAO dao(db_);
    for (std::vector<std::string>::const_iterator it = indexIds.begin(); it != indexIds.end(); ++it) {
        std::optional<db::models::ProfileExItem> ex = dao.getByIndexId(*it);
        if (ex.has_value()) {
            rows.push_back(std::move(*ex));
        }
    }
    return rows;
}

// ---------------------------------------------------------------
std::unordered_map<std::string, long long> AppController::getRunningDurations() {
    std::unordered_map<std::string, long long> result;
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT index_id, duration_ms FROM proxy_runtime_history "
        "WHERE ended_at IS NULL AND duration_ms IS NOT NULL;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* idx = sqlite3_column_text(stmt, 0);
        long long dur = sqlite3_column_int64(stmt, 1);
        if (idx != nullptr) {
            result[reinterpret_cast<const char*>(idx)] = dur;
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

// ---------------------------------------------------------------
// Background-thread variant of getRunningDurations().  Opens its own
// read-only SQLite connection so no DB access happens on the UI thread,
// then posts RunningDurationsLoadedEvent back to the handler.  Mirrors
// loadProxiesAsync()'s PRAGMA setup for WAL read consistency.
// ---------------------------------------------------------------
void AppController::getRunningDurationsAsync(wxEvtHandler* handler) {
    std::string dbPath = config_.database_path;

    std::thread([dbPath, handler]() {
        std::unordered_map<std::string, long long> durations;
        sqlite3* readerDb = nullptr;
        if (sqlite3_open(dbPath.c_str(), &readerDb) != SQLITE_OK) {
            Logger::write("sqlite3_open failed for getRunningDurationsAsync: " + dbPath, LogLevel::ERR);
            if (readerDb) sqlite3_close(readerDb);
            wxQueueEvent(handler, new RunningDurationsLoadedEvent(std::move(durations)));
            return;
        }
        sqlite3_busy_timeout(readerDb, 5000);
        sqlite3_exec(readerDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(readerDb, "PRAGMA cache_size=-8000;", nullptr, nullptr, nullptr);
        sqlite3_exec(readerDb, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(readerDb, "PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);
        sqlite3_exec(readerDb, "PRAGMA mmap_size=268435456;", nullptr, nullptr, nullptr);

        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT index_id, duration_ms FROM proxy_runtime_history "
            "WHERE ended_at IS NULL AND duration_ms IS NOT NULL;";
        if (sqlite3_prepare_v2(readerDb, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* idx = sqlite3_column_text(stmt, 0);
                long long dur = sqlite3_column_int64(stmt, 1);
                if (idx != nullptr) {
                    durations[reinterpret_cast<const char*>(idx)] = dur;
                }
            }
            sqlite3_finalize(stmt);
        }

        sqlite3_close(readerDb);

        wxQueueEvent(handler, new RunningDurationsLoadedEvent(std::move(durations)));
    }).detach();
}

// ---------------------------------------------------------------
// Testing / Cancellation
// ---------------------------------------------------------------
void AppController::testSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler) {
        AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, wxHandler};
        if (!guard.isAllowed()) return;
    workerThread_ = std::thread(&AppController::doTestSubscription, this, subId, wxHandler);
}

void AppController::testSingleProxyAsync(const std::string& indexId, wxEvtHandler* wxHandler) {
        AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, wxHandler};
        if (!guard.isAllowed()) return;
    workerThread_ = std::thread(&AppController::doTestSingleProxy, this, indexId, wxHandler);
}

void AppController::testAllProxiesAsync(wxEvtHandler* wxHandler) {
        AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, wxHandler};
        if (!guard.isAllowed()) return;
    workerThread_ = std::thread(&AppController::doTestAllProxies, this, wxHandler);
}

void AppController::testOnlineProxiesAsync(wxEvtHandler* wxHandler, bool silent) {
    if (silent) {
        // 周期 silent 探活：独立线程，不占共享 workerThread_/isRunning_
        // （用户操作优先）。用户在跑其它操作、或上一轮探活尚未结束 →
        // 静默跳过本轮。
        if (isRunning_ || onlineProbeRunning_) {
            Logger::write("[OnlineProbe] skipped: user operation running "
                          "or previous probe still in flight", LogLevel::DEBUG);
            return;
        }
        // 上一轮探活已结束但尚未被 join（std::thread 完成后 joinable()
        // 仍为 true，直到 join/detach 为止）：此处快速回收，否则后续
        // 周期探活会被该残留 joinable 状态永久跳过。
        if (probeThread_.joinable()) {
            probeThread_.join();
        }
        // Mark the periodic silent probe so the config dialog can distinguish
        // this background task (ordinary config save allowed) from a
        // user-initiated operation (save blocked). doTestOnlineProxies clears
        // the flag via its ScopeGuard on every exit path.
        onlineProbeRunning_ = true;
        // 探活独立线程不消费/复位 cancelRequested_（AsyncOperationGuard 仅
        // 服务于用户操作路径）。断连或 cancelTest() 残留的 true 会让每轮
        // 探活线程在循环首行立即退出 → 周期探活静默停摆。此处置位安全：
        // 上一行检查已保证 isRunning_==false（无用户操作在跑）；且本函数
        // 在 UI 线程执行，与用户操作入口串行。
        cancelRequested_ = false;
        probeThread_ = std::thread(&AppController::doTestOnlineProxies, this, wxHandler, true);
        return;
    }

    // 手动「测试在线代理」：优先取消正在运行的探活（可中断后台任务），
    // 再走共享 workerThread_（用户操作占 isRunning_，原逻辑不变）。
    if (probeThread_.joinable()) {
        cancelRequested_ = true;
        probeThread_.join();
        cancelRequested_ = false;
    }
    AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, wxHandler};
    if (!guard.isAllowed()) return;
    workerThread_ = std::thread(&AppController::doTestOnlineProxies, this, wxHandler, false);
}

void AppController::cancelTest() {
    cancelRequested_ = true;
    long long ts = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    Logger::write("[AppController] cancelTest() called @ " + std::to_string(ts) + " ms (steady), cancelRequested_ set to true", LogLevel::DEBUG);
}

bool AppController::isTestCancelled() const {
    return cancelRequested_.load();
}

// ---------------------------------------------------------------
// Find operations  (sync — kept for CLI path in main.cpp)
// ---------------------------------------------------------------
TestResult AppController::findFirstProxy() {
    std::string xrayPath = config_.proxy.xray_executable;
    std::string configDir = utils::getExecutableDir() + "/config";
    XrayManager* manager = XrayManager::getInstance(xrayPath, configDir, config_.xray_workers, db_);
    ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms,
                        nullptr, &netMon_);
    finder.findFirstWorkingProxy();
    return finder.getLastResult();
}

TestResult AppController::findBestProxy() {
    std::string xrayPath = config_.proxy.xray_executable;
    std::string configDir = utils::getExecutableDir() + "/config";
    XrayManager* manager = XrayManager::getInstance(xrayPath, configDir, config_.xray_workers, db_);
    ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms,
                        nullptr, &netMon_);
    finder.findWorkingProxy();
    return finder.getLastResult();
}

// ---------------------------------------------------------------
// Find operations  (async — used by GUI MainFrame)
// ---------------------------------------------------------------
void AppController::findFirstProxyAsync(wxEvtHandler* wxHandler) {
        AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, wxHandler};
        if (!guard.isAllowed()) return;
    workerThread_ = std::thread(&AppController::doFindFirstProxy, this, wxHandler);
}

void AppController::findBestProxyAsync(wxEvtHandler* wxHandler) {
        AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, wxHandler};
        if (!guard.isAllowed()) return;
    workerThread_ = std::thread(&AppController::doFindBestProxy, this, wxHandler);
}

// ---------------------------------------------------------------
// Export / Tool operations
// ---------------------------------------------------------------
std::tuple<bool, int, std::string> AppController::exportShareLinks() {
    db::models::ProfileitemDAO dao(db_);

    std::string sql = R"(
        SELECT p.*, COALESCE(pe.Delay, 0) as ExDelay
        FROM ProfileItem p
        LEFT JOIN ProfileExItem pe ON p.IndexId = pe.IndexId
        WHERE CAST(COALESCE(pe.Delay, 0) AS INTEGER) > 0
        ORDER BY CAST(pe.Delay AS INTEGER) ASC
    )";

    std::vector<db::models::Profileitem> profiles = dao.getAll(sql);

    std::string output;
    int exportCount = 0;
    for (const db::models::Profileitem& profile : profiles) {
        std::string link = share::ShareLink::toShareUri(
            profile.configtype,
            profile.address,
            profile.port,
            profile.id,
            profile.security,
            profile.network,
            profile.flow,
            profile.sni,
            profile.alpn,
            profile.fingerprint,
            profile.allowinsecure,
            profile.path,
            profile.requesthost,
            profile.headertype,
            profile.streamsecurity,
            profile.remarks,
            profile.echconfiglist,
            profile.publickey,
            profile.shortid
        );
        if (!link.empty()) {
            output += link + "\n";
            ++exportCount;
        }
    }

    if (output.empty()) {
        Logger::write("No proxies to export", LogLevel::INFO);
        return {true, 0, ""};
    }

    char timestamp[32];
    time_t now = time(nullptr);
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));

    std::filesystem::path outPath = std::filesystem::path(utils::getExecutableDir()) / "proxies" / ("proxies_" + std::string(timestamp) + ".txt");
    std::filesystem::create_directories(outPath.parent_path());

    std::ofstream outFile(outPath, std::ios::binary);
    outFile << output;
    outFile.close();

    Logger::write("Exported " + std::to_string(exportCount) + " proxies to: " + outPath.string(), LogLevel::INFO);
    return {true, exportCount, outPath.filename().string()};
}

bool AppController::deduplicate() {
    update::SubitemUpdaterV2 updater(db_, config_.proxy.xray_executable, config_, nullptr, "");
    return updater.deduplicate();
}

bool AppController::syncDatabases(const std::string& src, const std::string& dst) {
    update::SubitemUpdaterV2 updater(db_, config_.proxy.xray_executable, config_, nullptr, "");
    return updater.syncDatabases(src.empty() ? config_.sync.source_db : src,
                                  dst.empty() ? config_.sync.target_db : dst);
}

bool AppController::generateConfig(const std::string& indexId) {
    try {
        config::ConfigGenerator gen(db_);
        std::vector<db::models::Profileitem> profiles = gen.loadProfiles("SELECT * FROM ProfileItem;");
        for (const db::models::Profileitem& p : profiles) {
            if (p.indexid == indexId) {
                config::XrayConfig result = gen.generateConfig(p);
                return !result.outbound_json.empty();
            }
        }
        Logger::write("Profile not found: " + indexId, LogLevel::ERR);
    } catch (const std::exception& e) {
        Logger::write(std::string("Generate config error: ") + e.what(), LogLevel::ERR);
    }
    return false;
}

// ---------------------------------------------------------------
// Xray
// ---------------------------------------------------------------
void AppController::stopXray() {
    XrayManager::release();
}

void AppController::restartNetworkMonitor() {
    netMon_.Stop();
    if (config_.network_monitor.enabled) {
        netMon_.setEnabled(true);
        netMon_.Start(config_.network_monitor.checkUrls,
                      config_.network_monitor.checkIntervalMs,
                      config_.network_monitor.checkTimeoutMs);
    } else {
        netMon_.setEnabled(false);
    }
}

// ---------------------------------------------------------------
// Standalone proxy management
// ---------------------------------------------------------------
bool AppController::isStandaloneProxyRunning(const std::string& indexId) const {
    const bool useSingBox = config_.proxy.use_singbox;
    const std::string configFileName =
        "standalone_" + indexId + (useSingBox ? "-singbox.json" : "-xray.json");
    return proc::ProcessInspector::isProcessRunningWithConfig(configFileName);
}

// (readFileTail removed — xray/sing-box output is now streamed to the local log
//  directly via an anonymous pipe + detached reader thread in startStandaloneProxy.)

// Reads the SOCKS inbound port from a standalone config file (xray:
// inbounds[].port / sing-box: listen_port); 0 when unreadable. Defined after
// getWatchedStandaloneMonitors, declared here so startStandaloneProxy (R6
// takeover) can resolve the port of a re-launched process.
// NOTE: this must stay a file-scope `static` declaration (NOT inside an
// anonymous namespace) so it denotes the same entity as the definition below.
static int readStandaloneInboundPort(const std::string& configPath);

bool AppController::startStandaloneProxy(const std::string& indexId, int overridePort) {
    std::unique_lock<std::mutex> lock(standaloneMutex_);

    // Fetch proxy from DB
    db::models::ProfileitemDAO dao(db_);
    db::models::Profileitem profile;
    {
        std::optional<db::models::Profileitem> opt = dao.getByIndexId(indexId);
        if (!opt) {
            Logger::write("[StandaloneProxy] Profile not found: " + indexId, LogLevel::ERR);
            return false;
        }
        profile = std::move(*opt);
    }

    // Determine which proxy backend to use based on use_singbox toggle
    bool useSingBox = config_.proxy.use_singbox;

    // R1: Check for an existing process using the same config file first, before
    // doing any further work (outbound generation, template read, port change,
    // process launch). The config file name is derived from the index id, so the
    // probe catches both a prior run from this same startStandaloneProxy call and
    // any externally-launched process using an identical config file name.
    //
    // Unlike the pre-3649905 behavior (which killed ALL xray/sing-box processes
    // by name), cleanup here is scoped to THIS indexId's config only: we locate
    // the exact process(es) whose command line references
    // "standalone_<indexId>-xray.json" / "-singbox.json" and, after user consent,
    // terminate only those specific PIDs before starting a fresh instance.
    const std::string configFileName = "standalone_" + indexId + (useSingBox ? "-singbox.json" : "-xray.json");
    std::vector<proc::ProcessInfo> existingProcs;
    {
        const char* exeNames[] = { "xray.exe", "sing-box.exe" };
        for (int e = 0; e < 2; ++e) {
            std::vector<proc::ProcessInfo> procs =
                proc::ProcessInspector::enumerateByName(exeNames[e]);
            for (std::size_t i = 0; i < procs.size(); ++i) {
                if (procs[i].commandLine.empty()) {
                    continue;  // PEB read failed - cannot confirm config match
                }
                if (proc::ProcessInspector::extractConfigFileName(procs[i].commandLine)
                        == configFileName) {
                    existingProcs.push_back(procs[i]);
                }
            }
        }
    }
    if (!existingProcs.empty()) {
        Logger::write("[StandaloneProxy] Found " + std::to_string(existingProcs.size())
                      + " existing process(es) using config '" + configFileName
                      + "'. Asking user whether to terminate before restarting.",
                      LogLevel::INFO);
        // Release the mutex while the modal dialog is shown (deadlock guard:
        // the dialog pumps messages and must not run while standaloneMutex_ is held).
        lock.unlock();
        const int answer = wxMessageBox(
            "已发现使用该配置的独立代理进程仍在运行。\n是否关闭旧进程后重新启动？",
            "独立代理已存在", wxYES_NO | wxICON_QUESTION, topWindow_);
        lock.lock();
        if (answer != wxYES) {
            Logger::write("[StandaloneProxy] User declined to terminate the existing "
                          "process; aborting start.", LogLevel::INFO);
            return false;
        }
        // Terminate each matching process (scoped to this indexId's config only).
        for (std::size_t i = 0; i < existingProcs.size(); ++i) {
            HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, existingProcs[i].pid);
            if (!hProcess) {
                Logger::write("[StandaloneProxy] OpenProcess failed for pid "
                              + std::to_string(existingProcs[i].pid), LogLevel::ERR);
                continue;
            }
            const BOOL ok = TerminateProcess(hProcess, 1);
            WaitForSingleObject(hProcess, 5000);
            CloseHandle(hProcess);
            Logger::write("[StandaloneProxy] Terminated existing standalone process pid "
                          + std::to_string(existingProcs[i].pid)
                          + (ok ? " (ok)" : " (failed)"), LogLevel::INFO);
        }
        // Re-check after termination; if a matching process still runs (e.g. access
        // denied), abort rather than risk a port conflict.
        if (proc::ProcessInspector::isProcessRunningWithConfig(configFileName)) {
            Logger::write("[StandaloneProxy] Existing process could not be terminated; "
                          "aborting start.", LogLevel::ERR);
            return false;
        }
    }

    // Use override port if provided, otherwise use configured SOCKS port
    int socksPort = (overridePort > 0) ? overridePort : config_.proxy.socks_base_port;
    
    // Determine executable and asset directory (needed for both paths)
    std::string exeDir = utils::getExecutableDir();
    std::string executable;
    std::string assetDir;
    std::string envVarName;
    
    if (useSingBox) {
        executable = config_.proxy.singbox_executable;
        assetDir = config_.proxy.singbox_asset_dir;
        envVarName = "SING_BOX_LOCATION_ASSET";
    } else {
        executable = config_.proxy.xray_executable;
        assetDir = config_.proxy.xray_asset_dir;
        envVarName = "XRAY_LOCATION_ASSET";
    }
    
    // Use ConfigBuilder to generate complete config from database templates
    Logger::write("[StandaloneProxy] Using ConfigBuilder for standalone proxy: " + indexId, LogLevel::INFO);
    config::ConfigBuilder builder(db_);
    config::ConfigVariables vars;
    vars.socksPort = socksPort;
    // P0-3: the API port used to be the hard-coded literal 10085 with no
    // availability check, so a second standalone proxy (or the pool) collided on
    // it and fell into the 5s failure branches. Honour the configured base and
    // shift away from a genuinely occupied port. Note that a TIME_WAIT-only row
    // does NOT block a bind on Windows (tw5), so it will NOT trigger a shift here.
    const int apiBase = (config_.proxy.api_base_port > 0)
                            ? config_.proxy.api_base_port
                            : 10085;
    int apiPort = apiBase;
    if (utils::isPortBlocking(apiPort)) {
        const int shifted = utils::findAvailablePort(apiPort + 1, 32);
        if (shifted > 0) {
            apiPort = shifted;
        } else {
            // Never let 0 reach the config template (it would render
            // "api.listen": "127.0.0.1:0"), and the 5s failure branches downstream
            // only cover timeout / crash / connectivity, not an invalid config.
            Logger::write("[AppController] no free gRPC API port in ["
                          + std::to_string(apiBase + 1) + ", "
                          + std::to_string(apiBase + 32) + "] (base="
                          + std::to_string(apiBase) + ")", LogLevel::ERR);
            return false;
        }
    }
    vars.apiPort = apiPort;
    
    std::string backend = useSingBox ? "singbox" : "xray";
    boost::json::value configJson = builder.build(config::ConfigMode::Standalone, backend, profile, vars);
    
    boost::json::object configObj;
    if (configJson.is_object()) {
        configObj = configJson.as_object();
        Logger::write("[StandaloneProxy] Config generated by ConfigBuilder for: " + indexId, LogLevel::DEBUG);
    } else {
        Logger::write("[StandaloneProxy] ConfigBuilder returned invalid config", LogLevel::ERR);
        return false;
    }

    // Ensure config subdirectory exists
    std::string configDir = exeDir + "\\config";
    std::error_code ec;
    if (!std::filesystem::exists(configDir) && !std::filesystem::create_directories(configDir, ec)) {
        Logger::write("[StandaloneProxy] Failed to create config dir: " + ec.message(), LogLevel::ERR);
        return false;
    }

    // Write the final config file
    std::string configPath = configDir + "\\standalone_" + indexId + (useSingBox ? "-singbox.json" : "-xray.json");
    {
        std::ofstream configFile(configPath);
        if (!configFile.is_open()) {
            Logger::write("[StandaloneProxy] Failed to write config: " + utils::toWindowsPath(configPath), LogLevel::ERR);
            return false;
        }
        configFile << boost::json::serialize(configObj);
        configFile.close();
    }

    // Determine asset directory
    if (assetDir.empty()) {
        assetDir = std::filesystem::path(executable).parent_path().parent_path().string();
    }

    // Build command line
    std::string cmd;
    if (useSingBox) {
        cmd = "\"" + executable + "\" run -c \"" + configPath + "\"";
    } else {
        cmd = "\"" + executable + "\" run -c \"" + configPath + "\"";
    }
    Logger::write("[StandaloneProxy] Executing: " + cmd, LogLevel::INFO);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    // Set environment variable (inherited by the child via its environment block)
    SetEnvironmentVariableA(envVarName.c_str(), assetDir.c_str());

    // Launch in its OWN console window so xray/sing-box output (config parse
    // errors, "address already in use", panic stacks) is shown directly to the
    // user. No stdout/stderr redirection: the output goes to the visible window.
    // On failure the return value (GetLastError for CreateProcess, or the process
    // exit code on early crash) is logged below — see logCrashReason.
    BOOL created = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                                  CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi);
    if (!created) {
        DWORD err = GetLastError();
        Logger::write("[StandaloneProxy] 启动失败: CreateProcess 返回值=0(FALSE), GetLastError="
                      + std::to_string(err), LogLevel::ERR);
        return false;
    }

    // Helper: log the process exit code so the failure reason is also captured in
    // the application's local log. xray/sing-box stdout/stderr (incl. crash reasons)
    // is shown in the independent proxy console window; here we record the exit code.
    auto logCrashReason = [&](const std::string& where) {
        DWORD code = 0;
        const bool exited =
            (GetExitCodeProcess(pi.hProcess, &code) && code != STILL_ACTIVE);
        std::string msg = "[StandaloneProxy] " + where + " for " + indexId
                          + " (SOCKS5 :" + std::to_string(socksPort) + ")";
        msg += exited ? (", exitCode=" + std::to_string(code))
                     : ", exitCode=STILL_ACTIVE(unknown)";
        msg += "; xray/sing-box 输出（含崩溃原因）见独立代理控制台窗口";
        Logger::write(msg, LogLevel::ERR);
    };

    // Store process info (no Job Object — process runs independently from parent)
    StandaloneProxyInfo info;
    info.indexId = indexId;
    info.configPath = configPath;
    info.socksPort = socksPort;
    info.processHandle = pi.hProcess;
    info.running = true;
    info.startedByThisSession = true;
    standaloneProxies_[indexId] = std::move(info);
    // Remember that this pid is one of ours (spec v1.6): the dangling-adoption
    // scan takes the "bypass detection" route for our own processes instead
    // of probing the OS for a port we already know. The creation time is
    // captured from the handle we own, so a recycled pid can be detected.
    SessionStartedProxy sessionInfo;
    sessionInfo.socksPort = socksPort;
    sessionInfo.creationTime = proc::ProcessInspector::processCreationTime(pi.hProcess);
    sessionStartedProxies_[pi.dwProcessId] = sessionInfo;

    CloseHandle(pi.hThread);

    // R2: Verify proxy connectivity synchronously (before notifying the UI), so the
    // UI only sees the proxy as "started" when it is known to be operational.
    // Spec §3.3.2: retry up to 3 attempts through the SOCKS5 port; on failure ask the
    // user whether to terminate the process or leave it alive but unmanaged.
    {
        // Wait for the SOCKS port to become reachable first: the xray/sing-box
        // process takes several seconds to start listening, so probing too early
        // always fails with connection refused. Budget 3x the test timeout.
        const bool portReady = proxy::ConnectivityVerifier::waitForPort(
            socksPort, config_.test_timeout_ms * 3, pi.hProcess);
        if (!portReady) {
            const bool dead = (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0);
            if (dead) {
                logCrashReason("flash-crash (process exited before SOCKS port ready)");
            } else {
                Logger::write("[StandaloneProxy] SOCKS port never became ready for " + indexId
                              + " on SOCKS5 :" + std::to_string(socksPort)
                              + " (process alive, timeout)", LogLevel::WARN);
            }
            exDao_.updateTestResult(indexId, -1, false, dead ? "process crashed" : "port never ready");
            // Release lock before modal dialog — see deadlock note above.
            lock.unlock();
            const std::string msg = dead
                ? ("代理进程闪崩（SOCKS 端口从未就绪）。崩溃原因见独立代理控制台窗口")
                : "代理进程已启动，但 SOCKS 端口在超时内未就绪。";
            const int rc = wxMessageBox(msg, "代理启动失败", wxYES_NO | wxCANCEL, topWindow_);
            lock.lock();
            if (rc == wxYES) {
                const BOOL termOk = TerminateProcess(pi.hProcess, 1);
                if (!termOk) {
                    const DWORD termErr = GetLastError();
                    Logger::write("[StandaloneProxy] TerminateProcess FAILED (err=" + std::to_string(termErr)
                                  + ") for " + indexId, LogLevel::ERR);
                }
                WaitForSingleObject(pi.hProcess, 5000);
                CloseHandle(pi.hProcess);
                standaloneProxies_.erase(indexId);
                // The process is gone: drop the session registration so the
                // adoption scan no longer mistakes this pid for one of ours.
                sessionStartedProxies_.erase(pi.dwProcessId);
                Logger::write("[StandaloneProxy] Terminated unverified process for " + indexId,
                              LogLevel::INFO);
                return false;
            }
            StandaloneProxyInfo& info = standaloneProxies_[indexId];
            info.running = false;
            info.managed = false;
            Logger::write("[StandaloneProxy] Proxy left unmanaged (port never ready) for " + indexId,
                          LogLevel::WARN);
            return true;
        }

        // Crash-aware gate: the SOCKS port may have opened, then the process died
        // before we ran the connectivity probe. Don't run a pointless test against a
        // dead proxy — report the crash instead of a misleading "connectivity FAILED".
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
            logCrashReason("port opened then crashed before connectivity test");
            exDao_.updateTestResult(indexId, -1, false, "process crashed after port ready");
            lock.unlock();
            const int rc = wxMessageBox(
                "代理进程在连通性验证前闪崩。崩溃原因见独立代理控制台窗口",
                "代理启动失败", wxYES_NO | wxCANCEL, topWindow_);
            lock.lock();
            if (rc == wxYES) {
                const BOOL termOk = TerminateProcess(pi.hProcess, 1);
                if (!termOk) {
                    const DWORD termErr = GetLastError();
                    Logger::write("[StandaloneProxy] TerminateProcess FAILED (err=" + std::to_string(termErr)
                                  + ") for " + indexId, LogLevel::ERR);
                }
                WaitForSingleObject(pi.hProcess, 5000);
                CloseHandle(pi.hProcess);
                standaloneProxies_.erase(indexId);
                // The process is gone: drop the session registration so the
                // adoption scan no longer mistakes this pid for one of ours.
                sessionStartedProxies_.erase(pi.dwProcessId);
                Logger::write("[StandaloneProxy] Terminated crashed process for " + indexId,
                              LogLevel::INFO);
                return false;
            }
            StandaloneProxyInfo& info = standaloneProxies_[indexId];
            info.running = false;
            info.managed = false;
            Logger::write("[StandaloneProxy] Proxy left unmanaged (crashed after port ready) for " + indexId,
                          LogLevel::WARN);
            return true;
        }

        const bool ok = proxy::ConnectivityVerifier::verify(
            socksPort, config_.test_url, config_.test_timeout_ms, 3);
        if (!ok) {
            Logger::write("[StandaloneProxy] Connectivity test FAILED for " + indexId
                          + " on SOCKS5 :" + std::to_string(socksPort), LogLevel::WARN);
            exDao_.updateTestResult(indexId, -1, false, "connectivity verification failed");
            // Ask the user whether to keep the (possibly broken) process alive.
            // topWindow_ may be null in headless/CLI runs; wxMessageBox accepts a
            // null parent and still shows the dialog.
            // Release lock before modal dialog — see note in portReady path above.
            lock.unlock();
            const int rc = wxMessageBox("代理已启动但连通性验证失败，是否关闭该进程？",
                                        "连通性验证失败", wxYES_NO | wxCANCEL, topWindow_);
            lock.lock();
            if (rc == wxYES) {
                const BOOL termOk = TerminateProcess(pi.hProcess, 1);
                if (!termOk) {
                    const DWORD termErr = GetLastError();
                    Logger::write("[StandaloneProxy] TerminateProcess FAILED (err=" + std::to_string(termErr)
                                  + ") for " + indexId, LogLevel::ERR);
                }
                WaitForSingleObject(pi.hProcess, 5000);
                CloseHandle(pi.hProcess);
                standaloneProxies_.erase(indexId);
                // The process is gone: drop the session registration so the
                // adoption scan no longer mistakes this pid for one of ours.
                sessionStartedProxies_.erase(pi.dwProcessId);
                Logger::write("[StandaloneProxy] Terminated unverified process for " + indexId,
                              LogLevel::INFO);
                return false;
            }
            // wxNO / wxCANCEL → keep the process alive AND keep monitoring it.
            // Monitoring must not depend on the connectivity probe result: the
            // process is running, so it needs a history row + exit-listener watch
            // and must show up in the floating monitor. Tracking (below) proceeds
            // unchanged; only the "startup time" stamp stays success-only.
            Logger::write("[StandaloneProxy] Proxy kept alive but unverified; tracking it anyway for "
                          + indexId, LogLevel::WARN);
        } else {
            Logger::write("[StandaloneProxy] Connectivity test PASSED for " + indexId
                          + " on SOCKS5 :" + std::to_string(socksPort), LogLevel::REPORT);
            // Write startup time into ProfileExItem.message (+<startup time>)
            exDao_.updateStartupTime(indexId, db_);
        }
        if (topWindow_) {
            wxQueueEvent(topWindow_, new StandaloneProxyEvent(
                indexId, "127.0.0.1", socksPort, /*started=*/true,
                utils::getCurrentTimestamp()));
        }
    }

    // Track runtime history for PID-based exit listening. started_at uses the
    // process system creation time (spec §3.4) so the session time base is
    // real and the (indexId, pid, startedAt) triplet identifies this exact
    // process instance; fall back to now when creation time cannot be read.
    const std::string startedAt =
        proc::ProcessInspector::processCreationTime(pi.dwProcessId);
    const std::string safeStartedAt =
        startedAt.empty() ? utils::getCurrentTimestampFormatted() : startedAt;
    int64_t rhId = historyDao_.insertStart(
        indexId, safeStartedAt, static_cast<int64_t>(pi.dwProcessId), db_);
    if (rhId >= 0) {
        runtimeHistoryId_ = rhId;
        // Record the SOCKS port so a later adoption can recover it when the
        // on-disk config file is gone. Best effort: a failure here must not
        // abort a successfully launched proxy.
        if (!historyDao_.updateSocksPort(rhId, socksPort, db_)) {
            Logger::write("[StandaloneProxy] Failed to record socks_port for "
                          + indexId, LogLevel::WARN);
        }
        const std::string startedAtStr = safeStartedAt;

        // Heartbeat/finalize baseline: process may have started before we
        // began watching (normally ~0 here; nonzero for adoption/takeover).
        const long long baselineMs =
            safeStartedAt.empty()
                ? 0LL
                : proc::ProcessInspector::durationMsBetween(
                      safeStartedAt, utils::getCurrentTimestampFormatted());

        // Remember the pid so the notify callback can drop our session
        // bookkeeping for it (spec v1.6): once the process is gone the pid
        // must no longer be treated as "one of ours", otherwise a recycled
        // pid could be mistaken for a session started proxy.
        const DWORD startedPid = pi.dwProcessId;
        proxyWatchKeys_[indexId] = exitListener_->watch(
            pi.hProcess, indexId, rhId,
            // InsertFn — not needed for standalone, returns -1
            [](sqlite3*, const std::string&, const std::string&) -> int64_t { return -1; },
            // AliveFn — always alive since we have the handle
            [rhId](int64_t, sqlite3*) -> bool { return true; },
            // EnumerateFn — return our single row
            [rhId, startedAtStr]() -> std::vector<std::pair<int64_t, std::string>> {
                return {{rhId, startedAtStr}};
            },
            // FinalizeFn — backfill ended_at/exit_code/duration
            [this](sqlite3* db, const std::string& idx, int64_t hid,
                   const std::string& endedAt, int exitCode, int64_t durMs) -> bool {
                return historyDao_.finalizeStop(hid, endedAt, exitCode, durMs, db_);
            },
            // NotifyFn — mark not-running + notify UI
            [this, startedPid](const std::string& idx, int64_t hid) {
                {
                    std::lock_guard<std::mutex> lock(standaloneMutex_);
                    auto it = standaloneProxies_.find(idx);
                    if (it != standaloneProxies_.end()) {
                        it->second.running = false;
                    }
                    sessionStartedProxies_.erase(startedPid);
                }
                if (topWindow_) {
                    wxQueueEvent(topWindow_, new StandaloneProxyEvent(
                        idx, "", 0, /*started=*/false, "进程已退出"));
                }
            },
            // TakeoverFn (R6 auto-takeover): when the old process exits but a new one
            // with the same config file is detected, take over the new process.
            [this, indexId](
                int oldPid, const std::string& oldIndexId, int64_t oldHistoryId,
                const std::string& cfName) -> bool {
                // If the proxy was manually stopped (running=false), do NOT takeover.
                {
                    std::lock_guard<std::mutex> lock(standaloneMutex_);
                    auto it = standaloneProxies_.find(indexId);
                    if (it == standaloneProxies_.end() || !it->second.running) {
                        return false;
                    }
                }
                // Find any new xray/sing-box process using the same config file
                const std::string exeName =
                    (cfName.find("-singbox.json") != std::string::npos)
                        ? "sing-box.exe"
                        : "xray.exe";
                std::vector<proc::ProcessInfo> procs =
                    proc::ProcessInspector::enumerateByName(exeName);
                for (std::size_t i = 0; i < procs.size(); ++i) {
                    if (procs[i].pid == static_cast<DWORD>(oldPid)) {
                        continue;  // skip the exiting process itself
                    }
                    if (!proc::ProcessInspector::matchesConfig(
                            procs[i].commandLine,
                            /*nameMatched=*/true,
                            /*readFailed=*/procs[i].commandLine.empty(),
                            cfName)) {
                        continue;
                    }
                    // PROCESS_TERMINATE is required so the adopted process can
                    // later be stopped by stopStandaloneProxy (TerminateProcess
                    // fails with ERROR_ACCESS_DENIED without it).
                    HANDLE newHandle = OpenProcess(
                        PROCESS_QUERY_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE,
                        FALSE, procs[i].pid);
                    if (!newHandle) {
                        continue;
                    }
                    // Close the old handle from standaloneProxies_ (pi is a
                    // stack-local in startStandaloneProxy and must NOT be
                    // accessed here — it is already destroyed).
                    HANDLE oldHandle = nullptr;
                    {
                        std::lock_guard<std::mutex> lock(standaloneMutex_);
                        auto it = standaloneProxies_.find(indexId);
                        if (it != standaloneProxies_.end()) {
                            oldHandle = it->second.processHandle;
                            it->second.processHandle = nullptr;
                        }
                    }
                    if (oldHandle) {
                        CloseHandle(oldHandle);
                    }
                    // Insert new history start for the takeover session
                    // (fallback must be datetime-formatted: stored as started_at)
                    const std::string newStartedAt =
                        procs[i].creationTime.empty()
                            ? utils::getCurrentTimestampFormatted()
                            : procs[i].creationTime;
                    // Resolve the SOCKS port of the takeover session through the
                    // same chain as adoption (spec v1.4): new process command
                    // line config -> remembered port of the PREVIOUS process
                    // -> live LISTEN ports of the new process. The D2 gate is
                    // applied here because the remembered port belongs to the
                    // old process and must be disproven by the live facts of
                    // the new one before it is accepted.
                    int takeoverPort = 0;
                    std::string takeoverConfigPath =
                        proc::ProcessInspector::extractConfigFullPath(
                            procs[i].commandLine);
                    int takeoverCmdlinePort = 0;
                    if (!takeoverConfigPath.empty()) {
                        takeoverCmdlinePort = readStandaloneInboundPort(takeoverConfigPath);
                    }
                    int takeoverMemoryPort = 0;
                    {
                        std::lock_guard<std::mutex> infoLock(standaloneMutex_);
                        std::unordered_map<std::string, StandaloneProxyInfo>::const_iterator infoIt =
                            standaloneProxies_.find(indexId);
                        if (infoIt != standaloneProxies_.end()) {
                            takeoverMemoryPort = infoIt->second.socksPort;
                        }
                    }
                    const int takeoverApiPort =
                        (config_.proxy.api_base_port > 0) ? config_.proxy.api_base_port : 10085;
                    const proc::ProcessInspector::TcpPortScan takeoverScan =
                        proc::ProcessInspector::scanListeningTcpPorts(procs[i].pid);
                    const std::vector<int> takeoverOsPorts = takeoverScan.ports;
                    // The R6 takeover fires the moment the old process exits, so
                    // the replacement process usually has NOT bound its SOCKS
                    // inbound yet. An empty port set therefore means "not
                    // listening YET", not "not listening at all" - the config
                    // and in-memory ports must NOT be disproven here, otherwise
                    // the takeover would record port 0.
                    const bool takeoverOsFactsReadable =
                        takeoverScan.readOk && !takeoverOsPorts.empty();
                    const std::vector<int> takeoverOsCandidates =
                        standalone_adoption::buildOsCandidates(takeoverOsPorts, takeoverApiPort);
                    std::vector<int> takeoverSocksPorts;
                    for (std::vector<int>::const_iterator it = takeoverOsCandidates.begin();
                         it != takeoverOsCandidates.end(); ++it) {
                        if (utils::respondsToSocks5Greeting(*it, 200)) {
                            takeoverSocksPorts.push_back(*it);
                        }
                    }
                    std::vector<standalone_adoption::PortCandidate> takeoverChain;
                    standalone_adoption::PortCandidate takeoverCmdlineCand;
                    takeoverCmdlineCand.port = takeoverCmdlinePort;
                    takeoverCmdlineCand.source = standalone_adoption::PortSource::CmdlineConfig;
                    takeoverChain.push_back(takeoverCmdlineCand);
                    standalone_adoption::PortCandidate takeoverMemoryCand;
                    takeoverMemoryCand.port = takeoverMemoryPort;
                    takeoverMemoryCand.source = standalone_adoption::PortSource::RuntimeHistory;
                    takeoverChain.push_back(takeoverMemoryCand);
                    const std::vector<int> takeoverOrderedOs =
                        standalone_adoption::orderOsCandidatesBySocks(
                            takeoverOsCandidates, takeoverSocksPorts);
                    for (std::vector<int>::const_iterator it = takeoverOrderedOs.begin();
                         it != takeoverOrderedOs.end(); ++it) {
                        standalone_adoption::PortCandidate takeoverOsCand;
                        takeoverOsCand.port = *it;
                        takeoverOsCand.source = standalone_adoption::PortSource::OsTcpTable;
                        takeoverChain.push_back(takeoverOsCand);
                    }
                    const standalone_adoption::PortResolution takeoverResolution =
                        standalone_adoption::resolveChainPort(
                            takeoverChain, takeoverOsPorts, takeoverSocksPorts,
                            takeoverApiPort, takeoverOsFactsReadable);
                    takeoverPort = takeoverResolution.port;
                    // Append the takeover session row carrying the resolved
                    // port (append semantics: a takeover is a NEW instance).
                    int64_t newRhId = historyDao_.insertStartWithPort(
                        indexId, newStartedAt,
                        static_cast<int64_t>(procs[i].pid), takeoverPort, db_);
                    if (newRhId < 0) {
                        CloseHandle(newHandle);
                        return false;
                    }
                    // Update standaloneProxies_ with the new handle AND the
                    // new port/startedAt/configPath — otherwise the in-memory
                    // entry keeps the port of the previous process and the
                    // periodic online probe keeps hitting a dead port.
                    {
                        std::lock_guard<std::mutex> lock(standaloneMutex_);
                        auto& sp = standaloneProxies_[indexId];
                        sp.processHandle = newHandle;
                        sp.running = true;
                        sp.runtimeHistoryId = newRhId;
                        sp.socksPort = takeoverPort;
                        sp.startedAt = newStartedAt;
                        if (!takeoverConfigPath.empty()) {
                            sp.configPath = takeoverConfigPath;
                        }
                    }
                    runtimeHistoryId_ = newRhId;
                    const long long takeoverBaselineMs =
                        newStartedAt.empty()
                            ? 0LL
                            : proc::ProcessInspector::durationMsBetween(
                                  newStartedAt, utils::getCurrentTimestampFormatted());
                    const proc::ProcessExitListener::WatchKey takeoverWatchKey =
                        exitListener_->watch(
                        newHandle, indexId, newRhId,
                        [](sqlite3*, const std::string&, const std::string&) -> int64_t {
                            return -1;
                        },
                        [newRhId](int64_t, sqlite3*) -> bool { return true; },
                        [newRhId, newStartedAt]()
                            -> std::vector<std::pair<int64_t, std::string>> {
                            return {{newRhId, newStartedAt}};
                        },
                        [this](sqlite3* db, const std::string& idx, int64_t hid,
                               const std::string& endedAt, int exitCode,
                               int64_t durMs) -> bool {
                            return historyDao_.finalizeStop(
                                hid, endedAt, exitCode, durMs, db_);
                        },
                        [this](const std::string& idx, int64_t hid) {
                            {
                                std::lock_guard<std::mutex> lock(standaloneMutex_);
                                auto it = standaloneProxies_.find(idx);
                                if (it != standaloneProxies_.end()) {
                                    it->second.running = false;
                                }
                            }
                            if (topWindow_) {
                                wxQueueEvent(
                                    topWindow_,
                                    new StandaloneProxyEvent(
                                        idx, "", 0, /*started=*/false,
                                        "进程已退出"));
                            }
                        },
                        /*takeoverFn*/ nullptr,
                        /*configFileName*/ "",
                        /*heartbeatFn*/ [this](const std::string&, int64_t hid,
                                               int64_t elapsedMs) {
                            historyDao_.touchHeartbeat(hid, elapsedMs, db_);
                        },
                        /*baselineElapsedMs*/ takeoverBaselineMs);
                    {
                        // proxyWatchKeys_ is shared with the adoption sweep,
                        // which reads and writes it under standaloneMutex_.
                        std::lock_guard<std::mutex> lock(standaloneMutex_);
                        proxyWatchKeys_[indexId] = takeoverWatchKey;
                    }
                    Logger::write(
                        "[StandaloneProxy] R6 auto-takeover: adopted new " +
                        exeName + " pid=" +
                        std::to_string(procs[i].pid) +
                        " for indexId=" + indexId,
                        LogLevel::REPORT);
                    return true;
                }
                // No takeover candidate found — fall through to normal finalize
                return false;
            },
            configFileName,  // R6: pass configFileName into the Watcher
            /*heartbeatFn*/ [this](const std::string&, int64_t hid,
                                   int64_t elapsedMs) {
                historyDao_.touchHeartbeat(hid, elapsedMs, db_);
            },
            /*baselineElapsedMs*/ baselineMs
        );

        // Backfill the map entry (this function already holds standaloneMutex_)
        // so stopStandaloneProxy can unwatch this session and the monitor
        // dialog can join the live process with its history row. Without this,
        // watchKey stays 0 and unwatch is skipped (watcher leak).
        StandaloneProxyInfo& startedInfo = standaloneProxies_[indexId];
        startedInfo.runtimeHistoryId = rhId;
        startedInfo.watchKey = proxyWatchKeys_[indexId];
        startedInfo.startedAt = safeStartedAt;
    }
    Logger::write("[StandaloneProxy] Started " + indexId + " on SOCKS5 :" + std::to_string(socksPort),
                  LogLevel::REPORT);
    return true;
}

int AppController::getStandaloneSocksPort(const std::string& indexId) const {
    std::lock_guard<std::mutex> lock(standaloneMutex_);
    std::unordered_map<std::string, StandaloneProxyInfo>::const_iterator it = standaloneProxies_.find(indexId);
    if (it != standaloneProxies_.end() && (it->second.running || !it->second.managed)) {
        return it->second.socksPort;
    }
    return -1;
}

void AppController::setTopWindow(wxWindow* win) {
    topWindow_ = win;
}

std::vector<std::string> AppController::getRunningStandaloneIds() const {
    std::lock_guard<std::mutex> lock(standaloneMutex_);
    std::vector<std::string> ids;
    for (std::unordered_map<std::string, StandaloneProxyInfo>::const_iterator it2 = standaloneProxies_.begin(); it2 != standaloneProxies_.end(); ++it2) {
        if (it2->second.running) {
            ids.push_back(it2->first);
        }
    }
    return ids;
}

int AppController::getRunningStandaloneCount() const {
    std::lock_guard<std::mutex> lock(standaloneMutex_);
    int count = 0;
    for (std::unordered_map<std::string, StandaloneProxyInfo>::const_iterator it = standaloneProxies_.begin(); it != standaloneProxies_.end(); ++it) {
        if (it->second.running) {
            ++count;
        }
    }
    return count;
}

// Forward declaration moved above startStandaloneProxy (anonymous namespace)
// so the R6 takeover path can resolve the port of a re-launched process.

std::vector<StandaloneMonitorRow> AppController::getWatchedStandaloneMonitors() {
    // Pass 1: copy watched entries under the lock. Only running && managed
    // entries are shown — matches the "currently watched" semantics of the
    // monitor dialog (unmanaged processes have no history row / no watcher).
    struct WatchedEntry {
        std::string indexId;
        int socksPort;
        int64_t historyId;
        std::string fallbackStartedAt;
        std::string configFileName;  // 用于从磁盘配置文件重新读取实际监听端口
        std::string configPath;      // 权威完整配置路径（start/adopt 时记录）
    };
    std::vector<WatchedEntry> watched;
    {
        std::lock_guard<std::mutex> lock(standaloneMutex_);
        for (std::unordered_map<std::string, StandaloneProxyInfo>::const_iterator it =
                 standaloneProxies_.begin(); it != standaloneProxies_.end(); ++it) {
            if (!it->second.running || !it->second.managed) {
                continue;
            }
            WatchedEntry e;
            e.indexId = it->first;
            e.socksPort = it->second.socksPort;
            e.historyId = it->second.runtimeHistoryId;
            e.fallbackStartedAt = it->second.startedAt;
            e.configFileName = it->second.configFileName;
            e.configPath = it->second.configPath;
            watched.push_back(e);
        }
    }
    if (watched.empty()) {
        return std::vector<StandaloneMonitorRow>();
    }

    // Pass 2: join with in-progress history rows (pid/startedAt source of
    // truth). Small result set (one row per watched process), same UI-thread
    // read pattern as getRunningDurations().
    std::vector<db::models::ProxyRuntimeHistoryItem> sessions =
        historyDao_.getInProgressSessions();
    std::unordered_map<int64_t, const db::models::ProxyRuntimeHistoryItem*> byId;
    for (std::size_t i = 0; i < sessions.size(); ++i) {
        byId[sessions[i].id] = &sessions[i];
    }

    const std::string now = utils::getCurrentTimestampFormatted();
    db::models::ProfileitemDAO profileDao(db_);
    db::models::ProfileExItemDAO exDao(db_);
    std::vector<StandaloneMonitorRow> rows;
    for (std::size_t j = 0; j < watched.size(); ++j) {
        StandaloneMonitorRow row;
        row.indexId = watched[j].indexId;
        // Join the in-progress history row FIRST: it carries the port that
        // was resolved (and verified) when the process was adopted, and it is
        // the only *verified* port source available on this path.
        std::unordered_map<int64_t, const db::models::ProxyRuntimeHistoryItem*>::const_iterator hit =
            byId.find(watched[j].historyId);
        int historyPort = 0;
        if (hit != byId.end()) {
            row.pid = hit->second->pid;
            row.startedAt = hit->second->startedAt;
            historyPort = hit->second->socksPort;
        } else {
            // History row missing (insertStart failed or legacy entry):
            // fall back to the in-memory startedAt, pid unknown.
            row.pid = -1;
            row.startedAt = watched[j].fallbackStartedAt;
        }
        {
            // Port priority (spec v1.4):
            //   1) history socks_port  — resolved + verified at adoption time
            //   2) in-memory socksPort  — same verified value, kept as the
            //                             fallback for legacy rows without
            //                             a socks_port column value
            //   3) disk config file    — UNVERIFIED: the file may belong to
            //                             another process / another
            //                             installation (the 2026-09-02
            //                             port-drift defect class)
            // The disk value used to win outright, which re-introduced the
            // stale-port regression (live 10809 vs stale disk copy 10810).
            int diskPort = 0;
            std::string diskPath = watched[j].configPath;
            if (!diskPath.empty()) {
                diskPort = readStandaloneInboundPort(diskPath);
            }
            if (diskPort <= 0) {
                // Only the config directory of the running executable is probed
                // (spec v1.1.1): `<exeDir>\worker\config` belongs to the
                // separate `bin\worker\validproxy.exe` copy (a second
                // installation); probing it mixes ports of the same indexId
                // across installations (the 2026-09-02 port-drift defect
                // class). A process started by that copy carries the absolute
                // config path in its command line, so it is resolved by the
                // cmdline tier at adoption time.
                diskPath = utils::getExecutableDir() + "\\config\\" +
                           watched[j].configFileName;
                diskPort = readStandaloneInboundPort(diskPath);
            }
            if (historyPort > 0) {
                row.socksPort = historyPort;
            } else if (watched[j].socksPort > 0) {
                row.socksPort = watched[j].socksPort;
            } else {
                row.socksPort = diskPort;
            }
        }
        // Host = ProfileItem.Address (spec v1.1); "-" rendered by the dialog
        // when the profile has been deleted or the address is empty.
        std::optional<db::models::Profileitem> profile =
            profileDao.getByIndexId(watched[j].indexId);
        if (profile.has_value()) {
            row.host = profile->address;
        }
        row.durationMs = row.startedAt.empty()
            ? 0LL
            : proc::ProcessInspector::durationMsBetween(row.startedAt, now);
        if (row.durationMs < 0) {
            row.durationMs = 0;
        }
        // 读取 ProfileExItem.delay 作为独立代理的最后测试延迟
        std::optional<db::models::ProfileExItem> ex = exDao.getByIndexId(watched[j].indexId);
        if (ex.has_value() && utils::isDelayValid(ex->delay)) {
            try {
                row.lastDelayMs = std::stoll(ex->delay);
            } catch (...) {
                row.lastDelayMs = -1;
            }
        } else {
            row.lastDelayMs = -1;
        }
        rows.push_back(row);
    }
    return rows;
}

// ---------------------------------------------------------------
// Unified snapshot for the floating monitor panel: standalone watched
// proxies first, then proxy-pool members. Each source is snapshotted
// under its own mutex (no cross-lock ordering dependency).
// ---------------------------------------------------------------
std::vector<UnifiedMonitorRow> AppController::getUnifiedMonitorRows() {
    // Standalone watched proxies (running && managed), then proxy-pool members.
    // Each source is snapshotted under its own mutex (no cross-lock ordering
    // dependency); the pure merge logic lives in UnifiedMonitorRows.h so it can
    // be unit-tested without linking the full AppController dependency chain.
    const std::vector<StandaloneMonitorRow> standalone = getWatchedStandaloneMonitors();
    const std::vector<proxy::PoolMemberView> members = getPoolMembers();
    return buildUnifiedMonitorRows(standalone, members);
}

// ---------------------------------------------------------------
// Reads the SOCKS inbound port from a standalone config file on disk
// (xray: inbounds[0].port / sing-box: inbounds[0].listen_port).
// Returns 0 when the file cannot be read or parsed.
// ---------------------------------------------------------------
static int readStandaloneInboundPort(const std::string& configPath) {
    std::ifstream f(configPath.c_str());
    if (!f.is_open()) {
        return 0;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    boost::system::error_code ec;
    boost::json::value root = boost::json::parse(ss.str(), ec);
    if (ec || !root.is_object()) {
        return 0;
    }
    const boost::json::object& obj = root.as_object();
    boost::json::object::const_iterator it = obj.find("inbounds");
    if (it == obj.end() || !it->value().is_array() ||
        it->value().as_array().empty()) {
        return 0;
    }
    const boost::json::array& arr = it->value().as_array();
    int idx = standalone_config::findSocksInboundIndex(arr);
    int target = (idx >= 0) ? idx : 0;
    if (target < 0 || target >= static_cast<int>(arr.size()) ||
        !arr[target].is_object()) {
        return 0;
    }
    const boost::json::object& inbound = arr[target].as_object();
    boost::json::object::const_iterator p = inbound.find("listen_port");
    if (p != inbound.end() && p->value().is_int64()) {
        return static_cast<int>(p->value().as_int64());
    }
    p = inbound.find("port");
    if (p != inbound.end() && p->value().is_int64()) {
        return static_cast<int>(p->value().as_int64());
    }
    return 0;
}

// ---------------------------------------------------------------
// Small JSON field readers used by the configuration cross-check.
// The project forbids `auto`, so the value types are checked explicitly.
// ---------------------------------------------------------------
static void readJsonString(const boost::json::object& obj, const char* key,
                            std::string& out) {
    boost::json::object::const_iterator it = obj.find(key);
    if (it != obj.end() && it->value().is_string()) {
        out = it->value().as_string();
    }
}

static void readJsonIntAsString(const boost::json::object& obj, const char* key,
                                 std::string& out) {
    boost::json::object::const_iterator it = obj.find(key);
    if (it == obj.end()) {
        return;
    }
    if (it->value().is_int64()) {
        out = std::to_string(it->value().as_int64());
    } else if (it->value().is_uint64()) {
        out = std::to_string(it->value().as_uint64());
    } else if (it->value().is_string()) {
        out = it->value().as_string();
    }
}

// ---------------------------------------------------------------
// Reads the proxy outbound's key items out of a standalone config file
// so that they can be compared with the ProfileItem row of the same
// indexId (see the cross-check inside adoptDanglingStandaloneProxies).
//
// Only the xray shape is handled: the proxy outbound carries
// settings.vnext[0] (vless / vmess) or settings.servers[0] (trojan).
// The REMOTE server port is read here - the local SOCKS port lives in
// inbounds[0].port and is NOT part of ProfileItem.
//
// Returns false when the file cannot be read, the outbound layout is not
// recognised, or a required item is missing (the caller then skips the
// comparison instead of reporting a bogus mismatch).
// ---------------------------------------------------------------
static bool readStandaloneProxySnapshot(
    const std::string& configPath,
    standalone_adoption::ProxyRecordSnapshot& out) {
    std::ifstream f(configPath.c_str());
    if (!f.is_open()) {
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    boost::system::error_code ec;
    boost::json::value root = boost::json::parse(ss.str(), ec);
    if (ec || !root.is_object()) {
        return false;
    }
    const boost::json::object& obj = root.as_object();
    boost::json::object::const_iterator it = obj.find("outbounds");
    if (it == obj.end() || !it->value().is_array() ||
        it->value().as_array().empty()) {
        return false;
    }
    const boost::json::array& arr = it->value().as_array();
    const boost::json::object* proxy = nullptr;
    for (std::size_t k = 0; k < arr.size(); ++k) {
        if (!arr[k].is_object()) {
            continue;
        }
        const boost::json::object& candidate = arr[k].as_object();
        boost::json::object::const_iterator tag = candidate.find("tag");
        if (tag != candidate.end() && tag->value().is_string() &&
            tag->value().as_string() == "proxy") {
            proxy = &candidate;
            break;
        }
    }
    if (proxy == nullptr) {
        // Fall back to the first outbound: the standalone config always emits
        // the proxy outbound first (ConfigBuilder::buildStandaloneConfig).
        if (!arr[0].is_object()) {
            return false;
        }
        proxy = &arr[0].as_object();
    }
    const boost::json::object& outbound = *proxy;
    readJsonString(outbound, "protocol", out.protocol);
    boost::json::object::const_iterator settingsIt = outbound.find("settings");
    if (settingsIt == outbound.end() || !settingsIt->value().is_object()) {
        return false;
    }
    const boost::json::object& settings = settingsIt->value().as_object();
    bool serverFound = false;
    boost::json::object::const_iterator vnext = settings.find("vnext");
    if (vnext != settings.end() && vnext->value().is_array() &&
        !vnext->value().as_array().empty() &&
        vnext->value().as_array()[0].is_object()) {
        const boost::json::object& server =
            vnext->value().as_array()[0].as_object();
        readJsonString(server, "address", out.address);
        readJsonIntAsString(server, "port", out.port);
        boost::json::object::const_iterator users = server.find("users");
        if (users != server.end() && users->value().is_array() &&
            !users->value().as_array().empty() &&
            users->value().as_array()[0].is_object()) {
            readJsonString(users->value().as_array()[0].as_object(), "id",
                           out.userId);
        }
        serverFound = true;
    } else {
        boost::json::object::const_iterator servers = settings.find("servers");
        if (servers != settings.end() && servers->value().is_array() &&
            !servers->value().as_array().empty() &&
            servers->value().as_array()[0].is_object()) {
            const boost::json::object& server =
                servers->value().as_array()[0].as_object();
            readJsonString(server, "address", out.address);
            readJsonIntAsString(server, "port", out.port);
            readJsonString(server, "password", out.userId);
            serverFound = true;
        }
    }
    if (!serverFound) {
        return false;
    }
    // Optional items: request host (ws / xhttp) and the reality public key.
    boost::json::object::const_iterator stream = outbound.find("streamSettings");
    if (stream != outbound.end() && stream->value().is_object()) {
        const boost::json::object& streamObj = stream->value().as_object();
        boost::json::object::const_iterator ws = streamObj.find("wsSettings");
        if (ws != streamObj.end() && ws->value().is_object()) {
            readJsonString(ws->value().as_object(), "host", out.requestHost);
        }
        boost::json::object::const_iterator xhttp = streamObj.find("xhttpSettings");
        if (xhttp != streamObj.end() && xhttp->value().is_object()) {
            readJsonString(xhttp->value().as_object(), "host", out.requestHost);
        }
        boost::json::object::const_iterator reality =
            streamObj.find("realitySettings");
        if (reality != streamObj.end() && reality->value().is_object()) {
            readJsonString(reality->value().as_object(), "publicKey",
                           out.publicKey);
        }
    }
    if (out.protocol.empty() || out.address.empty() || out.port.empty() ||
        out.userId.empty()) {
        return false;
    }
    return true;
}

void AppController::adoptDanglingStandaloneProxies() {
    // Only one adoption sweep may run at a time. The sweep registers watchers
    // and appends runtime-history rows; two concurrent sweeps would duplicate
    // both, and the second one overwrites proxyWatchKeys_[indexId] which
    // leaks the first watcher (its thread keeps running, its key is lost).
    // A sweep that finds the flag already set simply returns - the next
    // monitor tick picks the work up.
    if (!standalone_adoption::tryBeginAdoption(adoptInFlight_)) {
        Logger::write("[StandaloneProxy] Adoption scan already in progress; "
                      "skipping this round", LogLevel::DEBUG);
        return;
    }
    ScopeGuard<std::atomic<bool>> adoptGuard(adoptInFlight_);
    const char* exeNames[] = { "xray.exe", "sing-box.exe" };
    for (int e = 0; e < 2; ++e) {
        std::vector<proc::ProcessInfo> procs =
            proc::ProcessInspector::enumerateByName(exeNames[e]);
        for (std::size_t i = 0; i < procs.size(); ++i) {
            if (procs[i].commandLine.empty()) {
                continue;  // PEB read failed - cannot extract config name
            }
            std::string configFileName =
                proc::ProcessInspector::extractConfigFileName(procs[i].commandLine);
            if (configFileName.empty()) {
                continue;  // not a standalone proxy launch
            }
            // Extract indexId from "standalone_<indexId>-xray.json" /
            // "standalone_<indexId>-singbox.json".
            const std::string prefix = "standalone_";
            std::string indexId;
            if (configFileName.compare(0, prefix.size(), prefix) == 0) {
                const std::string body = configFileName.substr(prefix.size());
                const std::string xraySuffix = "-xray.json";
                const std::string singboxSuffix = "-singbox.json";
                if (body.size() > singboxSuffix.size() &&
                    body.compare(body.size() - singboxSuffix.size(),
                                 singboxSuffix.size(), singboxSuffix) == 0) {
                    indexId = body.substr(0, body.size() - singboxSuffix.size());
                } else if (body.size() > xraySuffix.size() &&
                           body.compare(body.size() - xraySuffix.size(),
                                        xraySuffix.size(), xraySuffix) == 0) {
                    indexId = body.substr(0, body.size() - xraySuffix.size());
                }
            }
            if (indexId.empty()) {
                continue;
            }

            // ---- Route decision (spec v1.6) ---------------------------------
            // A proxy that THIS session started already knows its own SOCKS
            // port: startStandaloneProxy configured it and waitForPort
            // confirmed it. Probing the OS again is pure waste (one TCP table
            // scan plus a SOCKS5 handshake per candidate) and may even resolve
            // the wrong port. Such a proxy takes the "bypass detection" route:
            // no live probe at all; the port comes from the history table or
            // from our own bookkeeping, and the only work left is making sure
            // the history table has a row for it.
            //
            // A proxy that was ADOPTED (started outside this application) still
            // needs the full live detection — we know nothing about its port.
            bool alreadyManaged = false;
            bool startedHere = false;
            int sessionPort = 0;
            int memoryPort = 0;
            {
                std::lock_guard<std::mutex> lock(standaloneMutex_);
                alreadyManaged =
                    standaloneProxies_.find(indexId) != standaloneProxies_.end() ||
                    proxyWatchKeys_.find(indexId) != proxyWatchKeys_.end();
                std::unordered_map<std::string, StandaloneProxyInfo>::const_iterator
                    infoIt = standaloneProxies_.find(indexId);
                if (infoIt != standaloneProxies_.end()) {
                    memoryPort = infoIt->second.socksPort;
                }
                std::unordered_map<DWORD, SessionStartedProxy>::const_iterator
                    sessionIt = sessionStartedProxies_.find(procs[i].pid);
                if (sessionIt != sessionStartedProxies_.end()) {
                    // The pid is only "ours" when it still refers to the very
                    // instance we launched: compare the creation time captured
                    // at launch with the one the enumeration reports now. A
                    // recycled pid must NOT take the bypass route.
                    const std::string& launchedAt = sessionIt->second.creationTime;
                    const bool sameInstance =
                        launchedAt.empty() || procs[i].creationTime.empty() ||
                        launchedAt == procs[i].creationTime;
                    if (sameInstance) {
                        startedHere = true;
                        sessionPort = sessionIt->second.socksPort;
                    }
                }
            }
            const standalone_adoption::AdoptionRoute route =
                standalone_adoption::decideAdoptionRoute(alreadyManaged,
                                                          startedHere);
            if (route == standalone_adoption::AdoptionRoute::Skip) {
                continue;
            }

            // ---- D5 instance identity gate (spec v1.5) --------------------
            // The candidate list is produced by enumerating the running
            // xray / sing-box images. Between that enumeration and this
            // adoption the original process may have died and a NEW process
            // may have reused the same pid. Opening a read-only handle and
            // comparing the creation timestamp proves whether the handle
            // still belongs to the enumerated instance; if it does not, the
            // live process is a FOREIGN one and must not be attached to
            // this indexId.
            //
            // The gate uses PROCESS_QUERY_LIMITED_INFORMATION because it is
            // never stricter than the enumeration itself (reading the command
            // line already needs PROCESS_QUERY_INFORMATION | PROCESS_VM_READ),
            // so the gate can never drop a process the enumeration accepted.
            // The full mask (incl. PROCESS_TERMINATE) is opened further below,
            // only for the processes we really adopt.
            //
            // An unreadable creation time is NOT evidence of a mismatch: the
            // gate then reports "unverifiable" and the process is KEPT.
            std::string instanceStartedAt;
            {
                HANDLE probeHandle = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION, FALSE, procs[i].pid);
                if (probeHandle) {
                    instanceStartedAt =
                        proc::ProcessInspector::processCreationTime(probeHandle);
                    CloseHandle(probeHandle);
                }
                const standalone_adoption::InstanceCheck identity =
                    standalone_adoption::checkInstanceIdentity(
                        procs[i].creationTime, instanceStartedAt);
                if (identity == standalone_adoption::InstanceCheck::Recycled) {
                    bool firstSkip = false;
                    {
                        std::lock_guard<std::mutex> lock(standaloneMutex_);
                        // Fingerprint-based de-duplication: the same pid with a
                        // different creation time is a DIFFERENT process and
                        // must be reported again (otherwise a recycled pid stays
                        // permanently at DEBUG level and the "alive but not
                        // monitored" case stays invisible in the log file).
                        const std::string skipFingerprint =
                            instanceStartedAt.empty() ? procs[i].creationTime
                                                      : instanceStartedAt;
                        std::unordered_map<DWORD, std::string>::iterator recIt =
                            discardedStandalonePids_.find(procs[i].pid);
                        const bool sameInstance =
                            (recIt == discardedStandalonePids_.end()) ||
                            standalone_adoption::isSameProcessFingerprint(
                                recIt->second, skipFingerprint);
                        if (!sameInstance) {
                            discardedStandalonePids_.erase(procs[i].pid);
                        }
                        // NOTE: operator[] would insert, so it must NOT be used
                        // to detect the first occurrence.
                        firstSkip = discardedStandalonePids_.insert_or_assign(
                            procs[i].pid, skipFingerprint).second;
                    }
                    Logger::write(
                        "[StandaloneProxy] Skipping pid=" +
                        std::to_string(procs[i].pid) + " indexId=" + indexId +
                        ": instance identity check=" +
                        standalone_adoption::instanceCheckLabel(identity) +
                        " (enumeratedCreatedAt=" + procs[i].creationTime +
                        " handleCreatedAt=" + instanceStartedAt +
                        "); the pid was reused, the enumerated process is gone.",
                        firstSkip ? LogLevel::WARN : LogLevel::DEBUG);
                    continue;
                }
                if (identity ==
                    standalone_adoption::InstanceCheck::Unverifiable) {
                    Logger::write(
                        "[StandaloneProxy] Instance identity for pid=" +
                        std::to_string(procs[i].pid) + " indexId=" + indexId +
                        " is unverifiable (enumeratedCreatedAt=" +
                        procs[i].creationTime + " handleCreatedAt=" +
                        instanceStartedAt +
                        "); keeping the process (a mismatch cannot be proven).",
                        LogLevel::DEBUG);
                }
            }

            // ---- Port resolution (deliberately BEFORE OpenProcess / history
            // row / watch registration): the port is recovered from the
            // runtime history first, then from the config file, then from the
            // live OS evidence; if all of them fail the process is DISCARDED.
            // Nothing may be created for a process we are about to drop (no
            // handle, no history row, no start_count bump, no watcher).
            //
            // Chain (spec v1.4, user ruling "历史表优先"):
            //   1. proxy_runtime_history  (indexId + pid + started_at triple)
            //   2. the config file located from the process command line
            //   3. the config file found in the known config directory
            //   4. the live LISTEN ports of the pid (OS TCP table)
            //
            // The live facts are collected ONCE and used twice: to produce the
            // OS tier candidates and to D2-validate the chain-selected
            // candidate. A candidate DISPROVEN by the live facts is skipped and
            // the chain falls through to the next source (this is what corrects
            // a stale recorded port, e.g. the 10810 leftover of the
            // 2026-09-02 port-drift defect). An UNVERIFIED / UNKNOWN candidate
            // is ACCEPTED so a healthy proxy is never dropped.
            const int apiPort = (config_.proxy.api_base_port > 0)
                                    ? config_.proxy.api_base_port
                                    : 10085;
            // Live OS facts are only needed for proxies we did NOT start
            // ourselves (spec v1.6 bypass route): for our own processes the
            // port is already known, so the TCP table scan and the SOCKS5
            // handshakes are skipped entirely.
            std::vector<int> osPorts;
            std::vector<int> socksPorts;
            std::vector<int> orderedOsCandidates;
            // True when the OS TCP table could actually be enumerated for this
            // pid. False means "no live evidence at all" - nothing may be
            // disproven in that case (D-C).
            bool osFactsReadable = false;
            if (route == standalone_adoption::AdoptionRoute::FullDetect) {
                const proc::ProcessInspector::TcpPortScan scan =
                    proc::ProcessInspector::scanListeningTcpPorts(procs[i].pid);
                osPorts = scan.ports;
                osFactsReadable = scan.readOk;
                const std::vector<int> osCandidates =
                    standalone_adoption::buildOsCandidates(osPorts, apiPort);
                for (std::vector<int>::const_iterator it = osCandidates.begin();
                     it != osCandidates.end(); ++it) {
                    if (utils::respondsToSocks5Greeting(*it, 200)) {
                        socksPorts.push_back(*it);
                    }
                }
                orderedOsCandidates =
                    standalone_adoption::orderOsCandidatesBySocks(osCandidates,
                                                                  socksPorts);
            }
            // Tier 1: the port recorded in proxy_runtime_history for THIS
            // process instance. The row is bound to (indexId, pid, startedAt),
            // so it is the port the process was really launched with and cannot
            // be invalidated by a later edit of the config file — which is why
            // it outranks the file tiers.
            // creationTime may be empty (PEB unreadable) — the DAO then matches
            // on (indexId, pid) only, because a "now" fallback would differ
            // from the value stored by the launching session.
            const int historyPort = historyDao_.lookupSocksPort(
                indexId, static_cast<int64_t>(procs[i].pid),
                procs[i].creationTime, db_);
            // Tier 2: the config file the process was launched with — the
            // full config path is derived from the launching command line
            // (cmdline `-c`) and read directly. It is read NOW, so the file may
            // already describe a different port than the running process.
            int cmdlinePort = 0;
            std::string adoptedConfigPath;
            const std::string fullPath =
                proc::ProcessInspector::extractConfigFullPath(procs[i].commandLine);
            if (!fullPath.empty()) {
                cmdlinePort = readStandaloneInboundPort(fullPath);
                if (cmdlinePort > 0) {
                    adoptedConfigPath = fullPath;
                }
            }
            // Tier 3: fallback — a bare-filename `-c` (no directory) or a launch
            // without a readable path can't be located from the command line, so
            // probe the config directory of the running executable only.
            // Least reliable tier: it may hit a stale copy of the config.
            // NOTE: `<exeDir>\worker\config` is deliberately NOT probed — it
            // belongs to the separate `bin\worker\validproxy.exe` copy (a
            // second installation), and probing it mixes ports of the same
            // indexId across installations (the 2026-09-02 port-drift defect
            // class). A process started by that copy carries the absolute
            // config path in its command line, so the cmdline tier above
            // already resolves it.
            int dirPort = 0;
            if (cmdlinePort <= 0) {
                const std::string adoptedConfigDir =
                    utils::getExecutableDir() + "\\config\\";
                dirPort = readStandaloneInboundPort(
                    adoptedConfigDir + configFileName);
                if (dirPort > 0) {
                    adoptedConfigPath = adoptedConfigDir + configFileName;
                }
            }
            // Assemble the candidate chain in priority order.
            std::vector<standalone_adoption::PortCandidate> portChain;
            if (historyPort > 0) {
                standalone_adoption::PortCandidate candidate;
                candidate.port = historyPort;
                candidate.source = standalone_adoption::PortSource::RuntimeHistory;
                portChain.push_back(candidate);
            }
            if (cmdlinePort > 0) {
                standalone_adoption::PortCandidate candidate;
                candidate.port = cmdlinePort;
                candidate.source = standalone_adoption::PortSource::CmdlineConfig;
                portChain.push_back(candidate);
            }
            if (dirPort > 0) {
                standalone_adoption::PortCandidate candidate;
                candidate.port = dirPort;
                candidate.source = standalone_adoption::PortSource::DirProbe;
                portChain.push_back(candidate);
            }
            for (std::vector<int>::const_iterator it = orderedOsCandidates.begin();
                 it != orderedOsCandidates.end(); ++it) {
                standalone_adoption::PortCandidate candidate;
                candidate.port = *it;
                candidate.source = standalone_adoption::PortSource::OsTcpTable;
                portChain.push_back(candidate);
            }
            standalone_adoption::PortResolution resolution;
            int adoptedPort = 0;
            if (route == standalone_adoption::AdoptionRoute::BypassDetect) {
                // Our own process: the port is the one WE configured. Prefer the
                // history row (already verified), then the in-memory entry, then
                // the session registry. The file tiers are ignored on purpose:
                // the config file may have been edited after the launch.
                adoptedPort = standalone_adoption::resolveBypassPort(
                    historyPort, memoryPort, sessionPort);
                resolution.port = adoptedPort;
                resolution.source = (adoptedPort > 0)
                    ? standalone_adoption::PortSource::RuntimeHistory
                    : standalone_adoption::PortSource::None;
                resolution.verdict = (adoptedPort > 0)
                    ? standalone_adoption::PortVerdict::Verified
                    : standalone_adoption::PortVerdict::Unknown;
            } else {
                resolution = standalone_adoption::resolveChainPort(
                    portChain, osPorts, socksPorts, apiPort, osFactsReadable);
                adoptedPort = resolution.port;
            }
            // ------------------------------------------------------------------
            // Cross-check the on-disk configuration against the current
            // ProfileItem row of the same indexId.
            //
            // The live OS facts can only arbitrate the LOCAL port; they say
            // nothing about WHICH node the running process actually serves.
            // Comparing the configuration with the database row is the only
            // way to see that the process is running a STALE node definition
            // (the user edited the proxy, or a subscription refresh changed
            // it, without restarting the standalone proxy).
            //
            // The verdict is DIAGNOSTIC ONLY: a mismatch is reported but it
            // never blocks the adoption. A live proxy that serves an outdated
            // node is still a live proxy, and dropping it would hide it from
            // the monitor. The port that is written back is always the one the
            // resolution chain produced.
            // ------------------------------------------------------------------
            standalone_adoption::RecordCompareResult recordCheck;
            if (!adoptedConfigPath.empty()) {
                standalone_adoption::ProxyRecordSnapshot configSnapshot;
                if (readStandaloneProxySnapshot(adoptedConfigPath,
                                                configSnapshot)) {
                    db::models::ProfileitemDAO recordDao(db_);
                    std::optional<db::models::Profileitem> recordItem =
                        recordDao.getByIndexId(indexId);
                    if (recordItem.has_value()) {
                        standalone_adoption::ProxyRecordSnapshot dbSnapshot;
                        dbSnapshot.protocol = recordItem->configtype;
                        dbSnapshot.address = recordItem->address;
                        dbSnapshot.port = recordItem->port;
                        dbSnapshot.userId = recordItem->id;
                        dbSnapshot.requestHost = recordItem->requesthost;
                        dbSnapshot.publicKey = recordItem->publickey;
                        recordCheck = standalone_adoption::compareProxyRecord(
                            configSnapshot, dbSnapshot);
                    }
                }
            }
            if (recordCheck.verdict ==
                standalone_adoption::RecordVerdict::Mismatch) {
                std::string fields;
                for (std::vector<std::string>::const_iterator fit =
                         recordCheck.mismatchedFields.begin();
                     fit != recordCheck.mismatchedFields.end(); ++fit) {
                    if (!fields.empty()) {
                        fields += ",";
                    }
                    fields += *fit;
                }
                // The sweep runs every 30s: report the very same mismatch
                // once at WARN and downgrade the repeats to DEBUG.
                bool firstReport = true;
                {
                    std::lock_guard<std::mutex> lock(staleConfigMutex_);
                    std::unordered_map<std::string, std::string>::iterator it =
                        staleConfigWarned_.find(indexId);
                    if (it != staleConfigWarned_.end() && it->second == fields) {
                        firstReport = false;
                    } else {
                        staleConfigWarned_[indexId] = fields;
                    }
                }
                Logger::write(
                    "[StandaloneProxy] Configuration of pid="
                    + std::to_string(procs[i].pid) + " indexId=" + indexId
                    + " does not match the current database record (" + fields
                    + "); the running proxy still uses the older definition. "
                    + "Restart the proxy to apply the new one.",
                    firstReport ? LogLevel::WARN : LogLevel::DEBUG);
            }
            // Fingerprint of the process instance this discard refers to. The
            // handle-derived creation time is authoritative; the enumerated one
            // is the fallback when the handle could not be opened.
            const std::string discardFingerprint =
                instanceStartedAt.empty() ? procs[i].creationTime
                                          : instanceStartedAt;
            if (adoptedPort <= 0) {
                if (route == standalone_adoption::AdoptionRoute::BypassDetect &&
                    alreadyManaged) {
                    // Our own process, already tracked: an unknown port is no
                    // reason to drop it — the live entry keeps working.
                    Logger::write(
                        "[StandaloneProxy] Session-started proxy pid="
                        + std::to_string(procs[i].pid) + " indexId=" + indexId
                        + " has no known SOCKS port yet; keeping the existing "
                        + "tracking.",
                        LogLevel::DEBUG);
                    continue;
                }
                // Discard: the process runs, but we cannot know which port it
                // serves, so it is not tracked at all.
                // The dangling history row is deliberately NOT closed here: the
                // process IS alive, so the open row describes the truth, and
                // closing it would make the next cycle append a duplicate row
                // (and bump start_count again) for the very same instance.
                // Logged once per pid AND per process instance: the 30s sweep
                // would otherwise spam the log file. The fingerprint makes a
                // recycled pid count as a NEW instance, so it is reported again.
                bool firstDiscard = false;
                {
                    std::lock_guard<std::mutex> lock(standaloneMutex_);
                    std::unordered_map<DWORD, std::string>::iterator recIt =
                        discardedStandalonePids_.find(procs[i].pid);
                    const bool sameInstance =
                        (recIt == discardedStandalonePids_.end()) ||
                        standalone_adoption::isSameProcessFingerprint(
                            recIt->second, discardFingerprint);
                    if (!sameInstance) {
                        // A different process now owns this pid: drop the stale
                        // fingerprint so the next discard is a first discard.
                        discardedStandalonePids_.erase(procs[i].pid);
                    }
                    // NOTE: operator[] would insert, so it must NOT be used to
                    // detect the first discard. insert().second is true only
                    // when the key was absent.
                    firstDiscard = discardedStandalonePids_.insert_or_assign(
                        procs[i].pid, discardFingerprint).second;
                }
                Logger::write(
                    "[StandaloneProxy] Discarding dangling process pid="
                    + std::to_string(procs[i].pid) + " indexId=" + indexId
                    + ": no SOCKS port could be resolved ("
                    + (osFactsReadable
                           ? "the OS TCP table was readable but the process owns "
                             "no LISTEN port"
                           : "no live LISTEN facts, config missing and no "
                             "runtime-history port")
                    + "). "
                    + "It is no longer tracked; clicking Start will offer to close it.",
                    firstDiscard ? LogLevel::WARN : LogLevel::DEBUG);
                continue;
            }

            // Bypass route for a proxy we already manage: the process is
            // already tracked (handle + exit watcher + in-memory entry), so
            // there is nothing to adopt again. The only thing that can be
            // missing is the history row — e.g. after a database switch the new
            // database has no row for this still-running process. Append it now
            // (append-only semantics) so the history stays complete.
            if (route == standalone_adoption::AdoptionRoute::BypassDetect &&
                alreadyManaged) {
                std::string bypassStartedAt;
                if (!instanceStartedAt.empty()) {
                    bypassStartedAt = instanceStartedAt;
                } else if (!procs[i].creationTime.empty()) {
                    bypassStartedAt = procs[i].creationTime;
                } else {
                    bypassStartedAt = utils::getCurrentTimestampFormatted();
                }
                int64_t bypassRhId = historyDao_.findInProgressHistory(
                    indexId, static_cast<int64_t>(procs[i].pid),
                    bypassStartedAt, db_);
                // Same rule as the main adoption path: a live row matching
                // (indexId, pid) belongs to this instance even when its
                // started_at differs — reuse it and only refresh the
                // duration instead of appending a duplicate session row.
                bool reusedHistoryRow = (bypassRhId >= 0);
                if (bypassRhId < 0) {
                    bypassRhId = historyDao_.findInProgressHistoryLoose(
                        indexId, static_cast<int64_t>(procs[i].pid), db_);
                    reusedHistoryRow = (bypassRhId >= 0);
                }
                bool appendedRow = false;
                if (bypassRhId < 0) {
                    // The history table lacks the live record of this instance
                    // (e.g. another database was selected) — append a new row
                    // carrying the port so the history stays complete.
                    bypassRhId = historyDao_.insertStartWithPort(
                        indexId, bypassStartedAt,
                        static_cast<int64_t>(procs[i].pid), adoptedPort, db_);
                    appendedRow = (bypassRhId >= 0);
                }
                if (bypassRhId < 0) {
                    Logger::write("[StandaloneProxy] Failed to record history row for "
                                  + indexId, LogLevel::WARN);
                    continue;
                }
                if (reusedHistoryRow) {
                    const std::string rowStartedAt =
                        historyDao_.getStartedAt(bypassRhId, db_);
                    const std::string durationBase =
                        rowStartedAt.empty() ? bypassStartedAt : rowStartedAt;
                    historyDao_.touchHeartbeat(
                        bypassRhId,
                        proc::ProcessInspector::durationMsBetween(
                            durationBase, utils::getCurrentTimestampFormatted()),
                        db_);
                }
                // Keep the port authoritative in the history row (it may be
                // missing or stale on a reused row).
                if (!historyDao_.updateSocksPort(bypassRhId, adoptedPort, db_)) {
                    Logger::write("[StandaloneProxy] Failed to record socks_port for "
                                  + indexId, LogLevel::WARN);
                }
                if (appendedRow) {
                    Logger::write("[StandaloneProxy] Session-started proxy pid="
                                  + std::to_string(procs[i].pid) + " indexId=" + indexId
                                  + " on SOCKS5 :" + std::to_string(adoptedPort)
                                  + " route=bypass-detect historyRow="
                                  + std::to_string(bypassRhId),
                                  LogLevel::REPORT);
                }
                continue;
            }

            // Must include PROCESS_TERMINATE so that stopStandaloneProxy() /
            // shutdownStandaloneProxies() can actually kill this adopted orphan.
            // Without it, TerminateProcess fails with ERROR_ACCESS_DENIED and the
            // orphan squats its port forever (bug: adopted xray never cleaned up).
            HANDLE hProcess = OpenProcess(
                PROCESS_QUERY_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE,
                FALSE, procs[i].pid);
            if (!hProcess) {
                continue;
            }

            // Write-side re-check (D-A2). The route decision at the top of the
            // loop is followed by the live detection (a TCP table scan plus one
            // SOCKS5 handshake per candidate), i.e. hundreds of milliseconds.
            // During that window the user may have started this very proxy, or
            // another sweep may have adopted it, and both register a watcher
            // for the same indexId. Re-checking here (under the lock, BEFORE
            // any history row is written) keeps "check - write" atomic and
            // prevents us from overwriting the other registration, which
            // would leak its watcher thread.
            {
                std::lock_guard<std::mutex> lock(standaloneMutex_);
                if (standaloneProxies_.find(indexId) != standaloneProxies_.end() ||
                    proxyWatchKeys_.find(indexId) != proxyWatchKeys_.end()) {
                    Logger::write(
                        "[StandaloneProxy] Another registration already owns "
                        "indexId=" + indexId + " pid=" +
                        std::to_string(procs[i].pid) +
                        "; abandoning this adoption.",
                        LogLevel::DEBUG);
                    CloseHandle(hProcess);
                    continue;
                }
            }

            // Reuse an in-progress history session for THIS exact process instance
            // (indexId + pid + system creation time). The history table is the
            // single source of truth for adopted standalone proxies, so when the
            // triple does not match it means the table lacks the live record of
            // this instance: a NEW row is APPENDED (carrying the resolved
            // port) instead of updating an unrelated existing row.
            // started_at is the instance identity of the adopted process. The
            // D5 gate already produced the authoritative value from a handle
            // that belongs to the very process we are adopting, so it is
            // preferred over the enumeration value. The final fallback only
            // avoids writing an empty string into the column — it does NOT
            // make the (indexId, pid, started_at) triple match, because the
            // launching session wrote its own "now" (a different instant).
            // Fallback must be datetime-formatted: it is stored as started_at
            // and later parsed by durationMsBetween (epoch seconds would break).
            std::string procStartedAt;
            if (!instanceStartedAt.empty()) {
                procStartedAt = instanceStartedAt;
            } else if (!procs[i].creationTime.empty()) {
                procStartedAt = procs[i].creationTime;
            } else {
                procStartedAt = utils::getCurrentTimestampFormatted();
            }
            int64_t rhId = historyDao_.findInProgressHistory(
                indexId, static_cast<int64_t>(procs[i].pid),
                procStartedAt, db_);
            // A row that is still in progress and matches (indexId, pid)
            // belongs to THIS process instance, even when the started_at
            // differs: the timestamp has three fallback levels and the
            // writer's and the reader's "now" are always different instants.
            // Such a row must be reused (only its duration refreshed) instead
            // of being closed and duplicated by an append.
            bool reusedHistoryRow = (rhId >= 0);
            if (rhId < 0) {
                rhId = historyDao_.findInProgressHistoryLoose(
                    indexId, static_cast<int64_t>(procs[i].pid), db_);
                reusedHistoryRow = (rhId >= 0);
            }
            if (rhId < 0) {
                // No live record for this process at all: append a new session
                // row carrying the resolved port (append-only semantics keep
                // the history table complete).
                rhId = historyDao_.insertStartWithPort(
                    indexId, procStartedAt,
                    static_cast<int64_t>(procs[i].pid), adoptedPort, db_);
            }
            if (rhId < 0) {
                CloseHandle(hProcess);
                continue;
            }
            if (reusedHistoryRow) {
                // Refresh the duration of the reused row against the timestamp
                // that row was actually created with (it may differ from this
                // scan's fallback value).
                const std::string rowStartedAt =
                    historyDao_.getStartedAt(rhId, db_);
                const std::string durationBase =
                    rowStartedAt.empty() ? procStartedAt : rowStartedAt;
                historyDao_.touchHeartbeat(
                    rhId,
                    proc::ProcessInspector::durationMsBetween(
                        durationBase, utils::getCurrentTimestampFormatted()),
                    db_);
            }
            const std::string startedAt = procStartedAt;

            const DWORD adoptedPid = procs[i].pid;
            proc::ProcessExitListener::WatchKey key = exitListener_->watch(
                hProcess, indexId, rhId,
                [](sqlite3*, const std::string&, const std::string&) -> int64_t {
                    return -1;
                },
                [](int64_t, sqlite3*) -> bool { return true; },
                [rhId, startedAt]()
                    -> std::vector<std::pair<int64_t, std::string>> {
                    return {{rhId, startedAt}};
                },
                [this](sqlite3* db, const std::string&, int64_t hid,
                       const std::string& endedAt, int exitCode,
                       int64_t durMs) -> bool {
                    return historyDao_.finalizeStop(hid, endedAt, exitCode, durMs, db_);
                },
                [this, adoptedPid](const std::string& idx, int64_t hid) {
                    {
                        std::lock_guard<std::mutex> lock(standaloneMutex_);
                        auto it = standaloneProxies_.find(idx);
                        if (it != standaloneProxies_.end()) {
                            it->second.running = false;
                        }
                        sessionStartedProxies_.erase(adoptedPid);
                    }
                    if (topWindow_) {
                        wxQueueEvent(topWindow_, new StandaloneProxyEvent(
                            idx, "", 0, /*started=*/false, "进程已退出"));
                    }
                },
                /*takeoverFn*/ nullptr,
                configFileName,
                /*heartbeatFn*/ [this](const std::string&, int64_t hid,
                                       int64_t elapsedMs) {
                    historyDao_.touchHeartbeat(hid, elapsedMs, db_);
                },
                /*baselineElapsedMs*/ proc::ProcessInspector::durationMsBetween(
                    startedAt, utils::getCurrentTimestampFormatted()));
            if (key == 0) {
                CloseHandle(hProcess);
                continue;
            }

            // Remember the resolved port for the next GUI session. Covers both
            // the reused-row and the newly-created-row cases (an externally
            // launched process adopted here would otherwise keep a NULL port
            // and be discarded after its config file is deleted).
            if (!historyDao_.updateSocksPort(rhId, adoptedPort, db_)) {
                Logger::write("[StandaloneProxy] Failed to record socks_port for adopted "
                              + indexId, LogLevel::WARN);
            }

            {
                std::lock_guard<std::mutex> lock(standaloneMutex_);
                StandaloneProxyInfo& info = standaloneProxies_[indexId];
                info.indexId = indexId;
                info.configFileName = configFileName;
                info.configPath = adoptedConfigPath;
                info.socksPort = adoptedPort;
                info.processHandle = hProcess;
                info.running = true;
                info.managed = true;
                info.runtimeHistoryId = rhId;
                info.watchKey = key;
                info.startedAt = startedAt;
                proxyWatchKeys_[indexId] = key;
            }
            Logger::write("[StandaloneProxy] Adopted dangling process pid="
                          + std::to_string(procs[i].pid) + " indexId=" + indexId
                          + " on SOCKS5 :" + std::to_string(adoptedPort)
                          + " source="
                          + standalone_adoption::portSourceLabel(resolution.source)
                          + " verdict="
                          + standalone_adoption::portVerdictLabel(resolution.verdict)
                          + " record="
                          + standalone_adoption::recordVerdictLabel(recordCheck.verdict)
                          + " config=" + configFileName, LogLevel::REPORT);
            // Write startup time into ProfileExItem.message (+<startup time>)
            exDao_.updateStartupTime(indexId, db_);
            // Notify the UI so the history/health columns refresh for the adopted proxy.
            if (topWindow_) {
                wxQueueEvent(topWindow_, new StandaloneProxyEvent(
                    indexId, "", 0, /*started=*/true, startedAt));
            }
        }
    }
}

bool AppController::stopStandaloneProxy(const std::string& indexId) {
    // P0-2 four-phase sequence (Spec §3.2).
    //
    // The real invariant here is NOT "clear the bookkeeping before terminating" —
    // it is "never call unwatch() while holding standaloneMutex_". unwatch() joins the
    // watcher thread, and that thread may be inside notifyFn blocked on
    // standaloneMutex_ => a deterministic AppHang. Clearing the map entry first (the
    // old order) is also harmful: if TerminateProcess fails we would have no handle
    // left to retry with, and the port would stay occupied with no way back.
    proc::ProcessExitListener::WatchKey key = 0;
    HANDLE hProcess = nullptr;
    int socksPort = -1;
    DWORD stoppedPid = 0;
    {
        std::lock_guard<std::mutex> lock(standaloneMutex_);
        auto it = standaloneProxies_.find(indexId);
        if (it == standaloneProxies_.end() || !it->second.running) {
            return false;
        }
        key = it->second.watchKey;
        hProcess = it->second.processHandle;
        socksPort = it->second.socksPort;
        // The handle keeps the pid valid even after the process died, so this
        // is a reliable way to drop the session bookkeeping for it (spec
        // v1.6): a stopped proxy must no longer count as "one of ours".
        stoppedPid = (hProcess != nullptr) ? GetProcessId(hProcess) : 0;
        // Intentionally do NOT touch it->second in this phase.
    }

    // Phase 2 — terminate WITHOUT the lock (notifyFn must be able to take it).
    bool terminated = false;
    if (hProcess) {
        const BOOL termOk = TerminateProcess(hProcess, 1);
        if (!termOk) {
            const DWORD err = GetLastError();
            Logger::write("[StandaloneProxy] TerminateProcess FAILED (err="
                          + std::to_string(err) + ") for " + indexId, LogLevel::ERR);
        } else {
            // bugfix #74: TerminateProcess is asynchronous, so the kernel may keep
            // reporting the SOCKS port as LISTEN for a short window (<200ms). Waiting
            // here guarantees the port is free by the time we return. 3s is only a
            // worst-case bound; a clean termination returns far sooner.
            WaitForSingleObject(hProcess, 3000);
            terminated = true;
        }
    }

    if (!terminated) {
        // Phase 3b — keep the bookkeeping intact (processHandle / running / watchKey
        // all preserved) so a later stopStandaloneProxy can retry with the same
        // handle. The handle is still closed by shutdownStandaloneProxies on exit, so
        // there is no leak; staying watched lets a natural exit still refresh the UI.
        Logger::write("[StandaloneProxy] Stop FAILED for " + indexId
                      + " on SOCKS5 :" + std::to_string(socksPort)
                      + " (processHandle="
                      + std::to_string(reinterpret_cast<intptr_t>(hProcess)) + ")",
                      LogLevel::ERR);
        return false;
    }

    // Phase 3a-1 — clear the bookkeeping under the lock. Re-find instead of reusing
    // the phase-1 iterator: notifyFn may have run concurrently during phase 2.
    {
        std::lock_guard<std::mutex> lock(standaloneMutex_);
        auto it = standaloneProxies_.find(indexId);
        if (it != standaloneProxies_.end()) {
            it->second.processHandle = nullptr;
            it->second.running = false;
            it->second.watchKey = 0;
            standaloneProxies_.erase(it);
        }
        if (stoppedPid != 0) {
            sessionStartedProxies_.erase(stoppedPid);
        }
    }

    // Phase 3a-2 — everything that can block runs WITHOUT the lock.
    CloseHandle(hProcess);
    if (key != 0) {
        exitListener_->unwatch(key);
    }

    // P0-2(c): process exit != port released. Bounded convergence check, LISTEN only.
    if (!utils::waitPortReleased(socksPort, 1000)) {
        Logger::write("[AppController] SOCKS port " + std::to_string(socksPort)
                      + " still LISTEN 1s after stop; a restarted proxy will need a shifted port", LogLevel::WARN);
        utils::logPortOccupants(socksPort);
    }

    Logger::write("[StandaloneProxy] Stopped " + indexId + " on SOCKS5 :"
                  + std::to_string(socksPort), LogLevel::REPORT);
    return true;
}

void AppController::shutdownStandaloneProxies() {
    shutdownRequested_.store(true);
    stopProxyPool();
    std::lock_guard<std::mutex> lock(standaloneMutex_);
    // Deprecated (per user directive): on app exit we NO LONGER auto-terminate
    // standalone proxy processes. Each standalone xray is left RUNNING as an
    // orphan so it can be re-adopted by the next session's
    // adoptDanglingStandaloneProxies(), and can only ever be closed manually
    // (via stopStandaloneProxy). Here we only release OUR bookkeeping safely:
    // - unwatch the exit watcher so a later process exit cannot call back into
    //   a torn-down app;
    // - close OUR process-handle reference (this does NOT kill the xray).
    // The standalone proxies themselves keep listening on their fixed ports and
    // are picked up again on the next app start.
    for (auto& kv : standaloneProxies_) {
        if (kv.second.watchKey != 0) {
            exitListener_->unwatch(kv.second.watchKey);
            kv.second.watchKey = 0;
        }
        if (kv.second.processHandle) {
            CloseHandle(kv.second.processHandle);
            kv.second.processHandle = nullptr;
        }
        kv.second.running = false;
    }
    standaloneProxies_.clear();
    if (exitListener_) {
        exitListener_->shutdown();
    }
}

// ---------------------------------------------------------------
// Standalone proxy POOL
// ---------------------------------------------------------------
bool AppController::isProxyPoolEnabled() const {
    return config_.standalone_pool.enabled;
}

bool AppController::startProxyPool() {
    if (!config_.standalone_pool.enabled) return false;
    {
        std::lock_guard<std::mutex> lock(poolMutex_);
        if (proxyPool_) return true;
    }
    std::string xrayPath = config_.proxy.xray_executable;
    if (xrayPath.empty()) {
        Logger::write("[AppController] proxy pool not started: proxy.xray_executable not configured",
                      LogLevel::ERR);
        return false;
    }
    // Ensure the pool's xray can load geoip.dat/geosite.dat for the CN/private
    // direct routing rules and the DNS geosite matchers. Inherited by the child
    // xray via CreateProcessW, mirroring the standalone proxy asset-dir setup.
    {
        std::string assetDir = config_.proxy.xray_asset_dir;
        if (assetDir.empty()) {
            std::error_code ec2;
            assetDir = std::filesystem::path(xrayPath).parent_path().parent_path().string();
        }
        if (!assetDir.empty()) {
            SetEnvironmentVariableA("XRAY_LOCATION_ASSET", assetDir.c_str());
        }
    }
    std::string configDir = utils::getExecutableDir() + "/config";
    std::error_code ec;
    if (!std::filesystem::exists(configDir) && !std::filesystem::create_directories(configDir, ec)) {
        Logger::write("[AppController] cannot create pool config dir: " + utils::toWindowsPath(configDir), LogLevel::ERR);
        return false;
    }
    // Resolve pool listen ports via PortManager so the pool never collides with
    // an already-running standalone proxy / Xray instance (both allocate via the
    // same manager). The configured socksPort/apiPort are only *desired* hints.
    // Retry several times: on each attempt we shift the *desired* base port so a
    // transiently-busy port (e.g. a just-exited xray still racing to release its
    // listener, or an external process) is skipped instead of being handed to a
    // fresh xray that would fail to listen and force a noisy restart.
    const int kMaxAttempts = 5;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        config::StandalonePoolConfig poolCfg = config_.standalone_pool;
        // Reuse the global test url so the pool's observatory probes the same
        // endpoint the rest of the app uses (fixes the frozen-field health bug).
        poolCfg.probeUrl = config_.test_url;
        if (attempt > 0) {
            // Shift the scan window past the port that was busy on the last try.
            // P1-2: shift relative to the *configured* base ports instead of the
            // hard-coded 10809/10810, so a user-configured base is honoured on
            // every attempt. Byte-identical under the default config (where the
            // configured bases are 10809/10810 anyway) — and resolvePoolPorts
            // still validates each shifted port, so the guess stays a hint.
            const int baseSocks = (config_.standalone_pool.socksPort > 0)
                                      ? config_.standalone_pool.socksPort
                                      : 10809;
            const int baseApi = (config_.standalone_pool.apiPort > 0)
                                    ? config_.standalone_pool.apiPort
                                    : 10810;
            poolCfg.socksPort = baseSocks + attempt;
            poolCfg.apiPort = baseApi + attempt;
        }
        if (!proxy::resolvePoolPorts(poolCfg)) {
            Logger::write("[AppController] no free port available for proxy pool (attempt "
                          + std::to_string(attempt) + ")", LogLevel::ERR);
            continue;
        }
        std::shared_ptr<proxy::StandaloneProxyPool> pool(
            new proxy::StandaloneProxyPool(poolCfg, xrayPath, configDir, db_));
        pool->onMembersChanged = [this](const std::vector<proxy::PoolMemberView>& members) {
            if (topWindow_) {
                wxQueueEvent(topWindow_, new PoolMembersUpdatedEvent(members));
            }
        };
        // Dead members removed from the pool are written back to ProfileExItem
        // exactly like a failed standalone-proxy test (delay=-1 + history reset
        // so the health score drops to 0), keeping the proxy list in sync with
        // the pool's death verdict.
        pool->onMemberRemoved = [this](long long indexId) {
            if (!db_) return;
            db::models::ProfileExItemDAO dao(db_);
            dao.updateTestResult(std::to_string(indexId), -1, false,
                                 "pool member removed (dead)");
        };
        if (!pool->start()) {
            Logger::write("[AppController] proxy pool start failed (attempt "
                          + std::to_string(attempt) + "), socks=" + std::to_string(poolCfg.socksPort)
                          + " api=" + std::to_string(poolCfg.apiPort), LogLevel::ERR);
            PortManager::freePort(poolCfg.socksPort);
            PortManager::freePort(poolCfg.apiPort);
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(poolMutex_);
            proxyPool_ = pool;
            poolSocksPort_ = poolCfg.socksPort;
            poolApiPort_ = poolCfg.apiPort;
        }
        Logger::write("[AppController] proxy pool started (socks=" + std::to_string(poolCfg.socksPort)
                      + ", api=" + std::to_string(poolCfg.apiPort) + ")", LogLevel::INFO);
        return true;
    }
    Logger::write("[AppController] proxy pool start gave up after "
                  + std::to_string(kMaxAttempts) + " attempts", LogLevel::ERR);
    return false;
}

void AppController::stopProxyPool() {
    std::shared_ptr<proxy::StandaloneProxyPool> pool;
    int socks = 0, api = 0;
    {
        std::lock_guard<std::mutex> lock(poolMutex_);
        pool = proxyPool_;
        proxyPool_.reset();
        socks = poolSocksPort_;
        api = poolApiPort_;
        poolSocksPort_ = 0;
        poolApiPort_ = 0;
    }
    if (pool) pool->stop();
    if (socks > 0) PortManager::freePort(socks);
    if (api > 0) PortManager::freePort(api);
}

bool AppController::isProxyPoolRunning() const {
    std::lock_guard<std::mutex> lock(poolMutex_);
    return proxyPool_ && proxyPool_->isRunning();
}

bool AppController::injectProxyToPool(const std::string& indexId) {
    std::shared_ptr<proxy::StandaloneProxyPool> pool;
    {
        std::lock_guard<std::mutex> lock(poolMutex_);
        pool = proxyPool_;
    }
    if (!pool) return false;
    std::optional<db::models::Profileitem> profile = getProxyByIndexId(indexId);
    if (!profile) return false;
    bool ok = pool->injectMember(*profile);
    if (ok) {
        // 加入代理池意味着代理已被启动并注入 Xray balancer，与独立代理启动
        // 语义一致：刷新 ProfileExItem.message 的启动时间侧（保留测试时间侧）。
        exDao_.updateStartupTime(indexId, db_);
    }
    return ok;
}

bool AppController::removePoolMember(long long indexId, bool graceful) {
    std::shared_ptr<proxy::StandaloneProxyPool> pool;
    {
        std::lock_guard<std::mutex> lock(poolMutex_);
        pool = proxyPool_;
    }
    if (!pool) return false;
    return pool->removeMember(indexId, graceful);
}

std::vector<proxy::PoolMemberView> AppController::getPoolMembers() const {
    std::lock_guard<std::mutex> lock(poolMutex_);
    if (!proxyPool_) return std::vector<proxy::PoolMemberView>();
    return proxyPool_->getMembers();
}

bool AppController::isIndependentProbeEnabled() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return config_.independent_probe.enabled;
}

void AppController::setIndependentProbeEnabled(bool on) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.independent_probe.enabled = on;
    Logger::write(std::string("[Config] independent_probe.enabled -> ") +
                  (on ? "true" : "false"), LogLevel::INFO);
}

void AppController::setPoolReportHealth(bool on) {
    std::lock_guard<std::mutex> lock(poolMutex_);
    if (proxyPool_) proxyPool_->setReportHealth(on);
}

void AppController::setPoolAutoPruneDead(bool on) {
    std::lock_guard<std::mutex> lock(poolMutex_);
    if (proxyPool_) proxyPool_->setAutoPruneDead(on);
}

void AppController::setPoolAutoOptimize(bool on) {
    std::lock_guard<std::mutex> lock(poolMutex_);
    if (proxyPool_) proxyPool_->setAutoOptimize(on);
}

void AppController::probePoolNow() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    if (proxyPool_) {
        proxyPool_->probeNow();
    }
}

// ---------------------------------------------------------------
// Async workers
// ---------------------------------------------------------------
void AppController::doUpdateSubscription(const std::string& subId, wxEvtHandler* wxHandler) {
    // Scope guard: reset isRunning_ on every exit path (including early returns and exceptions)
    ScopeGuard<std::atomic<bool>> _guard{isRunning_};

    try {
update::SubitemUpdaterV2 updater(db_, config_.proxy.xray_executable, config_, nullptr, "", &cancelRequested_, &netMon_);
         bool ok = updater.runSingle(subId);

        std::string msg = ok ? "Update completed: " + subId : "Update failed: " + subId;
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, msg));
        }
        if (ok && wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0,
                std::string(ui::refresh_cmd::kRowsChanged)));
        }
        Logger::write(msg, ok ? LogLevel::REPORT : LogLevel::ERR);
    } catch (const std::exception& e) {
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("ERR:") + e.what()));
        }
        Logger::write(std::string("Update error: ") + e.what(), LogLevel::ERR);
    }
}

void AppController::doUpdateAllSubscriptions(wxEvtHandler* wxHandler) {
    // Scope guard: reset isRunning_ on every exit path (including early returns and exceptions)
    ScopeGuard<std::atomic<bool>> _guard{isRunning_};

    try {
update::SubitemUpdaterV2 updater(db_, config_.proxy.xray_executable, config_, nullptr, "", &cancelRequested_, &netMon_);
         bool ok = updater.run();

        std::string msg = ok ? "All subscriptions updated" : "Update (all) had failures";
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, msg));
        }
        if (ok && wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0,
                std::string(ui::refresh_cmd::kRowsChanged)));
        }

        Logger::write(msg, ok ? LogLevel::REPORT : LogLevel::ERR);
    } catch (const std::exception& e) {
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("ERR:") + e.what()));
        }
        Logger::write(std::string("Update (all) error: ") + e.what(), LogLevel::ERR);
    }
}

void AppController::doTestSubscription(const std::string& subId, wxEvtHandler* wxHandler) {
    // Scope guard: reset isRunning_ on every exit path (including early returns and exceptions)
    ScopeGuard<std::atomic<bool>> _guard{isRunning_};

    try {
        ProxyBatchTester tester(db_, config_, "", &cancelRequested_, &netMon_);
        bool ok = tester.runWithSubId(subId);

        // Batch resolve regions for valid proxies only when actual tests were performed
        if (tester.getTotalProxies() > 0) {
            resolveRegionsForValidProxies(subId);
        }

        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0,
                std::string(ui::refresh_cmd::kRowsChanged)));
        }

        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(2, ok ? "Test completed" : "Test failed"));
        }

        // Always send completion event (both success and failure) to restore UI state
        if (wxHandler) {
            wxQueueEvent(wxHandler, new ProxyTestProgressEvent(0, 0, "", "", "", ok ? "Batch test finished" : "Batch test failed", true));
        }
        // Broadcast to MainFrame for delay column refresh.
        // Use dynamic_cast to safely get the top-level window from wxEvtHandler.
        // Create a fresh event copy for the MainFrame receiver.
        if (wxWindow* win = dynamic_cast<wxWindow*>(wxHandler)) {
            if (wxWindow* topLevel = wxGetTopLevelParent(win)) {
                if (topLevel != win) {
                    wxQueueEvent(topLevel, new ProxyTestProgressEvent(0, 0, "", "", "", "Batch test finished", true));
                }
            }
        }

        Logger::write(std::string("Batch test ") + (ok ? "succeeded" : "failed"), LogLevel::REPORT);
    } catch (const std::exception& e) {
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("ERR:") + e.what()));
        }
        Logger::write(std::string("Batch test error: ") + e.what(), LogLevel::ERR);
    }
}

void AppController::doTestSingleProxy(const std::string& indexId, wxEvtHandler* wxHandler) {
    // Scope guard: reset isRunning_ on every exit path (including early returns and exceptions)
    ScopeGuard<std::atomic<bool>> _guard{isRunning_};

    try {
        // Show per-proxy testing progress in status bar field 0
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("Testing proxy ") + indexId + "..."));
        }

        ProxyBatchTester tester(db_, config_, "", &cancelRequested_, &netMon_);
        bool ok = tester.runWithIndexId(indexId);

        // Get actual test result (delay + message) from the tester
        TestResult result = tester.getLastResult();
        std::string delayStr = utils::isTestResultValid(result.success, result.latencyMs) ? std::to_string(result.latencyMs) : std::string("");
        std::string message   = result.success
                                    ? std::to_string(result.latencyMs) + "ms"
                                    : result.errorMsg.empty() ? "FAIL" : result.errorMsg;

        // Result row update (indexId+address in remarks col, delay+message in their cols)
        if (wxHandler) {
            wxQueueEvent(wxHandler,
                         new ProxyTestProgressEvent(1, 1, indexId, /*remarks=*/"",
                                                      delayStr, message, /*isCompleted=*/true));
        }
        // Status bar update via MainFrame::onStatusUpdate
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(2, ok ? "Test completed" : "Test failed"));
        }

        Logger::write(std::string("Single proxy test ") + (ok ? "succeeded" : "failed"), LogLevel::REPORT);
    } catch (const std::exception& e) {
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("ERR:") + e.what()));
        }
        Logger::write(std::string("Single proxy test error: ") + e.what(), LogLevel::ERR);
    }
}

void AppController::doTestAllProxies(wxEvtHandler* wxHandler) {
    // Scope guard: reset isRunning_ on every exit path (including early returns and exceptions)
    ScopeGuard<std::atomic<bool>> _guard{isRunning_};

    try {
        // Show batch testing progress in status bar field 0
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "Testing all proxies..."));
        }

        ProxyBatchTester tester(db_, config_, "", &cancelRequested_, &netMon_);
        bool ok = tester.run();

        // Batch resolve regions for all valid proxies only when actual tests were performed
        if (tester.getTotalProxies() > 0) {
            resolveRegionsForValidProxies();
        }

        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0,
                std::string(ui::refresh_cmd::kRowsChanged)));
        }

        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(2, ok ? "All proxies test completed" : "All proxies test had failures"));
        }

        // Always send completion event to restore UI state
        if (wxHandler) {
            wxQueueEvent(wxHandler, new ProxyTestProgressEvent(0, 0, "", "", "", ok ? "All batch test finished" : "All batch test failed", true));
        }
        // Broadcast to MainFrame for delay column refresh
        if (wxWindow* win = dynamic_cast<wxWindow*>(wxHandler)) {
            if (wxWindow* topLevel = wxGetTopLevelParent(win)) {
                if (topLevel != win) {
                    wxQueueEvent(topLevel, new ProxyTestProgressEvent(0, 0, "", "", "", "All batch test finished", true));
                }
            }
        }

        Logger::write(std::string("All proxies batch test ") + (ok ? "succeeded" : "failed"), LogLevel::REPORT);
    } catch (const std::exception& e) {
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("ERR:") + e.what()));
        }
        Logger::write(std::string("All proxies batch test error: ") + e.what(), LogLevel::ERR);
    }
}

void AppController::doTestOnlineProxies(wxEvtHandler* wxHandler, bool silent) {
    // Scope guard: reset isRunning_ on every exit path — only for the MANUAL
    // path (AsyncOperationGuard set it). The silent probe runs on
    // probeThread_ without holding isRunning_, so it must NOT reset it —
    // doing so would clear a user-initiated operation's guard if one started
    // while the probe was running.
    std::optional<ui::ScopeGuard<std::atomic<bool>>> runningGuard;
    if (!silent) {
        runningGuard.emplace(isRunning_);
    }
    // Clear the silent-probe marker (no-op for manual runs, where it
    // never gets set).
    ui::ScopeGuard<std::atomic<bool>> _probeGuard{onlineProbeRunning_};

    // Snapshot the config fields the probe loop reads so a concurrent
    // saveConfig() (allowed while the silent probe runs — see
    // MainFrame::onMenuConfig) cannot race with these reads. saveConfig()
    // writes config_ under the same configMutex_.
    std::string probeTestUrl;
    int probeTimeoutMs = 0;
    int probeFailStreak = 0;
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        probeTestUrl = config_.test_url;
        probeTimeoutMs = config_.test_timeout_ms;
        probeFailStreak = config_.standalone_pool.evaluate.pruneFailStreak;
    }

    try {
        // Show testing progress in status bar field 0 (skipped for the silent
        // periodic probe so it never disturbs the UI).
        if (wxHandler && !silent) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "Testing online proxies..."));
        }

        std::vector<StandaloneMonitorRow> monitors = getWatchedStandaloneMonitors();
        int total = 0;
        int success = 0;
        int failed = 0;
        std::vector<std::string> failedIndexIds;
        std::string failedHostsStr;
        // Every monitored proxy whose row was refreshed by updateTestResult
        // (including the port<=0 branch, which writes -1) — the UI needs
        // these indexIds to refresh exactly those rows incrementally when
        // the silent probe finishes.
        std::vector<std::string> monitorIndexIds;

        for (const StandaloneMonitorRow& mon : monitors) {
            if (cancelRequested_.load()) break;
            total++;
            monitorIndexIds.push_back(mon.indexId);

            int port = mon.socksPort;
            if (port <= 0) {
                failed++;
                failedIndexIds.push_back(mon.indexId);
                failedHostsStr += (failedHostsStr.empty() ? std::string() : " | ") + mon.host + ":" + std::to_string(mon.socksPort);
                // Thresholded WARN: frequent failures pile up in consecutive_failures;
                // only log once the standalone_pool prune threshold is reached.
                std::optional<db::models::ProfileExItem> exBad =
                    exDao_.getByIndexId(mon.indexId);
                int curBad = exBad ? exBad->consecutive_failures : 0;
                // D5: log only when the streak FIRST crosses the threshold.
                // `>=` re-logged the same warning on every probe cycle (~5s) for
                // the whole 3-minute failure window, producing a WARN storm.
                // The rule lives in BatchWorkerPolicy so it is unit-tested.
                if (batch_policy::shouldLogProbeFailureCrossing(curBad, probeFailStreak)) {
                    Logger::write("[OnlineProbe] test failed: " + mon.indexId
                                  + ": socks port unknown", LogLevel::WARN);
                }
                exDao_.updateTestResult(mon.indexId, -1, false, "socks port unknown");
                continue;
            }

            ProxyTester tester(nullptr, probeTestUrl, probeTimeoutMs);
            TestResult r = tester.test(port, &cancelRequested_, nullptr);
            if (r.success) {
                success++;
            } else {
                failed++;
                failedIndexIds.push_back(mon.indexId);
                failedHostsStr += (failedHostsStr.empty() ? std::string() : " | ") + mon.host + ":" + std::to_string(mon.socksPort);
                // Thresholded WARN: reuse standalone_pool.evaluate.pruneFailStreak
                // (default 3); updateTestResult below already bumps
                // consecutive_failures by 1, so pre-read it for the comparison.
                std::optional<db::models::ProfileExItem> ex =
                    exDao_.getByIndexId(mon.indexId);
                int cur = ex ? ex->consecutive_failures : 0;
                // D5: see the note above — crossing, not ">=" .
                if (batch_policy::shouldLogProbeFailureCrossing(cur, probeFailStreak)) {
                    Logger::write("[OnlineProbe] test failed: " + mon.indexId
                                  + ": " + r.errorMsg, LogLevel::WARN);
                }
            }
            exDao_.updateTestResult(mon.indexId, r.latencyMs, r.success, r.errorMsg);
        }

        // Periodic silent probe: no status-bar chatter, no result dialog — but
        // still notify the UI to refresh ONLY the tested rows (incremental,
        // keeps the full-table 53k reload off the UI thread).
        if (wxHandler && silent && !monitorIndexIds.empty()) {
            wxQueueEvent(wxHandler, new OnlineProbeFinishedEvent(monitorIndexIds));
        }

        // Periodic silent probe: no status-bar chatter, no result event.
        if (wxHandler && !silent) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(2, total > 0 ? "Online proxies test completed" : "No online proxies found"));
        }
        // Report totals and failed list back to the panel (manual trigger only)
        if (wxHandler && !silent) {
            wxQueueEvent(wxHandler, new TestOnlineProxiesEvent(failedIndexIds, total, success, failed));
        }

        // Log only meaningful events (spec LogLevelRefinement §3.2):
        //  - failures: REPORT (unchanged)
        //  - manual test fully successful: REPORT (user-initiated, visible)
        //  - periodic silent probe fully successful: DEBUG (no log spam)
        if (failed > 0) {
            Logger::write(std::string("Online proxies test finished: total=") + std::to_string(total) +
                              ", success=" + std::to_string(success) + ", failed=" + std::to_string(failed) +
                              ", failed_hosts=" + failedHostsStr,
                          LogLevel::REPORT);
        } else if (silent) {
            Logger::write(std::string("Online proxies test finished: total=") + std::to_string(total) +
                              ", success=" + std::to_string(success) + ", failed=0",
                          LogLevel::DEBUG);
        } else {
            Logger::write(std::string("Online proxies test finished: total=") + std::to_string(total) +
                              ", success=" + std::to_string(success) + ", failed=0",
                          LogLevel::REPORT);
        }
    } catch (const std::exception& e) {
        if (wxHandler && !silent) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("ERR:") + e.what()));
        }
        Logger::write(std::string("Online proxies test error: ") + e.what(), LogLevel::ERR);
    }
}

// ---------------------------------------------------------------
// Async find workers
//
// Payload encoding sent via StatusUpdateEvent:
//   "FOUND:<indexId>:<address>"   — working proxy found
//   "NOTFOUND"                    — no working proxy found
//   "ERR:<msg>"                   — exception occurred
//   "CANCELLED"                   — operation cancelled
// ---------------------------------------------------------------
void AppController::doFindFirstProxy(wxEvtHandler* wxHandler) {
    // Scope guard: reset isRunning_ on every exit path (including early returns and exceptions)
    ScopeGuard<std::atomic<bool>> _guard{isRunning_};

    try {
        if (isTestCancelled()) {
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "CANCELLED"));
            }
            return;
        }
        std::string xrayPath = config_.proxy.xray_executable;
        std::string configDir = utils::getExecutableDir() + "/config";
        XrayManager* manager = XrayManager::getInstance(xrayPath, configDir, config_.xray_workers, db_);
        if (!manager) {
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "ERR:Failed to create XrayManager"));
            }
            return;
        }
        // Start Xray instances if not already running
        if (manager->getInstanceCount() == 0) {
            int started = manager->start(config_.xray_workers, config_.xray_start_port, config_.xray_api_port);
            if (started == 0) {
                if (wxHandler) {
                    wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "ERR:Failed to start Xray instances"));
                }
                return;
            }
        }
        ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms, &cancelRequested_, &netMon_);

        std::pair<int, int> ports = finder.findFirstWorkingProxy();

        if (isTestCancelled()) {
            XrayManager::release();
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "CANCELLED"));
            }
            return;
        }

        TestResult res = finder.getLastResult();

        if (res.success && ports.first > 0) {
            std::string payload = "FOUND:" + res.indexId + ":" + res.address;
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, payload));
            }
        } else {
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "NOTFOUND"));
            }
        }

        finder.release();

    } catch (const std::exception& e) {
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("ERR:") + e.what()));
        }
    }
}

void AppController::doFindBestProxy(wxEvtHandler* wxHandler) {
    // Scope guard: reset isRunning_ on every exit path (including early returns and exceptions)
    ScopeGuard<std::atomic<bool>> _guard{isRunning_};

    try {
        if (isTestCancelled()) {
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "CANCELLED"));
            }
            return;
        }
        std::string xrayPath = config_.proxy.xray_executable;
        std::string configDir = utils::getExecutableDir() + "/config";
        XrayManager* manager = XrayManager::getInstance(xrayPath, configDir, config_.xray_workers, db_);
        if (!manager) {
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "ERR:Failed to create XrayManager"));
            }
            return;
        }
        // Start Xray instances if not already running
        if (manager->getInstanceCount() == 0) {
            int started = manager->start(config_.xray_workers, config_.xray_start_port, config_.xray_api_port);
            if (started == 0) {
                if (wxHandler) {
                    wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "ERR:Failed to start Xray instances"));
                }
                return;
            }
        }
        ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms, &cancelRequested_, &netMon_);

        std::pair<int, int> ports = finder.findWorkingProxy();

        if (isTestCancelled()) {
            XrayManager::release();
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "CANCELLED"));
            }
            return;
        }

        TestResult res = finder.getLastResult();

        if (res.success && ports.first > 0) {
            std::string payload = "FOUND:" + res.indexId + ":" + res.address;
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, payload));
            }
        } else {
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "NOTFOUND"));
            }
        }

        finder.release();

    } catch (const std::exception& e) {
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("ERR:") + e.what()));
        }
    }
}

void AppController::doSyncDatabases(wxEvtHandler* wxHandler) {
    // Scope guard: reset isRunning_ on every exit path (including early returns and exceptions)
    ScopeGuard<std::atomic<bool>> _guard{isRunning_};

    try {
        update::SubitemUpdaterV2 updater(db_, config_.proxy.xray_executable, config_, nullptr, "", &cancelRequested_);
        bool ok = updater.syncDatabases(config_.sync.source_db, config_.sync.target_db);
        std::string msg = ok ? "数据库同步完成" : "数据库同步失败，请查看日志";
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, msg));
        }
        if (ok && wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0,
                std::string(ui::refresh_cmd::kRowsChanged)));
        }
        Logger::write(msg, ok ? LogLevel::REPORT : LogLevel::ERR);
    } catch (const std::exception& e) {
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("ERR:") + e.what()));
        }
        Logger::write(std::string("Sync databases error: ") + e.what(), LogLevel::ERR);
    }
}

void AppController::findProxyByIndexIdAsync(const std::string& indexId, wxEvtHandler* wxHandler) {
        AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, wxHandler};
        if (!guard.isAllowed()) return;
    workerThread_ = std::thread([this, indexId, wxHandler]() {
        ScopeGuard<std::atomic<bool>> _guard{isRunning_};
        try {
            std::vector<db::models::Profileitem> proxies = loadProxies();
            // Try exact indexId match first
            std::vector<db::models::Profileitem>::iterator it = std::find_if(proxies.begin(), proxies.end(),
                [&indexId](const db::models::Profileitem& p) { return p.indexid == indexId; });
            
            // If not found, try address prefix match
            if (it == proxies.end()) {
                it = std::find_if(proxies.begin(), proxies.end(),
                    [&indexId](const db::models::Profileitem& p) { 
                        return p.address.find(indexId) == 0; 
                    });
            }
            
            if (it == proxies.end()) {
                if (wxHandler) {
                    wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "NOTFOUND:" + indexId));
                }
                return;
            }
            
            ProxyBatchTester tester(db_, config_, "", &cancelRequested_, &netMon_);
            bool ok = tester.runWithIndexId(it->indexid);

            if (wxHandler) {
                TestResult result = tester.getLastResult();
                std::string delayStr = utils::isTestResultValid(result.success, result.latencyMs) ? std::to_string(result.latencyMs) : std::string("");
                std::string message = result.success ? std::to_string(result.latencyMs) + "ms"
                                                     : result.errorMsg.empty() ? "FAIL" : result.errorMsg;
                wxQueueEvent(wxHandler,
                             new ProxyTestProgressEvent(1, 1, it->indexid, "", delayStr,
                                                        message, true));
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, ok ? ("FOUND:" + it->indexid + ":" + it->address) : ("ERR:" + message)));
            }

        } catch (const std::exception& e) {
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("ERR:") + e.what()));
            }
        }
    });
}

void AppController::syncDatabasesAsync(wxEvtHandler* wxHandler) {
        AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, wxHandler};
        if (!guard.isAllowed()) return;
    workerThread_ = std::thread(&AppController::doSyncDatabases, this, wxHandler);
}

// ---------------------------------------------------------------
// Batch Region Resolution
// ---------------------------------------------------------------
int AppController::resolveRegionsForValidProxies(const std::string& subId) {
    RegionBatchResolver resolver(db_, config_, nullptr, &netMon_);
    return resolver.run(subId);
}

// ---------------------------------------------------------------
// Async Batch Region Resolution (standalone, triggered from UI)
// ---------------------------------------------------------------
void AppController::resolveRegionsBatchAsync(wxEvtHandler* handler, const std::string& subId) {
    AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, handler};
    if (!guard.isAllowed()) return;
    workerThread_ = std::thread(&AppController::doResolveRegionsBatch, this, handler, subId);
}

void AppController::doResolveRegionsBatch(wxEvtHandler* handler, const std::string& subId) {
    ScopeGuard<std::atomic<bool>> _guard{isRunning_};

    try {
        if (isTestCancelled()) {
            if (handler) {
                wxQueueEvent(handler, new StatusUpdateEvent(0, "CANCELLED"));
            }
            return;
        }

        if (handler) {
            wxQueueEvent(handler, new StatusUpdateEvent(0, "RESOLVE_REGION_START"));
            // Also notify the top-level MainFrame window so the cancel button
            // gets enabled.  Custom wxEvent types do NOT propagate up the
            // window hierarchy (unlike wxCommandEvent).
            if (wxWindow* win = dynamic_cast<wxWindow*>(handler)) {
                if (wxWindow* topLevel = wxGetTopLevelParent(win)) {
                    if (topLevel != win) {
                        wxQueueEvent(topLevel, new StatusUpdateEvent(0, "RESOLVE_REGION_START"));
                    }
                }
            }
        }

        RegionBatchResolver resolver(db_, config_, &cancelRequested_, &netMon_);
        resolver.setProgressCallback([handler](const std::string& indexId, const std::string& region, int processed, int total) {
            if (handler) {
                std::string msg = "地区解析: " + indexId;
                if (!region.empty()) {
                    msg += " -> " + region;
                }
                msg += " (" + std::to_string(processed) + "/" + std::to_string(total) + ")";
                wxQueueEvent(handler, new ProxyTestProgressEvent(processed, total, "", "", "", msg, false));
            }
        });
        int resolved = resolver.run(subId);

        std::string msg;
        if (resolved > 0) {
            msg = "批量地区解析完成: " + std::to_string(resolved) + " 个代理";
        } else {
            msg = "地区解析完成（无新结果）";
        }

        if (handler) {
            wxQueueEvent(handler, new StatusUpdateEvent(0, msg));
            // Send completion event to restore UI state and trigger refresh
            wxQueueEvent(handler, new ProxyTestProgressEvent(0, 0, "", "", "", msg, true));
        }

        // Also broadcast to top-level window for proxy list refresh
        if (wxWindow* win = dynamic_cast<wxWindow*>(handler)) {
            if (wxWindow* topLevel = wxGetTopLevelParent(win)) {
                if (topLevel != win) {
                    wxQueueEvent(topLevel, new ProxyTestProgressEvent(0, 0, "", "", "", msg, true));
                    // Ask MainFrame to reload the proxy rows so the Region
                    // column reflects freshly resolved values (refreshResults
                    // alone only re-reads ProfileExItem test results).
                    wxQueueEvent(topLevel, new StatusUpdateEvent(0, "REGION_RESOLVE_DONE"));
                }
            }
        }

        Logger::write("resolveRegionsBatch: " + msg, LogLevel::REPORT);
    } catch (const std::exception& e) {
        if (handler) {
            wxQueueEvent(handler, new StatusUpdateEvent(0, std::string("地区解析错误: ") + e.what()));
}
        Logger::write(std::string("resolveRegionsBatch error: ") + e.what(), LogLevel::ERR);
     }
 }

// ---------------------------------------------------------------
// Single proxy region resolution (uses RegionBatchResolver pattern)
// ---------------------------------------------------------------
void AppController::resolveSingleProxyRegionAsync(const std::string& indexId, wxEvtHandler* handler) {
    AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, handler};
    if (!guard.isAllowed()) return;
    workerThread_ = std::thread(&AppController::doResolveSingleProxyRegion, this, indexId, handler);
}

void AppController::doResolveSingleProxyRegion(const std::string& indexId, wxEvtHandler* handler) {
    ScopeGuard<std::atomic<bool>> _guard{isRunning_};

    try {
        if (isTestCancelled()) {
            if (handler) {
                wxQueueEvent(handler, new StatusUpdateEvent(0, "CANCELLED"));
            }
            return;
        }

        if (handler) {
            wxQueueEvent(handler, new StatusUpdateEvent(0, "正在解析单个代理地区..."));
        }

        // Query the specific proxy by indexId
        std::string sql = "SELECT p.IndexId, p.Address, p.Remarks"
                          " FROM ProfileItem p"
                          " INNER JOIN ProfileExItem e ON p.IndexId = e.IndexId"
                          " WHERE p.IndexId = ? AND CAST(e.delay AS INTEGER) > 0 LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            Logger::write("resolveSingleRegion: SQL error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
            if (handler) {
                wxQueueEvent(handler, new StatusUpdateEvent(0, "查询代理失败"));
            }
            return;
        }
        sqlite3_bind_text(stmt, 1, indexId.c_str(), -1, SQLITE_TRANSIENT);

        std::string proxyIndexId, proxyAddress, proxyRemarks;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* rawIdx = sqlite3_column_text(stmt, 0);
            if (rawIdx) proxyIndexId = reinterpret_cast<const char*>(rawIdx);
            const unsigned char* rawAddr = sqlite3_column_text(stmt, 1);
            if (rawAddr) proxyAddress = reinterpret_cast<const char*>(rawAddr);
            const unsigned char* rawRem = sqlite3_column_text(stmt, 2);
            if (rawRem) proxyRemarks = reinterpret_cast<const char*>(rawRem);
        }
        sqlite3_finalize(stmt);

        if (proxyIndexId.empty()) {
            Logger::write("resolveSingleRegion: proxy not found or invalid: " + indexId, LogLevel::INFO);
            if (handler) {
                wxQueueEvent(handler, new StatusUpdateEvent(0, "代理未找到或无效"));
            }
            return;
        }

        Logger::write("[RegionBatchResolver] Resolving region for " + proxyAddress + " (" + proxyIndexId + ")", LogLevel::INFO);

        // Reuse shared ipwho.is resolution logic from RegionBatchResolver
        std::string response = RegionBatchResolver::fetchRegionFromIpWhoIs(proxyAddress);
        if (response.empty()) {
            if (handler)
                wxQueueEvent(handler, new StatusUpdateEvent(0, "API请求失败"));
            return;
        }

        std::string region = RegionBatchResolver::parseRegionFromJson(response);
        if (region.empty()) {
            if (handler)
                wxQueueEvent(handler, new StatusUpdateEvent(0, "无法解析地区"));
            return;
        }

        // Update database
        const char* updateSql = "UPDATE ProfileItem SET Region = ? WHERE IndexId = ?;";
        sqlite3_stmt* updateStmt = nullptr;
        if (sqlite3_prepare_v2(db_, updateSql, -1, &updateStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(updateStmt, 1, region.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(updateStmt, 2, proxyIndexId.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(updateStmt) == SQLITE_DONE) {
                Logger::write("[UI] Resolved region for " + proxyIndexId + ": " + region, LogLevel::INFO);
            }
            sqlite3_finalize(updateStmt);
        }

        std::string msg = "解析成功: " + region;
        if (handler) {
            wxQueueEvent(handler, new StatusUpdateEvent(0, msg));
            wxQueueEvent(handler, new ProxyTestProgressEvent(0, 0, proxyIndexId, "", "", msg, true));
            // Ask MainFrame to reload the proxy rows so the Region column
            // shows the freshly resolved value.
            if (wxWindow* win = dynamic_cast<wxWindow*>(handler)) {
                if (wxWindow* topLevel = wxGetTopLevelParent(win)) {
                    if (topLevel != win) {
                        wxQueueEvent(topLevel, new StatusUpdateEvent(0, "REGION_RESOLVE_DONE"));
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        if (handler) {
            wxQueueEvent(handler, new StatusUpdateEvent(0, std::string("解析错误: ") + e.what()));
        }
        Logger::write(std::string("resolveSingleProxyRegion error: ") + e.what(), LogLevel::ERR);
    }
}

// ---------------------------------------------------------------
// AutoTask
// ---------------------------------------------------------------
void AppController::runAutoTaskAsync(wxEvtHandler* wxHandler) {
        AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, wxHandler};
        if (!guard.isAllowed()) return;
    workerThread_ = std::thread(&AppController::doRunAutoTask, this, wxHandler);
}

void AppController::resumeAutoTaskAsync(wxEvtHandler* wxHandler) {
        AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, wxHandler};
        if (!guard.isAllowed()) return;
    workerThread_ = std::thread(&AppController::doResumeAutoTask, this, wxHandler);
}

void AppController::doAutoTaskImpl(wxEvtHandler* wxHandler, bool resume) {
    ScopeGuard<std::atomic<bool>> _guard{isRunning_};

    try {
        std::string baseDir = std::filesystem::path(config_.database_path).parent_path().string();
        AutoTaskManager manager(db_, config_, baseDir, &cancelRequested_, &netMon_);

        manager.setProgressCallback([wxHandler](const AutoTaskProgress& progress) {
            wxString stepName(progress.step_name);
            wxString msg = wxString::Format(L"自动任务 [%d/%d] %s",
                progress.current_step, progress.total_steps,
                stepName);
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0, msg.ToStdString()));
            }
        });

        bool ok = resume ? manager.resume() : manager.run(config_.auto_task.steps);

        std::string msg = ok ? "AutoTask completed" : "AutoTask failed";
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, msg));
        }
        Logger::write(msg, ok ? LogLevel::REPORT : LogLevel::ERR);
    } catch (const std::exception& e) {
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("AutoTask error: ") + e.what()));
        }
        Logger::write(std::string("doAutoTaskImpl error: ") + e.what(), LogLevel::ERR);
    }
}

void AppController::doRunAutoTask(wxEvtHandler* wxHandler) {
    doAutoTaskImpl(wxHandler, false);
}

void AppController::doResumeAutoTask(wxEvtHandler* wxHandler) {
    doAutoTaskImpl(wxHandler, true);
}
