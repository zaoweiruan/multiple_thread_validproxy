#ifndef XRAY_MANAGER_H
#define XRAY_MANAGER_H

#include <string>
#include <vector>
#include <memory>
#include "XrayInstance.h"

class XrayManager {
public:
    XrayManager(const std::string& xrayPath, const std::string& configDir);
    ~XrayManager();
    
    int start(int count, int startPort, int apiPort);
    void stopAll();
    XrayInstance* getInstance(int index);
    int getInstanceCount() const;
    std::vector<std::pair<int, int>> getPortPairs();

private:
    std::vector<std::unique_ptr<XrayInstance>> instances_;
    std::string xrayPath_;
    std::string configDir_;
};

#endif // XRAY_MANAGER_H