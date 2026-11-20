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

#include "AutoTaskManager.h"
#include "Profileitem.h"
#include "ProfileExItem.h"
#include "ConfigGenerator.h"
#include "ProxyFinder.h"
#include "ConfigReader.h"
#include "config/ActiveConfigPath.h"
#include "i18n/I18n.h"
#include "XrayApi.h"
#include "XrayManager.h"
#include "SubitemUpdaterV2.h"
#include "ShareLink.h"
#include "ProxyBatchTester.h"
#include "Utils.h"
#include "AutoTaskStatePath.h"
#include "Logger.h"
#include "service/CloudSyncService.h"
#include "service/UploadGate.h"
#include "service/DatabaseBootstrap.h"
#include "net/WebDavClient.h"
#include "net/WebDavCredentials.h"
#include "Subitem.h"
#include <boost/json.hpp>
#include "version.h"
#include "service/DatabaseConnectionService.h"
#include "FullConfigTemplateDAO.h"
#include "SqlQuery.h"
#include "config/TemplateDecomposer.h"


namespace {

XrayManager* g_xrayManager = nullptr;
std::atomic<bool> g_cancelRequested{false};
std::string syncSourceDb;
std::string syncTargetDb;
std::string importFilePath;

class CurlGlobalGuard {
public:
    CurlGlobalGuard() {
        curl_global_init(CURL_GLOBAL_ALL);
    }
    ~CurlGlobalGuard() {
        curl_global_cleanup();
    }
};

// show-sub 表格列宽（UTF-8 字节数，与 std::setw 口径一致）。
const int kSubTableRemarks = 40;
const int kSubTableId = 20;
const int kSubTableUrl = 50;
const int kSubTableCount = 9;    // 容纳 “代理数”(9 字节) 与 “Proxies”
const int kSubTableEnabled = 8;  // 容纳 “启用”(6 字节) 与 “Enabled”

void printSubTableHeader() {
    std::cout << std::left << std::setw(kSubTableRemarks) << "remarks" << " | "
              << std::setw(kSubTableId) << "id" << " | "
              << std::setw(kSubTableUrl) << "url" << " | "
              << std::right << std::setw(kSubTableCount)
              << i18n::u8(i18n::StrId::Cli_SubHeaderProxies) << " | "
              << std::setw(kSubTableEnabled)
              << i18n::u8(i18n::StrId::Cli_SubHeaderEnabled) << "\n";
}

void printSubTableRule() {
    std::cout << std::string(kSubTableRemarks, '-') << "-+-"
              << std::string(kSubTableId, '-') << "-+-"
              << std::string(kSubTableUrl, '-') << "-+-"
              << std::string(kSubTableCount, '-') << "-+-"
              << std::string(kSubTableEnabled, '-') << "\n";
}

void logInfo(const std::string& msg, LogLevel level = LogLevel::INFO) {
    Logger::write(msg, level);
}

void logError(const std::string& msg, LogLevel level = LogLevel::ERR) {
    Logger::write(msg, level);
}

// CLI 的启动语言：与 main_gui 共用同一套解析（config.ui.language → 内置 zh-CN）。
// 每个命令在配置加载成功后、输出人可读提示前调用一次（Spec §7.2）。
static std::optional<config::AppConfig> loadConfig(const std::string& configPath) {
    std::optional<config::AppConfig> cfg = config::ConfigReader::load(configPath);
    if (cfg) {
        i18n::setLanguage(i18n::resolveStartupLanguage(cfg->ui.language));
    }
    return cfg;
}

static bool openDatabase(const config::AppConfig& config, sqlite3*& db, const std::string& context) {
    // CLI database bootstrap.
    //
    // This used to open with SQLITE_OPEN_CREATE, which silently produced a
    // database containing only whatever the DAOs happen to create on first use
    // (no SubItem/ProfileItem/ComponentBundle ...). The CLI cannot prompt, so the
    // rule is: create the full schema when the file is absent or empty, and
    // refuse (never destroy) anything else.
    if (config.database_path.empty()) {
        Logger::write(context + " - database.path is empty", LogLevel::ERR);
        std::cerr << context << " - database.path is empty" << std::endl;
        return false;
    }
    {
        const service::DbInspection inspection =
                service::DatabaseBootstrap::inspect(config.database_path);
        const bool absent = inspection.probe == service::DbProbe::MissingPath ||
                            inspection.probe == service::DbProbe::MissingFile ||
                            inspection.probe == service::DbProbe::MissingDirectory ||
                            inspection.probe == service::DbProbe::EmptyFile;
        if (inspection.probe != service::DbProbe::Ok) {
            if (!absent) {
                Logger::write(context + " - database is not usable: " + inspection.detail,
                              LogLevel::ERR);
                Logger::write(context + " - probed path: " +
                                      utils::toWindowsPath(config.database_path),
                              LogLevel::ERR);
                std::cerr << context << " - database is not usable: " << inspection.detail
                          << std::endl;
                std::cerr << context << " - probed path: "
                          << utils::toWindowsPath(config.database_path) << std::endl;
                std::cerr << context
                          << " - start validproxy.exe (GUI) to repair or rebuild it"
                          << std::endl;
                return false;
            }
            std::string createError;
            if (!service::DatabaseBootstrap::create(config.database_path, createError)) {
                Logger::write(context + " - failed to create database: " + createError,
                              LogLevel::ERR);
                std::cerr << context << " - failed to create database: " << createError
                          << std::endl;
                return false;
            }
            Logger::write(context + " - created database schema at " +
                                  utils::toWindowsPath(config.database_path),
                          LogLevel::REPORT);
        }
    }

    // Existence is verified above, so never pass SQLITE_OPEN_CREATE here: that is
    // what allowed an empty database to slip through unnoticed.
    if (sqlite3_open_v2(config.database_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        Logger::write(context + " - Failed to open database: " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);
        Logger::write(context + " - Database path from config: " + utils::toWindowsPath(config.database_path), LogLevel::ERR);
        std::cerr << context << " - Failed to open database: " << sqlite3_errmsg(db) << std::endl;
        std::cerr << context << " - Database path from config: " << utils::toWindowsPath(config.database_path) << std::endl;
        return false;
    }
    // Apply performance pragmas (WAL, cache, sync, etc.) and composite dedup index
    service::DatabaseConnectionService::applyPragmas(db);
    
    // Seed default config templates if table is empty
    {
        db::FullConfigTemplateDAO configTemplateDao(db);
        configTemplateDao.createTableIfNotExists();
        configTemplateDao.seedDefaultTemplates();
    }

    // Seed default SQL query library entries if table is empty
    {
        db::SqlQueryDAO sqlQueryDao(db);
        sqlQueryDao.createTableIfNotExists();
        sqlQueryDao.seedDefaults(config.sql_query, config.sql_by_subid);
    }
    
    return true;
}

void printHelp() {
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
              << "  -TA, -test-all      Test all proxies in the database\n"
              << "  -D, -dedup           Remove duplicate proxies from database\n"
              << "  -DC, -decompose       Decompose templates into ComponentBundle entries\n"

<< "                         from decomposed Bundles and save to template table\n"
              << "  -RB, -rebuild-bundles Rebuild minimal (core x component-type) Bundles\n"
              << "                         and rewrite each template's component chain\n"
              << "  -SM, -sync-materialize Re-materialize each template's Config column from\n"
              << "                         its EXISTING component chain (does NOT rebuild\n"
              << "                         bundles; safe after editing chains by hand)\n"
              << "  -TU, -tourl         Export proxies (delay>0) to share links file\n"
              << "  -S, -sync [src[:dst]] Sync valid proxies from source to target DB\n"
               << "  -IS, -import-sub-config <file|url>  Batch import subitems from file or URL\n"
               << "  -AT, -auto-task      Run configured auto-task pipeline (update→test→dedup→sync)\n"
               << "  -CA, -cancel-task    Cancel a running auto-task\n"
               << "  -RS, -resume-task    Resume a cancelled auto-task from breakpoint\n"
              << "  -h, --help           Ignored (test-all runs by default)\n\n"
               << "If no options are provided, test-all mode is executed silently.\n";
}

BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        std::cout << "\nCtrl+C detected, stopping xray instances..." << std::endl;
        g_cancelRequested.store(true);
        if (g_xrayManager) {
            g_xrayManager->stopAll();
        }

        return TRUE;
    }
    return FALSE;
}

