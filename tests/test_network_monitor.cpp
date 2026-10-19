#include <gtest/gtest.h>
#include "NetworkMonitor.h"
#include "test_utils.h"
#include <thread>
#include <chrono>

static const std::vector<std::string> kBadUrls = {
    "http://127.0.0.1:1"
};

static const int kShortIntervalMs = 50;
static const int kShortTimeoutMs = 50;
static const int kWaitMs = 500;

TEST(NetworkMonitorTest, EnabledFalse_ReturnsTrue) {
    NetworkMonitor nm(false);
    EXPECT_TRUE(nm.IsConnected());
    EXPECT_FALSE(nm.IsEnabled());
}

TEST(NetworkMonitorTest, IsConnected_Default_False) {
    NetworkMonitor nm;
    EXPECT_FALSE(nm.IsConnected());
    EXPECT_TRUE(nm.IsEnabled());
}

TEST(NetworkMonitorTest, StartStop_NoCrash) {
    NetworkMonitor nm;
    EXPECT_TRUE(nm.Start(kBadUrls, kShortIntervalMs, kShortTimeoutMs));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    nm.Stop();
}

TEST(NetworkMonitorTest, StartWithUnreachableUrl_ConnectedBecomesFalse) {
    NetworkMonitor nm;
    nm.Start(kBadUrls, kShortIntervalMs, kShortTimeoutMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(kWaitMs));
    nm.Stop();
    EXPECT_FALSE(nm.IsConnected());
}

TEST(NetworkMonitorTest, StopWithoutStart_NoCrash) {
    NetworkMonitor nm;
    nm.Stop();
}

TEST(NetworkMonitorTest, DoubleStop_NoCrash) {
    NetworkMonitor nm;
    nm.Start(kBadUrls, kShortIntervalMs, kShortTimeoutMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    nm.Stop();
    nm.Stop();
}

TEST(NetworkMonitorTest, RestartAfterStop_ResetsConnection) {
    NetworkMonitor nm;
    nm.Start(kBadUrls, kShortIntervalMs, kShortTimeoutMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(kWaitMs));
    nm.Stop();
    EXPECT_FALSE(nm.IsConnected());

    nm.Start(kBadUrls, kShortIntervalMs, kShortTimeoutMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(kWaitMs));
    nm.Stop();
    EXPECT_FALSE(nm.IsConnected());
}

TEST(NetworkMonitorTest, StartWithEmptyUrls_ConnectedBecomesFalse) {
    NetworkMonitor nm;
    std::vector<std::string> emptyUrls;
    nm.Start(emptyUrls, kShortIntervalMs, kShortTimeoutMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(kWaitMs));
    nm.Stop();
    EXPECT_FALSE(nm.IsConnected());
}

TEST(NetworkMonitorTest, LoggingOnConnectionLost) {
    LogCapture capture;
    {
        NetworkMonitor nm;

        // Phase 1: Connect to a reachable URL to transition to connected state
        nm.Start({"https://www.example.com"}, kShortIntervalMs, 3000);
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));

        // Phase 2: If connected, switch to bad URLs to trigger the LOST transition
        if (nm.IsConnected()) {
            nm.Start(kBadUrls, kShortIntervalMs, kShortTimeoutMs);
            std::this_thread::sleep_for(std::chrono::milliseconds(kWaitMs));
        }
        nm.Stop();
    }

    bool foundLost = false;
    std::vector<LogCapture::Entry> entries = capture.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].message.find("LOST") != std::string::npos &&
            entries[i].level == LogLevel::ERR) {
            foundLost = true;
            break;
        }
    }
    EXPECT_TRUE(foundLost);
}

// --- F8: majority vote so a single flaky URL cannot declare an outage ---------

TEST(NetworkMonitorTest, MajorityVerdict_TwoOfThreeStaysConnected) {
    // The jitter regression: 2 of 3 check URLs reachable must stay connected.
    // Before F8 any single failure meant disconnected.
    EXPECT_TRUE(NetworkMonitor::majorityVerdict(2, 1, 3));
}

TEST(NetworkMonitorTest, MajorityVerdict_OneOfThreeIsDown) {
    EXPECT_FALSE(NetworkMonitor::majorityVerdict(1, 2, 3));
}

TEST(NetworkMonitorTest, MajorityVerdict_AllProbesAbstainedStaysConnected) {
    // All-DNS-failure round: legacy semantics (2026-08-06 fix) must be preserved.
    EXPECT_TRUE(NetworkMonitor::majorityVerdict(0, 0, 3));
}

TEST(NetworkMonitorTest, MajorityVerdict_NoUrlsIsDown) {
    EXPECT_FALSE(NetworkMonitor::majorityVerdict(0, 0, 0));
}

TEST(NetworkMonitorTest, MajorityVerdict_SingleUrlNeedsSuccess) {
    EXPECT_TRUE(NetworkMonitor::majorityVerdict(1, 0, 1));
    EXPECT_FALSE(NetworkMonitor::majorityVerdict(0, 1, 1));
}

TEST(NetworkMonitorTest, ConsecutiveFailures_CountsCorrectly) {
    NetworkMonitor nm;
    nm.resetProbeCount();

    nm.Start(kBadUrls, kShortIntervalMs, kShortTimeoutMs);
    std::this_thread::sleep_for(std::chrono::milliseconds(kWaitMs * 2));
    nm.Stop();

    // Should have some consecutive failures recorded
    int failures = nm.getConsecutiveFailures();
    EXPECT_GE(failures, 1);
}
