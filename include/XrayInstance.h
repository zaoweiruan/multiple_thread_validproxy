#ifndef XRAY_INSTANCE_H
#define XRAY_INSTANCE_H

#include <string>
#include <windows.h>

class XrayInstance {
public:
    XrayInstance(const std::string& xrayPath, int socksPort, int apiPort, const std::string& configDir);
    ~XrayInstance();
    
    bool start();
    void stop();
    bool isRunning() const;
    int getSocksPort() const;
    int getApiPort() const;
    std::string getConfigPath() const;

private:
    std::string xrayPath_;
    int socksPort_;
    int apiPort_;
    std::string configPath_;
    HANDLE processHandle_;
    HANDLE jobObject_;
    bool running_;
    
    bool createConfigFile();
};

#endif // XRAY_INSTANCE_H