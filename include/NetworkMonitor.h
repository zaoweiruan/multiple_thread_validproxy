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

    /// Verdict of one probe round — pure, no I/O, unit-testable without a network.
    ///
    /// F8 (2026-10-19): a single flaky check URL must not be able to declare the
    /// whole network down. Requires strictly more successes than transport
    /// failures, instead of "every URL must succeed".
    ///
    /// DNS failures abstain (they are jitter, not an outage — see the
    /// 2026-08-06 probe-counter fix). If every probe abstained, the verdict stays
    /// connected, preserving the pre-F8 behaviour exactly.
    ///
    /// okCount      - probes that succeeded
    /// netFailCount - probes that failed with a transport error (not a DNS error)
    /// totalUrls    - number of configured check URLs; 0 means "nothing to probe"
    static bool majorityVerdict(int okCount, int netFailCount, int totalUrls);

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
    std::thread thread_;

    // Diagnostic probe state. Since D2 was removed (2026-10-19) NetworkMonitor is
    // a pure probe: these counters drive logging only and never raise a
    // cancellation. Stopping the batch belongs to ProxyBatchTester.
    std::atomic<int> consecutiveFailures_{0};   // non-DNS failures (diagnostic only)
    std::atomic<int> dnsFailures_{0};           // DNS-specific failures (neutral vote)
};
