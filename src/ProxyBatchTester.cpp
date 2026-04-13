#include "ProxyBatchTester.h"
#include "ConfigGenerator.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>

ProxyBatchTester::ProxyBatchTester(sqlite3* db, const config::AppConfig& config)
    : db_(db), config_(config), totalProxies_(0), successCount_(0), failedCount_(0) {
    
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
    std::string sql = config_.sql_query;
    
    if (!subId.empty()) {
        size_t pos = sql.find("{subid}");
        if (pos != std::string::npos) {
            sql.replace(pos, 7, subId);
        }
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

void ProxyBatchTester::testProxiesMultiThreaded() {
    // Placeholder - to be implemented
    std::cout << "Multi-threaded testing not yet implemented" << std::endl;
}

void ProxyBatchTester::printSummary() {
    std::cout << "Total: " << totalProxies_ << ", Success: " << successCount_ << ", Failed: " << failedCount_ << std::endl;
}

bool ProxyBatchTester::run() {
    auto proxies = loadProxies();
    totalProxies_ = static_cast<int>(proxies.size());
    
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
    auto proxies = loadProxies(subId);
    totalProxies_ = static_cast<int>(proxies.size());
    
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