static int runDefaultTest(const std::string& configPath, const std::string& exeDir,
                          const std::filesystem::path& logDir, const std::string& logMode) {
    // Init Logger before config loading, so config load errors are visible
    Logger::init(logDir.string(), logMode);

    std::optional<config::AppConfig> appConfig = loadConfig(configPath);
    if (appConfig) utils::setTimeWaitBlocking(appConfig->port.time_wait_blocking);
    if (!appConfig) {
        logError("Failed to load config from: " + configPath);
        Logger::close();
        return 1;
    }

    // Re-configure Logger with config-specified settings
    Logger::setFileEnabled(appConfig->log_enabled);
    Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
    Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));

    // Log version info
    Logger::write(std::string(APP_NAME) + " v" + APP_VERSION + " (" + APP_GIT_TAG + ")", LogLevel::REPORT);
    Logger::write(std::string("Build: ") + APP_BUILD_TYPE + " | " + APP_BUILD_TIME, LogLevel::REPORT);
    Logger::write(std::string("Compiler: ") + __VERSION__, LogLevel::REPORT);

    logInfo("validproxy starting...");
    logInfo("Config loaded from: " + configPath);
    logInfo("Database path: " + appConfig->database_path);

    int numWorkers = appConfig->xray_workers;
    int startPort = appConfig->xray_start_port;

    logInfo("Workers: " + std::to_string(numWorkers));
    logInfo("Start port: " + std::to_string(startPort));

    std::chrono::system_clock::time_point startTime = std::chrono::system_clock::now();
    time_t startTimeT = std::chrono::system_clock::to_time_t(startTime);
    char startTimeStr[32];
    strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %H:%M:%S", localtime(&startTimeT));

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&startTimeT));

    std::cout << "Log file: " << Logger::getLogDir() << "/" << Logger::getPrefix() << "_" << timestamp << ".log" << std::endl;

    sqlite3* db = nullptr;
    if (!openDatabase(*appConfig, db, "[main] default")) {
        Logger::close();
        return 1;
    }
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    // Additional pragmas for performance and data integrity (matching DatabaseConnectionService::applyPragmas)
    sqlite3_exec(db, "PRAGMA cache_size=-8000", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA temp_store=MEMORY", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA mmap_size=268435456", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA journal_size_limit=67108864", nullptr, nullptr, nullptr);
    std::cout << "Database opened: " << appConfig->database_path << std::endl;

ProxyBatchTester tester(db, *appConfig, exeDir, &g_cancelRequested);
     g_xrayManager = tester.getXrayManager();
    bool testResult = tester.run();

    if (!testResult) {
        logError("No testable proxies found. Check your SQL query and database.");
        logError("  SQL: " + appConfig->sql_query);
        logError("  DB:  " + appConfig->database_path);
    }

    if (appConfig->notification_enabled && appConfig->notification_on_test) {
        utils::sendNotification("Proxy Test Complete", testResult ? "All tests completed successfully" : "Some tests failed");
    }

    if (g_xrayManager) {
        XrayManager::release();
    }

    sqlite3_close(db);
    Logger::close();

    std::cout << "validproxy " << (testResult ? "finished" : "failed") << std::endl;
    return testResult ? 0 : 1;
}

} // anonymous namespace

