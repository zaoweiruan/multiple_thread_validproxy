#include "XrayManager.h"
#include "PortManager.h"
#include "Logger.h"
#include <thread>
#include <chrono>

XrayManager* XrayManager::instance_ = nullptr;
std::mutex XrayManager::instanceMutex_;

XrayManager* XrayManager::getInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    return instance_;
}

XrayManager* XrayManager::getInstance(const std::string& xrayPath, const std::string& configDir, int workers) {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (!instance_) {
        instance_ = new XrayManager(xrayPath, configDir, workers);
    }
    return instance_;
}

void XrayManager::release() {
    std::lock_guard<std::mutex> lock(instanceMutex_);
    if (instance_) {
        instance_->stopAll();
        delete instance_;
        instance_ = nullptr;
    }
}

XrayManager::XrayManager(const std::string& xrayPath, const std::string& configDir, int workers)
    : xrayPath_(xrayPath), configDir_(configDir), workers_(workers) {}

XrayManager::~XrayManager() {
    stopAll();
}

int XrayManager::start(int testCount) {
    return start(testCount, 1080, 10080);
}

int XrayManager::start(int count, int startPort, int apiPort) {
    if (count > workers_) {
        count = workers_;
    }
    
    Logger::write("XrayManager::start: count=" + std::to_string(count) + ", startPort=" + std::to_string(startPort) + ", apiPort=" + std::to_string(apiPort) + ", configDir=" + configDir_, LogLevel::REPORT);
    
    std::vector<int> usedPorts;
    int actualCount = 0;
    
    for (int i = 0; i < count; ++i) {
        int socksPort = PortManager::findAvailable(startPort + i, 1000);
        int apiPortAddr = PortManager::findAvailable(apiPort + i, 1000);
        
        if (socksPort <= 0 || apiPortAddr <= 0) {
            Logger::write("XrayManager: failed to find available ports", LogLevel::ERR);
            break;
        }
        
        auto instance = std::make_unique<XrayInstance>(xrayPath_, socksPort, apiPortAddr, configDir_);
        if (instance->start()) {
            instances_.push_back(std::move(instance));
            actualCount++;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } else {
            Logger::write("XrayManager: failed to start instance " + std::to_string(i), LogLevel::WARN);
        }
    }
    Logger::write("XrayManager::start: actualCount=" + std::to_string(actualCount), LogLevel::REPORT);
    return actualCount;
}

void XrayManager::stopAll() {
    for (auto& inst : instances_) {
        inst->stop();
    }
    instances_.clear();
}

XrayInstance* XrayManager::getInstance(int index) {
    if (index >= 0 && index < static_cast<int>(instances_.size())) {
        return instances_[index].get();
    }
    return nullptr;
}

int XrayManager::getInstanceCount() const {
    return static_cast<int>(instances_.size());
}

std::vector<std::pair<int, int>> XrayManager::getPortPairs() {
    std::vector<std::pair<int, int>> pairs;
    for (auto& inst : instances_) {
        pairs.push_back({inst->getSocksPort(), inst->getApiPort()});
    }
    return pairs;
}