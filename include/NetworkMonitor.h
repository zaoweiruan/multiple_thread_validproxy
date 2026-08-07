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

    /// Register an external flag that will be set to true when the network
    /// transitions from connected to disconnected. This allows the monitor
    /// to trigger cancellation in dependent operations (e.g. batch testing).
    void setCancelOnDisconnect(std::atomic<bool>* flag) { cancelOnDisconnect_ = flag; }

    /// Configure probe-on-disconnect behavior: wait for N consecutive failed
    /// checks before setting cancelOnDisconnect_ instead of acting on the first failure.
    /// If maxProbes > 0: use probe mode (grace window before cancel).
    /// If maxProbes == 0: immediate cancel on disconnect (legacy behavior).
    void setProbeOnDisconnect(int maxProbes) {
        maxProbes_ = maxProbes;
        probeEnabled_ = (maxProbes > 0);
    }

    int getConsecutiveFailures() const { return consecutiveFailures_.load(); }
    int getDnsFailures() const { return dnsFailures_.load(); }
    void resetProbeCount() { consecutiveFailures_ = 0; dnsFailures_ = 0; }

private:
    void ThreadLoop();

    /// Result of a single probe check.
    struct ProbeResult { bool success; bool isDnsError; };
    ProbeResult CheckURLWithDnsFlag(const std::string& url, int timeoutMs);

    bool enabled_{true};
    std::vector<std::string> urls_;
    int checkIntervalMs_{10000};
    int checkTimeoutMs_{5000};
    std::atomic<bool> connected_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> firstCheckDone_{false};
    std::atomic<bool>* cancelOnDisconnect_{nullptr};
    std::thread thread_;

    // Probe-on-disconnect state
    bool probeEnabled_{true};
    int  maxProbes_{3};
    std::atomic<int> consecutiveFailures_{0};   // non-DNS failures (triggers cancel)
    std::atomic<int> dnsFailures_{0};           // DNS-specific failures (does NOT trigger cancel)
};