// Every sub-command branch loads config.json itself (there is no single
// configuration entry point), so the port-semantics rollback switch must be
// published into utils right after each successful load. A CLI process never
// constructs AppController, so unlike the GUI (AppController constructor +
// saveConfig) it would otherwise leave g_timeWaitBlocking stuck at its default
// false, making port.time_wait_blocking un-rollable for CLI runs.

// ---------------------------------------------------------------------------
// WebDAV cloud sync (manual CLI entry points; the GUI has menu items for the
// same operations). Credentials are embedded at build time - see
// scripts/provision_webdav_secret.py.
// ---------------------------------------------------------------------------
static std::string readWebDavRemoteDir(const std::string& configPath) {
    const char* const kDefault = "validproxy";
    std::ifstream in(configPath, std::ios::binary);
    if (!in.is_open()) {
        return kDefault;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    boost::system::error_code ec;
    boost::json::value root = boost::json::parse(ss.str(), ec);
    if (ec || !root.is_object()) {
        return kDefault;
    }
    boost::json::value* webdav = root.as_object().if_contains("webdav");
    if (webdav == nullptr || !webdav->is_object()) {
        return kDefault;
    }
    boost::json::value* dir = webdav->as_object().if_contains("remote_dir");
    if (dir == nullptr || !dir->is_string()) {
        return kDefault;
    }
    const std::string value = dir->as_string().c_str();
    return value.empty() ? std::string(kDefault) : value;
}

static int runCloudSync(const std::string& mode, const std::string& configPath,
                        const std::string& exeDir, const std::string& logDir) {
    Logger::init(logDir, "cloud");

    // Upload is gated exactly like the GUI menu item: the env token must match the
    // value baked in at build time. Refuse before touching the network.
    if (mode == "cloudpush" && !service::isUploadEnabled()) {
        const std::string message =
                "[cloud] upload disabled: VALIDPROXY_UPLOAD_TOKEN is absent or does not "
                "match the compiled-in value";
        logError(message);
        std::cerr << message << std::endl;
        Logger::close();
        return 3;
    }

    net::WebDavCredentials credentials;
    std::string credError;
    if (!net::WebDavCredentialsStore::load(credentials, credError)) {
        logError(credError);
        std::cerr << "[cloud] " << credError << std::endl;
        Logger::close();
        return 2;
    }

    const std::string remoteDir = readWebDavRemoteDir(configPath);
    net::WebDavClient client(credentials, "https://dav.jianguoyun.com/dav/");

    std::optional<config::AppConfig> appConfig = loadConfig(configPath);
    if (!appConfig && mode != "cloudpullconfig") {
        logError("Failed to load config from: " + configPath);
        Logger::close();
        return 1;
    }

    const std::string localConfigPath =
            (std::filesystem::path(exeDir) / "config.json").string();

    // cloudpullconfig does not need a database; the other two do.
    if (mode == "cloudpullconfig") {
        const service::CloudSyncResult result =
                service::CloudSyncService::pullConfigOnly(client, remoteDir, localConfigPath);
        std::cout << (result.ok ? "[cloud] " : "[cloud] FAILED: ") << result.detail
                  << result.error << std::endl;
        Logger::close();
        return result.ok ? 0 : 1;
    }

    sqlite3* db = nullptr;
    if (!openDatabase(*appConfig, db, "[cloud] sync")) {
        Logger::close();
        return 1;
    }
    db::models::SubitemDAO subDao(db);
    service::CloudSyncService service(client, remoteDir, localConfigPath, subDao);

    int exitCode = 0;
    if (mode == "cloudpullsubs") {
        const service::CloudSyncResult result = service.pullSubscriptions();
        std::cout << (result.ok ? "[cloud] " : "[cloud] FAILED: ") << result.detail
                  << result.error << std::endl;
        exitCode = result.ok ? 0 : 1;
    } else {  // cloudpush
        const service::CloudSyncResult cfgResult = service.pushConfig();
        std::cout << "[cloud] config: " << (cfgResult.ok ? cfgResult.detail : cfgResult.error)
                  << std::endl;
        const service::CloudSyncResult subResult = service.pushSubscriptions();
        std::cout << "[cloud] subscriptions: "
                  << (subResult.ok ? subResult.detail : subResult.error) << std::endl;
        exitCode = (cfgResult.ok && subResult.ok) ? 0 : 1;
    }

    sqlite3_close(db);
    Logger::close();
    return exitCode;
}

int main(int argc, char* argv[]) {
    // 人可读文本（中文/英文）统一以 UTF-8 字节输出。cmd.exe 默认代码页是 CP936
    // （简体中文系统），会把 UTF-8 当成 GBK 显示成「浠ｇ悊鏁?」这类乱码；
    // 这里把控制台输入/输出代码页切到 UTF-8。仅影响控制台显示，重定向输出
    // 仍是同一份 UTF-8 字节。
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
    CurlGlobalGuard curlGuard;  // RAII guard for curl global init/cleanup
    
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
        } else if (arg == "-ui" || arg == "--ui") {
            std::cerr << "Error: GUI mode not available in CLI build.\n"
                      << "Use validproxy.exe for GUI mode.\n";
            return 1;
        } else if (arg == "--gui") {
            std::cerr << "Error: GUI mode not available in CLI build.\n"
                      << "Use validproxy.exe for GUI mode.\n";
            return 1;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << APP_NAME << " " << APP_VERSION << "\n"
                      << "Git tag: " << APP_GIT_TAG << "\n"
                      << "Git commit: " << APP_GIT_COMMIT << "\n"
                      << "Build: " << APP_BUILD_TYPE << " " << APP_BUILD_TIME << "\n";
            return 0;
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
} else if (arg == "-DC" || arg == "-decompose" || arg == "--decompose") {
            commandMode = "decompose";
        } else if (arg == "-RB" || arg == "-rebuild-bundles" || arg == "--rebuild-bundles") {
            commandMode = "rebuildbundles";
        } else if (arg == "-SM" || arg == "-sync-materialize" || arg == "--sync-materialize") {
            commandMode = "syncmaterialize";
        } else if (arg == "-TA" || arg == "-test-all" || arg == "--test-all") {
            commandMode = "test-all";
        } else if (arg == "-TU" || arg == "-tourl" || arg == "--tourl") {
            commandMode = "tourl";
        } else if (arg == "-CP" || arg == "-cloud-pull-subs" || arg == "--cloud-pull-subs") {
            commandMode = "cloudpullsubs";
        } else if (arg == "-CU" || arg == "-cloud-push" || arg == "--cloud-push") {
            commandMode = "cloudpush";
        } else if (arg == "-CF" || arg == "-cloud-pull-config" || arg == "--cloud-pull-config") {
            commandMode = "cloudpullconfig";
        } else if (arg == "-S" || arg == "-sync" || arg == "--sync") {
            commandMode = "sync";
            if (i + 1 < argc) {
                std::string syncParam = argv[++i];
                // Parse "source:target" or just "source"
                // Must skip Windows drive letter colon (e.g., C:\) when searching for separator
                size_t colonPos = std::string::npos;
                size_t searchFrom = 0;
                while (true) {
                    size_t pos = syncParam.find(':', searchFrom);
                    if (pos == std::string::npos) break;
                    // Check if this colon is part of a Windows drive letter (e.g., C:\ or D:/)
                    bool isDriveLetter = false;
                    if (pos > 0) {
                        char before = syncParam[pos - 1];
                        bool isLetter = (before >= 'A' && before <= 'Z') || (before >= 'a' && before <= 'z');
                        if (isLetter && pos + 1 < syncParam.length()) {
                            char after = syncParam[pos + 1];
                            if (after == '\\' || after == '/') {
                                isDriveLetter = true;
                            }
                        }
                    }
                    if (!isDriveLetter) {
                        colonPos = pos;
                        break;
                    }
                    searchFrom = pos + 1;
                }
                if (colonPos != std::string::npos) {
                    syncSourceDb = syncParam.substr(0, colonPos);
                    syncTargetDb = syncParam.substr(colonPos + 1);
                } else {
                    syncSourceDb = syncParam;
                    // target will be read from config
                }
            }
            // If no argument provided, source and target will be read from config
        } else if (arg == "-AT" || arg == "-auto-task" || arg == "--auto-task") {
            commandMode = "auto-task";
        } else if (arg == "-CA" || arg == "-cancel-task" || arg == "--cancel-task") {
            commandMode = "cancel-task";
        } else if (arg == "-RS" || arg == "-resume-task" || arg == "--resume-task") {
            commandMode = "resume-task";
        } else if (arg == "-IS" || arg == "-import-sub-config") {
            commandMode = "import-sub";
            if (i + 1 < argc) {
                importFilePath = argv[++i];
            } else {
                std::cerr << "Error: -IS requires a file path or URL" << std::endl;
                return 1;
            }
        } else if (arg.find(".json") != std::string::npos) {
            std::filesystem::path p(arg);
            if (p.is_relative()) {
                p = std::filesystem::path(exeDir) / p;
            }
            configPath = p.lexically_normal().string();
        } else {
            std::cerr << "Error: Unknown option '" << arg << "'" << std::endl << std::endl;
            printHelp();
            return 1;
        }
    }
    
    // publish process-level effective config path (-c/--config)
    config::setActiveConfigPath(configPath);

    if (commandMode == "test-all") {
        return runDefaultTest(configPath, exeDir, logDir, "test-all");
    }

    // ---- WebDAV cloud sync (manual triggers) ----
    if (commandMode == "cloudpullconfig" || commandMode == "cloudpullsubs" ||
        commandMode == "cloudpush") {
        return runCloudSync(commandMode, configPath, exeDir, logDir.string());
    }
    
    
    if (commandMode == "generator") {
        Logger::init(logDir.string(), commandMode);
        std::optional<config::AppConfig> appConfig = loadConfig(configPath);
        if (appConfig) utils::setTimeWaitBlocking(appConfig->port.time_wait_blocking);
        if (!appConfig) {
            logError("Failed to load config from: " + configPath);
            Logger::close();
            return 1;
        }

        Logger::setFileEnabled(appConfig->log_enabled);
        Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
        logInfo("validproxy starting...");
        
        sqlite3* db = nullptr;
        if (!openDatabase(*appConfig, db, "[main] generator")) {
            Logger::close();
            return 1;
        }
        sqlite3_busy_timeout(db, 5000);
        sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
        // Additional pragmas for performance and data integrity (matching DatabaseConnectionService::applyPragmas)
        sqlite3_exec(db, "PRAGMA cache_size=-8000", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA temp_store=MEMORY", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA mmap_size=268435456", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA journal_size_limit=67108864", nullptr, nullptr, nullptr);

        db::models::ProfileitemDAO profileDao(db);
        std::optional<db::models::Profileitem> profileOpt = profileDao.getByIndexId(generatorIndexId);

        if (!profileOpt) {
            logError("Profile not found: " + generatorIndexId);
            sqlite3_close(db);
            Logger::close();
            return 1;
        }

        config::ConfigGenerator configGen(db);
        config::XrayConfig config = configGen.generateConfig(*profileOpt);

        std::cout << "\n=== Generated Outbound JSON ===" << std::endl;
        std::cout << config.outbound_json << std::endl;

        std::string outFile = (configDir / (generatorIndexId + ".json")).string();

        std::ofstream out(outFile);
        if (out.is_open()) {
            out << config.outbound_json;
            out.close();
            std::cout << "\nSaved to: " << outFile << std::endl;
        } else {
            logError("Failed to write file: " + outFile);
        }

        sqlite3_close(db);
        Logger::close();
        return 0;
    }
    
    if (commandMode == "show-sub") {
        Logger::init(logDir.string(), commandMode);
        
        std::optional<config::AppConfig> appConfig = loadConfig(configPath);
        if (appConfig) utils::setTimeWaitBlocking(appConfig->port.time_wait_blocking);
        if (!appConfig) {
            logError("Failed to load config from: " + configPath);
            Logger::close();
            return 1;
        }
        
        Logger::setFileEnabled(appConfig->log_enabled);
        Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
        logInfo("validproxy starting...");
        
        sqlite3* db = nullptr;
        if (!openDatabase(*appConfig, db, "[main] show-sub")) {
            Logger::close();
            return 1;
        }
        sqlite3_busy_timeout(db, 5000);
        sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
        // Additional pragmas for performance and data integrity (matching DatabaseConnectionService::applyPragmas)
        sqlite3_exec(db, "PRAGMA cache_size=-8000", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA temp_store=MEMORY", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA mmap_size=268435456", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA journal_size_limit=67108864", nullptr, nullptr, nullptr);
        
        db::models::SubitemDAO subDao(db);
        std::vector<db::models::Subitem> subs = subDao.getAll();
        
        std::cout << "\n=== Subscriptions ===" << std::endl;
        printSubTableHeader();
        
        for (const db::models::Subitem& sub : subs) {
            std::string remarks = sub.remarks;
            std::string id = sub.id;
            std::string url = sub.url;
            const char* enabled = (sub.enabled == "1")
                                      ? i18n::u8(i18n::StrId::Cli_Yes)
                                      : i18n::u8(i18n::StrId::Cli_No);
            
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
            
            std::cout << std::left << std::setw(kSubTableRemarks) << remarks << " | "
                     << std::setw(kSubTableId) << id << " | "
                     << std::setw(kSubTableUrl) << url << " | "
                     << std::right << std::setw(kSubTableCount) << proxyCount << " | "
                     << std::setw(kSubTableEnabled) << enabled << "\n";
        }
        
        printSubTableRule();
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
        for (const std::pair<int, std::string>& typeCountEntry : typeCounts) {
            int type = typeCountEntry.first;
            const std::string& desc = typeCountEntry.second;
            std::string sql = "SELECT COUNT(*) FROM ProfileItem WHERE ConfigType = " + std::to_string(type);
            sqlite3_stmt* stmt2 = nullptr;
            int count =0;
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
        Logger::close();
        
        return 0;
    }
    
    if (commandMode == "find-proxy" || commandMode == "findminproxy") {
        Logger::init(logDir.string(), commandMode);
        
        std::optional<config::AppConfig> appConfig = loadConfig(configPath);
        if (appConfig) utils::setTimeWaitBlocking(appConfig->port.time_wait_blocking);
        if (!appConfig) {
            logError("Failed to load config from: " + configPath);
            Logger::close();
            return 1;
        }
        
        Logger::setFileEnabled(appConfig->log_enabled);
        Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
        logInfo("validproxy starting...");
        
        sqlite3* db = nullptr;
        if (!openDatabase(*appConfig, db, "[main] find-proxy")) {
            Logger::close();
            return 1;
        }
        sqlite3_busy_timeout(db, 5000);
        sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
        // Additional pragmas for performance and data integrity (matching DatabaseConnectionService::applyPragmas)
        sqlite3_exec(db, "PRAGMA cache_size=-8000", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA temp_store=MEMORY", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA mmap_size=268435456", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA journal_size_limit=67108864", nullptr, nullptr, nullptr);
        
        std::string exeBaseDir = exeDir;
        std::filesystem::path configDirFs = std::filesystem::path(exeBaseDir) / "config";
        std::string configDirStr = configDirFs.string();
        
        XrayManager* xrayMgr = XrayManager::getInstance(appConfig->proxy.xray_executable, configDirStr, appConfig->xray_workers, db);
        int started = xrayMgr->start(1, appConfig->xray_start_port, appConfig->xray_api_port);
        
        if (started == 0) {
            logError("Failed to start xray instance");
            sqlite3_close(db);
            Logger::close();
            return 1;
        }
        
        ProxyFinder finder(db, xrayMgr, appConfig->proxy.xray_executable, appConfig->test_url, "", appConfig->test_timeout_ms);
        
        std::pair<int, int> ports;
        if (commandMode == "find-proxy") {
            ports = finder.findFirstWorkingProxy();
        } else {
            ports = finder.findWorkingProxy();
        }
        
        if (ports.first > 0) {
            TestResult res = finder.getLastResult();
            std::cout << "\n=== Working Proxy ===" << std::endl;
            std::cout << "IndexId: " << res.indexId << std::endl;
            std::cout << "Address: " << res.address << ":" << res.port << std::endl;
            std::cout << "Delay: " << res.latencyMs << "ms" << std::endl;
            std::cout << "SocksPort: " << ports.first << std::endl;
            std::cout << "ApiPort: " << ports.second << std::endl;
        } else {
            logError("No working proxy found");
        }
        
        finder.release();
        XrayManager::release();
        
        sqlite3_close(db);
        Logger::close();
        
        return ports.first > 0 ? 0 : 1;
    }
    
    if (commandMode == "tourl") {
        Logger::init(logDir.string(), commandMode);
        
        std::optional<config::AppConfig> appConfig = loadConfig(configPath);
        if (appConfig) utils::setTimeWaitBlocking(appConfig->port.time_wait_blocking);
        if (!appConfig) {
            logError("Failed to load config from: " + configPath);
            Logger::close();
            return 1;
        }
        
        Logger::setFileEnabled(appConfig->log_enabled);
        Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
        logInfo("validproxy starting...");
        
        sqlite3* db = nullptr;
        if (!openDatabase(*appConfig, db, "[main] tourl")) {
            Logger::close();
            return 1;
        }
        sqlite3_busy_timeout(db, 5000);
        sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
        // Additional pragmas for performance and data integrity (matching DatabaseConnectionService::applyPragmas)
        sqlite3_exec(db, "PRAGMA cache_size=-8000", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA temp_store=MEMORY", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA mmap_size=268435456", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA journal_size_limit=67108864", nullptr, nullptr, nullptr);
        
        db::models::ProfileitemDAO profileDao(db);
        db::models::ProfileExItemDAO exDao(db);
        
        std::string sql = R"(
            SELECT p.*, COALESCE(pe.Delay, 0) as ExDelay
            FROM ProfileItem p
            LEFT JOIN ProfileExItem pe ON p.IndexId = pe.IndexId
            WHERE CAST(COALESCE(pe.Delay, 0) AS INTEGER) > 0
            ORDER BY CAST(pe.Delay AS INTEGER) ASC
        )";
        
        std::vector<db::models::Profileitem> profiles = profileDao.getAll(sql);
        std::cout << "Found " << profiles.size() << " proxies with delay > 0" << std::endl;
        
        std::string output;
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
        Logger::write(result ? "completed" : "failed", LogLevel::REPORT);
        Logger::close();
        
        return result ? 0 : 1;
    }
    
    if (commandMode == "sync") {
        Logger::init(logDir.string(), commandMode);
        
        std::optional<config::AppConfig> appConfig = loadConfig(configPath);
        if (appConfig) utils::setTimeWaitBlocking(appConfig->port.time_wait_blocking);
        if (!appConfig) {
            logError("Failed to load config from: " + configPath);
            Logger::close();
            return 1;
        }
        
        Logger::setFileEnabled(appConfig->log_enabled);
        Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
        logInfo("validproxy starting sync...");
        
        // Determine source and target databases
        std::string sourceDb = !syncSourceDb.empty() ? syncSourceDb : appConfig->sync.source_db;
        std::string targetDb = !syncTargetDb.empty() ? syncTargetDb : appConfig->sync.target_db;
        
        if (sourceDb.empty() || targetDb.empty()) {
            logError("Source or target database not specified");
            logInfo("Usage: validproxy -S source.db:target.db");
            logInfo("   or: validproxy -S source.db (target from config)");
            Logger::close();
            return 1;
        }
        
        update::SubitemUpdaterV2 updater(nullptr, "", *appConfig, nullptr, exeDir);
        bool result = updater.syncDatabases(sourceDb, targetDb);
        
        Logger::write(result ? "sync completed" : "sync failed", LogLevel::REPORT);
        Logger::close();
        return result ? 0 : 1;
    }
    
    if (commandMode == "import-sub") {
        Logger::init(logDir.string(), commandMode);
        
        // 判断是 URL 还是文件
        bool isUrl = (importFilePath.rfind("http://", 0) == 0 || 
                       importFilePath.rfind("https://", 0) == 0);
        
        sqlite3* db = nullptr;
        
        std::optional<config::AppConfig> appConfig = loadConfig(configPath);
        if (appConfig) utils::setTimeWaitBlocking(appConfig->port.time_wait_blocking);
        if (!appConfig) {
            logError("Failed to load config from: " + configPath);
            Logger::close();
            return 1;
        }
        
        Logger::setFileEnabled(appConfig->log_enabled);
        Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
        logInfo("validproxy starting import...");
        
        if (!openDatabase(*appConfig, db, "[main] import-sub-config")) {
            Logger::close();
            return 1;
        }
        sqlite3_busy_timeout(db, 5000);
        sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
        // Additional pragmas for performance and data integrity (matching DatabaseConnectionService::applyPragmas)
        sqlite3_exec(db, "PRAGMA cache_size=-8000", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA temp_store=MEMORY", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA mmap_size=268435456", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA journal_size_limit=67108864", nullptr, nullptr, nullptr);
        
        update::SubitemUpdaterV2 updater(db, appConfig->proxy.xray_executable, *appConfig, 
                                                          nullptr, exeDir);
        
        bool result = false;
        if (isUrl) {
            result = updater.importSingleUrl(importFilePath);
        } else {
            // 作为文件处理
            std::filesystem::path filePath(importFilePath);
            if (!std::filesystem::exists(filePath)) {
                logError("File not found: " + importFilePath);
                sqlite3_close(db);
                Logger::close();
                return 1;
            }
            result = updater.importSubitemsFromFile(importFilePath);
        }
        
        Logger::write(result ? "import completed" : "import failed", LogLevel::REPORT);
        sqlite3_close(db);
        Logger::close();
        return result ? 0 : 1;
    }
    
    if (commandMode == "auto-task") {
        Logger::init(logDir.string(), commandMode);

        std::optional<config::AppConfig> appConfig = loadConfig(configPath);
        if (appConfig) utils::setTimeWaitBlocking(appConfig->port.time_wait_blocking);
        if (!appConfig) {
            logError("Failed to load config from: " + configPath);
            Logger::close();
            return 1;
        }

        Logger::setFileEnabled(appConfig->log_enabled);
        Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
        logInfo("validproxy starting auto-task...");

        sqlite3* db = nullptr;
        if (!openDatabase(*appConfig, db, "[main] auto-task")) {
            Logger::close();
            return 1;
        }
        sqlite3_busy_timeout(db, 5000);
        sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
        // Additional pragmas for performance and data integrity (matching DatabaseConnectionService::applyPragmas)
        sqlite3_exec(db, "PRAGMA cache_size=-8000", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA temp_store=MEMORY", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA mmap_size=268435456", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA journal_size_limit=67108864", nullptr, nullptr, nullptr);

        std::vector<std::string> steps;
        if (!appConfig->auto_task.steps.empty()) {
            steps = appConfig->auto_task.steps;
            logInfo("AutoTask: using configured steps: " + std::to_string(steps.size()));
        } else {
            steps = {"update", "test", "dedup", "sync"};
            logInfo("AutoTask: using default steps [update, test, dedup, sync]");
        }

        AutoTaskManager manager(db, *appConfig, exeDir, &g_cancelRequested);
        manager.setProgressCallback([](const AutoTaskProgress& p) {
            std::cout << "\rAutoTask: [" << std::to_string(p.current_step + 1)
                      << "/" << std::to_string(p.total_steps) << "] "
                      << p.step_name << " (" << std::to_string(p.percent) << "%)    " << std::flush;
        });

        SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
        g_cancelRequested.store(false);

        bool result = manager.run(steps);

        if (manager.getState().cancelled) {
            std::cout << "\nAutoTask cancelled at step "
                      << manager.getState().current_step_index + 1 << std::endl;
            std::cout << "State saved to: " << manager.getStateFilePath() << std::endl;
            std::cout << "Use -RS to resume or -CA to clear." << std::endl;
        }

        sqlite3_close(db);
        Logger::write(result ? "auto-task completed" : "auto-task failed", LogLevel::REPORT);
        Logger::close();
        return result ? 0 : 1;
    }

    if (commandMode == "cancel-task") {
        Logger::init(logDir.string(), commandMode);
        std::optional<config::AppConfig> appConfig = loadConfig(configPath);
        if (appConfig) utils::setTimeWaitBlocking(appConfig->port.time_wait_blocking);
        if (!appConfig) {
            logError("Failed to load config from: " + configPath);
            Logger::close();
            return 1;
        }
        Logger::setFileEnabled(appConfig->log_enabled);
        Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));

        std::string stateFile = autotask::resolveStateFilePath(
            appConfig->auto_task.state_file, utils::getExecutableDir());
        autotask::ensureParentDirectory(stateFile);

        AutoTaskState saved = AutoTaskManager::loadStateFile(stateFile);
        if (saved.task_id.empty()) {
            std::cout << "No auto-task state file found at: " << stateFile << std::endl;
            Logger::close();
            return 0;
        }

        if (saved.completed) {
            std::cout << "Auto-task already completed; nothing to cancel." << std::endl;
            Logger::close();
            return 0;
        }

        if (saved.current_step_index < 0 || saved.current_step_index >= static_cast<int>(saved.steps.size())) {
            std::cout << "Auto-task state has invalid step index; cannot cancel cleanly. Remove state file manually." << std::endl;
            Logger::close();
            return 0;
        }

        saved.cancelled = true;
        saved.completed = false;
        for (AutoTaskStepInfo& step : saved.steps) {
            if (step.status == StepStatus::RUNNING) {
                step.status = StepStatus::CANCELLED;
            }
        }
        if (!AutoTaskManager::saveStateFile(stateFile, saved)) {
            std::cout << "Warning: failed to write auto-task state file: " << stateFile << std::endl;
        }
        std::cout << "Auto-task cancelled (state marked as cancelled)." << std::endl;
        Logger::close();
        return 0;
    }

    if (commandMode == "resume-task") {
        Logger::init(logDir.string(), commandMode);

        std::optional<config::AppConfig> appConfig = loadConfig(configPath);
        if (appConfig) utils::setTimeWaitBlocking(appConfig->port.time_wait_blocking);
        if (!appConfig) {
            logError("Failed to load config from: " + configPath);
            Logger::close();
            return 1;
        }

        Logger::setFileEnabled(appConfig->log_enabled);
        Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
        logInfo("validproxy resuming auto-task...");

        sqlite3* db = nullptr;
        if (!openDatabase(*appConfig, db, "[main] resume-task")) {
            Logger::close();
            return 1;
        }
        sqlite3_busy_timeout(db, 5000);
        sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
        // Additional pragmas for performance and data integrity (matching DatabaseConnectionService::applyPragmas)
        sqlite3_exec(db, "PRAGMA cache_size=-8000", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA temp_store=MEMORY", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA mmap_size=268435456", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA journal_size_limit=67108864", nullptr, nullptr, nullptr);

        AutoTaskManager manager(db, *appConfig, exeDir, &g_cancelRequested);
        manager.setProgressCallback([](const AutoTaskProgress& p) {
            std::cout << "\rAutoTask: [" << std::to_string(p.current_step + 1)
                      << "/" << std::to_string(p.total_steps) << "] "
                      << p.step_name << " (" << std::to_string(p.percent) << "%)    " << std::flush;
        });

        SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
        g_cancelRequested.store(false);

        bool result = manager.resume();

        sqlite3_close(db);
        Logger::write(result ? "auto-task resumed and completed" : "auto-task resume failed", LogLevel::REPORT);
        Logger::close();
        return result ? 0 : 1;
    }

    if (commandMode == "dedup") {
        Logger::init(logDir.string(), commandMode);
        
        std::optional<config::AppConfig> appConfig = loadConfig(configPath);
        if (appConfig) utils::setTimeWaitBlocking(appConfig->port.time_wait_blocking);
        if (!appConfig) {
            logError("Failed to load config from: " + configPath);
            Logger::close();
            return 1;
        }
        
        Logger::setFileEnabled(appConfig->log_enabled);
        Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
        logInfo("validproxy starting...");
        
        sqlite3* db = nullptr;
        if (!openDatabase(*appConfig, db, "[main] dedup")) {
            Logger::close();
            return 1;
        }
        
        update::SubitemUpdaterV2 subUpdaterV2(db,
                                              appConfig->proxy.xray_executable,
                                              *appConfig,
                                              nullptr,
                                              exeDir);
        bool result = subUpdaterV2.deduplicate();
        
        sqlite3_close(db);
        Logger::write(result ? "completed" : "failed", LogLevel::REPORT);
        Logger::close();
        
        return result ? 0 : 1;
    }
    
    if (commandMode == "decompose") {
        Logger::init(logDir.string(), commandMode);
        
        std::optional<config::AppConfig> appConfig = loadConfig(configPath);
        if (appConfig) utils::setTimeWaitBlocking(appConfig->port.time_wait_blocking);
        if (!appConfig) {
            logError("Failed to load config from: " + configPath);
            Logger::close();
            return 1;
        }
        
        Logger::setFileEnabled(appConfig->log_enabled);
        Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
        logInfo("validproxy decompose starting...");
        
        sqlite3* db = nullptr;
        if (!openDatabase(*appConfig, db, "[main] decompose")) {
            Logger::close();
            return 1;
        }
        sqlite3_busy_timeout(db, 5000);
        sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA cache_size=-8000", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA temp_store=MEMORY", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA mmap_size=268435456", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA journal_size_limit=67108864", nullptr, nullptr, nullptr);
        
        config::TemplateDecomposer decomposer(db);
        int saved = decomposer.decomposeAndSave(true);
        
        std::cout << "Decomposition complete: " << saved << " bundles saved" << std::endl;
        
        sqlite3_close(db);
        Logger::write("decompose completed (" + std::to_string(saved) + " bundles)", LogLevel::REPORT);
        Logger::close();
        
        return 0;
    }
    
    // -RB: 按「代理类型 × 组件类型」重建最小粒度组件包，并回写各模板组件链
    if (commandMode == "rebuildbundles") {
        Logger::init(logDir.string(), commandMode);

        std::optional<config::AppConfig> appConfig = loadConfig(configPath);
        if (appConfig) utils::setTimeWaitBlocking(appConfig->port.time_wait_blocking);
        if (!appConfig) {
            logError("Failed to load config from: " + configPath);
            Logger::close();
            return 1;
        }

        Logger::setFileEnabled(appConfig->log_enabled);
        Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
        logInfo("validproxy rebuild-bundles starting...");

        sqlite3* db = nullptr;
        if (!openDatabase(*appConfig, db, "[main] rebuild-bundles")) {
            Logger::close();
            return 1;
        }
        sqlite3_busy_timeout(db, 5000);
        sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);

        config::TemplateDecomposer decomposer(db);
        const int saved = decomposer.rebuildBundlesAndChains();

        std::cout << "Rebuild complete: " << saved << " minimal bundles" << std::endl;

        sqlite3_close(db);
        Logger::write("rebuild-bundles completed (" + std::to_string(saved) + " bundles)",
                      LogLevel::REPORT);
        Logger::close();

        return 0;
    }

    // -SM, -sync-materialize: 仅按**现有组件链**重新物化各模板的 Config 列。
    // 与 -RB 的区别：-RB 会按「core × component-type」重建最小包并改写链
    // （曾把链里引用的 xray_DNS 拆成 xray_DNS_base/_pool/_standalone，导致物化丢 dns/log 节）；
    // 本命令不碰组件包表，只跑 TemplateDecomposer::syncConfigsFromChains()。
    if (commandMode == "syncmaterialize") {
        Logger::init(logDir.string(), commandMode);

        std::optional<config::AppConfig> appConfig = loadConfig(configPath);
        if (!appConfig) utils::setTimeWaitBlocking(appConfig->port.time_wait_blocking);
        if (!appConfig) {
            logError("Failed to load config from: " + configPath);
            Logger::close();
            return 1;
        }

        Logger::setFileEnabled(appConfig->log_enabled);
        Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
        logInfo("validproxy sync-materialize starting...");

        sqlite3* db = nullptr;
        if (!openDatabase(*appConfig, db, "[main] sync-materialize")) {
            Logger::close();
            return 1;
        }
        sqlite3_busy_timeout(db, 5000);

        config::TemplateDecomposer decomposer(db);
        const int synced = decomposer.syncConfigsFromChains();

        std::cout << "Materialize complete: " << synced << " templates" << std::endl;

        sqlite3_close(db);
        Logger::write("sync-materialize completed (" + std::to_string(synced) + " templates)",
                      LogLevel::REPORT);
        Logger::close();

        return 0;
    }

    // 组件链只能经由「配置链 → 解析 → 保存」产生（配置编辑器「组件链编辑」页），
    // 不再提供 CLI 自动生成：模板 = 组件链配置 + 组件包间整合关系，没有默认值。
    
    if (!singleSubId.empty()) {
        Logger::init(logDir.string(), commandMode.empty() ? "test" : commandMode);
        
        std::optional<config::AppConfig> appConfig = loadConfig(configPath);
        if (appConfig) utils::setTimeWaitBlocking(appConfig->port.time_wait_blocking);
        if (!appConfig) {
            logError("Failed to load config from: " + configPath);
            Logger::close();
            return 1;
        }
        
        Logger::setFileEnabled(appConfig->log_enabled);
        Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
        logInfo("validproxy starting...");
        
        sqlite3* db = nullptr;
        if (!openDatabase(*appConfig, db, "[main] " + commandMode)) {
            Logger::close();
            return 1;
        }
        
        logInfo("Mode: " + commandMode + ", subscription: " + singleSubId);
        
        bool result = false;
        
if (commandMode == "test-sub") {
             ProxyBatchTester tester(db, *appConfig, exeDir, &g_cancelRequested);
             g_xrayManager = tester.getXrayManager();
            result = tester.runWithSubId(singleSubId);
            
            if (appConfig->notification_enabled && appConfig->notification_on_test) {
                utils::sendNotification("Proxy Test Complete", result ? "Test completed successfully" : "Test failed");
            }
        } 
        else if (commandMode == "update") {
             update::SubitemUpdaterV2 subUpdaterV2(db,
                                                    appConfig->proxy.xray_executable,
                                                    *appConfig,
                                                    nullptr,
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
        
        sqlite3_close(db);
        Logger::write(result ? "completed" : "failed", LogLevel::REPORT);
        Logger::close();
        
        return result ? 0 : 1;
    }
    
    // No arguments: silent default to test-all mode
    if (commandMode.empty()) {
        commandMode = "test-all";
    }
    
    return runDefaultTest(configPath, exeDir, logDir, "test");
}
