#include "test/XrayWorkerPool.h"
#include "Logger.h"
#include <algorithm>
#include <thread>
#include <chrono>

namespace test {

XrayWorkerPool::XrayWorkerPool(const XrayInstanceConfig& config)
    : config_(config), manager_(nullptr), startedCount_(0) {
}

int XrayWorkerPool::calculateInstanceCount(int proxyCount) const {
    if (proxyCount <= 0 || config_.xrayWorkers <= 0) return 0;
    return std::min(proxyCount, config_.xrayWorkers);
}

bool XrayWorkerPool::start(int count) {
    if (count <= 0) return false;

    manager_ = XrayManager::getInstance(config_.xrayExecutable, config_.configDir, config_.xrayWorkers);
    if (!manager_) return false;

    startedCount_ = manager_->start(count, config_.startPort, config_.apiPort);
    if (startedCount_ <= 0) return false;

    // Warm-up: give Xray processes time to open their gRPC API ports.
    // Worker threads call addOutbound immediately, and each xray api subprocess
    // can block up to 5s if gRPC isn't ready yet.
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    return true;
}

std::vector<std::pair<int, int>> XrayWorkerPool::getPortPairs() const {
    if (!manager_) return {};
    return manager_->getPortPairs();
}

void XrayWorkerPool::stopAll() {
    if (manager_) {
        manager_->stopAll();
    }
    startedCount_ = 0;
}

} // namespace test
