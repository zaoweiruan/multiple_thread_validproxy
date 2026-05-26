#include "ProxyFinder.h"
#include "XrayManager.h"
#include "XrayInstance.h"
#include "ConfigGenerator.h"
#include "XrayApi.h"
#include "Profileitem.h"
#include "ProfileExItem.h"
#include "Logger.h"
#include "CurlEasyHandle.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>

ProxyFinder::ProxyFinder(sqlite3* db, XrayManager* manager, const std::string& xrayPath, 
                         const std::string& testUrl, const std::string& targetUrl, 
                         int timeoutMs, std::atomic<bool>* cancelFlag)
    : db_(db), manager_(manager), xrayPath_(xrayPath), testUrl_(testUrl), targetUrl_(targetUrl),
      timeoutMs_(timeoutMs), currentSocksPort_(-1), currentApiPort_(-1), cancelRequested_(cancelFlag) {
    lastResult_ = {false, -1, "", "", "", 0, 0};
}

ProxyFinder::~ProxyFinder() {
    release();
}

std::pair<int, int> ProxyFinder::findFirstWorkingProxy(const std::string& targetUrl) {
    std::pair<int, int> result = {-1, -1};
    
    if (!manager_ || manager_->getInstanceCount() == 0) {
        Logger::write("ERROR: ProxyFinder: No xray instances running", LogLevel::ERR);
        return result;
    }
    
    auto proxies = loadFallbackProxies();
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
        if (isCancelled()) {
            Logger::write("ProxyFinder: Cancelled during first proxy search", LogLevel::WARN);
            return result;
        }
        const auto& proxy = validProxies[i];
        
        int workerIndex = i % manager_->getInstanceCount();
        auto instance = manager_->getInstance(workerIndex);
        if (!instance) continue;
        
        currentSocksPort_ = instance->getSocksPort();
        currentApiPort_ = instance->getApiPort();
        
        std::cout << "ProxyFinder: Testing proxy " << (i+1) << "/" << validProxies.size() 
                 << " (indexId=" << proxy.indexId << ", socks=" << currentSocksPort_ << ")" << std::endl;
        
        if (!injectProxyToXray(proxy.indexId)) {
            Logger::write("ERROR: ProxyFinder: Failed to inject proxy: " + proxy.indexId, LogLevel::ERR);
            removeProxyFromXray();
            continue;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        auto testRes = testProxyConnectivity(currentSocksPort_, testUrl);
        
        db::models::ProfileExItemDAO exDao(db_);
        exDao.updateTestResult(proxy.indexId, testRes.latencyMs, testRes.success, testRes.errorMsg);
        
        if (testRes.success) {
            std::cout << "ProxyFinder: Found working proxy at socks=" << currentSocksPort_ << std::endl;
            lastResult_ = {true, testRes.latencyMs, "", proxy.indexId, proxy.address, proxy.socksPort, proxy.delay};
            result = {currentSocksPort_, currentApiPort_};
            return result;
        } else {
            Logger::write("ProxyFinder: Proxy failed: " + testRes.errorMsg, LogLevel::INFO);
        }
        
        removeProxyFromXray();
    }
    
    Logger::write("ERROR: ProxyFinder: No working proxy found", LogLevel::ERR);
    return result;
}

