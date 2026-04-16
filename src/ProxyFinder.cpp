#include "ProxyFinder.h"
#include "XrayManager.h"
#include "XrayInstance.h"
#include "ConfigGenerator.h"
#include "XrayApi.h"
#include "Profileitem.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>

void ProxyFinder::log(const std::string& msg) {
    std::cout << msg << std::endl;
    if (logOut_ && !logOut_->fail()) {
        *logOut_ << msg << std::endl;
        logOut_->flush();
    }
}

ProxyFinder::ProxyFinder(sqlite3* db, XrayManager* manager, const std::string& xrayPath, 
                         const std::string& testUrl, const std::string& targetUrl, 
                         int timeoutMs, std::ostream* logOut)
    : db_(db), manager_(manager), xrayPath_(xrayPath), testUrl_(testUrl), targetUrl_(targetUrl),
      timeoutMs_(timeoutMs), currentSocksPort_(-1), currentApiPort_(-1), logOut_(logOut) {
    lastResult_ = {false, -1, "", "", "", 0, 0};
}

ProxyFinder::~ProxyFinder() {
    release();
}

std::pair<int, int> ProxyFinder::findFirstWorkingProxy(const std::string& targetUrl) {
    std::pair<int, int> result = {-1, -1};
    
    if (!manager_ || manager_->getInstanceCount() == 0) {
        std::cerr << "ProxyFinder: No xray instances running" << std::endl;
        return result;
    }
    
    auto proxies = loadFallbackProxies(100);
    auto validProxies = proxies;
    validProxies.erase(
        std::remove_if(validProxies.begin(), validProxies.end(),
            [](const FallbackProxy& p) { return p.delay <= 0; }),
        validProxies.end()
    );
    
    std::string testUrl = targetUrl.empty() ? testUrl_ : targetUrl;
    std::cout << "ProxyFinder: Testing " << validProxies.size() << " proxies (first match)..." << std::endl;
    std::cout << "ProxyFinder: Using test URL: " << testUrl << std::endl;
    
    for (size_t i = 0; i < validProxies.size(); ++i) {
        const auto& proxy = validProxies[i];
        
        int workerIndex = i % manager_->getInstanceCount();
        auto instance = manager_->getInstance(workerIndex);
        if (!instance) continue;
        
        currentSocksPort_ = instance->getSocksPort();
        currentApiPort_ = instance->getApiPort();
        
        std::cout << "ProxyFinder: Testing proxy " << (i+1) << "/" << validProxies.size() 
                 << " (indexId=" << proxy.indexId << ", socks=" << currentSocksPort_ << ")" << std::endl;
        
        if (!injectProxyToXray(proxy.indexId)) {
            std::cerr << "ProxyFinder: Failed to inject proxy: " << proxy.indexId << std::endl;
            removeProxyFromXray();
            continue;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        auto testRes = testProxyConnectivity(currentSocksPort_, testUrl);
        if (testRes.success) {
            std::cout << "ProxyFinder: Found working proxy at socks=" << currentSocksPort_ << std::endl;
            lastResult_ = {true, testRes.latencyMs, "", proxy.indexId, proxy.address, proxy.socksPort, proxy.delay};
            result = {currentSocksPort_, currentApiPort_};
            return result;
        } else {
            std::cerr << "ProxyFinder: Proxy failed: " << testRes.errorMsg << std::endl;
        }
        
        removeProxyFromXray();
    }
    
    std::cerr << "ProxyFinder: No working proxy found" << std::endl;
    return result;
}

std::pair<int, int> ProxyFinder::findWorkingProxy(const std::string& targetUrl) {
    std::pair<int, int> result = {-1, -1};
    
    if (!manager_ || manager_->getInstanceCount() == 0) {
        std::cerr << "ProxyFinder: No xray instances running" << std::endl;
        return result;
    }
    
    auto proxies = loadFallbackProxies(100);
    auto validProxies = proxies;
    validProxies.erase(
        std::remove_if(validProxies.begin(), validProxies.end(),
            [](const FallbackProxy& p) { return p.delay <= 0; }),
        validProxies.end()
    );
    
    std::string testUrl = targetUrl.empty() ? testUrl_ : targetUrl;
    std::cout << "ProxyFinder: Testing " << validProxies.size() << " proxies..." << std::endl;
    std::cout << "ProxyFinder: Using test URL: " << testUrl << std::endl;
    
    // 测试所有代理，记录实际延迟
    std::vector<TestResult> allResults;
    
    for (size_t i = 0; i < validProxies.size(); ++i) {
        const auto& proxy = validProxies[i];
        
        int workerIndex = i % manager_->getInstanceCount();
        auto instance = manager_->getInstance(workerIndex);
        if (!instance) continue;
        
        currentSocksPort_ = instance->getSocksPort();
        currentApiPort_ = instance->getApiPort();
        
        std::cout << "ProxyFinder: Testing proxy " << (i+1) << "/" << validProxies.size() 
                 << " (indexId=" << proxy.indexId << ", socks=" << currentSocksPort_ << ")" << std::endl;
        
        if (!injectProxyToXray(proxy.indexId)) {
            std::cerr << "ProxyFinder: Failed to inject proxy: " << proxy.indexId << std::endl;
            removeProxyFromXray();
            continue;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        auto testRes = testProxyConnectivity(currentSocksPort_, testUrl);
        if (testRes.success) {
            testRes.indexId = proxy.indexId;
            testRes.address = proxy.address;
            testRes.port = proxy.socksPort;
            testRes.delay = testRes.latencyMs;
            allResults.push_back(testRes);
            std::cout << "ProxyFinder: Working proxy: delay=" << testRes.latencyMs << "ms" << std::endl;
        } else {
            std::cerr << "ProxyFinder: Proxy failed: " << testRes.errorMsg << std::endl;
        }
        
        removeProxyFromXray();
    }
    
    if (allResults.empty()) {
        std::cerr << "ProxyFinder: No working proxy found" << std::endl;
        return result;
    }
    
    // 按实际延迟排序，找到最小的
    std::sort(allResults.begin(), allResults.end(), 
        [](const TestResult& a, const TestResult& b) { return a.latencyMs < b.latencyMs; });
    
    const auto& best = allResults[0];
    lastResult_ = best;
    
    // 找到对应的端口
    for (size_t i = 0; i < validProxies.size(); ++i) {
        if (validProxies[i].indexId == best.indexId) {
            int workerIndex = i % manager_->getInstanceCount();
            auto instance = manager_->getInstance(workerIndex);
            if (instance) {
                result = {instance->getSocksPort(), instance->getApiPort()};
                std::cout << "ProxyFinder: Best proxy at socks=" << result.first 
                         << " (delay=" << best.latencyMs << "ms)" << std::endl;
            }
            break;
        }
    }
    
    return result;
}

void ProxyFinder::release() {
    removeProxyFromXray();
    currentSocksPort_ = -1;
    currentApiPort_ = -1;
}

ProxyFinder::TestResult ProxyFinder::testProxyConnectivity(int socksPort, const std::string& targetUrl) {
    TestResult result = {false, -1, ""};
    
    std::string urlToTest = targetUrl.empty() ? testUrl_ : targetUrl;
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.errorMsg = "curl_init_failed";
        return result;
    }
    
    std::string proxyUrl = "http://127.0.0.1:" + std::to_string(socksPort);
    curl_easy_setopt(curl, CURLOPT_PROXY, proxyUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, urlToTest.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeoutMs_);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    
    double totalTime = 0;
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &totalTime);
    result.latencyMs = static_cast<long>(totalTime * 1000);
    
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        result.errorMsg = curl_easy_strerror(res);
    } else if (responseCode != 200 && responseCode != 204) {
        result.errorMsg = "HTTP " + std::to_string(responseCode);
    } else {
        result.success = true;
    }
    
    return result;
}

