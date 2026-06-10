#include "ProxyFinder.h"
#include "XrayManager.h"
#include "XrayInstance.h"
#include "ConfigGenerator.h"
#include "XrayApi.h"
#include "Profileitem.h"
#include "ProfileExItem.h"
#include "Logger.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>

ProxyFinder::ProxyFinder(sqlite3* db, XrayManager* manager, const std::string& xrayPath, 
                         const std::string& testUrl, const std::string& targetUrl, 
                         int timeoutMs)
    : db_(db), manager_(manager), xrayPath_(xrayPath), testUrl_(testUrl), targetUrl_(targetUrl),
      timeoutMs_(timeoutMs), currentSocksPort_(-1), currentApiPort_(-1) {
    lastResult_ = {false, -1, "", "", 0, 0, ""};
}

ProxyFinder::~ProxyFinder() {
    release();
}

std::pair<int, int> ProxyFinder::findFirstWorkingProxy(const std::string& targetUrl) {
    std::pair<int, int> result = {-1, -1};
    
    if (!manager_ || manager_->getInstanceCount() == 0) {
        Logger::write("ProxyFinder: No xray instances running", LogLevel::ERR);
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
    Logger::write("ProxyFinder: Testing " + std::to_string(validProxies.size()) + " proxies (first match)...", LogLevel::INFO);
    Logger::write("ProxyFinder: Using test URL: " + testUrl, LogLevel::INFO);
    
    for (size_t i = 0; i < validProxies.size(); ++i) {
        const auto& proxy = validProxies[i];
        
        int workerIndex = i % manager_->getInstanceCount();
        auto instance = manager_->getInstance(workerIndex);
        if (!instance) continue;
        
        currentSocksPort_ = instance->getSocksPort();
        currentApiPort_ = instance->getApiPort();
        
        Logger::write("ProxyFinder: Testing proxy " + std::to_string(i+1) + "/" + std::to_string(validProxies.size()) + 
                 " (indexId=" + proxy.indexId + ", socks=" + std::to_string(currentSocksPort_) + ")", LogLevel::INFO);
        
        if (!injectProxyToXray(proxy.indexId)) {
            Logger::write("ProxyFinder: Failed to inject proxy: " + proxy.indexId, LogLevel::ERR);
            removeProxyFromXray();
            continue;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        auto testRes = testProxyConnectivity(currentSocksPort_, testUrl);
        
        db::models::ProfileExItemDAO exDao(db_);
        exDao.updateTestResult(proxy.indexId, testRes.latencyMs, testRes.success, testRes.errorMsg);
        
        if (testRes.success) {
            Logger::write("ProxyFinder: Found working proxy at socks=" + std::to_string(currentSocksPort_), LogLevel::INFO);
            lastResult_ = {true, testRes.latencyMs, testRes.errorMsg, proxy.indexId, proxy.address, proxy.port, proxy.delay};
            result = {currentSocksPort_, currentApiPort_};
            return result;
        } else {
            Logger::write("ProxyFinder: Proxy failed: " + testRes.errorMsg, LogLevel::ERR);
        }
        
        removeProxyFromXray();
    }
    
    Logger::write("ProxyFinder: No working proxy found", LogLevel::ERR);
    return result;
}
