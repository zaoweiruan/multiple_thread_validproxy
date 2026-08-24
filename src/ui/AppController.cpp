#include "AppController.h"
#include "Events.h"
#include "ui/AsyncOperationGuard.h"
#include "ui/ScopeGuard.h"
#include "service/DatabaseConnectionService.h"

using ui::AsyncOperationGuard;
using ui::ScopeGuard;

#include "SubitemUpdaterV2.h"
#include "Profileexitem.h"
#include "ProxyBatchTester.h"
#include "ConfigGenerator.h"
#include "config/OutboundBuilderFactory.h"
#include "config/SingBoxOutboundBuilderFactory.h"
#include "ShareLink.h"
#include "AutoTaskManager.h"
#include "Utils.h"
#include "Logger.h"
#include "RegionBatchResolver.h"
#include "ProcessInspector.h"
#include "UrlFetcher.h"
#include "ProxyConnectivityVerifier.h"
#include "ProxyScorer.h"

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
    if (!config_.network_monitor.enabled) {
        netMon_.setEnabled(false);
    } else {
        netMon_.Start(config_.network_monitor.checkUrls,
                     config_.network_monitor.checkIntervalMs,
                     config_.network_monitor.checkTimeoutMs);
        // Configure probe-on-disconnect: wait for N consecutive failed probes
        // before setting the cancel flag, instead of aborting immediately.
        // maxProbes > 0: probe mode (grace window). maxProbes == 0: immediate cancel.
        netMon_.setProbeOnDisconnect(config_.network_monitor.maxProbes);
        // When the monitor exhausts its probe threshold, it will set this flag,
        // which propagates to ProxyBatchTester via externalCancel,
        // causing active tests to abort.
        netMon_.setCancelOnDisconnect(&cancelRequested_);
    }
    exitListener_ = new proc::ProcessExitListener();
}

AppController::~AppController() {
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
        // DIAGNOSTIC INSTRUMENTATION (Phase 3/4 test per systematic-debugging skill)
        // Measures real elapsed time from destructor entry to join completion or 5s timeout.
        // REMOVE after hypothesis verification.
        std::chrono::steady_clock::time_point joinWaitStart = std::chrono::steady_clock::now();
        Logger::write("[AppController] Destructor: starting 5s join wait for workerThread_", LogLevel::DEBUG);

        // Wait up to 5 seconds for thread to finish gracefully
        // If thread doesn't respond, detach it to allow process exit
        std::future<void> fut = std::async(std::launch::async, [this]() {
            if (workerThread_.joinable()) {
                workerThread_.join();
            }
        });
        if (fut.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - joinWaitStart).count();
            Logger::write("[AppController] Destructor: 5s timeout fired after " + std::to_string(elapsedMs) + " ms — detaching", LogLevel::WARN);
            workerThread_.detach();
        } else {
            long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - joinWaitStart).count();
            Logger::write("[AppController] Destructor: worker thread joined successfully after " + std::to_string(elapsedMs) + " ms", LogLevel::DEBUG);
        }
    }

    // Now it is safe to shut down XrayManager — no thread still holds a ref
    XrayManager::release();
}