std::vector<ProxyFinder::FallbackProxy> ProxyFinder::loadFallbackProxies(int maxCount) {
    std::vector<FallbackProxy> proxies;
    
    std::string sql = "SELECT pi.IndexId, pi.Address, pi.Port, pi.PreSocksPort, COALESCE(pe.Delay, 999999) AS Delay "
                  "FROM ProfileItem pi "
                  "LEFT JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId "
                  "WHERE pi.Address IS NOT NULL AND pi.Address != '' "
                  "AND pi.ConfigType IN ('1', '3', '4', '5', '6', '7', '8', '9', '10') "
                  "AND COALESCE(pe.Delay, 0) > 0 "
                  "ORDER BY CAST(pe.Delay AS INTEGER) ASC "
                  "LIMIT " + std::to_string(maxCount) + ";";
    
    log("ProxyFinder: SQL: " + sql);
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "ProxyFinder: SQL prepare failed - " << sqlite3_errmsg(db_) << std::endl;
        return proxies;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FallbackProxy proxy;
        proxy.indexId = (const char*)sqlite3_column_text(stmt, 0);
        proxy.address = (const char*)sqlite3_column_text(stmt, 1);
        
        const char* delayText = (const char*)sqlite3_column_text(stmt, 4);
        proxy.delay = delayText ? std::stoi(delayText) : 999999;
        
        const char* preSocksPort = (const char*)sqlite3_column_text(stmt, 3);
        const char* port = (const char*)sqlite3_column_text(stmt, 2);
        
        if (preSocksPort && strlen(preSocksPort) > 0) {
            proxy.socksPort = std::stoi(preSocksPort);
        } else if (port && strlen(port) > 0) {
            proxy.socksPort = std::stoi(port);
        } else {
            continue;
        }
        
        proxies.push_back(proxy);
    }
    
    sqlite3_finalize(stmt);
    log("ProxyFinder: Loaded " + std::to_string(proxies.size()) + " proxies from database");
    return proxies;
}

bool ProxyFinder::injectProxyToXray(const std::string& indexId) {
    if (currentApiPort_ <= 0) return false;
    
    db::models::ProfileitemDAO profileDao(db_);
    std::string sql = "SELECT * FROM ProfileItem WHERE IndexId = '" + indexId + "';";
    auto profiles = profileDao.getAll(sql);
    
    if (profiles.empty()) {
        std::cerr << "ProxyFinder: Profile not found: " << indexId << std::endl;
        return false;
    }
    
    auto profile = profiles[0];
    profile.presocksport = std::to_string(currentSocksPort_);
    
    try {
        profile.checkRequired();
    } catch (const std::exception& e) {
        std::cerr << "ProxyFinder: Config error: " << e.what() << std::endl;
        return false;
    }
    
    config::ConfigGenerator configGen(db_);
    auto config = configGen.generateConfig(profile);
    
    std::string xrayApiAddr = "127.0.0.1:" + std::to_string(currentApiPort_);
    
    xray::XrayApi xrayApi(xrayPath_, xrayApiAddr);
    
    std::string tag = "proxy";
    std::string addResult;
    
    xrayApi.removeOutbound(tag);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    if (!xrayApi.addOutbound(config.outbound_json, tag, addResult)) {
        std::cerr << "ProxyFinder: addOutbound failed: " << xrayApi.getLastError() << std::endl;
        return false;
    }
    
    return true;
}

void ProxyFinder::removeProxyFromXray() {
    // 暂时不清理
}