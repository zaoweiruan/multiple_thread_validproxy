#include <iostream>
#include <iomanip>
#include <memory>
#include <fstream>
#include <sstream>
#include <sqlite3.h>
#include <curl/curl.h>
#include <chrono>
#include <ctime>
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
#include "ShareLink.h"
#include "ProxyBatchTester.h"
#include "Utils.h"
#include "Logger.h"

namespace {

XrayManager* g_xrayManager = nullptr;
std::string g_commandMode;

std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
    return std::string(buf);
}

void logInfo(const std::string& msg) {
    std::string mode = g_commandMode.empty() ? "main" : g_commandMode;
    std::string output = "[" + getTimestamp() + "] [" + mode + "] " + msg;
    std::cout << output << std::endl;
    Logger::write(output);
}

void logInfo(const char* msg) {
    logInfo(std::string(msg));
}

void logError(const std::string& msg) {
    std::string mode = g_commandMode.empty() ? "main" : g_commandMode;
    std::string output = "[" + getTimestamp() + "] [" + mode + "] " + msg;
    std::cerr << output << std::endl;
    Logger::write(output);
}

void logError(const char* msg) {
    std::string mode = g_commandMode.empty() ? "main" : g_commandMode;
    std::cerr << "[" << getTimestamp() << "] [" << mode << "] " << msg << std::endl;
}

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
        } else if (arg == "-TU" || arg == "-tourl" || arg == "--tourl") {
            commandMode = "tourl";
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
                      << "  -TU, -tourl         Export proxies (delay>0) to share links file\n"
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
    
    g_commandMode = commandMode;
    Logger::init(logDir.string(), g_commandMode.empty() ? "main" : g_commandMode);
    logInfo("validproxy starting...");
    
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
        std::cout << "remarks                                    | id                    | url                                               | 代理数 | 启用\n";
        std::cout << "-----------------------------------------+-----------------------+---------------------------------------------------+--------+----\n";
        
        for (const auto& sub : subs) {
            std::string remarks = sub.remarks;
            std::string id = sub.id;
            std::string url = sub.url;
            std::string enabled = (sub.enabled == "1") ? "是" : "否";
            
            if (remarks.length() > 40) remarks = remarks.substr(0, 37) + "...";
            if (id.length() > 20) id = id.substr(0, 17) + "...";
            if (url.length() > 50) url = url.substr(0, 47) + "...";
            
            std::string countSql = "SELECT COUNT(*) FROM ProfileItem WHERE Subid = '" + sub.id + "'";
            sqlite3_stmt* cntStmt = nullptr;
            int proxyCount = 0;
            if (sqlite3_prepare_v2(db, countSql.c_str(), -1, &cntStmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(cntStmt) == SQLITE_ROW) {
                    proxyCount = sqlite3_column_int(cntStmt, 0);
                }
                sqlite3_finalize(cntStmt);
            }
            
            std::cout << std::left << std::setw(40) << remarks << " | "
                     << std::setw(20) << id << " | "
                     << std::setw(50) << url << " | "
                     << std::right << std::setw(6) << proxyCount << " | "
                     << std::setw(4) << enabled << "\n";
        }
        
        std::cout << "-----------------------------------------+-----------------------+---------------------------------------------------+--------+----\n";
        std::cout << "Total: " << subs.size() << " subscriptions\n";
        
        std::cout << "\n=== ProfileItem Statistics ===" << std::endl;
        std::cout << "ConfigType  | Count | Description\n";
        std::cout << "-----------+-------+-------------\n";
        
        std::vector<std::pair<int, std::string>> typeCounts = {
            {1, "VMess"}, {2, "Custom"}, {3, "Shadowsocks"}, {4, "SOCKS"},
            {5, "VLESS"}, {6, "Trojan"}, {7, "Hysteria2"}, {8, "TUIC"},
            {9, "WireGuard"}, {10, "HTTP"}, {11, "Anytls"}, {12, "Naive"},
            {16, "WireGuard"}, {17, "TUIC"}
        };
        
        int totalCount = 0;
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
            totalCount += count;
            std::cout << std::setw(10) << type << " | " << std::setw(5) << count << " | " << desc << "\n";
        }
        
        std::cout << "-----------+-------+-------------\n";
        std::cout << "Total      | " << std::setw(5) << totalCount << " | All profiles\n";
        
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
        std::filesystem::path configDir = baseDir / "config";
        std::string configDirStr = configDir.string();
        
        char timestamp[32];
        time_t now = time(nullptr);
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));
        std::string logFile = (logDir / ("find_proxy_" + std::string(timestamp) + ".log")).string();
        std::ofstream logOutStream(logFile, std::ios::out | std::ios::trunc);
        std::cout << "Log file: " << logFile << std::endl;
        
        XrayManager* xrayMgr = XrayManager::getInstance(appConfig->xray_executable, configDirStr, appConfig->xray_workers, logOutStream.is_open() ? &logOutStream : nullptr);
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
        std::filesystem::path configDirFs = baseDir / "config";
        std::string configDirStr = configDirFs.string();
        
        char timestamp[32];
        time_t now = time(nullptr);
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));
        std::string logFile = (logDir / ("findmin_proxy_" + std::string(timestamp) + ".log")).string();
        std::ofstream logOutStream(logFile, std::ios::out | std::ios::trunc);
        std::cout << "Log file: " << logFile << std::endl;
        
        XrayManager* xrayMgr = XrayManager::getInstance(appConfig->xray_executable, configDirStr, appConfig->xray_workers, logOutStream.is_open() ? &logOutStream : nullptr);
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
        
        logInfo(result ? "completed" : "failed");
        return result ? 0 : 1;
    }
    
    if (commandMode == "tourl") {
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
        
        db::models::ProfileitemDAO profileDao(db);
        db::models::ProfileexitemDAO exDao(db);
        
        std::string sql = R"(
            SELECT p.*, COALESCE(pe.Delay, 0) as ExDelay 
            FROM ProfileItem p 
            LEFT JOIN ProfileExItem pe ON p.IndexId = pe.IndexId 
            WHERE p.SubId != 'custom' AND CAST(COALESCE(pe.Delay, 0) AS INTEGER) > 0
            ORDER BY CAST(pe.Delay AS INTEGER) ASC
        )";
        
        auto profiles = profileDao.getAll(sql);
        std::cout << "Found " << profiles.size() << " proxies with delay > 0" << std::endl;
        
        std::string output;
        for (const auto& profile : profiles) {
            auto link = share::ShareLink::toShareUri(
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
                profile.remarks
            );
            if (!link.empty()) {
                output += link + "\n";
            }
        }
        
        bool result = false;
        if (!output.empty()) {
            char timestamp[32];
            time_t now = time(nullptr);
            strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));
            
            std::filesystem::path outPath = std::filesystem::path(exeDir) / "proxies" / ("proxies_" + std::string(timestamp) + ".txt");
            std::filesystem::create_directories(outPath.parent_path());
            
            std::ofstream outFile(outPath, std::ios::binary);
            outFile << output;
            outFile.close();
            
            std::cout << "Exported to: " << outPath.string() << std::endl;
            result = true;
        } else {
            std::cout << "No proxies to export" << std::endl;
        }
        
        sqlite3_close(db);
        curl_global_cleanup();
        
        logInfo(result ? "completed" : "failed");
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
        } else if (commandMode == "tourl") {
            sqlite3* db = nullptr;
            curl_global_init(CURL_GLOBAL_DEFAULT);
            
            if (sqlite3_open(appConfig->database_path.c_str(), &db) != SQLITE_OK) {
                std::cerr << "Failed to open database: " << sqlite3_errmsg(db) << std::endl;
                return 1;
            }
            
            db::models::ProfileitemDAO profileDao(db);
            db::models::ProfileexitemDAO exDao(db);
            
            std::string sql = R"(
                SELECT p.*, COALESCE(pe.Delay, 0) as ExDelay 
                FROM ProfileItem p 
                LEFT JOIN ProfileExItem pe ON p.IndexId = pe.IndexId 
                WHERE p.IsSub = 'true' AND CAST(COALESCE(pe.Delay, 0) AS INTEGER) > 0
                ORDER BY CAST(pe.Delay AS INTEGER) ASC
            )";
            
            auto profiles = profileDao.getAll(sql);
            std::cout << "Found " << profiles.size() << " proxies with delay > 0" << std::endl;
            
            std::string output;
            for (const auto& profile : profiles) {
                auto link = share::ShareLink::toShareUri(
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
                    profile.remarks
                );
                if (!link.empty()) {
                    output += link + "\n";
                }
            }
            
            if (!output.empty()) {
                char timestamp[32];
                time_t now = time(nullptr);
                strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));
                
                std::filesystem::path outPath = std::filesystem::path(exeDir) / "proxies" / ("proxies_" + std::string(timestamp) + ".txt");
                std::filesystem::create_directories(outPath.parent_path());
                
                std::ofstream outFile(outPath, std::ios::binary);
                outFile << output;
                outFile.close();
                
                std::cout << "Exported to: " << outPath.string() << std::endl;
                result = true;
            } else {
                std::cout << "No proxies to export" << std::endl;
                result = false;
            }
            
            sqlite3_close(db);
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
        
        logInfo(result ? "completed" : "failed");
        
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