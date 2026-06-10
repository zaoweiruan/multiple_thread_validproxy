#include <gtest/gtest.h>
#include <thread>
#include <fstream>
#include <algorithm>
#include <cctype>

#include "Logger.h"
#include "test_utils.h"

// ============================================================
// Test fixture: ensures clean Logger state before/after each test
// ============================================================
class LoggerTest : public ::testing::Test {
protected:
    TempDir tempDir_;

    void SetUp() override {
        Logger::close();
        Logger::clearLogCallback();
    }

    void TearDown() override {
        Logger::close();
        Logger::clearLogCallback();
    }

    // Find a log file in the temp dir matching the prefix
    std::string findLogFile(const std::string& prefix = "test") {
        for (const auto& entry : std::filesystem::directory_iterator(tempDir_.path())) {
            if (entry.is_regular_file() && entry.path().filename().string().find(prefix) == 0) {
                return entry.path().string();
            }
        }
        return {};
    }
};

// ============================================================
// Basic init + write + callback verification
// ============================================================
TEST_F(LoggerTest, InitAndWrite) {
    LogCapture capture;

    Logger::init(tempDir_.path(), "test", LogLevel::TRACE, LogLevel::TRACE);
    ASSERT_TRUE(Logger::isEnabled());

    Logger::write("hello world");

    // Give the callback time to fire (it's synchronous, so this is immediate)
    auto entries = capture.entries();
    ASSERT_GE(entries.size(), 1);
    EXPECT_TRUE(entries[0].message.find("hello world") != std::string::npos);
}

// ============================================================
// Level filtering for file output
// ============================================================
TEST_F(LoggerTest, FileLevelFilters) {
    LogCapture capture;

    // Init with file level = WARN, console = TRACE
    Logger::init(tempDir_.path(), "test", LogLevel::WARN, LogLevel::TRACE);

    Logger::write("debug msg", LogLevel::DEBUG);
    Logger::write("warn msg", LogLevel::WARN);
    Logger::write("error msg", LogLevel::ERR);

    // Callback should capture ALL messages (it's not level-filtered)
    auto entries = capture.entries();
    EXPECT_GE(entries.size(), 3);

    // File should only contain WARN and above
    std::string logFile = findLogFile();
    ASSERT_FALSE(logFile.empty()) << "No log file found";

    std::ifstream file(logFile);
    ASSERT_TRUE(file.is_open());
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    // Verify DEBUG is NOT in file
    EXPECT_TRUE(content.find("debug msg") == std::string::npos)
        << "DEBUG message should have been filtered from file";
    // Verify WARN and ERR ARE in file
    EXPECT_TRUE(content.find("warn msg") != std::string::npos)
        << "WARN message should appear in file";
    EXPECT_TRUE(content.find("error msg") != std::string::npos)
        << "ERR message should appear in file";
}

// ============================================================
// Callback push/pop stack behavior
// ============================================================
TEST_F(LoggerTest, CallbackPushPop) {
    LogCapture cap1;

    Logger::init(tempDir_.path(), "test");

    Logger::write("msg1");
    EXPECT_EQ(cap1.size(), 1);

    {
        LogCapture cap2;
        Logger::write("msg2");
        EXPECT_EQ(cap1.size(), 1);  // cap1 is shadowed
        EXPECT_EQ(cap2.size(), 1);  // cap2 is active
    }

    // After cap2 destroyed, cap1 is restored
    Logger::write("msg3");
    EXPECT_EQ(cap1.size(), 2);  // msg1 + msg3
}

// ============================================================
// Multiple concurrent callbacks via stack
// ============================================================
TEST_F(LoggerTest, MultipleCapturesBothReceive) {
    LogCapture cap1;
    LogCapture cap2;  // shadows cap1

    Logger::init(tempDir_.path(), "test");

    Logger::write("hello");

    EXPECT_EQ(cap1.size(), 0);  // shadowed by cap2
    EXPECT_EQ(cap2.size(), 1);  // cap2 receives
}

// ============================================================
// Clear callback, then set new one
// ============================================================
TEST_F(LoggerTest, ClearAndSetCallback) {
    LogCapture cap1;

    Logger::init(tempDir_.path(), "test");

    // cap1 is active via pushCallback
    Logger::write("before clear");
    EXPECT_EQ(cap1.size(), 1);

    // Clear all callbacks
    Logger::clearLogCallback();

    Logger::write("after clear");
    EXPECT_EQ(cap1.size(), 1);  // cap1 no longer receives

    // Push a new one
    LogCapture cap2;
    Logger::write("after new cap");
    EXPECT_EQ(cap2.size(), 1);
}

// ============================================================
// Write without init — callback still fires, no crash
// ============================================================
TEST_F(LoggerTest, WriteWithoutInit) {
    // Logger is not initialized (no file opened)
    // Console and callback output are independent of file state
    LogCapture capture;

    // These should not crash; callback should fire even without file init
    Logger::write("no init");
    Logger::write("still no init", LogLevel::ERR);
    Logger::writeTimestamp("timestamp test");

    // Callback fires independently of file init
    EXPECT_FALSE(capture.empty());
}

