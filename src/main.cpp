#include <iostream>
#include <iomanip>
#include <memory>
#include <fstream>
#include <sstream>
#include <sqlite3.h>
#include <curl/curl.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <filesystem>
#include <vector>
#include <queue>
#include <mutex>
#include <windows.h>
#include <random>

#include "Profileitem.h"
#include "Profileexitem.h"
#include "ConfigGenerator.h"
#include "ProxyFinder.h"
#include "ConfigReader.h"
#include "XrayApi.h"
#include "XrayManager.h"
#include "SubitemUpdaterV2.h"
#include "ProxyBatchTester.h"
#include "Utils.h"

namespace {

XrayManager* g_xrayManager = nullptr;

BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        std::cout << "\nCtrl+C detected, stopping xray instances..." << std::endl;
        if (g_xrayManager) {
            g_xrayManager->stopAll();
        }
        exit(1);
    }
    return FALSE;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
    
    std::cout << "validproxy starting..." << std::endl;

    std::string exeDir = utils::getExecutableDir();
    
    std::filesystem::path baseDir = std::filesystem::path(exeDir);
    std::filesystem::path configDir = baseDir / "config";
    std::filesystem::path logDir = baseDir / "log";
    if (!std::filesystem::exists(configDir)) {
        std::filesystem::create_directory(configDir);
    }
    if (!std::filesystem::exists(logDir)) {
        std::filesystem::create_directory(logDir);
    }
    
    std::string configPath = config::ConfigReader::getDefaultConfigPath();
    std::string singleSubId;
    std::string commandMode;
    std::string generatorIndexId;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                std::string p = argv[++i];
                std::filesystem::path fp(p);
                if (fp.is_relative()) {
                    fp = std::filesystem::path(exeDir) / fp;
                }
                configPath = fp.lexically_normal().string();
            }
        } else if (arg == "-show-sub" || arg == "--show-sub") {
            commandMode = "show-sub";
        } else if (arg == "-G" || arg == "-generator" || arg == "--generator") {
            if (i + 1 < argc) {
                generatorIndexId = argv[++i];
                commandMode = "generator";
            }
        } else if (arg == "-FMIN" || arg == "-findminproxy" || arg == "--findminproxy") {
            commandMode = "findminproxy";
        } else if (arg == "-F" || arg == "-find-proxy" || arg == "--find-proxy") {
            commandMode = "find-proxy";
        } else if (arg == "-T" || arg == "-test-sub" || arg == "--test-sub") {
            if (i + 1 < argc) {
                singleSubId = argv[++i];
                commandMode = "test-sub";
            }
        } else if (arg == "-UA" || arg == "-update-all" || arg == "--update-all") {
            singleSubId = "__all__";
            commandMode = "update";
        } else if (arg == "-U" || arg == "-update" || arg == "--update") {
            if (i + 1 < argc) {
                singleSubId = argv[++i];
                commandMode = "update";
            }
        } else if (arg == "-D" || arg == "-dedup" || arg == "--dedup") {
            commandMode = "dedup";
        } else if (arg == "-h" || arg == "--help") {
std::cout << "Usage: validproxy [options]\n"
                      << "Options:\n"
                      << "  -c, --config <path>   Config file path (default: config.json)\n"
                      << "  -show-sub, --show-sub  Show all subscriptions\n"
                      << "  -G, -generator <id>  Generate outbound JSON for profile by indexId\n"
                      << "  -F, -find-proxy     Find first working proxy (first found)\n"
                      << "  -FMIN,-findminproxy   Find first working proxy (sorted by delay)\n"
                      << "  -U, -update <id>      Update single subscription by ID\n"
                      << "  -UA, -update-all     Update all enabled subscriptions\n"
                      << "  -T, -test-sub <id>   Test proxies from subscription by ID\n"
                      << "  -D, -dedup           Remove duplicate proxies from database\n"
                      << "  -h, --help           Show this help\n";
            return 0;
        } else if (arg.find(".json") != std::string::npos) {
            std::filesystem::path p(arg);
            if (p.is_relative()) {
                p = std::filesystem::path(exeDir) / p;
            }
            configPath = p.lexically_normal().string();
        } else {
            std::cerr << "Error: Unknown option '" << arg << "'" << std::endl;
            std::cerr << "Run 'validproxy --help' for usage information" << std::endl;
            return 1;
        }
    }
    
    if (commandMode == "generator") {
        auto appConfig = config::ConfigReader::load(configPath);
        if (!appConfig) {
            std::cerr << "Failed to load config from: " << configPath << std::endl;
            return 1;
        }
        
        sqlite3* db = nullptr;
        if (sqlite3_open(appConfig->database_path.c_str(), &db) != SQLITE_OK) {
            std::cerr << "Failed to open database: " << sqlite3_errmsg(db) << std::endl;
            return 1;
        }
        
        db::models::ProfileitemDAO profileDao(db);
        auto profileOpt = profileDao.getByIndexId(generatorIndexId);
        
        if (!profileOpt) {
            std::cerr << "Profile not found: " << generatorIndexId << std::endl;
            sqlite3_close(db);
            return 1;
        }
        
        config::ConfigGenerator configGen(db);
        auto config = configGen.generateConfig(*profileOpt);
        
        std::cout << "\n=== Generated Outbound JSON ===" << std::endl;
        std::cout << config.outbound_json << std::endl;
        
        std::string outFile = (configDir / (generatorIndexId + ".json")).string();
        
        std::ofstream out(outFile);
        if (out.is_open()) {
            out << config.outbound_json;
            out.close();
            std::cout << "\nSaved to: " << outFile << std::endl;
        } else {
            std::cerr << "Failed to write file: " << outFile << std::endl;
        }
        
        sqlite3_close(db);
        return 0;
    }
    
    if (commandMode == "show-sub") {
        auto appConfig = config::ConfigReader::load(configPath);
        if (!appConfig) {
            std::cerr << "Failed to load config from: " << configPath << std::endl;
            return 1;
        }
        
        sqlite3* db = nullptr;
        curl_global_init(CURL_GLOBAL_DEFAULT);
        
        if (sqlite3_open(appConfig->database_path.c_str(), &db) != SQLITE_OK) {
            std::cerr << "Failed to open database: " << sqlite3_errmsg(db) << std::endl;
            return 1;
        }
        
        db::models::SubitemDAO subDao(db);
        auto subs = subDao.getAll();
        
        std::cout << "\n=== Subscriptions ===" << std::endl;
        std::cout << "remarks                                    | id                    | url                                               | 启用\n";
        std::cout << "-----------------------------------------+-----------------------+---------------------------------------------------+----\n";
        
        for (const auto& sub : subs) {
            std::string remarks = sub.remarks;
            std::string id = sub.id;
            std::string url = sub.url;
            std::string enabled = (sub.enabled == "1") ? "是" : "否";
            
            if (remarks.length() > 40) remarks = remarks.substr(0, 37) + "...";
            if (id.length() > 20) id = id.substr(0, 17) + "...";
            if (url.length() > 50) url = url.substr(0, 47) + "...";
            
            std::cout << std::left << std::setw(40) << remarks << " | "
                     << std::setw(20) << id << " | "
                     << std::setw(50) << url << " | "
                     << std::right << std::setw(4) << enabled << "\n";
        }
        
        std::cout << "-----------------------------------------+-----------------------+---------------------------------------------------+----\n";
        std::cout << "Total: " << subs.size() << " subscriptions\n";
        
        db::models::ProfileitemDAO profileDao(db);
        
        std::string sqlTotal = "SELECT COUNT(*) FROM ProfileItem";
        sqlite3_stmt* stmt = nullptr;
        int totalCount = 0;
        if (sqlite3_prepare_v2(db, sqlTotal.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                totalCount = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        
        std::cout << "\n=== ProfileItem Statistics ===" << std::endl;
        std::cout << "ConfigType  | Count | Description\n";
        std::cout << "-----------+-------+-------------\n";
        
        std::vector<std::pair<int, std::string>> typeCounts = {
            {1, "VMess"},
            {2, "Custom"},
            {3, "Shadowsocks"},
            {4, "SOCKS"},
            {5, "VLESS"},
            {6, "Trojan"},
            {7, "Hysteria2"},
            {8, "TUIC"},
            {9, "WireGuard"},
            {10, "HTTP"},
            {11, "Anytls"},
            {12, "Naive"},
            {16, "WireGuard"},
            {17, "TUIC"}
        };
        
        for (const auto& [type, desc] : typeCounts) {
            std::string sql = "SELECT COUNT(*) FROM ProfileItem WHERE ConfigType = " + std::to_string(type);
            sqlite3_stmt* stmt2 = nullptr;
            int count = 0;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt2, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt2) == SQLITE_ROW) {
                    count = sqlite3_column_int(stmt2, 0);
                }
                sqlite3_finalize(stmt2);
            }
            std::cout << std::setw(10) << type << " | " << std::setw(5) << count << " | " << desc << "\n";
        }
        
        std::cout << "-----------+-------+-------------\n";
        std::cout << "Total      | " << std::setw(5) << totalCount << " | All profiles\n";
        
        std::cout << "\n=== ProfileItem by Subscription ===" << std::endl;
        std::cout << "SubId                      | Count\n";
        std::cout << "--------------------------+------\n";
        
        std::string sqlBySub = "SELECT Subid, COUNT(*) as cnt FROM ProfileItem WHERE Subid != '' AND Subid IS NOT NULL GROUP BY Subid ORDER BY cnt DESC";
        sqlite3_stmt* stmt3 = nullptr;
        if (sqlite3_prepare_v2(db, sqlBySub.c_str(), -1, &stmt3, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt3) == SQLITE_ROW) {
                const char* subid = (const char*)sqlite3_column_text(stmt3, 0);
                int count = sqlite3_column_int(stmt3, 1);
                std::string sid = subid ? subid : "(null)";
                if (sid.length() > 22) sid = sid.substr(0, 19) + "...";
                std::cout << std::setw(24) << std::left << sid << " | " << count << "\n";
            }
            sqlite3_finalize(stmt3);
        }
        
        sqlite3_close(db);
        curl_global_cleanup();
        
        return 0;
    }
    
