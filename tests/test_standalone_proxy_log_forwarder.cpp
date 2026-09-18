#include "StandaloneProxyLogForwarder.h"

#include <gtest/gtest.h>

#include <windows.h>
#include <string>
#include <vector>

namespace {

struct Captured {
    LogLevel level;
    std::string message;
};

// Feeds `data` into an anonymous pipe (simulating a child process writing to its
// stdout/stderr), closes the write end, then runs the real forwarder and records
// every line it emits via the log callback.
std::vector<Captured> runForwarder(const std::string& data,
                                   const std::string& indexId) {
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    EXPECT_TRUE(CreatePipe(&readEnd, &writeEnd, &sa, 0));

    DWORD written = 0;
    EXPECT_TRUE(WriteFile(writeEnd, data.data(),
                          static_cast<DWORD>(data.size()), &written, nullptr));
    EXPECT_EQ(written, static_cast<DWORD>(data.size()));
    CloseHandle(writeEnd);  // signal EOF to the reader

    std::vector<Captured> out;
    standalone_proxy::forwardChildLog(
        readEnd, indexId,
        [&out](const std::string& msg, LogLevel lv) { out.push_back({lv, msg}); });
    return out;
}

}  // namespace

TEST(StandaloneProxyLogForwarderTest, ErrorLinesRoutedToErrAndPrefixed) {
    const std::string data =
        "2026-01-01T00:00:00 [info] xray core starting\r\n"
        "ERROR: configuration error: address already in use\r\n"
        "panic: runtime error\r\n"
        "failed to bind socket\r\n"
        "normal status line\r\n"
        "final line without newline";

    auto out = runForwarder(data, "42");

    ASSERT_EQ(out.size(), 6u);

    // [0] plain informational -> TRACE
    EXPECT_EQ(out[0].level, LogLevel::TRACE);
    EXPECT_EQ(out[0].message, "[xray:42] 2026-01-01T00:00:00 [info] xray core starting");

    // [1] contains "error" -> ERR
    EXPECT_EQ(out[1].level, LogLevel::ERR);
    EXPECT_EQ(out[1].message, "[xray:42] ERROR: configuration error: address already in use");

    // [2] contains "panic" -> ERR
    EXPECT_EQ(out[2].level, LogLevel::ERR);
    EXPECT_EQ(out[2].message, "[xray:42] panic: runtime error");

    // [3] contains "fail" -> ERR
    EXPECT_EQ(out[3].level, LogLevel::ERR);
    EXPECT_EQ(out[3].message, "[xray:42] failed to bind socket");

    // [4] plain -> TRACE
    EXPECT_EQ(out[4].level, LogLevel::TRACE);
    EXPECT_EQ(out[4].message, "[xray:42] normal status line");

    // [5] trailing line with no newline must still be emitted, and CR stripped
    EXPECT_EQ(out[5].level, LogLevel::TRACE);
    EXPECT_EQ(out[5].message, "[xray:42] final line without newline");

    // No captured message should retain a trailing carriage return.
    for (const auto& c : out) {
        EXPECT_FALSE(c.message.empty() || c.message.back() == '\r')
            << "unexpected trailing CR in: " << c.message;
        EXPECT_EQ(c.message.rfind("[xray:42] ", 0), 0u);
    }
}

TEST(StandaloneProxyLogForwarderTest, NullHandleIsNoOp) {
    std::vector<Captured> out;
    standalone_proxy::forwardChildLog(
        nullptr, "7",
        [&out](const std::string& msg, LogLevel lv) { out.push_back({lv, msg}); });
    EXPECT_TRUE(out.empty());
}