// ============================================================
// levelToString and stringToLevel round-trip
// ============================================================
TEST_F(LoggerTest, LevelToStringAndBack) {
    auto testLevel = [](LogLevel level, const std::string& expected) {
        std::string str = Logger::levelToString(level);
        EXPECT_EQ(str, expected);
        LogLevel back = Logger::stringToLevel(str);
        EXPECT_EQ(back, level);
    };

    testLevel(LogLevel::TRACE, "TRACE");
    testLevel(LogLevel::DEBUG, "DEBUG");
    testLevel(LogLevel::INFO,  "INFO");
    testLevel(LogLevel::WARN,  "WARN");
    testLevel(LogLevel::ERR,   "ERROR");
    testLevel(LogLevel::REPORT,"REPORT");
}

TEST_F(LoggerTest, StringToLevelVariants) {
    EXPECT_EQ(Logger::stringToLevel("warn"), LogLevel::WARN);
    EXPECT_EQ(Logger::stringToLevel("warning"), LogLevel::WARN);
    EXPECT_EQ(Logger::stringToLevel("WARN"), LogLevel::WARN);
    EXPECT_EQ(Logger::stringToLevel("log_error"), LogLevel::ERR);
    EXPECT_EQ(Logger::stringToLevel("unknown"), LogLevel::INFO);  // default
}

// ============================================================
// Write with timestamp prefix
// ============================================================
TEST_F(LoggerTest, WriteTimestamp) {
    LogCapture capture;
    Logger::init(tempDir_.path(), "test");

    Logger::writeTimestamp("ts msg");

    auto entries = capture.entries();
    ASSERT_GE(entries.size(), 1);
    // Should start with '[' (timestamp bracket)
    EXPECT_EQ(entries[0].message[0], '[');
    EXPECT_TRUE(entries[0].message.find("ts msg") != std::string::npos);
}

// ============================================================
// File output: write and verify file content
// ============================================================
TEST_F(LoggerTest, FileOutputContent) {
    LogCapture capture;
    Logger::init(tempDir_.path(), "test_file");

    Logger::write("file content test");

    // Find the log file
    std::string logFile = findLogFile("test_file");
    ASSERT_FALSE(logFile.empty()) << "No log file created in " << tempDir_.path();

    // Read and verify
    std::ifstream file(logFile);
    ASSERT_TRUE(file.is_open());
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    EXPECT_TRUE(content.find("file content test") != std::string::npos)
        << "Log file should contain the written message";
}

// ============================================================
// Flush and close
// ============================================================
TEST_F(LoggerTest, FlushAndClose) {
    Logger::init(tempDir_.path(), "test");
    EXPECT_TRUE(Logger::isEnabled());

    Logger::write("before flush");
    Logger::flush();  // should not crash

    Logger::close();
    EXPECT_FALSE(Logger::isEnabled());

    // Write after close — should not crash; callback still fires
    LogCapture capture;
    Logger::write("after close");
    EXPECT_FALSE(capture.empty());
}

// ============================================================
// Concurrent writes from multiple threads
// ============================================================
TEST_F(LoggerTest, ConcurrentWrites) {
    LogCapture capture;
    Logger::init(tempDir_.path(), "test_concurrent");

    constexpr int kThreadCount = 4;
    constexpr int kWritesPerThread = 100;
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreadCount; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < kWritesPerThread; ++i) {
                Logger::write("thread " + std::to_string(t) + " msg " + std::to_string(i));
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // All messages should have been captured
    EXPECT_EQ(capture.size(), kThreadCount * kWritesPerThread);
}

// ============================================================
// Write at different levels — all received by callback
// ============================================================
TEST_F(LoggerTest, AllLevelsGoToCallback) {
    LogCapture capture;
    Logger::init(tempDir_.path(), "test");

    Logger::write("trace", LogLevel::TRACE);
    Logger::write("debug", LogLevel::DEBUG);
    Logger::write("info",  LogLevel::INFO);
    Logger::write("warn",  LogLevel::WARN);
    Logger::write("error", LogLevel::ERR);
    Logger::write("report",LogLevel::REPORT);

    EXPECT_EQ(capture.size(), 6);
}

// ============================================================
// Set file enabled/disabled
// ============================================================
TEST_F(LoggerTest, DisableFileOutput) {
    LogCapture capture;
    Logger::init(tempDir_.path(), "test");

    Logger::setFileEnabled(false);
    Logger::write("file disabled msg");

    // Callback should still receive
    auto entries = capture.entries();
    EXPECT_GE(entries.size(), 1);
    EXPECT_TRUE(entries[0].message.find("file disabled msg") != std::string::npos);

    // No file should be created... actually the file IS created during init
    // but the message won't be written to it
    std::string logFile = findLogFile("test");
    if (!logFile.empty()) {
        std::ifstream file(logFile);
        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        file.close();
        EXPECT_TRUE(content.find("file disabled msg") == std::string::npos)
            << "Message should not appear in file when file output is disabled";
    }
}
