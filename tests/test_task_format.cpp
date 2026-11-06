// tests/test_task_format.cpp — steady_clock stamp → elapsed seconds
// (spec 2026-11-03 v1.2 §3.7 / §5.2 T8)
//
// TaskHandle::createdAt/startedAt store steady_clock::now().time_since_epoch().count()
// as a decimal string (TaskQueue.cpp). steady_clock has no wall-clock epoch, so the
// UI shows *relative* time only.
#include <gtest/gtest.h>

#include <chrono>
#include <ctime>
#include <string>

#include "task/TaskFormat.h"

namespace {

std::string stampSecondsAgo(std::chrono::steady_clock::time_point now, long long seconds) {
    const std::chrono::steady_clock::time_point then = now - std::chrono::seconds(seconds);
    return std::to_string(then.time_since_epoch().count());
}

} // namespace

TEST(TaskFormatTest, ElapsedSecondsForRecentStamp) {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    EXPECT_EQ(task::elapsedSecondsSince(stampSecondsAgo(now, 12), now), 12);
    EXPECT_EQ(task::elapsedSecondsSince(stampSecondsAgo(now, 0), now), 0);
    EXPECT_EQ(task::elapsedSecondsSince(stampSecondsAgo(now, 3600), now), 3600);
}

TEST(TaskFormatTest, EmptyOrGarbageStampReturnsMinusOne) {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    EXPECT_EQ(task::elapsedSecondsSince("", now), -1);
    EXPECT_EQ(task::elapsedSecondsSince("abc", now), -1);
    EXPECT_EQ(task::elapsedSecondsSince("12abc", now), -1);   // partial parse must be rejected
    EXPECT_EQ(task::elapsedSecondsSince("99999999999999999999999999", now), -1);  // out of range
}

TEST(TaskFormatTest, FutureStampClampsToZero) {
    // Recorded in the future (or a mismatched clock) must never render "-5s".
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const std::string future = std::to_string(
        (now + std::chrono::seconds(30)).time_since_epoch().count());
    EXPECT_EQ(task::elapsedSecondsSince(future, now), 0);
}

namespace {

// Independent expectation builder: format wall-clock t directly (HH:MM:SS).
std::string fmtWall(std::chrono::system_clock::time_point t) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(t);
    const std::tm* lt = std::localtime(&tt);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", lt);
    return std::string(buf);
}

} // namespace

TEST(TaskFormatTest, WallClockTextMapsSteadyStampToWallTime) {
    // A fixed pair of clocks: the mapping is exactly "wallNow - elapsed".
    const std::chrono::system_clock::time_point wallNow = std::chrono::system_clock::now();
    const std::chrono::steady_clock::time_point steadyNow = std::chrono::steady_clock::now();

    // Stamp taken 3661s (1h01m01s) before steadyNow → wall time = wallNow-3661s.
    const std::string stamp = std::to_string(
        (steadyNow - std::chrono::seconds(3661)).time_since_epoch().count());
    const std::string got = task::wallClockText(stamp, steadyNow, wallNow);
    EXPECT_EQ(got, fmtWall(wallNow - std::chrono::seconds(3661)));
    EXPECT_EQ(got.size(), 8u);           // "HH:MM:SS"
    EXPECT_EQ(got[2], ':');
    EXPECT_EQ(got[5], ':');
}

TEST(TaskFormatTest, WallClockTextZeroElapsedEqualsWallNow) {
    const std::chrono::system_clock::time_point wallNow = std::chrono::system_clock::now();
    const std::chrono::steady_clock::time_point steadyNow = std::chrono::steady_clock::now();
    const std::string stamp = std::to_string(steadyNow.time_since_epoch().count());
    EXPECT_EQ(task::wallClockText(stamp, steadyNow, wallNow), fmtWall(wallNow));
}

TEST(TaskFormatTest, WallClockTextGarbageReturnsDash) {
    const std::chrono::system_clock::time_point wallNow = std::chrono::system_clock::now();
    const std::chrono::steady_clock::time_point steadyNow = std::chrono::steady_clock::now();
    EXPECT_EQ(task::wallClockText("", steadyNow, wallNow), "- -");
    EXPECT_EQ(task::wallClockText("xyz", steadyNow, wallNow), "- -");
    EXPECT_EQ(task::wallClockText("12xyz", steadyNow, wallNow), "- -");
}
