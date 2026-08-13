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
    // Last observed exit code of the child process (STILL_ACTIVE while alive).
    DWORD lastExitCode() const;

private:
    std::string xrayPath_;
    int socksPort_;
    int apiPort_;
    std::string configPath_;
    std::string stdoutLogPath_;
    std::string stderrLogPath_;
    HANDLE processHandle_;
    HANDLE jobObject_;
    HANDLE stdoutFile_;
    HANDLE stderrFile_;
    mutable std::atomic<bool> running_;
    mutable DWORD lastExitCode_;
    mutable std::mutex stateMutex_;
    
    static constexpr DWORD GRACEFUL_SHUTDOWN_MS = 500;
    static constexpr size_t DEATH_STDERR_TAIL_BYTES = 4096;
    
    bool createConfigFile();
    // Redirect child stdout/stderr into per-instance log files (CREATE_ALWAYS).
    bool openRedirectFiles();
    void closeRedirectFiles();
    // Log exit code + tail of the stderr redirect file; caches the code in lastExitCode_.
    void logDeathDetails(DWORD exitCode) const;
    // Read the trailing maxBytes of a file, truncated to a full line boundary.
    static std::string readFileTail(const std::string& path, size_t maxBytes);
};

#endif // XRAY_INSTANCE_H