#include "ProxyBatchTester.h"
#include "ConfigGenerator.h"
#include "Utils.h"
#include "Logger.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <windows.h>

ProxyBatchTester::ProxyBatchTester(sqlite3* db, const config::AppConfig& config, const std::string& baseDir)
    : db_(db), config_(config), totalProxies_(0), successCount_(0), failedCount_(0), processedCount_(0) {
    
    std::string exeBaseDir = baseDir.empty() ? utils::getExecutableDir() : baseDir;
    std::string configDir = exeBaseDir + "/config";
    
    xrayManager_ = XrayManager::getInstance(config_.xray_executable, configDir, config_.xray_workers);
    proxyTester_ = new ProxyTester(xrayManager_, config_.test_url, config_.test_timeout_ms);
}

ProxyBatchTester::~ProxyBatchTester() {
    delete proxyTester_;
    delete xrayManager_;
}

std::vector<db::models::Profileitem> ProxyBatchTester::loadProxies(const std::string& subId) {
    config::ConfigGenerator configGen(db_);
    std::string sql;
    
    if (!subId.empty() && !config_.sql_by_subid.empty()) {
        sql = config_.sql_by_subid;
        size_t pos = sql.find("{subid}");
        if (pos != std::string::npos) {
            sql.replace(pos, 7, subId);
        }
    } else {
        sql = config_.sql_query;
    }
    
    Logger::write("Executing SQL: " + sql, LogLevel::DEBUG);
    return configGen.loadProfiles(sql);
}

int ProxyBatchTester::calculateXrayInstanceCount(int proxyCount) {
    int maxWorkers = config_.xray_workers;
    return std::min(proxyCount, maxWorkers);
}

bool ProxyBatchTester::startXrayInstances(int count) {
    int actual = xrayManager_->start(count, config_.xray_start_port, config_.xray_api_port);
    return actual > 0;
}

void ProxyBatchTester::workerThreadFunc(int workerId, int socksPort, int apiPort) {
    std::string xrayApiAddr = "127.0.0.1:" + std::to_string(apiPort);
    xray::XrayApi xrayApi(config_.xray_executable, xrayApiAddr);
    db::models::ProfileExItemDAO exItemDao(db_);
    config::ConfigGenerator configGen(db_);
    
    while (true) {
        int profileIdx = -1;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (proxiesQueue_.empty()) break;
            profileIdx = proxiesQueue_.front();
            proxiesQueue_.pop();
        }
        
        if (profileIdx < 0 || profileIdx >= static_cast<int>(proxies_.size())) {
            std::lock_guard<std::mutex> lock(queueMutex_);
            processedCount_++;
            continue;
        }
        
        const auto& profile = proxies_[profileIdx];
        
db::models::Profileitem configProfile = profile;
        
        try {
            configProfile.checkRequired();
        } catch (const std::exception& e) {
            std::string errorDetail = profile.address + ":" + profile.port + " (" + profile.configtype + ") - " + e.what();
            Logger::write("CONFIG_ERROR: " + profile.indexid + " - " + errorDetail, LogLevel::ERR);
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                failedCount_++;
                processedCount_++;
            }
            exItemDao.updateTestResult(profile.indexid, -1, false, "CONFIG_ERROR");
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
                Logger::write("[Worker-" + std::to_string(workerId) + "] 注入xray outbound 错误: " + xrayApi.getLastError(), LogLevel::ERR);
                Logger::write("[Worker-" + std::to_string(workerId) + "] XRAY_ERROR - " + profile.indexid + " (tag=" + tag + ") - " + xrayApi.getLastError(), LogLevel::ERR);
                Logger::write("  Xray output: " + addResult, LogLevel::ERR);
                Logger::write("  Outbound JSON: " + config.outbound_json, LogLevel::ERR);
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    failedCount_++;
                    processedCount_++;
                }
                exItemDao.updateTestResult(profile.indexid, -1, false, "XRAY_ERROR");
                continue;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            
            auto result = proxyTester_->test(socksPort);
            
            int currentNum;
            if (result.success) {
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    currentNum = ++processedCount_;
                    successCount_++;
                }
                Logger::write(std::string("[Worker-" + std::to_string(workerId) + "] [") + std::to_string(currentNum) + "/" + std::to_string(totalProxies_) + "] " + profile.address + ":" + profile.port +
                          " (" + utils::getProtocolName(profile.configtype) + ") OK " + std::to_string(result.latencyMs) + "ms", LogLevel::INFO);
            } else {
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    currentNum = ++processedCount_;
                    failedCount_++;
                }
                Logger::write(std::string("[Worker-" + std::to_string(workerId) + "] [") + std::to_string(currentNum) + "/" + std::to_string(totalProxies_) + "] " + profile.address + ":" + profile.port +
                          " (" + utils::getProtocolName(profile.configtype) + ") FAIL " + result.errorMsg, LogLevel::INFO);
            }
            
            exItemDao.updateTestResult(profile.indexid, result.latencyMs, result.success, result.errorMsg);
            
} catch (const std::exception& e) {
            Logger::write("[Worker-" + std::to_string(workerId) + "] Exception: " + e.what(), LogLevel::ERR);
            Logger::write("[Worker-" + std::to_string(workerId) + "] failed to build conf: " + e.what(), LogLevel::ERR);
            Logger::write("[Worker-" + std::to_string(workerId) + "] EXCEPTION - " + profile.indexid + " - " + e.what(), LogLevel::ERR);
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                failedCount_++;
                processedCount_++;
            }
            exItemDao.updateTestResult(profile.indexid, -1, false, e.what());
        }
    }
}

