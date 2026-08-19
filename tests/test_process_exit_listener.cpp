// Tests for ProcessExitListener (R6 auto-takeover, crash tracking)
// and ProcessInspector (config-file matching, PEB read-failure fallback).
#include "ProcessExitListener.h"
#include "ProcessInspector.h"
#include "ProxyRuntimeHistory.h"
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <string>
#include <thread>
#include <chrono>
#include <windows.h>

HANDLE makeSignaledHandle() {
    return CreateEventA(nullptr, FALSE, FALSE, nullptr);
}

void signalHandle(HANDLE h) {
    SetEvent(h);
}

class ProcessExitListenerTest : public ::testing::Test {
protected:
    sqlite3* db_ = nullptr;
    proc::ProcessExitListener listener;
    int finalizeCallCount = 0;
    std::string lastIndexId;
    int64_t lastHistoryId = -1;
    int lastExitCode = -1;
    int64_t lastDurationMs = -1;

    void SetUp() override {
        ASSERT_EQ(sqlite3_open(":memory:", &db_), SQLITE_OK);
        exec("CREATE TABLE ProfileExItem ("
             "IndexId TEXT PRIMARY KEY, Delay TEXT, Speed TEXT, Sort TEXT, "
             "Message TEXT, consecutive_failures INTEGER DEFAULT 0, "
             "start_count INTEGER DEFAULT 0, total_runtime_ms INTEGER DEFAULT 0, crash_count INTEGER DEFAULT 0)");
        exec("CREATE TABLE proxy_runtime_history ("
             "id INTEGER PRIMARY KEY AUTOINCREMENT, "
             "index_id TEXT, started_at TEXT, ended_at TEXT, "
             "exit_code INTEGER, duration_ms INTEGER, source INTEGER DEFAULT 0)");
        finalizeCallCount = 0;
    }

    void TearDown() override {
        listener.shutdown();
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
    }

    void exec(const std::string& sql) {
        char* errMsg = nullptr;
        ASSERT_EQ(sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg), SQLITE_OK)
            << (errMsg ? errMsg : "sql error");
        sqlite3_free(errMsg);
    }

    void insertProxy(const std::string& indexId) {
        sqlite3_stmt* stmt = nullptr;
        ASSERT_EQ(sqlite3_prepare_v2(db_,
            "INSERT OR REPLACE INTO ProfileExItem (IndexId) VALUES (?)", -1, &stmt, nullptr), SQLITE_OK);
        sqlite3_bind_text(stmt, 1, indexId.c_str(), -1, SQLITE_TRANSIENT);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE);
        sqlite3_finalize(stmt);
    }

    proc::ProcessExitListener::WatchKey setupWatcher(const std::string& indexId,
                                                      int64_t historyId,
                                                      HANDLE signalEvent) {
        const std::string startedAt = "2026-08-14 08:00:00";
        return listener.watch(signalEvent, indexId, historyId,
            [](sqlite3*, const std::string&, const std::string&) -> int64_t { return -1; },
            [historyId](int64_t, sqlite3*) -> bool { return true; },
            [historyId, startedAt]() -> std::vector<std::pair<int64_t, std::string>> {
                return {{historyId, startedAt}};
            },
            [this](sqlite3*, const std::string& idx, int64_t hid,
                   const std::string&, int exitCode, int64_t durMs) -> bool {
                finalizeCallCount++;
                lastIndexId = idx;
                lastHistoryId = hid;
                lastExitCode = exitCode;
                lastDurationMs = durMs;
                return true;
            },
            [](const std::string&, int64_t){}
        );
    }
};

TEST_F(ProcessExitListenerTest, MockProcessExit_NormalExit_FinalizeCalled) {
    insertProxy("idx-a");
    // Spawn a real process that exits immediately with code 0.
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, const_cast<char*>("cmd /c exit 0"),
                             nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    ASSERT_TRUE(ok) << "CreateProcess failed";
    proc::ProcessExitListener::WatchKey key = setupWatcher("idx-a", 1, pi.hProcess);
    WaitForSingleObject(pi.hProcess, 10000);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    listener.unwatch(key);
    EXPECT_EQ(finalizeCallCount, 1);
    EXPECT_EQ(lastIndexId, "idx-a");
    EXPECT_EQ(lastHistoryId, 1);
    EXPECT_EQ(lastExitCode, 0);
    EXPECT_GE(lastDurationMs, 0);
    CloseHandle(pi.hThread);
}

TEST_F(ProcessExitListenerTest, MockProcessKill_CrashExitCode259_FinalizeCalled) {
    proc::ProcessExitEvent event;
    event.exitCode = 259;
    auto rows = std::vector<std::pair<int64_t, std::string>>{{2, "2026-08-14 08:00:00"}};
    EXPECT_TRUE(proc::ProcessExitListener::ShouldFinalize(
        event, "idx-b", 2, rows, "2026-08-14 08:00:00"));
}

