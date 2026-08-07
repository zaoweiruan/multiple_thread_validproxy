#include "AppController.h"
#include "Events.h"
#include "ui/AsyncOperationGuard.h"
#include "ui/ScopeGuard.h"
#include "service/DatabaseConnectionService.h"

using ui::AsyncOperationGuard;
using ui::ScopeGuard;

#include "SubitemUpdaterV2.h"
#include "ProxyBatchTester.h"
#include "ConfigGenerator.h"
#include "config/OutboundBuilderFactory.h"
#include "config/SingBoxOutboundBuilderFactory.h"
#include "ShareLink.h"
#include "AutoTaskManager.h"
#include "Utils.h"
#include "Logger.h"
#include "RegionBatchResolver.h"

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
}

AppController::~AppController() {
    netMon_.Stop();

    // Standalone proxy processes run independently — NOT terminated here,
    // so they keep running when the main program exits.

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
            wxQueueEvent(handler, new ProxyListLoadedEvent(subIdCopy, {}, {}));
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
        std::vector<db::models::Profileitem> allProxies = dao.getAll();

        std::vector<db::models::Profileitem> proxies;
        if (subIdCopy.empty()) {
            proxies = std::move(allProxies);
        } else {
            std::copy_if(allProxies.begin(), allProxies.end(), std::back_inserter(proxies),
                [&subIdCopy](const db::models::Profileitem& p) { return p.subid == subIdCopy; });
        }

        db::models::ProfileExItemDAO exDao(readerDb);
        std::vector<db::models::ProfileExItem> exItems = exDao.getAll();

        sqlite3_close(readerDb);

        wxQueueEvent(handler, new ProxyListLoadedEvent(subIdCopy,
                     std::move(proxies), std::move(exItems)));
    }).detach();
}

std::optional<db::models::Profileitem> AppController::getProxyByIndexId(const std::string& indexId) {
    db::models::ProfileitemDAO dao(db_);
    return dao.getByIndexId(indexId);
}

std::vector<db::models::ProfileExItem> AppController::loadProxyResults() {
    db::models::ProfileExItemDAO dao(db_);
    return dao.getAll();
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
bool AppController::startStandaloneProxy(const std::string& indexId, int overridePort) {
    std::lock_guard<std::mutex> lock(standaloneMutex_);

    // Fetch proxy from DB
    db::models::ProfileitemDAO dao(db_);
    db::models::Profileitem profile;
    {
        auto opt = dao.getByIndexId(indexId);
        if (!opt) {
            Logger::write("[StandaloneProxy] Profile not found: " + indexId, LogLevel::ERR);
            return false;
        }
        profile = std::move(*opt);
    }

    // Use override port if provided, otherwise use configured SOCKS port
    int socksPort = (overridePort > 0) ? overridePort : config_.proxy.socks_base_port;

    // Determine which proxy backend to use based on use_singbox toggle
    bool useSingBox = config_.proxy.use_singbox;

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

    // Check for existing sing-box process before starting
    if (useSingBox) {
        std::string exeName = utils::getProcessNameFromPath(executable);
        if (exeName.empty()) {
            exeName = "sing-box.exe";
        }

        if (utils::isProcessRunning(exeName)) {
            std::string msg = "发现正在运行的 " + exeName + " 进程。\n是否结束已有进程并启动新代理？";
            int result = MessageBoxA(nullptr, msg.c_str(), "Sing-box 进程提示",
                                     MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
            if (result == IDNO) {
                Logger::write("[StandaloneProxy] User declined to kill existing "
                              + exeName + " process", LogLevel::INFO);
                return false;
            }

            Logger::write("[StandaloneProxy] Killing existing " + exeName + " processes...",
                          LogLevel::INFO);
            utils::killProcessByName(exeName);
            // Wait briefly for process resources to be released
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
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

    Logger::write("[StandaloneProxy] Started " + indexId + " on SOCKS5 :" + std::to_string(socksPort),
                  LogLevel::REPORT);
    return true;
}

int AppController::getStandaloneSocksPort(const std::string& indexId) const {
    std::lock_guard<std::mutex> lock(standaloneMutex_);
    auto it = standaloneProxies_.find(indexId);
    if (it != standaloneProxies_.end() && it->second.running) {
        return it->second.socksPort;
    }
    return -1;
}

std::vector<std::string> AppController::getRunningStandaloneIds() const {
    std::lock_guard<std::mutex> lock(standaloneMutex_);
    std::vector<std::string> ids;
    for (const auto& [id, info] : standaloneProxies_) {
        if (info.running) {
            ids.push_back(id);
        }
    }
    return ids;
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
        ProxyBatchTester tester(db_, config_, "", &cancelRequested_, &netMon_);
        bool ok = tester.runWithIndexId(indexId);

        // Get actual test result (delay + message) from the tester
        TestResult result = tester.getLastResult();
        std::string delayStr = (result.latencyMs > 0) ? std::to_string(result.latencyMs) : std::string("");
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
                std::string delayStr = (result.latencyMs > 0) ? std::to_string(result.latencyMs) : std::string("");
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

        // Reuse shared ipinfo.io resolution logic from RegionBatchResolver
        std::string response = RegionBatchResolver::fetchRegionFromIpInfo(proxyAddress, config_.ipinfo_token);
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
