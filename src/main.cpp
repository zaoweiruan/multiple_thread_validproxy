#include <iostream>
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
#include "ConfigReader.h"
#include "XrayApi.h"
#include "SubitemUpdater.h"

namespace {

std::vector<db::models::Profileitem> g_profiles;
std::queue<int> g_profilesQueue;
std::atomic<int> g_processedCount{0};
int g_totalProxies = 0;
std::mutex g_queueMutex;
std::ofstream* g_logOut = nullptr;
bool g_logEnabled = false;
HANDLE g_xrayJob = nullptr;

bool isPortInUse(int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    closesocket(sock);
    
    return result == SOCKET_ERROR;
}

int findAvailablePort(int startPort, int maxAttempts = 100, const std::vector<int>& usedPorts = std::vector<int>()) {
    for (int i = 0; i < maxAttempts; ++i) {
        int port = startPort + i;
        if (port > 65535) port = 10000 + (port - 10000) % 50000;
        
        bool isUsed = false;
        for (int used : usedPorts) {
            if (used == port) {
                isUsed = true;
                break;
            }
        }
        
        if (!isUsed && !isPortInUse(port)) {
            return port;
        }
    }
    return -1;
}

void logToFile(const std::string& msg) {
    if (g_logOut && g_logOut->is_open() && g_logEnabled) {
        *g_logOut << msg << std::endl;
    }
}

void printConfigTypeHelp() {
    std::cout << "ConfigType values:" << std::endl;
    std::cout << "  1 = VMess" << std::endl;
    std::cout << "  3 = Shadowsocks" << std::endl;
    std::cout << "  5 = VLESS" << std::endl;
    std::cout << "  6 = Trojan" << std::endl;
}

bool initXrayJob() {
    g_xrayJob = CreateJobObjectA(NULL, NULL);
    if (!g_xrayJob) {
        std::cerr << "Failed to create job object: " << GetLastError() << std::endl;
        return false;
    }
    
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobLimit = {};
    jobLimit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(g_xrayJob, JobObjectExtendedLimitInformation, &jobLimit, sizeof(jobLimit))) {
        DWORD err = GetLastError();
        std::cerr << "Failed to set job limit: " << err << std::endl;
        CloseHandle(g_xrayJob);
        g_xrayJob = nullptr;
        return false;
    }
    
    std::cout << "Job object initialized" << std::endl;
    return true;
}

bool startXray(const std::string& xrayPath, const std::string& configPath) {
    std::string normalizedPath = configPath;
    for (char& c : normalizedPath) {
        if (c == '/') c = '\\';
    }
    
    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};
    
    std::string cmd = "\"" + xrayPath + "\" run -c \"" + normalizedPath + "\"";
    
    DWORD createFlags = CREATE_SUSPENDED | CREATE_NO_WINDOW;
    
    if (!CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, createFlags, NULL, NULL, &si, &pi)) {
        std::cerr << "Failed to start xray: " << GetLastError() << std::endl;
        return false;
    }
    
    if (g_xrayJob) {
        if (!AssignProcessToJobObject(g_xrayJob, pi.hProcess)) {
            std::cerr << "Failed to assign process to job: " << GetLastError() << std::endl;
        }
    }
    
    ResumeThread(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return true;
}

void stopXray() {
    std::cout << "Closing job object (xray processes will be terminated)..." << std::endl;
    if (g_xrayJob) {
        TerminateJobObject(g_xrayJob, 0);
        CloseHandle(g_xrayJob);
        g_xrayJob = nullptr;
    }
}

bool testProxy(int socksPort, const std::string& testUrl, int timeoutMs, long& latencyMs, std::string& errorMsg) {
    latencyMs = -1;
    errorMsg = "";
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        errorMsg = "curl_init_failed";
        return false;
    }
    
    std::string proxyUrl = "http://127.0.0.1:" + std::to_string(socksPort);
    curl_easy_setopt(curl, CURLOPT_PROXY, proxyUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, testUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    
    double totalTime = 0;
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &totalTime);
    latencyMs = static_cast<long>(totalTime * 1000);
    
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        errorMsg = curl_easy_strerror(res);
    } else if (responseCode != 200 && responseCode != 204) {
        errorMsg = "http_" + std::to_string(responseCode);
    }
    
    return res == CURLE_OK && (responseCode == 200 || responseCode == 204);
}

