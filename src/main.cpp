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
#include <windows.h>

#include "Profileitem.h"
#include "Profileexitem.h"
#include "ConfigGenerator.h"
#include "ConfigReader.h"
#include "XrayApi.h"

namespace {

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
    
    std::cout << "Config loaded from: " << configPath << std::endl;
    std::cout << "Database path: " << appConfig->database_path << std::endl;
    std::cout << "SQL query: " << appConfig->sql_query << std::endl;
    std::cout << "Xray executable: " << appConfig->xray_executable << std::endl;
    std::cout << "Xray config: " << appConfig->xray_config << std::endl;

    sqlite3* db = nullptr;
    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (sqlite3_open(appConfig->database_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Failed to open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }
    std::cout << "Database opened: " << appConfig->database_path << std::endl;

    std::string xrayApiAddr = "127.0.0.1:" + std::to_string(appConfig->xray_api_port);
    xray::XrayApi xrayApi(appConfig->xray_executable, xrayApiAddr);

    std::cout << "Starting xray with config: " << appConfig->xray_config << std::endl;
    if (!startXray(appConfig->xray_executable, appConfig->xray_config)) {
        std::cerr << "Failed to start xray" << std::endl;
        return 1;
    }
    std::cout << "Xray started successfully" << std::endl;

    config::ConfigGenerator configGen(db);
    db::models::ProfileexitemDAO exItemDao(db);
    
    auto profiles = configGen.loadProfiles(appConfig->sql_query);
    std::cout << "Loaded " << profiles.size() << " profiles from ProfileItem" << std::endl;

    auto exItems = configGen.loadProfileExItems();
    std::cout << "Loaded " << exItems.size() << " items from ProfileExItem" << std::endl;

    printConfigTypeHelp();

    int totalProxies = profiles.size();
    int testCount = 0;
    int successCount = 0;
    
    std::cout << "Total proxies to test: " << totalProxies << std::endl;
    
    for (const auto& profile : profiles) {
        // if (testCount >= 25) break;
        
        std::cout << "[" << (testCount + 1) << "/" << totalProxies << "] Testing Profile[" << profile.indexid << "]: " << profile.configtype << " " 
                  << profile.address << ":" << profile.port << " (" << profile.remarks << ")" << std::endl;
        
        auto config = configGen.generateConfig(profile);
        
        std::string tag = "proxy";

        std::cout << "  Removing existing proxy..." << std::endl;
        xrayApi.removeOutbound(tag);
        
        std::cout << "  Adding outbound: " << config.outbound_json << std::endl;
        bool added = xrayApi.addOutbound(config.outbound_json, tag);
        if (!added) {
            std::cerr << "  Failed to add outbound: " << xrayApi.getLastError() << std::endl;
            continue;
        }
        std::cout << "  Outbound added successfully" << std::endl;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        long latencyMs = -1;
        std::string errorMsg;
        bool connected = testProxy(appConfig->xray_socks_port, appConfig->test_url, appConfig->test_timeout_ms, latencyMs, errorMsg);
        
        if (connected) {
            std::cout << "  Proxy test: SUCCESS (latency: " << latencyMs << "ms)" << std::endl;
            successCount++;
        } else {
            std::cout << "  Proxy test: FAILED (" << errorMsg << ")" << std::endl;
        }
        
        exItemDao.updateTestResult(profile.indexid, latencyMs, connected, errorMsg);
        
        xrayApi.removeOutbound(tag);
        
        testCount++;
    }

    std::cout << "Tested " << testCount << " proxies, " << successCount << " successful" << std::endl;

    sqlite3_close(db);
    curl_global_cleanup();

    std::cout << "validproxy finished" << std::endl;
    return 0;
}
