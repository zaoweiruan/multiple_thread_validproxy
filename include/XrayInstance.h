#ifndef XRAY_INSTANCE_H
#define XRAY_INSTANCE_H

#include <string>
#include <mutex>
#include <atomic>
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
    std::atomic<bool> running_;
    mutable std::mutex stateMutex_;
    
    static constexpr DWORD GRACEFUL_SHUTDOWN_MS = 500;
    
    bool createConfigFile();
};

#endif // XRAY_INSTANCE_H