struct WorkerContext {
    int workerId;
    int socksPort;
    int apiPort;
    std::string xrayPath;
    std::string configPath;
    std::string testUrl;
    int timeoutMs;
    sqlite3* db;
    std::atomic<int>* successCount;
    std::mutex* printMutex;
    std::mutex* dbMutex;
};

void workerThreadFunc(const WorkerContext& ctx) {
    std::string xrayApiAddr = "127.0.0.1:" + std::to_string(ctx.apiPort);
    xray::XrayApi xrayApi(ctx.xrayPath, xrayApiAddr);
    db::models::ProfileexitemDAO exItemDao(ctx.db);
    
    while (true) {
        int profileIdx = -1;
        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            if (g_profilesQueue.empty()) break;
            profileIdx = g_profilesQueue.front();
            g_profilesQueue.pop();
        }
        
        if (profileIdx < 0 || profileIdx >= static_cast<int>(g_profiles.size())) continue;
        
        const auto& profile = g_profiles[profileIdx];
        int currentIdx = ++g_processedCount;
        
        {
            std::lock_guard<std::mutex> lock(*ctx.printMutex);
            std::cout << "[Worker-" << ctx.workerId << "] [" << currentIdx << "/" << g_totalProxies << "] Testing: " 
                      << profile.address << ":" << profile.port << " (" << profile.remarks << ")" << std::endl;
            logToFile("[Worker-" + std::to_string(ctx.workerId) + "] Testing " + profile.address + ":" + profile.port + " (" + profile.remarks + ")");
        }
        
        config::ConfigGenerator configGen(ctx.db);
        db::models::Profileitem configProfile = profile;
        
        try {
            configProfile.checkRequired();
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(*ctx.printMutex);
            std::cerr << "[Worker-" << ctx.workerId << "] Config error: " << e.what() << std::endl;
            std::string errorDetail = profile.address + ":" + profile.port + " (" + profile.configtype + ") - " + e.what();
            logToFile("CONFIG_ERROR: " + profile.indexid + " - " + errorDetail);
            {
                std::lock_guard<std::mutex> lock2(*ctx.dbMutex);
                exItemDao.updateTestResult(profile.indexid, -1, false, "CONFIG_ERROR");
            }
            continue;
        }
        
        try {
            auto config = configGen.generateConfig(profile);
            std::string tag = "proxy";
            
            xrayApi.removeOutbound(tag);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            xrayApi.removeOutbound(tag);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            
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
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
            }
            
            if (!addSuccess) {
                std::lock_guard<std::mutex> lock(*ctx.printMutex);
                std::cerr << "[Worker-" << ctx.workerId << "] Xray API error: " << xrayApi.getLastError() << std::endl;
                logToFile("[Worker-" + std::to_string(ctx.workerId) + "] XRAY_ERROR - " + profile.indexid + " - " + xrayApi.getLastError());
                logToFile("  Xray output: " + addResult);
                logToFile("  Outbound JSON: " + config.outbound_json);
                {
                    std::lock_guard<std::mutex> lock2(*ctx.dbMutex);
                    exItemDao.updateTestResult(profile.indexid, -1, false, "XRAY_ERROR");
                }
                continue;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            
            long latencyMs = -1;
            std::string errorMsg;
            bool connected = testProxy(ctx.socksPort, ctx.testUrl, ctx.timeoutMs, latencyMs, errorMsg);
            
            if (connected) {
                (*ctx.successCount)++;
                std::lock_guard<std::mutex> lock(*ctx.printMutex);
                std::cout << "[Worker-" << ctx.workerId << "] Test SUCCESS - URL: " << ctx.testUrl << " | Latency: " << latencyMs << "ms" << std::endl;
                logToFile("[Worker-" + std::to_string(ctx.workerId) + "] Test SUCCESS - " + profile.indexid + " | Latency: " + std::to_string(latencyMs) + "ms");
            } else {
                std::lock_guard<std::mutex> lock(*ctx.printMutex);
                std::cout << "[Worker-" << ctx.workerId << "] Test FAILED - URL: " << ctx.testUrl << " | Error: " << errorMsg << std::endl;
                logToFile("[Worker-" + std::to_string(ctx.workerId) + "] Test FAILED - " + profile.indexid + " | Error: " + errorMsg);
            }
            
            {
                std::lock_guard<std::mutex> lock(*ctx.dbMutex);
                exItemDao.updateTestResult(profile.indexid, latencyMs, connected, "TEST_FAILED");
            }
            
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(*ctx.printMutex);
            std::cerr << "[Worker-" << ctx.workerId << "] Exception: " << e.what() << std::endl;
            {
                std::lock_guard<std::mutex> lock(*ctx.dbMutex);
                exItemDao.updateTestResult(profile.indexid, -1, false, e.what());
            }
        }
    }
}

} // namespace

