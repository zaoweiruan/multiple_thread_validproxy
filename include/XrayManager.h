#ifndef XRAY_MANAGER_H
#define XRAY_MANAGER_H

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include "XrayInstance.h"

class XrayManager {
public:
    static XrayManager* getInstance();
    static XrayManager* getInstance(const std::string& xrayPath, const std::string& configDir, int workers);
    static void release();
    
    XrayManager(const std::string& xrayPath, const std::string& configDir, int workers = 4);
    ~XrayManager();
    
    int start(int testCount, int startPort, int apiPort);
    int start(int testCount);
    void stopAll();
    XrayInstance* getInstance(int index);
    int getInstanceCount() const;
    std::vector<std::pair<int, int>> getPortPairs();
    int getWorkers() const { return workers_; }
    bool isRunning() const { return getInstanceCount() > 0; }

    // Lifecycle management: locate the instance bound to apiPort and evaluate
    // whether its process is still healthy. If the process is dead or hung
    // (API port unresponsive), the instance is stopped, removed from the pool,
    // its ports freed, and a fresh instance is relaunched immediately with the
    // original config (same ports → same config file). Returns true if the
    // instance was found and successfully relaunched (ready to accept tests);
    // false if no instance is bound to apiPort or the relaunch failed.
    bool evaluateInstanceHealth(int apiPort);

 private:
    std::vector<std::unique_ptr<XrayInstance>> instances_;
    std::string xrayPath_;
    std::string configDir_;
    int workers_;

    // Running flag for lifecycle tracking
    bool stopped_{false};

    // Protects instances_ (T1.5). Never hold this lock while calling
    // XrayInstance::start() (contains 2s sleep) — it is only taken for
    // short read/commit sections and around stop()/clear in stopAll().
    mutable std::mutex instancesMutex_;

    static XrayManager* instance_;
    static std::mutex instanceMutex_;
};

#endif // XRAY_MANAGER_H