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

bool NetworkMonitor::CheckURL(const std::string& url, int timeoutMs) {
    try {
        CurlEasyHandle curl;
        if (!curl.valid()) return false;

        curl.setUrl(url)
            .setNoBody(true)
            .setTimeoutMs(static_cast<long>(timeoutMs))
            .setConnectTimeoutMs(static_cast<long>(timeoutMs / 2))
            .setFollowLocation(true);

        CURLcode res = curl_easy_perform(curl.get());
        if (res != CURLE_OK) return false;

        long httpCode = curl.getResponseCode();
        return (httpCode >= 200 && httpCode < 400);
    } catch (...) {
        return false;
    }
}

void NetworkMonitor::ThreadLoop() {
    while (!stopRequested_) {
        bool allOk = true;

        for (size_t i = 0; i < urls_.size(); ++i) {
            if (stopRequested_) break;

            if (!CheckURL(urls_[i], checkTimeoutMs_)) {
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
            // LOST — increment consecutive failures
            int fails = consecutiveFailures_.fetch_add(1) + 1;

            if (probeEnabled_ && fails < maxProbes_) {
                // Within probe window — log but don't cancel
                Logger::write("Network connection LOST (probe " + std::to_string(fails) +
                              "/" + std::to_string(maxProbes_) + ")", LogLevel::ERR);
            } else {
                // Exceeded max probes or probe disabled — trigger cancel
                Logger::write("Network connection LOST (probe " + std::to_string(fails) +
                              "/" + std::to_string(maxProbes_) + ")", LogLevel::ERR);
                if (cancelOnDisconnect_) {
                    cancelOnDisconnect_->store(true);
                    Logger::write("[NetworkMonitor] cancelOnDisconnect triggered after " +
                                  std::to_string(fails) + " failed probes", LogLevel::ERR);
                }
            }
        } else if (afterFirst && !prev && allOk) {
            // RESTORED — reset probe counter
            consecutiveFailures_.store(0);
            Logger::write("Network connection RESTORED (probes reset)", LogLevel::ERR);
        } else if (afterFirst && !allOk) {
            // Already disconnected — continue counting consecutive failures
            int fails = consecutiveFailures_.fetch_add(1) + 1;

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
                // Stop counting after cancel triggered to avoid log spam
                consecutiveFailures_.store(maxProbes_);
            }
        } else if (!afterFirst && !allOk) {
            // First check failed before any connection was established
            int fails = consecutiveFailures_.fetch_add(1) + 1;
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