#include <windows.h>

std::string getExecutableDir() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path().string();
}

int main(int argc, char* argv[]) {
    std::cout << "validproxy starting..." << std::endl;

    std::string exeDir = getExecutableDir();
    
    std::string configPath = config::ConfigReader::getDefaultConfigPath();
    std::string singleSubId;
    std::string commandMode;
    
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
        } else if (arg == "--update" || arg == "--update-sub") {
            if (i + 1 < argc) {
                singleSubId = argv[++i];
                commandMode = "update";
            }
        } else if (arg == "--test") {
            if (i + 1 < argc) {
                singleSubId = argv[++i];
                commandMode = "test";
            }
        } else if (arg == "--update-all") {
            singleSubId = "__all__";
            commandMode = "update";
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: validproxy [options]\n"
                      << "Options:\n"
                      << "  -c, --config <path>    Config file path (default: config.json)\n"
                      << "  --update <id>         Update single subscription by ID\n"
                      << "  --update-all           Update all enabled subscriptions\n"
                      << "  --test <id>           Test proxies from subscription by ID\n"
                      << "  -h, --help            Show this help\n";
            return 0;
        } else if (arg.find(".json") != std::string::npos) {
            std::filesystem::path p(arg);
            if (p.is_relative()) {
                p = std::filesystem::path(exeDir) / p;
            }
            configPath = p.lexically_normal().string();
        }
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
        
        if (commandMode == "test") {
            std::cout << "Running TEST mode for subscription: " << singleSubId << std::endl;
            std::cout << "Use --sub parameter to test. Note: For testing, please run without --update or --test to use the main test mode." << std::endl;
            sqlite3_close(db);
            curl_global_cleanup();
            return 0;
        }
        
        std::string logFile = "sub_update_" + std::string(timestamp) + ".log";
        std::ofstream logOut(logFile, std::ios::out | std::ios::trunc);
        
        std::cout << "Updating subscription: " << singleSubId << std::endl;
        logOut << "[" << timestamp << "] Starting subscription update for subId: " << singleSubId << std::endl;
        
        SubitemUpdater subUpdater(db, logOut.is_open() ? &logOut : nullptr,
                                   appConfig->xray_executable,
                                   10086,
                                   appConfig->test_timeout_ms,
                                   appConfig->xray_start_port,
                                   appConfig->priority_proxy_enabled);
        
        bool result;
        if (singleSubId == "__all__") {
            result = subUpdater.run();
        } else {
            result = subUpdater.runSingle(singleSubId);
        }
        
        if (logOut.is_open()) {
            logOut.close();
        }
        
        sqlite3_close(db);
        curl_global_cleanup();
        
        std::cout << "Subscription update " << (result ? "completed" : "failed") << std::endl;
        
        return result ? 0 : 1;
    }
    
    auto appConfig = config::ConfigReader::load(configPath);
    if (!appConfig) {
        std::cerr << "Failed to load config from: " << configPath << std::endl;
        return 1;
    }
    
    int numWorkers = appConfig->xray_workers;
    int startPort = appConfig->xray_start_port;
    int baseApiPort = appConfig->xray_api_port;
    bool logEnabled = appConfig->log_enabled;
    g_logEnabled = logEnabled;
    
    std::cout << "Config loaded from: " << configPath << std::endl;
    std::cout << "Database path: " << appConfig->database_path << std::endl;
    std::cout << "SQL query: " << appConfig->sql_query << std::endl;
    std::cout << "Xray executable: " << appConfig->xray_executable << std::endl;
    std::cout << "Workers: " << numWorkers << std::endl;
    std::cout << "Start port: " << startPort << std::endl;
    std::cout << "Base API port: " << baseApiPort << std::endl;

    auto startTime = std::chrono::system_clock::now();
    time_t startTimeT = std::chrono::system_clock::to_time_t(startTime);
    char startTimeStr[32];
    strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %H:%M:%S", localtime(&startTimeT));
    
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&startTimeT));
    
    std::filesystem::path binDir = std::filesystem::current_path();
    if (binDir.filename() == "bin") {
    } else {
        binDir = binDir / "bin";
    }
    if (!std::filesystem::exists(binDir)) {
        std::filesystem::create_directory(binDir);
    }
    std::string logFile = (binDir / ("test_log_" + std::string(timestamp) + ".txt")).string();
    std::ofstream logOut;
    if (logEnabled) {
        logOut.open(logFile);
        if (!logOut.is_open()) {
            std::cerr << "Failed to open log file: " << logFile << std::endl;
        } else {
            g_logOut = &logOut;
            logOut << "Test started at: " << startTimeStr << std::endl;
            logOut << "Workers: " << numWorkers << std::endl;
            logOut << "Start port: " << startPort << std::endl;
            logOut << "Test URL: " << appConfig->test_url << std::endl;
            logOut << "Test timeout: " << appConfig->test_timeout_ms << "ms" << std::endl;
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

    std::vector<std::pair<int, int>> workerPorts;
    std::vector<int> usedPorts;
    for (int i = 0; i < numWorkers; ++i) {
        int socksPort = findAvailablePort(startPort + i, 1000, usedPorts);
        int apiPort = findAvailablePort(baseApiPort + i, 1000, usedPorts);
        
        if (socksPort == -1) {
            socksPort = startPort + i;
        }
        if (apiPort == -1) {
            apiPort = baseApiPort + i;
        }
        
        usedPorts.push_back(socksPort);
        usedPorts.push_back(apiPort);
        
        if (socksPort != startPort + i) {
            std::cout << "Port " << (startPort + i) << " in use, using " << socksPort << std::endl;
            if (logEnabled && logOut.is_open()) {
                logOut << "Port " << (startPort + i) << " in use, using " << socksPort << std::endl;
            }
        }
        if (apiPort != baseApiPort + i) {
            std::cout << "Port " << (baseApiPort + i) << " in use, using " << apiPort << std::endl;
            if (logEnabled && logOut.is_open()) {
                logOut << "Port " << (baseApiPort + i) << " in use, using " << apiPort << std::endl;
            }
        }
        
        workerPorts.push_back({socksPort, apiPort});
        std::cout << "Worker " << i << ": socks=" << socksPort << " api=" << apiPort << std::endl;
        if (logEnabled && logOut.is_open()) {
            logOut << "Worker " << i << ": socks=" << socksPort << " api=" << apiPort << std::endl;
        }
    }

    std::vector<std::string> xrayConfigs;
    std::string baseDir = "bin";
    
    for (int i = 0; i < numWorkers; ++i) {
        int socksPort = workerPorts[i].first;
        int apiPort = workerPorts[i].second;
        
        std::string configContent = R"({
            "log": {"loglevel": "warning"},
            "api": {
                "tag": "api",
                "services": [
                    "HandlerService",
                    "LoggerService",
                    "StatsService"
                ]
            },
            "stats": {},
            "policy": {
                "levels": {"0": {"statsUserUplink": true, "statsUserDownlink": true}},
                "system": {"statsInboundUplink": true, "statsInboundDownlink": true, "statsOutboundUplink": true, "statsOutboundDownlink": true}
            },
            "inbounds": [
                {
                    "tag": "api",
                    "listen": "127.0.0.1",
                    "port": )" + std::to_string(apiPort) + R"(,
                    "protocol": "dokodemo-door",
                    "settings": {"address": "127.0.0.1"}
                },
                {
                    "tag": "socks-in",
                    "listen": "127.0.0.1",
                    "port": )" + std::to_string(socksPort) + R"(,
                    "protocol": "mixed",
                    "settings": {"auth": "noauth", "udp": true}
                }
            ],
            "outbounds": [
                {"tag": "direct", "protocol": "freedom"}
            ],
            "routing": {
                "domainStrategy": "AsIs",
                "rules": [
                    {"type": "field", "inboundTag": ["api"], "outboundTag": "api"},
                    {"type": "field", "outboundTag": "proxy", "network": "tcp"}
                ]
            }
        })";
        
        std::filesystem::path binDir = std::filesystem::current_path();
        if (binDir.filename() == "bin") {
        } else {
            binDir = binDir / "bin";
        }
        std::string configPath = (binDir / ("worker_config_" + std::to_string(socksPort) + ".json")).string();
        std::ofstream outFile(configPath);
        outFile << configContent;
        outFile.close();
        std::cout << "Worker " << i << " config: " << configPath << std::endl;
        xrayConfigs.push_back(configPath);
    }

    std::vector<std::thread> workerThreads;
    std::atomic<int> successCount{0};
    std::mutex printMutex;
    std::mutex dbMutex;

    if (!initXrayJob()) {
        std::cerr << "Warning: Failed to init job object, xray processes may not be cleaned up properly" << std::endl;
    }