// ---------------------------------------------------------------
// Config
// ---------------------------------------------------------------
bool AppController::saveConfig(const config::AppConfig& cfg) {
    config_ = cfg;
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
    // 1) Update historyDao_ to point to the new database handle
    historyDao_.setDb(db_);

    // 2) Clear stale standalone proxy state — old entries reference the
    //    previous database and may contain invalid runtimeHistoryId values.
    {
        std::lock_guard<std::mutex> lock(standaloneMutex_);
        standaloneProxies_.clear();
    }
    proxyWatchKeys_.clear();

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

bool AppController::importSubscription(const std::string& url) {
    try {
        update::SubitemUpdaterV2 updater(db_, config_.proxy.xray_executable, config_, nullptr, "");
        return updater.importSingleUrl(url);
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
    XrayManager* manager = XrayManager::getInstance(xrayPath, configDir, config_.xray_workers);
    ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms);
    finder.findFirstWorkingProxy();
    return finder.getLastResult();
}

TestResult AppController::findBestProxy() {
    std::string xrayPath = config_.proxy.xray_executable;
    std::string configDir = utils::getExecutableDir() + "/config";
    XrayManager* manager = XrayManager::getInstance(xrayPath, configDir, config_.xray_workers);
    ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms);
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
        // Configure probe-on-disconnect: maxProbes > 0 enables probe mode
        netMon_.setProbeOnDisconnect(config_.network_monitor.maxProbes);
        netMon_.setCancelOnDisconnect(&cancelRequested_);
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
    const std::string configFileName = "standalone_" + indexId + (useSingBox ? "-singbox.json" : "-xray.json");
    if (proc::ProcessInspector::isProcessRunningWithConfig(configFileName)) {
        Logger::write("[StandaloneProxy] A proxy using config '" + configFileName
                      + "' is already running. Please stop it before starting a new one.",
                      LogLevel::ERR);
        return false;
    }

    // Use override port if provided, otherwise use configured SOCKS port
    int socksPort = (overridePort > 0) ? overridePort : config_.proxy.socks_base_port;

    // Generate proxy outbound JSON using appropriate builder
    boost::json::object proxyOutbound;
    if (useSingBox) {
        config::SingBoxOutboundBuilderFactory factory;
        proxyOutbound = factory.create(profile, "proxy");
    } else {
        config::OutboundBuilderFactory factory;
        proxyOutbound = factory.create(profile, "proxy");
    }

    // Build outbounds array based on backend
    boost::json::array outboundsArr;
    outboundsArr.push_back(proxyOutbound);

    if (useSingBox) {
        // Sing-box: use "direct" and "block" tag types
        boost::json::object directOutbound;
        directOutbound["type"] = "direct";
        directOutbound["tag"] = "direct";
        outboundsArr.push_back(directOutbound);

        boost::json::object blockOutbound;
        blockOutbound["type"] = "block";
        blockOutbound["tag"] = "block";
        outboundsArr.push_back(blockOutbound);
    } else {
        // Xray: use "freedom" and "blackhole" protocols
        boost::json::object freedomOutbound;
        freedomOutbound["tag"] = "direct";
        freedomOutbound["protocol"] = "freedom";
        outboundsArr.push_back(freedomOutbound);

        boost::json::object blackholeOutbound;
        blackholeOutbound["tag"] = "block";
        blackholeOutbound["protocol"] = "blackhole";
        outboundsArr.push_back(blackholeOutbound);
    }

    // Read the full config template
    std::string exeDir = utils::getExecutableDir();
    std::string templatePath;
    std::string executable;
    std::string assetDir;
    std::string envVarName;

    if (useSingBox) {
        templatePath = config_.proxy.singbox_template_config_path;
        if (templatePath.empty()) {
            templatePath = exeDir + "\\singbox-config-template.json";
        }
        executable = config_.proxy.singbox_executable;
        assetDir = config_.proxy.singbox_asset_dir;
        envVarName = "SING_BOX_LOCATION_ASSET";
    } else {
        templatePath = config_.proxy.template_config_path;
        if (templatePath.empty()) {
            templatePath = exeDir + "\\xray-config-template.json";
        }
        executable = config_.proxy.xray_executable;
        assetDir = config_.proxy.xray_asset_dir;
        envVarName = "XRAY_LOCATION_ASSET";
    }

    std::ifstream templateFile(templatePath);
    if (!templateFile.is_open()) {
        Logger::write("[StandaloneProxy] Cannot open template: " + templatePath, LogLevel::ERR);
        return false;
    }
    std::string templateContent((std::istreambuf_iterator<char>(templateFile)),
                                 std::istreambuf_iterator<char>());
    templateFile.close();

    // Parse and modify the template
    boost::json::value templateVal = boost::json::parse(templateContent);
    boost::json::object configObj = templateVal.as_object();

    // Replace the SOCKS inbound port with the dynamically allocated port
    // Xray uses "inbounds[].port", sing-box uses "inbounds[].listen_port"
    if (configObj.contains("inbounds") && !configObj["inbounds"].as_array().empty()) {
        boost::json::object& inbound = configObj["inbounds"].as_array()[0].as_object();
        if (inbound.contains("listen_port")) {
            inbound["listen_port"] = socksPort;  // sing-box format
        } else if (inbound.contains("port")) {
            inbound["port"] = socksPort;  // Xray format
        }
    }

    // Replace the outbounds array with our generated one
    configObj["outbounds"] = outboundsArr;

    // For sing-box: inject DNS rule to prevent bootstrap loop.
    // The proxy server's own domain must resolve via local DNS, not through
    // the proxy (remote_dns has detour "proxy"), otherwise DNS resolution
    // would need the proxy before the proxy can connect.
    if (useSingBox) {
        // Check if address looks like a domain name (contains at least one letter and a dot)
        bool isDomain = false;
        for (char c : profile.address) {
            if (std::isalpha(static_cast<unsigned char>(c))) {
                isDomain = true;
                break;
            }
        }
        if (isDomain && profile.address.find('.') != std::string::npos) {
            // Insert DNS rule at the beginning of the rules array
            if (configObj.contains("dns")) {
                boost::json::object& dnsObj = configObj["dns"].as_object();
                if (dnsObj.contains("rules")) {
                    boost::json::array& rules = dnsObj["rules"].as_array();
                    boost::json::object proxyDnsRule;
                    proxyDnsRule["domain"] = boost::json::array{profile.address};
                    proxyDnsRule["server"] = "local_local";
                    rules.insert(rules.begin(), proxyDnsRule);
                    Logger::write("[StandaloneProxy] DNS bootstrap rule added for: " + profile.address, LogLevel::DEBUG);
                }
            }
        }
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
            Logger::write("[StandaloneProxy] Failed to write config: " + configPath, LogLevel::ERR);
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

    // Set environment variable (inherited by child)
    SetEnvironmentVariableA(envVarName.c_str(), assetDir.c_str());

    BOOL created = CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                                   CREATE_NEW_CONSOLE,  // Show proxy process in its own console window
                                   nullptr, nullptr, &si, &pi);
    if (!created) {
        DWORD err = GetLastError();
        Logger::write("[StandaloneProxy] CreateProcess failed: " + std::to_string(err), LogLevel::ERR);
        return false;
    }

    // Store process info (no Job Object — process runs independently from parent)
    StandaloneProxyInfo info;
    info.indexId = indexId;
    info.configPath = configPath;
    info.socksPort = socksPort;
    info.processHandle = pi.hProcess;
    info.running = true;
    standaloneProxies_[indexId] = std::move(info);

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
            socksPort, config_.test_timeout_ms * 3);
        if (!portReady) {
            Logger::write("[StandaloneProxy] SOCKS port never became ready for " + indexId
                          + " on SOCKS5 :" + std::to_string(socksPort), LogLevel::WARN);
            exDao_.updateTestResult(indexId, -1, false, "port never ready");
            // Release lock before modal dialog — wxMessageBox pumps WM_TIMER
            // which triggers onProxyMonTimer → getRunningStandaloneCount →
            // same-thread deadlock on standaloneMutex_.
            lock.unlock();
            const int rc = wxMessageBox("代理已启动但连通性验证失败，是否关闭该进程？",
                                        "连通性验证失败", wxYES_NO | wxCANCEL, topWindow_);
            lock.lock();
            if (rc == wxYES) {
                TerminateProcess(pi.hProcess, 1);
                WaitForSingleObject(pi.hProcess, 5000);
                CloseHandle(pi.hProcess);
                standaloneProxies_.erase(indexId);
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
                TerminateProcess(pi.hProcess, 1);
                WaitForSingleObject(pi.hProcess, 5000);
                CloseHandle(pi.hProcess);
                standaloneProxies_.erase(indexId);
                Logger::write("[StandaloneProxy] Terminated unverified process for " + indexId,
                              LogLevel::INFO);
                return false;
            }
            // wxNO / wxCANCEL → leave the process alive but unmanaged: no history row,
            // no exit-listener watch, no monitoring.
            StandaloneProxyInfo& info = standaloneProxies_[indexId];
            info.running = false;
            info.managed = false;
            Logger::write("[StandaloneProxy] Proxy left unmanaged (connectivity failed) for " + indexId,
                          LogLevel::WARN);
            return true;
        }
        Logger::write("[StandaloneProxy] Connectivity test PASSED for " + indexId
                      + " on SOCKS5 :" + std::to_string(socksPort), LogLevel::DEBUG);
        // Write startup time into ProfileExItem.message (+<startup time>)
        exDao_.updateStartupTime(indexId, db_);
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
        const std::string startedAtStr = safeStartedAt;
        // Capture configFileName by value for the takeoverFn lambda
        const std::string configFileNameCopy = configFileName;

        // Heartbeat/finalize baseline: process may have started before we
        // began watching (normally ~0 here; nonzero for adoption/takeover).
        const long long baselineMs =
            safeStartedAt.empty()
                ? 0LL
                : proc::ProcessInspector::durationMsBetween(
                      safeStartedAt, utils::getCurrentTimestampFormatted());

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
            [this](const std::string& idx, int64_t hid) {
                {
                    std::lock_guard<std::mutex> lock(standaloneMutex_);
                    auto it = standaloneProxies_.find(idx);
                    if (it != standaloneProxies_.end()) {
                        it->second.running = false;
                    }
                }
                if (topWindow_) {
                    wxQueueEvent(topWindow_, new StandaloneProxyEvent(
                        idx, "", 0, /*started=*/false, "进程已退出"));
                }
            },
            // TakeoverFn (R6 auto-takeover): when the old process exits but a new one
            // with the same config file is detected, take over the new process.
            [this, &configFileNameCopy, &pi, indexId](
                int oldPid, const std::string& oldIndexId, int64_t oldHistoryId,
                const std::string& cfName) -> bool {
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
                    HANDLE newHandle = OpenProcess(
                        PROCESS_QUERY_INFORMATION | SYNCHRONIZE,
                        FALSE, procs[i].pid);
                    if (!newHandle) {
                        continue;
                    }
                    // Close the old handle
                    if (pi.hProcess) {
                        CloseHandle(pi.hProcess);
                        pi.hProcess = nullptr;
                    }
                    // Insert new history start for the takeover session
                    // (fallback must be datetime-formatted: stored as started_at)
                    const std::string newStartedAt =
                        procs[i].creationTime.empty()
                            ? utils::getCurrentTimestampFormatted()
                            : procs[i].creationTime;
                    int64_t newRhId = historyDao_.insertStart(
                        indexId, newStartedAt,
                        static_cast<int64_t>(procs[i].pid), db_);
                    if (newRhId < 0) {
                        CloseHandle(newHandle);
                        return false;
                    }
                    // Update standaloneProxies_ with the new handle
                    {
                        std::lock_guard<std::mutex> lock(standaloneMutex_);
                        auto& sp = standaloneProxies_[indexId];
                        sp.processHandle = newHandle;
                        sp.running = true;
                        sp.runtimeHistoryId = newRhId;
                    }
                    runtimeHistoryId_ = newRhId;
                    const long long takeoverBaselineMs =
                        newStartedAt.empty()
                            ? 0LL
                            : proc::ProcessInspector::durationMsBetween(
                                  newStartedAt, utils::getCurrentTimestampFormatted());
                    proxyWatchKeys_[indexId] = exitListener_->watch(
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
            configFileNameCopy,  // R6: pass configFileName into the Watcher
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

std::vector<StandaloneMonitorRow> AppController::getWatchedStandaloneMonitors() {
    // Pass 1: copy watched entries under the lock. Only running && managed
    // entries are shown — matches the "currently watched" semantics of the
    // monitor dialog (unmanaged processes have no history row / no watcher).
    struct WatchedEntry {
        std::string indexId;
        int socksPort;
        int64_t historyId;
        std::string fallbackStartedAt;
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
    std::vector<StandaloneMonitorRow> rows;
    for (std::size_t j = 0; j < watched.size(); ++j) {
        StandaloneMonitorRow row;
        row.indexId = watched[j].indexId;
        row.socksPort = watched[j].socksPort;
        // Host = ProfileItem.Address (spec v1.1); "-" rendered by the dialog
        // when the profile has been deleted or the address is empty.
        std::optional<db::models::Profileitem> profile =
            profileDao.getByIndexId(watched[j].indexId);
        if (profile.has_value()) {
            row.host = profile->address;
        }
        std::unordered_map<int64_t, const db::models::ProxyRuntimeHistoryItem*>::const_iterator hit =
            byId.find(watched[j].historyId);
        if (hit != byId.end()) {
            row.pid = hit->second->pid;
            row.startedAt = hit->second->startedAt;
        } else {
            // History row missing (insertStart failed or legacy entry):
            // fall back to the in-memory startedAt, pid unknown.
            row.pid = -1;
            row.startedAt = watched[j].fallbackStartedAt;
        }
        row.durationMs = row.startedAt.empty()
            ? 0LL
            : proc::ProcessInspector::durationMsBetween(row.startedAt, now);
        if (row.durationMs < 0) {
            row.durationMs = 0;
        }
        rows.push_back(row);
    }
    return rows;
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
    const boost::json::value& first = it->value().as_array().at(0);
    if (!first.is_object()) {
        return 0;
    }
    const boost::json::object& inbound = first.as_object();
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

void AppController::adoptDanglingStandaloneProxies() {
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

            // Skip already-managed proxies (either in map or being watched).
            {
                std::lock_guard<std::mutex> lock(standaloneMutex_);
                if (standaloneProxies_.find(indexId) != standaloneProxies_.end() ||
                    proxyWatchKeys_.find(indexId) != proxyWatchKeys_.end()) {
                    continue;
                }
            }

            HANDLE hProcess = OpenProcess(
                PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, procs[i].pid);
            if (!hProcess) {
                continue;
            }

            // Reuse an in-progress history session for THIS exact process instance
            // (indexId + pid + system creation time), otherwise open a new one.
            // Fallback must be datetime-formatted: it is stored as started_at
            // and later parsed by durationMsBetween (epoch seconds would break).
            const std::string procStartedAt =
                procs[i].creationTime.empty()
                    ? utils::getCurrentTimestampFormatted()
                    : procs[i].creationTime;
            int64_t rhId = historyDao_.findInProgressHistory(
                indexId, static_cast<int64_t>(procs[i].pid),
                procStartedAt, db_);
            if (rhId < 0) {
                rhId = historyDao_.insertStart(
                    indexId, procStartedAt,
                    static_cast<int64_t>(procs[i].pid), db_);
            }
            if (rhId < 0) {
                CloseHandle(hProcess);
                continue;
            }
            const std::string startedAt = procStartedAt;

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
                [this](const std::string& idx, int64_t hid) {
                    {
                        std::lock_guard<std::mutex> lock(standaloneMutex_);
                        auto it = standaloneProxies_.find(idx);
                        if (it != standaloneProxies_.end()) {
                            it->second.running = false;
                        }
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

            // Resolve the SOCKS inbound port from the on-disk config so the
            // monitor dialog and logs can show it for adopted processes too.
            // Dangling processes may have been started by the worker copy
            // (bin\worker\validproxy.exe), whose configs live under
            // <exeDir>\worker\config\ — probe both known layouts.
            const std::string adoptedExeDir = utils::getExecutableDir();
            const std::string adoptedDirs[2] = {
                adoptedExeDir + "\\config\\",
                adoptedExeDir + "\\worker\\config\\"
            };
            int adoptedPort = 0;
            for (int di = 0; di < 2 && adoptedPort <= 0; ++di) {
                adoptedPort = readStandaloneInboundPort(
                    adoptedDirs[di] + configFileName);
            }
            // Step (b): an external program may launch the proxy with its config in
            // a non-standard directory (not <exeDir>\config nor <exeDir>\worker\config).
            // Derive the full config path from the launching command line and read the
            // port directly. This stays authoritative (no listener ambiguity) for both
            // xray (single inbound) and sing-box (many DNS inbounds).
            if (adoptedPort <= 0) {
                const std::string fullPath =
                    proc::ProcessInspector::extractConfigFullPath(procs[i].commandLine);
                if (!fullPath.empty()) {
                    adoptedPort = readStandaloneInboundPort(fullPath);
                }
            }
            if (adoptedPort <= 0) {
                Logger::write("[StandaloneProxy] Could not resolve inbound port from "
                              + configFileName, LogLevel::WARN);
            }

            {
                std::lock_guard<std::mutex> lock(standaloneMutex_);
                StandaloneProxyInfo& info = standaloneProxies_[indexId];
                info.indexId = indexId;
                info.configFileName = configFileName;
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
    std::lock_guard<std::mutex> lock(standaloneMutex_);
    auto it = standaloneProxies_.find(indexId);
    if (it == standaloneProxies_.end() || !it->second.running) {
        return false;
    }
    HANDLE hProcess = it->second.processHandle;
    if (hProcess) {
        TerminateProcess(hProcess, 1);
        CloseHandle(hProcess);
        it->second.processHandle = nullptr;
    }
    if (it->second.watchKey != 0) {
        exitListener_->unwatch(it->second.watchKey);
        it->second.watchKey = 0;
    }
    it->second.running = false;
    Logger::write("[StandaloneProxy] Stopped " + indexId + " on SOCKS5 :"
                  + std::to_string(it->second.socksPort), LogLevel::REPORT);
    return true;
}

void AppController::shutdownStandaloneProxies() {
    shutdownRequested_.store(true);
    std::lock_guard<std::mutex> lock(standaloneMutex_);
    for (auto& kv : standaloneProxies_) {
        if (kv.second.running && kv.second.processHandle) {
            TerminateProcess(kv.second.processHandle, 1);
            CloseHandle(kv.second.processHandle);
            kv.second.processHandle = nullptr;
            kv.second.running = false;
            if (kv.second.watchKey != 0) {
                exitListener_->unwatch(kv.second.watchKey);
                kv.second.watchKey = 0;
            }
        }
    }
    standaloneProxies_.clear();
    if (exitListener_) {
        exitListener_->shutdown();
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
        XrayManager* manager = XrayManager::getInstance(xrayPath, configDir, config_.xray_workers);
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
        ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms, &cancelRequested_);

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
        XrayManager* manager = XrayManager::getInstance(xrayPath, configDir, config_.xray_workers);
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
        ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms, &cancelRequested_);

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
    RegionBatchResolver resolver(db_, config_);
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

        RegionBatchResolver resolver(db_, config_, &cancelRequested_);
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
