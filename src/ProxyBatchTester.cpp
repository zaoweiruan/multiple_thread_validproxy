#include "ProxyBatchTester.h"
#include "ConfigGenerator.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <windows.h>

static std::string getExecutableDir() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path().string();
}

ProxyBatchTester::ProxyBatchTester(sqlite3* db, const config::AppConfig& config, const std::string& baseDir, std::ofstream* logOut)
    : db_(db), config_(config), totalProxies_(0), successCount_(0), failedCount_(0), processedCount_(0), logOut_(logOut) {
    
    std::string exeBaseDir = baseDir.empty() ? getExecutableDir() : baseDir;
    std::string configDir = exeBaseDir + "/config";
    
    std::filesystem::path configPath(configDir);
    if (!std::filesystem::exists(configPath)) {
        std::filesystem::create_directory(configPath);
    }
    
    xrayManager_ = new XrayManager(config_.xray_executable, configDir, logOut_);
    proxyTester_ = new ProxyTester(xrayManager_, config_.test_url, config_.test_timeout_ms);
}

ProxyBatchTester::~ProxyBatchTester() {
    delete proxyTester_;
    delete xrayManager_;
}

void ProxyBatchTester::writeLog(const std::string& msg) {
    if (logOut_ && logOut_->is_open()) {
        *logOut_ << msg << std::endl;
    }
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
        
        if (profileIdx < 0 || profileIdx >= static_cast<int>(proxies_.size())) {
            std::lock_guard<std::mutex> lock(queueMutex_);
            processedCount_++;
            continue;
        }
        
        const auto& profile = proxies_[profileIdx];
        
        std::cout << "[Worker-" << workerId << "] [" << (processedCount_ + 1) << "/" << totalProxies_ << "] Testing: " 
                  << profile.address << ":" << profile.port << " (" << profile.remarks << ")" << std::endl;
        if (config_.log_network_failures) {
            writeLog("[Worker-" + std::to_string(workerId) + "] Testing " + profile.address + ":" + profile.port + " (" + profile.remarks + ")");
        }
        
        db::models::Profileitem configProfile = profile;
        
        try {
            configProfile.checkRequired();
        } catch (const std::exception& e) {
            std::cerr << "[Worker-" << workerId << "] Config error: " << e.what() << std::endl;
            std::string errorDetail = profile.address + ":" + profile.port + " (" + profile.configtype + ") - " + e.what();
            writeLog("CONFIG_ERROR: " + profile.indexid + " - " + errorDetail);
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
                std::cerr << "[Worker-" << workerId << "] Xray API error: " << xrayApi.getLastError() << std::endl;
                writeLog("[Worker-" + std::to_string(workerId) + "] XRAY_ERROR - " + profile.indexid + " - " + xrayApi.getLastError());
                writeLog("  Xray output: " + addResult);
                writeLog("  Outbound JSON: " + config.outbound_json);
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
            
            if (result.success) {
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    successCount_++;
                    processedCount_++;
                }
                std::cout << "[Worker-" << workerId << "] Test SUCCESS - Latency: " << result.latencyMs << "ms" << std::endl;
                if (config_.log_network_failures) {
                    writeLog("[Worker-" + std::to_string(workerId) + "] Test SUCCESS - " + profile.indexid + " | Latency: " + std::to_string(result.latencyMs) + "ms");
                }
            } else {
                std::cout << "[Worker-" << workerId << "] Test FAILED - Error: " << result.errorMsg << std::endl;
                {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    failedCount_++;
                    processedCount_++;
                }
            }
            
            exItemDao.updateTestResult(profile.indexid, result.latencyMs, result.success, result.errorMsg);
            
        } catch (const std::exception& e) {
            std::cerr << "[Worker-" << workerId << "] Exception: " << e.what() << std::endl;
            writeLog("[Worker-" + std::to_string(workerId) + "] EXCEPTION - " + profile.indexid + " - " + e.what());
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
    std::cout << "========================================" << std::endl;
    std::cout << "Total: " << totalProxies_ << std::endl;
    std::cout << "Success: " << successCount_ << std::endl;
    std::cout << "Failed: " << failedCount_ << std::endl;
    std::cout << "========================================" << std::endl;
    writeLog("========================================");
    writeLog("Total: " + std::to_string(totalProxies_));
    writeLog("Success: " + std::to_string(successCount_));
    writeLog("Failed: " + std::to_string(failedCount_));
    writeLog("========================================");
}

bool ProxyBatchTester::run() {
    proxies_ = loadProxies();
    totalProxies_ = static_cast<int>(proxies_.size());
    
    if (totalProxies_ == 0) {
        std::cerr << "No proxies to test" << std::endl;
        writeLog("ERROR: No proxies to test");
        return false;
    }
    
    if (config_.log_network_failures) {
        writeLog("Loaded " + std::to_string(totalProxies_) + " proxies");
    }
    
    int instanceCount = calculateXrayInstanceCount(totalProxies_);
    if (!startXrayInstances(instanceCount)) {
        std::cerr << "Failed to start xray instances" << std::endl;
        writeLog("ERROR: Failed to start xray instances");
        return false;
    }
    
    if (config_.log_network_failures) {
        writeLog("Started " + std::to_string(instanceCount) + " xray instances");
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
        writeLog("ERROR: No proxies to test for subscription: " + subId);
        return false;
    }
    
    std::cout << "Testing " << totalProxies_ << " proxies from subscription: " << subId << std::endl;
    if (config_.log_network_failures) {
        writeLog("Testing " + std::to_string(totalProxies_) + " proxies from subscription: " + subId);
    }
    
    int instanceCount = calculateXrayInstanceCount(totalProxies_);
    if (!startXrayInstances(instanceCount)) {
        std::cerr << "Failed to start xray instances" << std::endl;
        writeLog("ERROR: Failed to start xray instances");
        return false;
    }
    
    if (config_.log_network_failures) {
        writeLog("Started " + std::to_string(instanceCount) + " xray instances");
    }
    
    testProxiesMultiThreaded();
    printSummary();
    
    xrayManager_->stopAll();
    return true;
}