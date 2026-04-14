#include "XrayManager.h"
#include "PortManager.h"
#include <iostream>
#include <thread>
#include <chrono>

XrayManager::XrayManager(const std::string& xrayPath, const std::string& configDir, std::ofstream* logOut)
    : xrayPath_(xrayPath), configDir_(configDir), logOut_(logOut) {}

XrayManager::~XrayManager() {
    stopAll();
}

int XrayManager::start(int count, int startPort, int apiPort) {
    std::cout << "XrayManager::start: count=" << count << ", startPort=" << startPort << ", apiPort=" << apiPort << ", configDir=" << configDir_ << std::endl;
    if (logOut_) {
        *logOut_ << "XrayManager::start: count=" << count << ", startPort=" << startPort << ", apiPort=" << apiPort << ", configDir=" << configDir_ << std::endl;
    }
    
    std::vector<int> usedPorts;
    int actualCount = 0;
    
    for (int i = 0; i < count; ++i) {
        int socksPort = PortManager::findAvailable(startPort + i, 1000);
        int apiPortAddr = PortManager::findAvailable(apiPort + i, 1000);
        
        std::cout << "XrayManager: trying ports socks=" << socksPort << ", api=" << apiPortAddr << std::endl;
        
        if (socksPort <= 0 || apiPortAddr <= 0) {
            std::cerr << "XrayManager: failed to find available ports" << std::endl;
            break;
        }
        
        auto instance = std::make_unique<XrayInstance>(xrayPath_, socksPort, apiPortAddr, configDir_);
        if (instance->start()) {
            instances_.push_back(std::move(instance));
            actualCount++;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } else {
            std::cerr << "XrayManager: failed to start instance " << i << std::endl;
            if (logOut_) {
                *logOut_ << "XrayManager: failed to start instance " << i << std::endl;
            }
        }
    }
    std::cout << "XrayManager::start: actualCount=" << actualCount << std::endl;
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