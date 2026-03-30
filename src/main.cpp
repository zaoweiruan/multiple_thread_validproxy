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

#include "Profileitem.h"
#include "Profileexitem.h"
#include "ConfigGenerator.h"
#include "ConfigReader.h"
#include "XrayApi.h"

namespace {

std::vector<db::models::Profileitem> g_profiles;
std::queue<int> g_profilesQueue;
std::atomic<int> g_processedCount{0};
int g_totalProxies = 0;
std::mutex g_queueMutex;

void printConfigTypeHelp() {
    std::cout << "ConfigType values:" << std::endl;
    std::cout << "  1 = VMess" << std::endl;
    std::cout << "  3 = Shadowsocks" << std::endl;
    std::cout << "  5 = VLESS" << std::endl;
    std::cout << "  6 = Trojan" << std::endl;
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
    
    if (!CreateProcessA(NULL, (LPSTR)cmd.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        std::cerr << "Failed to start xray: " << GetLastError() << std::endl;
        return false;
    }
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return true;
}

bool testProxy(int socksPort, const std::string& testUrl, int timeoutMs, long& latencyMs, std::string& errorMsg) {
    latencyMs = -1;
    errorMsg = "";
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        errorMsg = "curl_init_failed";
        return false;
    }
    
    std::string proxyUrl = "socks5://127.0.0.1:" + std::to_string(socksPort);
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
        }
        
        try {
            config::ConfigGenerator configGen(ctx.db);
            auto config = configGen.generateConfig(profile);
            
            std::string tag = "proxy";
            xrayApi.removeOutbound(tag);
            
            if (!xrayApi.addOutbound(config.outbound_json, tag)) {
                std::lock_guard<std::mutex> lock(*ctx.printMutex);
                std::cerr << "[Worker-" << ctx.workerId << "] Failed to add outbound: " << xrayApi.getLastError() << std::endl;
            {
                std::lock_guard<std::mutex> lock(*ctx.dbMutex);
                exItemDao.updateTestResult(profile.indexid, -1, false, xrayApi.getLastError());
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
                std::cout << "[Worker-" << ctx.workerId << "] SUCCESS (latency: " << latencyMs << "ms)" << std::endl;
            } else {
                std::lock_guard<std::mutex> lock(*ctx.printMutex);
                std::cout << "[Worker-" << ctx.workerId << "] FAILED (" << errorMsg << ")" << std::endl;
            }
            
            {
            std::lock_guard<std::mutex> lock(*ctx.dbMutex);
            exItemDao.updateTestResult(profile.indexid, latencyMs, connected, errorMsg);
        }
            xrayApi.removeOutbound(tag);
            
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

int main(int argc, char* argv[]) {
    std::cout << "validproxy starting..." << std::endl;

    std::string configPath = config::ConfigReader::getDefaultConfigPath();
    if (argc > 1) {
        configPath = argv[1];
    }
    
    auto appConfig = config::ConfigReader::load(configPath);
    if (!appConfig) {
        std::cerr << "Failed to load config from: " << configPath << std::endl;
        return 1;
    }
    
    int numWorkers = appConfig->test_threads;
    int startPort = appConfig->xray_start_port;
    int baseApiPort = appConfig->xray_api_port;
    
    std::cout << "Config loaded from: " << configPath << std::endl;
    std::cout << "Database path: " << appConfig->database_path << std::endl;
    std::cout << "SQL query: " << appConfig->sql_query << std::endl;
    std::cout << "Xray executable: " << appConfig->xray_executable << std::endl;
    std::cout << "Workers: " << numWorkers << std::endl;
    std::cout << "Start port: " << startPort << std::endl;
    std::cout << "Base API port: " << baseApiPort << std::endl;

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
    for (int i = 0; i < numWorkers; ++i) {
        int socksPort = startPort + i;
        int apiPort = baseApiPort + i;
        workerPorts.push_back({socksPort, apiPort});
        std::cout << "Worker " << i << ": socks=" << socksPort << " api=" << apiPort << std::endl;
    }

    std::vector<std::string> xrayConfigs;
    std::string baseDir = appConfig->xray_config.substr(0, appConfig->xray_config.find_last_of("/\\"));
    
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
                    "tag": "socks",
                    "listen": "127.0.0.1",
                    "port": )" + std::to_string(socksPort) + R"(,
                    "protocol": "socks",
                    "settings": {"auth": "noauth", "udp": true}
                }
            ],
            "outbounds": [
                {"tag": "direct", "protocol": "freedom"}
            ],
            "routing": {
                "domainStrategy": "AsIs",
                "rules": [
                    {"type": "field", "inboundTag": ["api"], "outboundTag": "api"}
                ]
            }
        })";
        
        std::string configPath = baseDir + "/worker_config_" + std::to_string(socksPort) + ".json";
        std::ofstream outFile(configPath);
        outFile << configContent;
        outFile.close();
        xrayConfigs.push_back(configPath);
    }

    std::vector<std::thread> workerThreads;
    std::atomic<int> successCount{0};
    std::mutex printMutex;
    std::mutex dbMutex;

    for (int i = 0; i < numWorkers; ++i) {
        if (!startXray(appConfig->xray_executable, xrayConfigs[i])) {
            std::cerr << "Failed to start xray worker " << i << std::endl;
        } else {
            std::cout << "Worker " << i << " xray started" << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    config::ConfigGenerator configGen(db);
    g_profiles = configGen.loadProfiles(appConfig->sql_query);
    g_totalProxies = g_profiles.size();
    
    std::cout << "Loaded " << g_profiles.size() << " profiles from ProfileItem" << std::endl;

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

    std::cout << "Tested " << g_processedCount << " proxies, " << successCount << " successful" << std::endl;

    sqlite3_close(db);
    curl_global_cleanup();

    std::cout << "validproxy finished" << std::endl;
    return 0;
}