TEST_F(ProcessExitListenerTest, ShutdownNoDbWrite_ShutdownBeforeExit_NoFinalize) {
    insertProxy("idx-c");
    // Spawn a real process that runs long enough.
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, const_cast<char*>("cmd /c timeout 999"),
                             nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    ASSERT_TRUE(ok) << "CreateProcess failed";
    proc::ProcessExitListener::WatchKey key = setupWatcher("idx-c", 3, pi.hProcess);
    // Shut down before the process exits — should cancel the watch.
    listener.shutdown();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, 5000);
    EXPECT_EQ(finalizeCallCount, 0);
    CloseHandle(pi.hThread);
}

TEST_F(ProcessExitListenerTest, ShouldFinalize_NonCrashSingleRow_ReturnsTrue) {
    proc::ProcessExitEvent event;
    event.exitCode = 1;
    auto rows = std::vector<std::pair<int64_t, std::string>>{{7, "2026-08-14 08:00:00"}};
    EXPECT_TRUE(proc::ProcessExitListener::ShouldFinalize(
        event, "x", 7, rows, "2026-08-14 08:00:00"));
}

TEST_F(ProcessExitListenerTest, ShouldFinalize_MultipleRows_ReturnsFalse) {
    proc::ProcessExitEvent event;
    event.exitCode = 1;
    auto rows = std::vector<std::pair<int64_t, std::string>>{
        {1, "2026-08-14 08:00:00"},
        {2, "2026-08-14 08:30:00"}
    };
    EXPECT_FALSE(proc::ProcessExitListener::ShouldFinalize(
        event, "x", 1, rows, "2026-08-14 08:00:00"));
}

TEST_F(ProcessExitListenerTest, ShouldFinalize_EmptyStartedAt_ReturnsFalse) {
    proc::ProcessExitEvent event;
    event.exitCode = 1;
    auto rows = std::vector<std::pair<int64_t, std::string>>{{5, ""}};
    EXPECT_FALSE(proc::ProcessExitListener::ShouldFinalize(
        event, "x", 5, rows, ""));
}

class ProcessInspectorTest : public ::testing::Test {
protected:
    void TearDown() override {
        proc::ProcessInspector::setEnumeratorForTesting(nullptr);
    }
};

TEST_F(ProcessInspectorTest, FindByConfig_MatchingCommandLine_ReturnsTrue) {
    proc::ProcessInspector::setEnumeratorForTesting(
        [](const std::string&) -> std::vector<proc::ProcessInfo> {
            proc::ProcessInfo info;
            info.pid = 1234;
            info.commandLine = L"C:\\xray.exe run -c \"C:\\config\\standalone_idx1-xray.json\"";
            return {info};
        });
    EXPECT_TRUE(proc::ProcessInspector::isProcessRunningWithConfig(
        "standalone_idx1-xray.json"));
}

TEST_F(ProcessInspectorTest, FindByConfig_NoMatchingProcess_ReturnsFalse) {
    proc::ProcessInspector::setEnumeratorForTesting(
        [](const std::string&) -> std::vector<proc::ProcessInfo> {
            return {};
        });
    EXPECT_FALSE(proc::ProcessInspector::isProcessRunningWithConfig(
        "standalone_idx1-xray.json"));
}

TEST_F(ProcessInspectorTest, CommandLineReadFailure_ConservativeFallback_NameMatched) {
    proc::ProcessInspector::setEnumeratorForTesting(
        [](const std::string&) -> std::vector<proc::ProcessInfo> {
            proc::ProcessInfo info;
            info.pid = 5678;
            info.commandLine = L"";
            return {info};
        });
    EXPECT_TRUE(proc::ProcessInspector::isProcessRunningWithConfig(
        "standalone_any-xray.json"));
}

TEST_F(ProcessInspectorTest, MatchesConfig_ReadFailedNameMatched_ReturnsTrue) {
    EXPECT_TRUE(proc::ProcessInspector::matchesConfig(
        L"", true, true, "standalone_x-xray.json"));
}

TEST_F(ProcessInspectorTest, MatchesConfig_ReadOkNoMatch_ReturnsFalse) {
    EXPECT_FALSE(proc::ProcessInspector::matchesConfig(
        L"C:\\other\\xray.exe run -c C:\\other\\config.json",
        true, false, "standalone_x-xray.json"));
}

TEST_F(ProcessInspectorTest, DurationMsBetween_ValidTimestamps) {
    long long d = proc::ProcessInspector::durationMsBetween(
        "2026-08-14 08:00:00", "2026-08-14 08:00:05");
    EXPECT_EQ(d, 5000LL);
}

TEST_F(ProcessInspectorTest, DurationMsBetween_InvalidFormat_ReturnsZero) {
    long long d = proc::ProcessInspector::durationMsBetween(
        "bad-ts", "2026-08-14 08:00:05");
    EXPECT_EQ(d, 0LL);
}

TEST_F(ProcessInspectorTest, NowTimestamp_NonEmpty) {
    std::string ts = proc::ProcessInspector::nowTimestamp();
    EXPECT_FALSE(ts.empty());
    EXPECT_EQ(ts.size(), 19u);
}