if (commandMode == "find-proxy") {
        auto appConfig = config::ConfigReader::load(configPath);
        if (!appConfig) {
            std::cerr << "Failed to load config from: " << configPath << std::endl;
            return 1;
        }
        
        sqlite3* db = nullptr;
        curl_global_init(CURL_GLOBAL_DEFAULT);
        
        if (sqlite3_open(appConfig->database_path.c_str(), &db) != SQLITE_OK) {
            std::cerr << "Failed to open database: " << sqlite3_errmsg(db) << std::endl;
            return 1;
        }
        
        std::string exeBaseDir = exeDir;
        std::string configDir = exeBaseDir + "/config";
        
        char timestamp[32];
        time_t now = time(nullptr);
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));
        std::string logFile = (logDir / ("find_proxy_" + std::string(timestamp) + ".log")).string();
        std::ofstream logOutStream(logFile, std::ios::out | std::ios::trunc);
        std::cout << "Log file: " << logFile << std::endl;
        
        XrayManager* xrayMgr = XrayManager::getInstance(appConfig->xray_executable, configDir, appConfig->xray_workers, logOutStream.is_open() ? &logOutStream : nullptr);
        int started = xrayMgr->start(1, appConfig->xray_start_port, appConfig->xray_api_port);
        
        if (started == 0) {
            std::cerr << "Failed to start xray instance" << std::endl;
            sqlite3_close(db);
            curl_global_cleanup();
            return 1;
        }
        
        ProxyFinder finder(db, xrayMgr, appConfig->xray_executable, appConfig->test_url, "", appConfig->test_timeout_ms, logOutStream.is_open() ? &logOutStream : nullptr);
        auto ports = finder.findFirstWorkingProxy();
        
        if (ports.first > 0) {
            auto res = finder.getLastResult();
            std::cout << "\n=== Working Proxy ===" << std::endl;
            std::cout << "IndexId: " << res.indexId << std::endl;
            std::cout << "Address: " << res.address << ":" << res.port << std::endl;
            std::cout << "Delay: " << res.latencyMs << "ms" << std::endl;
            std::cout << "SocksPort: " << ports.first << std::endl;
            std::cout << "ApiPort: " << ports.second << std::endl;
        } else {
            std::cerr << "No working proxy found" << std::endl;
        }
        
        finder.release();
        XrayManager::release();
        
        if (logOutStream.is_open()) {
            logOutStream.close();
        }
        
        sqlite3_close(db);
        curl_global_cleanup();
        
        return ports.first > 0 ? 0 : 1;
    }
    
    if (commandMode == "findminproxy") {
        auto appConfig = config::ConfigReader::load(configPath);
        if (!appConfig) {
            std::cerr << "Failed to load config from: " << configPath << std::endl;
            return 1;
        }
        
        sqlite3* db = nullptr;
        curl_global_init(CURL_GLOBAL_DEFAULT);
        
        if (sqlite3_open(appConfig->database_path.c_str(), &db) != SQLITE_OK) {
            std::cerr << "Failed to open database: " << sqlite3_errmsg(db) << std::endl;
            return 1;
        }
        
        std::string exeBaseDir = exeDir;
        std::string configDir = exeBaseDir + "/config";
        
        char timestamp[32];
        time_t now = time(nullptr);
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));
        std::string logFile = (logDir / ("findmin_proxy_" + std::string(timestamp) + ".log")).string();
        std::ofstream logOutStream(logFile, std::ios::out | std::ios::trunc);
        std::cout << "Log file: " << logFile << std::endl;
        
        XrayManager* xrayMgr = XrayManager::getInstance(appConfig->xray_executable, configDir, appConfig->xray_workers, logOutStream.is_open() ? &logOutStream : nullptr);
        int started = xrayMgr->start(1, appConfig->xray_start_port, appConfig->xray_api_port);
        
        if (started == 0) {
            std::cerr << "Failed to start xray instance" << std::endl;
            sqlite3_close(db);
            curl_global_cleanup();
            return 1;
        }
        
        ProxyFinder finder(db, xrayMgr, appConfig->xray_executable, appConfig->test_url, "", appConfig->test_timeout_ms, logOutStream.is_open() ? &logOutStream : nullptr);
        auto ports = finder.findWorkingProxy("");
        
        if (ports.first > 0) {
            auto res = finder.getLastResult();
            std::cout << "\n=== Working Proxy (Minimum Delay) ===" << std::endl;
            std::cout << "IndexId: " << res.indexId << std::endl;
            std::cout << "Address: " << res.address << ":" << res.port << std::endl;
            std::cout << "Delay: " << res.latencyMs << "ms" << std::endl;
            std::cout << "SocksPort: " << ports.first << std::endl;
            std::cout << "ApiPort: " << ports.second << std::endl;
        } else {
            std::cerr << "No working proxy found" << std::endl;
        }
        
        finder.release();
        XrayManager::release();
        
        if (logOutStream.is_open()) {
            logOutStream.close();
        }
        
        sqlite3_close(db);
        curl_global_cleanup();
        
        return ports.first > 0 ? 0 : 1;
    }
    
    if (commandMode == "dedup") {
        auto appConfig = config::ConfigReader::load(configPath);
        if (!appConfig) {
            std::cerr << "Failed to load config from: " << configPath << std::endl;
            return 1;
        }
        
        sqlite3* db = nullptr;
        curl_global_init(CURL_GLOBAL_DEFAULT);
        
        if (sqlite3_open(appConfig->database_path.c_str(), &db) != SQLITE_OK) {
            std::cerr << "Failed to open database: " << sqlite3_errmsg(db) << std::endl;
            return 1;
        }
        
        char timestamp[32];
        time_t now = time(nullptr);
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));
        
        std::string logFileName = "dedup_" + std::string(timestamp) + ".log";
        std::string logFile = (logDir / logFileName).string();
        std::ofstream logOut(logFile, std::ios::out | std::ios::trunc);
        
        std::cout << "Mode: " << commandMode << std::endl;
        
        update::SubitemUpdaterV2 subUpdaterV2(db,
                                              appConfig->xray_executable,
                                              *appConfig,
                                              logOut.is_open() ? &logOut : nullptr,
                                              exeDir);
        bool result = subUpdaterV2.deduplicate();
        
        if (logOut.is_open()) {
            logOut.close();
        }
        
        sqlite3_close(db);
        curl_global_cleanup();
        
        std::cout << commandMode << " " << (result ? "completed" : "failed") << std::endl;
        return result ? 0 : 1;
    }
    
    if (!singleSubId.empty()) {
        auto appConfig = config::ConfigReader::load(configPath);
        if (!appConfig) {
            std::cerr << "Failed to load config from: " << configPath << std::endl;
            return 1;
        }
        
        sqlite3* db = nullptr;
        curl_global_init(CURL_GLOBAL_DEFAULT);
        
        if (sqlite3_open(appConfig->database_path.c_str(), &db) != SQLITE_OK) {
            std::cerr << "Failed to open database: " << sqlite3_errmsg(db) << std::endl;
            return 1;
        }
        
        char timestamp[32];
        time_t now = time(nullptr);
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));
        
        std::string logFileName;
        if (commandMode == "test-sub") {
            logFileName = "test_proxy_" + std::string(timestamp) + ".log";
        } else if (commandMode == "update") {
            logFileName = "sub_update_" + std::string(timestamp) + ".log";
        } else if (commandMode == "dedup") {
            logFileName = "dedup_" + std::string(timestamp) + ".log";
        } else {
            logFileName = "test_proxy_" + std::string(timestamp) + ".log";
        }
        
        std::string logFile = (logDir / logFileName).string();
        std::ofstream logOut(logFile, std::ios::out | std::ios::trunc);
        
        if (commandMode == "dedup") {
            std::cout << "Mode: " << commandMode << std::endl;
        } else {
            std::cout << "Mode: " << commandMode << ", subscription: " << singleSubId << std::endl;
            logOut << "[" << timestamp << "] Starting " << commandMode << " for subId: " << singleSubId << std::endl;
        }
        
        bool result;
        if (commandMode == "test-sub") {
            ProxyBatchTester tester(db, *appConfig, exeDir, logOut.is_open() ? &logOut : nullptr);
            g_xrayManager = tester.getXrayManager();
            result = tester.runWithSubId(singleSubId);
            if (appConfig->notification_enabled && appConfig->notification_on_test) {
                utils::sendNotification("Proxy Test Complete", result ? "Test completed successfully" : "Test failed");
            }
        } else if (commandMode == "dedup") {
            update::SubitemUpdaterV2 subUpdaterV2(db,
                                                  appConfig->xray_executable,
                                                  *appConfig,
                                                  logOut.is_open() ? &logOut : nullptr,
                                                  exeDir);
            result = subUpdaterV2.deduplicate();
        } else {
            update::SubitemUpdaterV2 subUpdaterV2(db,
                                                  appConfig->xray_executable,
                                                  *appConfig,
                                                  logOut.is_open() ? &logOut : nullptr,
                                                  exeDir);
            if (singleSubId == "__all__") {
                result = subUpdaterV2.run();
            } else {
                result = subUpdaterV2.runSingle(singleSubId);
            }
            if (appConfig->notification_enabled && appConfig->notification_on_update) {
                utils::sendNotification("Subscription Update Complete", result ? "Update completed successfully" : "Update failed");
            }
        }
        
        if (logOut.is_open()) {
            logOut.close();
        }
        
        sqlite3_close(db);
        curl_global_cleanup();
        
        std::cout << commandMode << " " << (result ? "completed" : "failed") << std::endl;
        
        return result ? 0 : 1;
    }
    
    auto appConfig = config::ConfigReader::load(configPath);
    if (!appConfig) {
        std::cerr << "Failed to load config from: " << configPath << std::endl;
        return 1;
    }
    
    int numWorkers = appConfig->xray_workers;
    int startPort = appConfig->xray_start_port;
    bool logEnabled = appConfig->log_enabled;
    
    std::cout << "Config loaded from: " << configPath << std::endl;
    std::cout << "Database path: " << appConfig->database_path << std::endl;
    std::cout << "SQL query: " << appConfig->sql_query << std::endl;
    std::cout << "Xray executable: " << appConfig->xray_executable << std::endl;
    std::cout << "Workers: " << numWorkers << std::endl;
    std::cout << "Start port: " << startPort << std::endl;

    auto startTime = std::chrono::system_clock::now();
    time_t startTimeT = std::chrono::system_clock::to_time_t(startTime);
    char startTimeStr[32];
    strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %H:%M:%S", localtime(&startTimeT));
    
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&startTimeT));
    
    std::string logFile = (logDir / ("test_log_" + std::string(timestamp) + ".log")).string();
    std::ofstream logOutStream;
    if (logEnabled) {
        logOutStream.open(logFile, std::ios::out | std::ios::trunc);
        if (logOutStream.is_open()) {
            logOutStream << "Test started at: " << startTimeStr << std::endl;
            logOutStream << "Workers: " << numWorkers << std::endl;
            logOutStream << "Start port: " << startPort << std::endl;
            logOutStream << "Test URL: " << appConfig->test_url << std::endl;
            logOutStream << "Test timeout: " << appConfig->test_timeout_ms << "ms" << std::endl;
        }
    }
    std::cout << "Log file: " << logFile << " (enabled: " << logEnabled << ")" << std::endl;

    sqlite3* db = nullptr;
    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (sqlite3_open(appConfig->database_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Failed to open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    std::cout << "Database opened: " << appConfig->database_path << std::endl;

ProxyBatchTester tester(db, *appConfig, exeDir, logOutStream.is_open() ? &logOutStream : nullptr);
    g_xrayManager = tester.getXrayManager();
    bool testResult = tester.run();
    
    if (g_xrayManager) {
        XrayManager::release();
    }
    
    if (logOutStream.is_open()) {
        logOutStream.flush();
        logOutStream.close();
    }
    
    sqlite3_close(db);
    curl_global_cleanup();
    
    std::cout << "validproxy " << (testResult ? "finished" : "failed") << std::endl;
    return testResult ? 0 : 1;
}