std::pair<int, int> ProxyFinder::findWorkingProxy(const std::string& targetUrl) {
    std::pair<int, int> result = {-1, -1};
    
    if (!manager_ || manager_->getInstanceCount() == 0) {
        Logger::write("ERROR: ProxyFinder: No xray instances running", LogLevel::ERR);
        return result;
    }
    
    auto proxies = loadFallbackProxies();
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
        if (isCancelled()) {
            Logger::write("ProxyFinder: Cancelled during best proxy search", LogLevel::WARN);
            return result;
        }
        const auto& proxy = validProxies[i];
        
        int workerIndex = i % manager_->getInstanceCount();
        auto instance = manager_->getInstance(workerIndex);
        if (!instance) continue;
        
        currentSocksPort_ = instance->getSocksPort();
        currentApiPort_ = instance->getApiPort();
        
        std::cout << "ProxyFinder: Testing proxy " << (i+1) << "/" << validProxies.size() 
                 << " (indexId=" << proxy.indexId << ", socks=" << currentSocksPort_ << ")" << std::endl;
        
        if (!injectProxyToXray(proxy.indexId)) {
            Logger::write("ERROR: ProxyFinder: Failed to inject proxy: " + proxy.indexId, LogLevel::ERR);
            removeProxyFromXray();
            continue;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        auto testRes = testProxyConnectivity(currentSocksPort_, testUrl);
        
        db::models::ProfileExItemDAO exDao(db_);
        exDao.updateTestResult(proxy.indexId, testRes.latencyMs, testRes.success, testRes.errorMsg);
        
        if (testRes.success) {
            testRes.indexId = proxy.indexId;
            testRes.address = proxy.address;
            testRes.port = proxy.socksPort;
            testRes.delay = testRes.latencyMs;
            allResults.push_back(testRes);
            std::cout << "ProxyFinder: Working proxy: delay=" << testRes.latencyMs << "ms" << std::endl;
        } else {
            Logger::write("ProxyFinder: Proxy failed: " + testRes.errorMsg, LogLevel::INFO);
        }
        
        removeProxyFromXray();
    }
    
    if (allResults.empty()) {
        Logger::write("ERROR: ProxyFinder: No working proxy found", LogLevel::ERR);
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
    
    try {
        CurlEasyHandle curl;
        
        std::string proxyUrl = "http://127.0.0.1:" + std::to_string(socksPort);
        curl.setProxy(proxyUrl)
            .setUrl(urlToTest)
            .setNoBody()
            .setTimeoutMs(timeoutMs_)
            .setFollowLocation();
        
        curl.perform();
        
        result.latencyMs = static_cast<long>(curl.getTotalTime() * 1000);
        long responseCode = curl.getResponseCode();
        
        if (responseCode != 200 && responseCode != 204) {
            result.errorMsg = "HTTP " + std::to_string(responseCode);
        } else {
            result.success = true;
            result.delay = result.latencyMs;
        }
        
    } catch (const std::exception& e) {
        result.errorMsg = e.what();
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
                  "ORDER BY CAST(pe.Delay AS INTEGER) ASC";
    
    if (maxCount > 0) {
        sql += " LIMIT " + std::to_string(maxCount);
    }
    sql += ";";
    
    Logger::write("ProxyFinder: SQL: " + sql, LogLevel::DEBUG);
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::write("ERROR: ProxyFinder: SQL prepare failed - " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
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
    Logger::write("ProxyFinder: Loaded " + std::to_string(proxies.size()) + " proxies from database", LogLevel::DEBUG);
    return proxies;
}

bool ProxyFinder::injectProxyToXray(const std::string& indexId) {
    if (currentApiPort_ <= 0) return false;
    
    db::models::ProfileitemDAO profileDao(db_);
    std::string sql = "SELECT * FROM ProfileItem WHERE IndexId = '" + indexId + "';";
    auto profiles = profileDao.getAll(sql);
    
    if (profiles.empty()) {
        Logger::write("ERROR: ProxyFinder: Profile not found: " + indexId, LogLevel::ERR);
        return false;
    }
    
    auto profile = profiles[0];
    profile.presocksport = std::to_string(currentSocksPort_);
    
    try {
        profile.checkRequired();
    } catch (const std::exception& e) {
        Logger::write("ERROR: ProxyFinder: Config error: " + std::string(e.what()), LogLevel::ERR);
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
        Logger::write("ERROR: 注入xray outbound 错误: " + xrayApi.getLastError(), LogLevel::ERR);
        return false;
    }
    
    return true;
}

void ProxyFinder::removeProxyFromXray() {
    // 暂时不清理
}
