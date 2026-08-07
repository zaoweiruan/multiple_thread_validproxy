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
        bool allOk = true;

        for (size_t i = 0; i < urls_.size(); ++i) {
            if (stopRequested_) break;

            const std::string& url = urls_[i];

            ProbeResult pr = CheckURLWithDnsFlag(url, checkTimeoutMs_);
            if (!pr.success) {
                if (pr.isDnsError) {
                    // DNS error: do NOT increment consecutiveFailures_,
                    // but count dnsFailures separately for diagnostics
                    dnsFailures_.fetch_add(1);
                    continue;
                }
                allOk = false;
                break;
            }
        }

        if (urls_.empty()) {
            allOk = false;
        }

        bool prev = connected_.exchange(allOk);
        bool afterFirst = firstCheckDone_.exchange(true);

        if (afterFirst && prev && !allOk) {
            // LOST — increment consecutive failures (DNS errors are excluded above)
            int fails = (consecutiveFailures_.load() < maxProbes_) ? consecutiveFailures_.fetch_add(1) + 1 : maxProbes_;

            if (probeEnabled_ && fails < maxProbes_) {
                Logger::write("Network connection LOST (probe " + std::to_string(fails) +
                              "/" + std::to_string(maxProbes_) + ")", LogLevel::ERR);
            } else if (fails >= maxProbes_) {
                Logger::write("Network connection LOST (threshold reached: " + std::to_string(fails) +
                              "/" + std::to_string(maxProbes_) + ")", LogLevel::ERR);
                if (cancelOnDisconnect_) {
                    cancelOnDisconnect_->store(true);
                    Logger::write("[NetworkMonitor] cancelOnDisconnect triggered after " +
                                  std::to_string(fails) + " failed probes", LogLevel::ERR);
                }
                consecutiveFailures_.store(maxProbes_);
            }
        } else if (afterFirst && !prev && allOk) {
            // RESTORED — reset probe counter and DNS failure counter.
            // DNS resolution deduplication is handled entirely by the
            // program-wide DnsShareCache in CurlEasyHandle (CURLOPT_SHARE
            // + CURLOPT_DNS_CACHE_TIMEOUT=-1), so there is no per-monitor
            // cache to clear here.
            consecutiveFailures_.store(0);
            dnsFailures_.store(0);
            Logger::write("Network connection RESTORED (probes reset)", LogLevel::ERR);
        } else if (afterFirst && !allOk) {
            // Already disconnected — continue counting consecutive failures
            int fails = (consecutiveFailures_.load() < maxProbes_) ? consecutiveFailures_.fetch_add(1) + 1 : maxProbes_;

            if (probeEnabled_ && fails < maxProbes_) {
                Logger::write("Network connection LOST (probe " + std::to_string(fails) +
                              "/" + std::to_string(maxProbes_) + ")", LogLevel::ERR);
            } else {
                Logger::write("Network connection LOST (probe " + std::to_string(fails) +
                              "/" + std::to_string(maxProbes_) + ")", LogLevel::ERR);
                if (cancelOnDisconnect_ && !cancelOnDisconnect_->load()) {
                    cancelOnDisconnect_->store(true);
                    Logger::write("[NetworkMonitor] cancelOnDisconnect triggered after " +
                                  std::to_string(fails) + " failed probes", LogLevel::ERR);
                }
                consecutiveFailures_.store(maxProbes_);
            }
        } else if (!afterFirst && !allOk) {
            // First check failed before any connection was established
            int fails = (consecutiveFailures_.load() < maxProbes_) ? consecutiveFailures_.fetch_add(1) + 1 : maxProbes_;
            if (probeEnabled_ && fails < maxProbes_) {
                Logger::write("Network connection check failed (probe " + std::to_string(fails) +
                              "/" + std::to_string(maxProbes_) + ")", LogLevel::ERR);
            } else {
                Logger::write("Network connection check failed (probe " + std::to_string(fails) +
                              "/" + std::to_string(maxProbes_) + ")", LogLevel::ERR);
                if (cancelOnDisconnect_) {
                    cancelOnDisconnect_->store(true);
                }
            }
        }

        int slept = 0;
        while (slept < checkIntervalMs_ && !stopRequested_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            slept += 200;
        }
    }
}
