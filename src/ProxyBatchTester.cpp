#include "ProxyBatchTester.h"
#include "ConfigGenerator.h"
#include <iostream>
#include <filesystem>
#include <algorithm>

ProxyBatchTester::ProxyBatchTester(sqlite3* db, const config::AppConfig& config)
    : db_(db), config_(config), totalProxies_(0), successCount_(0), failedCount_(0), processedCount_(0) {
    
    std::string baseDir = std::filesystem::current_path().string();
    std::string configDir = baseDir + "/config";
    
    xrayManager_ = new XrayManager(config_.xray_executable, configDir);
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
    db::models::ProfileexitemDAO exItemDao(db_);
    config::ConfigGenerator configGen(db_);
    
    while (true) {
        int profileIdx = -1;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (proxiesQueue_.empty()) break;
            profileIdx = proxiesQueue_.front();
            proxiesQueue_.pop();
        }
        
        if (profileIdx < 0 || profileIdx >= static_cast<int>(proxies_.size())) continue;
        
        const auto& profile = proxies_[profileIdx];
        int currentIdx = ++processedCount_;
        
        std::cout << "[Worker-" << workerId << "] [" << currentIdx << "/" << totalProxies_ << "] Testing: " 
                  << profile.address << ":" << profile.port << " (" << profile.remarks << ")" << std::endl;
        
        db::models::Profileitem configProfile = profile;
        
        try {
            configProfile.checkRequired();
        } catch (const std::exception& e) {
            std::cerr << "[Worker-" << workerId << "] Config error: " << e.what() << std::endl;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                failedCount_++;
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
                std::cerr << "[Worker-" << workerId << "] Xray API error: " << xrayApi.getLastError() << std::endl;
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    failedCount_++;
                }
                exItemDao.updateTestResult(profile.indexid, -1, false, "XRAY_ERROR");
                continue;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            
            auto result = proxyTester_->test(socksPort);
            
            if (result.success) {
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    successCount_++;
                }
                std::cout << "[Worker-" << workerId << "] Test SUCCESS - Latency: " << result.latencyMs << "ms" << std::endl;
            } else {
                std::cout << "[Worker-" << workerId << "] Test FAILED - Error: " << result.errorMsg << std::endl;
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    failedCount_++;
                }
            }
            
            exItemDao.updateTestResult(profile.indexid, result.latencyMs, result.success, result.errorMsg);
            
        } catch (const std::exception& e) {
            std::cerr << "[Worker-" << workerId << "] Exception: " << e.what() << std::endl;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                failedCount_++;
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
    std::cout << "========================================" << std::endl;
    std::cout << "Total: " << totalProxies_ << std::endl;
    std::cout << "Success: " << successCount_ << std::endl;
    std::cout << "Failed: " << failedCount_ << std::endl;
    std::cout << "========================================" << std::endl;
}

bool ProxyBatchTester::run() {
    proxies_ = loadProxies();
    totalProxies_ = static_cast<int>(proxies_.size());
    
    if (totalProxies_ == 0) {
        std::cerr << "No proxies to test" << std::endl;
        return false;
    }
    
    int instanceCount = calculateXrayInstanceCount(totalProxies_);
    if (!startXrayInstances(instanceCount)) {
        std::cerr << "Failed to start xray instances" << std::endl;
        return false;
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
        std::cerr << "No proxies to test for subscription: " << subId << std::endl;
        return false;
    }
    
    std::cout << "Testing " << totalProxies_ << " proxies from subscription: " << subId << std::endl;
    
    int instanceCount = calculateXrayInstanceCount(totalProxies_);
    if (!startXrayInstances(instanceCount)) {
        std::cerr << "Failed to start xray instances" << std::endl;
        return false;
    }
    
    testProxiesMultiThreaded();
    printSummary();
    
    xrayManager_->stopAll();
    return true;
}