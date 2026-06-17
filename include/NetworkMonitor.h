#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <thread>

class NetworkMonitor {
public:
    NetworkMonitor(bool enabled = true);
    ~NetworkMonitor();

    NetworkMonitor(const NetworkMonitor&) = delete;
    NetworkMonitor& operator=(const NetworkMonitor&) = delete;

    bool Start(const std::vector<std::string>& urls,
               int checkIntervalMs,
               int checkTimeoutMs);
    void Stop();

bool IsConnected() const;
    bool IsEnabled() const;
    void setEnabled(bool enabled) { enabled_ = enabled; }

private:
    void ThreadLoop();
    bool CheckURL(const std::string& url, int timeoutMs);

    bool enabled_{true};
    std::vector<std::string> urls_;
    int checkIntervalMs_{10000};
    int checkTimeoutMs_{5000};
    std::atomic<bool> connected_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> firstCheckDone_{false};
    std::thread thread_;
};
