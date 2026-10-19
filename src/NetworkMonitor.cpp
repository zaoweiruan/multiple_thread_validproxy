#include "NetworkMonitor.h"
#include "CurlEasyHandle.h"
#include "Logger.h"

#include <curl/curl.h>

#include <chrono>
#include <thread>

NetworkMonitor::NetworkMonitor(bool enabled) : enabled_(enabled) {}

NetworkMonitor::~NetworkMonitor() {
    Stop();
}

bool NetworkMonitor::Start(const std::vector<std::string>& urls,
                           int checkIntervalMs,
                           int checkTimeoutMs) {
    Stop();
    urls_ = urls;
    checkIntervalMs_ = checkIntervalMs;
    checkTimeoutMs_ = checkTimeoutMs;
    stopRequested_ = false;
    thread_ = std::thread(&NetworkMonitor::ThreadLoop, this);
    return true;
}

void NetworkMonitor::Stop() {
    stopRequested_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool NetworkMonitor::majorityVerdict(int okCount, int netFailCount, int totalUrls) {
    if (totalUrls <= 0) {
        return false;  // nothing to probe -> treat as down (pre-existing behaviour)
    }
    const int decisive = okCount + netFailCount;
    if (decisive == 0) {
        // Every probe abstained (DNS only). Legacy behaviour: stay connected.
        return true;
    }
    return okCount > netFailCount;
}

bool NetworkMonitor::IsConnected() const {
    if (!enabled_) return true;
    return connected_;
}

bool NetworkMonitor::IsEnabled() const {
    return enabled_;
}

/// Check whether a failed probe was due to DNS resolution.
/// Returns true if the curl error is a DNS-related code.
static bool isDnsError(CURLcode code) {
    return (code == CURLE_COULDNT_RESOLVE_HOST ||
            code == CURLE_COULDNT_RESOLVE_PROXY);
}

NetworkMonitor::ProbeResult NetworkMonitor::CheckURLWithDnsFlag(const std::string& url, int timeoutMs) {
    NetworkMonitor::ProbeResult result{false, false};
    try {
        CurlEasyHandle curl;
        if (!curl.valid()) return result;

        curl.setUrl(url)
            .setNoBody(true)
            .setTimeoutMs(static_cast<long>(timeoutMs))
            .setConnectTimeoutMs(static_cast<long>(timeoutMs / 2))
            .setFollowLocation(true);

        CURLcode res = curl_easy_perform(curl.get());
        if (res != CURLE_OK) {
            result.isDnsError = isDnsError(res);
            if (!result.isDnsError) {
                Logger::write("NetworkMonitor: probe failed url=" + url +
                                  " curl_error=" + std::to_string(static_cast<int>(res)) +
                                  " (" + curl_easy_strerror(res) + ")",
                              LogLevel::WARN);
            } else {
                Logger::write("NetworkMonitor: DNS error url=" + url +
                                  " curl_error=" + std::to_string(static_cast<int>(res)) +
                                  " (" + curl_easy_strerror(res) + ")",
                              LogLevel::WARN);
            }
            return result;
        }

        long httpCode = curl.getResponseCode();
        if (httpCode >= 200 && httpCode < 400) {
            result.success = true;
        } else {
            Logger::write("NetworkMonitor: probe url=" + url +
                              " http_code=" + std::to_string(httpCode),
                          LogLevel::WARN);
        }
        return result;
    } catch (...) {
        return result;
    }
}

void NetworkMonitor::ThreadLoop() {
    while (!stopRequested_) {
        // F8 (2026-10-19): probe EVERY url instead of breaking on the first
        // failure, then decide by majority. Previously a single flaky URL flipped
        // connected_ to false and left the remaining URLs unprobed, which made the
        // 30s outage gate far easier to reach than it looks.
        int okCount = 0;
        int netFailCount = 0;
        int probedCount = 0;

        for (size_t i = 0; i < urls_.size(); ++i) {
            if (stopRequested_) break;

            ++probedCount;
            const std::string& url = urls_[i];

            ProbeResult pr = CheckURLWithDnsFlag(url, checkTimeoutMs_);
            if (pr.success) {
                ++okCount;
            } else if (pr.isDnsError) {
                // DNS error: a neutral vote (jitter, not an outage). Counted for
                // diagnostics only; it never votes against connectivity.
                dnsFailures_.fetch_add(1);
            } else {
                ++netFailCount;
            }
        }

        const bool allOk = majorityVerdict(okCount, netFailCount, probedCount);

        bool prev = connected_.exchange(allOk);
        bool afterFirst = firstCheckDone_.exchange(true);

        if (afterFirst && prev && !allOk) {
            // LOST — count for diagnostics only. D2 was removed (2026-10-19):
            // NetworkMonitor no longer raises any cancellation flag; stopping the
            // batch is ProxyBatchTester's job (see
            // docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md).
            const int fails = consecutiveFailures_.fetch_add(1) + 1;
            Logger::write("Network connection LOST (consecutive failures: " +
                          std::to_string(fails) + ")", LogLevel::ERR);
        } else if (afterFirst && !prev && allOk) {
            // RESTORED — reset counters. DNS dedup is handled by the program-wide
            // DnsShareCache in CurlEasyHandle (CURLOPT_SHARE +
            // CURLOPT_DNS_CACHE_TIMEOUT=-1), so there is no per-monitor cache here.
            consecutiveFailures_.store(0);
            dnsFailures_.store(0);
            Logger::write("Network connection RESTORED", LogLevel::ERR);
        } else if (afterFirst && !allOk) {
            const int fails = consecutiveFailures_.fetch_add(1) + 1;
            Logger::write("Network connection LOST (consecutive failures: " +
                          std::to_string(fails) + ")", LogLevel::ERR);
        } else if (!afterFirst && !allOk) {
            const int fails = consecutiveFailures_.fetch_add(1) + 1;
            Logger::write("Network connection check failed (consecutive failures: " +
                          std::to_string(fails) + ")", LogLevel::ERR);
        }

        int slept = 0;
        while (slept < checkIntervalMs_ && !stopRequested_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            slept += 200;
        }
    }
}
