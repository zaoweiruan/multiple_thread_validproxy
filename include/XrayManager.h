#ifndef XRAY_MANAGER_H
#define XRAY_MANAGER_H

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include "XrayInstance.h"

class XrayManager {
public:
    static XrayManager* getInstance();
    static XrayManager* getInstance(const std::string& xrayPath, const std::string& configDir, int workers, std::ofstream* logOut = nullptr);
    static void release();
    
    XrayManager(const std::string& xrayPath, const std::string& configDir, int workers = 4, std::ofstream* logOut = nullptr);
    ~XrayManager();
    
    int start(int testCount, int startPort, int apiPort);
    int start(int testCount);
    void stopAll();
    XrayInstance* getInstance(int index);
    int getInstanceCount() const;
    std::vector<std::pair<int, int>> getPortPairs();
    int getWorkers() const { return workers_; }
    bool isRunning() const { return getInstanceCount() > 0; }

private:
    std::vector<std::unique_ptr<XrayInstance>> instances_;
    std::string xrayPath_;
    std::string configDir_;
    int workers_;
    std::ofstream* logOut_;
    
    static XrayManager* instance_;
};

#endif // XRAY_MANAGER_H