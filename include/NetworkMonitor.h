#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <thread>

class NetworkMonitor {
public:
    NetworkMonitor();
    ~NetworkMonitor();

    NetworkMonitor(const NetworkMonitor&) = delete;
    NetworkMonitor& operator=(const NetworkMonitor&) = delete;

    bool Start(const std::vector<std::string>& urls,
               int checkIntervalMs,
               int checkTimeoutMs);
    void Stop();

    bool IsConnected() const;

private:
    void ThreadLoop();
    bool CheckURL(const std::string& url, int timeoutMs);

    std::vector<std::string> urls_;
    int checkIntervalMs_{10000};
    int checkTimeoutMs_{5000};
    std::atomic<bool> connected_{true};
    std::atomic<bool> stopRequested_{false};
    std::thread thread_;
};