void ProxyBatchTester::testProxiesMultiThreaded() {
    auto portPairs = xrayManager_->getPortPairs();
    int numWorkers = static_cast<int>(portPairs.size());
    
    for (int i = 0; i < totalProxies_; ++i) {
        proxiesQueue_.push(i);
    }
    
    std::vector<std::thread> threads;
    for (int i = 0; i < numWorkers; ++i) {
        int socksPort = portPairs[i].first;
        int apiPort = portPairs[i].second;
        threads.emplace_back(&ProxyBatchTester::workerThreadFunc, this, i, socksPort, apiPort);
    }
    
    for (auto& t : threads) {
        t.join();
    }
}

void ProxyBatchTester::printSummary() {
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Total: " + std::to_string(totalProxies_), LogLevel::REPORT);
    Logger::write("Success: " + std::to_string(successCount_), LogLevel::REPORT);
    Logger::write("Failed: " + std::to_string(failedCount_), LogLevel::REPORT);
    Logger::write("========================================", LogLevel::REPORT);
}

bool ProxyBatchTester::run() {
    proxies_ = loadProxies();
    totalProxies_ = static_cast<int>(proxies_.size());
    
    if (totalProxies_ == 0) {
        Logger::write("No proxies to test", LogLevel::WARN);
        return false;
    }
    
    Logger::write("Testing " + std::to_string(totalProxies_) + " proxies total", LogLevel::REPORT);
    
    int instanceCount = calculateXrayInstanceCount(totalProxies_);
    if (!startXrayInstances(instanceCount)) {
        Logger::write("Failed to start xray instances", LogLevel::WARN);
        return false;
    }

    if (config_.log_network_failures) {
        Logger::write("Started " + std::to_string(instanceCount) + " xray instances", LogLevel::INFO);
    }

    testProxiesMultiThreaded();
    printSummary();

    xrayManager_->stopAll();
    return true;
}

bool ProxyBatchTester::runWithSubId(const std::string& subId) {
    proxies_ = loadProxies(subId);
    totalProxies_ = static_cast<int>(proxies_.size());
    
if (totalProxies_ == 0) {
        Logger::write("No proxies to test for subscription: " + subId, LogLevel::WARN);
        return false;
    }

    Logger::write("Testing " + std::to_string(totalProxies_) + " proxies from subscription: " + subId, LogLevel::INFO);
    Logger::write("Testing " + std::to_string(totalProxies_) + " proxies total", LogLevel::REPORT);
    if (config_.log_network_failures) {
        Logger::write("Testing " + std::to_string(totalProxies_) + " proxies from subscription: " + subId, LogLevel::INFO);
    }
    
    int instanceCount = calculateXrayInstanceCount(totalProxies_);
    if (!startXrayInstances(instanceCount)) {
        Logger::write("Failed to start xray instances", LogLevel::WARN);
        return false;
    }
    
    if (config_.log_network_failures) {
        Logger::write("Started " + std::to_string(instanceCount) + " xray instances", LogLevel::INFO);
    }
    
    testProxiesMultiThreaded();
    printSummary();
    
    xrayManager_->stopAll();
    return true;
}