for (int i = 0; i < numWorkers; ++i) {
        if (!startXray(appConfig->xray_executable, xrayConfigs[i])) {
            std::cerr << "Failed to start xray worker " << i << std::endl;
        } else {
            std::cout << "Worker " << i << " xray started" << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
int testApiPort = workerPorts[0].second;
    std::string testApiAddr = "127.0.0.1:" + std::to_string(testApiPort);
    xray::XrayApi xrayApi(appConfig->xray_executable, testApiAddr);
    std::string pingResult;
    if (!xrayApi.ping(pingResult)) {
        std::cerr << "Warning: API port " << testApiPort << " not responding" << std::endl;
        if (logEnabled && logOut.is_open()) {
            logOut << "Warning: API port " << testApiPort << " not responding" << std::endl;
        }
    } else {
        std::cout << "API port " << testApiPort << " ready" << std::endl;
        if (logEnabled && logOut.is_open()) {
            logOut << "API port " << testApiPort << " ready" << std::endl;
        }
    }
    
    config::ConfigGenerator configGen(db);
    g_profiles = configGen.loadProfiles(appConfig->sql_query);
    g_totalProxies = g_profiles.size();
    
    printConfigTypeHelp();
    std::cout << "Total proxies to test: " << g_totalProxies << std::endl;

    for (int i = 0; i < g_totalProxies; ++i) {
        g_profilesQueue.push(i);
    }

    for (int i = 0; i < numWorkers; ++i) {
        WorkerContext ctx;
        ctx.workerId = i;
        ctx.socksPort = workerPorts[i].first;
        ctx.apiPort = workerPorts[i].second;
        ctx.xrayPath = appConfig->xray_executable;
        ctx.configPath = xrayConfigs[i];
        ctx.testUrl = appConfig->test_url;
        ctx.timeoutMs = appConfig->test_timeout_ms;
        ctx.db = db;
        ctx.successCount = &successCount;
        ctx.printMutex = &printMutex;
        ctx.dbMutex = &dbMutex;
        
        workerThreads.emplace_back(workerThreadFunc, ctx);
    }

    for (auto& t : workerThreads) {
        t.join();
    }

    auto endTime = std::chrono::system_clock::now();
    time_t endTimeT = std::chrono::system_clock::to_time_t(endTime);
    char endTimeStr[32];
    strftime(endTimeStr, sizeof(endTimeStr), "%Y-%m-%d %H:%M:%S", localtime(&endTimeT));
    
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(endTime - startTime);
    
    if (logEnabled && logOut.is_open()) {
        logOut << std::endl;
        logOut << "Test finished at: " << endTimeStr << std::endl;
        logOut << "Total proxies: " << g_processedCount << std::endl;
        logOut << "Successful: " << successCount << std::endl;
        logOut << "Failed: " << (g_processedCount - successCount) << std::endl;
        logOut << "Duration: " << duration.count() << " seconds" << std::endl;
        logOut.flush();
        logOut.close();
        g_logOut = nullptr;
    }
    
    std::cout << "Tested " << g_processedCount << " proxies, " << successCount << " successful" << std::endl;

    stopXray();

    sqlite3_close(db);
    curl_global_cleanup();

    std::cout << "validproxy finished" << std::endl;
    return 0;
}
