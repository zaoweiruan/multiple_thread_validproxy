#ifndef TEST_XRAY_WORKER_POOL_H
#define TEST_XRAY_WORKER_POOL_H

#include <string>
#include <vector>
#include <utility>
#include "XrayManager.h"

namespace test {

struct XrayInstanceConfig {
    std::string xrayExecutable;
    std::string configDir;
    int xrayWorkers;
    int startPort;
    int apiPort;
};

class XrayWorkerPool {
public:
    XrayWorkerPool(const XrayInstanceConfig& config);

    int calculateInstanceCount(int proxyCount) const;
    bool start(int count);
    std::vector<std::pair<int, int>> getPortPairs() const;
    void stopAll();

private:
    XrayInstanceConfig config_;
    XrayManager* manager_{nullptr};
    int startedCount_{0};
};

} // namespace test

#endif // TEST_XRAY_WORKER_POOL_H
