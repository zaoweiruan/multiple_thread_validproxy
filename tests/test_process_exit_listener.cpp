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

// ---- process creation time (PID-factor matching support) ----

// Launches a real independent child process (stands in for a standalone
// proxy process: xray.exe / sing-box.exe) and returns its pid + handle.
// The caller must TerminateProcess + CloseHandle when done.
bool launchStandInChild(DWORD& outPid, HANDLE& outProcess) {
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr,
                             const_cast<char*>("cmd /c timeout 999"),
                             nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                             &si, &pi);
    if (!ok) {
        return false;
    }
    outPid = pi.dwProcessId;
    outProcess = pi.hProcess;
    CloseHandle(pi.hThread);
    return true;
}

class ProcessInspectorTimeTest : public ::testing::Test {
protected:
    // FILETIME for 2026-08-20 00:00:00 UTC: 100ns ticks since 1601-01-01.
    static FILETIME makeFileTimeUtc(long long unixSeconds) {
        const long long ticks =
            (unixSeconds + 11644473600LL) * 10000000LL;
        ULARGE_INTEGER ui;
        ui.QuadPart = static_cast<unsigned long long>(ticks);
        FILETIME ft;
        ft.dwLowDateTime = ui.LowPart;
        ft.dwHighDateTime = ui.HighPart;
        return ft;
    }

    // Spawns a child process (proxy stand-in) that lives until teardown.
    DWORD spawnStandIn() {
        DWORD pid = 0;
        HANDLE proc = nullptr;
        EXPECT_TRUE(launchStandInChild(pid, proc));
        childHandles_.push_back(proc);
        return pid;
    }

    void TearDown() override {
        for (HANDLE h : childHandles_) {
            TerminateProcess(h, 1);
            WaitForSingleObject(h, 5000);
            CloseHandle(h);
        }
        childHandles_.clear();
        proc::ProcessInspector::setEnumeratorForTesting(nullptr);
    }

    std::vector<HANDLE> childHandles_;
};

TEST_F(ProcessInspectorTimeTest, FileTimeToString_Returns19CharTimestamp) {
    FILETIME ft = makeFileTimeUtc(1784592000LL);  // 2026-08-20 00:00:00 UTC
    std::string s = proc::ProcessInspector::fileTimeToString(ft);
    EXPECT_EQ(s.size(), 19u);
    // Format sanity: "YYYY-MM-DD HH:MM:SS" (time zone independent checks).
    EXPECT_EQ(s[4], '-');
    EXPECT_EQ(s[7], '-');
    EXPECT_EQ(s[10], ' ');
    EXPECT_EQ(s[13], ':');
    EXPECT_EQ(s[16], ':');
}

TEST_F(ProcessInspectorTimeTest, FileTimeToString_ZeroEpoch_Returns1970) {
    FILETIME ft = makeFileTimeUtc(0);  // 1970-01-01 00:00:00 UTC
    std::string s = proc::ProcessInspector::fileTimeToString(ft);
    EXPECT_EQ(s.substr(0, 4), "1970");
}

TEST_F(ProcessInspectorTimeTest, ProcessCreationTime_RealChildProcess_NonEmpty) {
    const DWORD pid = spawnStandIn();
    ASSERT_NE(pid, 0u);
    std::string s = proc::ProcessInspector::processCreationTime(pid);
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(s.size(), 19u);
}

TEST_F(ProcessInspectorTimeTest, ProcessCreationTime_InvalidPid_ReturnsEmpty) {
    std::string s = proc::ProcessInspector::processCreationTime(0xFFFFFFFFu);
    EXPECT_TRUE(s.empty());
}

TEST_F(ProcessInspectorTimeTest, EnumerateByName_PopulatesCreationTime) {
    // Stand in for a proxy process: spawn cmd.exe (a real, independent
    // process) and locate it by its exe name; creationTime must be filled.
    const DWORD pid = spawnStandIn();
    ASSERT_NE(pid, 0u);

    std::vector<proc::ProcessInfo> procs =
        proc::ProcessInspector::enumerateByName("cmd.exe");
    bool foundChild = false;
    for (const proc::ProcessInfo& p : procs) {
        if (p.pid == pid) {
            foundChild = true;
            EXPECT_FALSE(p.creationTime.empty());
            EXPECT_EQ(p.creationTime.size(), 19u);
        }
    }
    EXPECT_TRUE(foundChild);
}

// Baseline elapsed time (e.g. time before adoption) must be added to heartbeat
// elapsedMs so duration_ms reflects the process real start time (spec §3.5).
TEST_F(ProcessExitListenerTest, Heartbeat_IncludesBaseline) {
    const std::string indexId = "idx-baseline";
    insertProxy(indexId);
    // Never-signaled event handle = simulated long-running process.
    HANDLE h = makeSignaledHandle();
    // Reset it to non-signaled so WAIT_TIMEOUT fires on each heartbeat interval.
    ResetEvent(h);

    std::atomic<bool> gotHeartbeat{false};
    int64_t observedElapsedMs = 0;

    const int64_t baselineMs = 60000;
    proc::ProcessExitListener::WatchKey key = listener.watch(
        h, indexId, 77,
        [](sqlite3*, const std::string&, const std::string&) -> int64_t { return -1; },
        [](int64_t, sqlite3*) -> bool { return true; },
        []() -> std::vector<std::pair<int64_t, std::string>> { return {}; },
        [](sqlite3*, const std::string&, int64_t, const std::string&, int, int64_t) -> bool {
            return true;
        },
        [](const std::string&, int64_t) {},
        /*takeoverFn=*/nullptr,
        /*configFileName=*/"",
        /*heartbeatFn=*/[&](const std::string&, int64_t, int64_t elapsedMs) {
            gotHeartbeat.store(true);
            observedElapsedMs = elapsedMs;
        },
        /*heartbeatIntervalMs=*/30,
        /*baselineElapsedMs=*/baselineMs);

    // Give the watcher thread at least one WAIT_TIMEOUT cycle.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    listener.unwatch(key);
    CloseHandle(h);

    EXPECT_TRUE(gotHeartbeat.load());
    // Baseline (60000) + at least one interval => strictly greater than baseline.
    EXPECT_GT(observedElapsedMs, baselineMs);
}

// ---- shutdown() must JOIN watcher threads (not detach) ----
// Regression for the close-time crash: shutdown() used to detach watcher
// threads and immediately free the Watcher objects, leaving detached threads
// running on freed memory (freed std::function heartbeatFn => execute-AV).
// The fix must join all watcher threads BEFORE their Watcher objects die.
TEST_F(ProcessExitListenerTest, ShutdownJoinsWatcherThreads_NoUafNoHeartbeatAfter) {
    // Never-signaled event handle = simulated long-running process.
    HANDLE h = makeSignaledHandle();
    ResetEvent(h);

    std::atomic<int> heartbeatCount{0};

    listener.watch(
        h, "idx-shutdown-join", 99,
        [](sqlite3*, const std::string&, const std::string&) -> int64_t { return -1; },
        [](int64_t, sqlite3*) -> bool { return true; },
        []() -> std::vector<std::pair<int64_t, std::string>> { return {}; },
        [](sqlite3*, const std::string&, int64_t, const std::string&, int, int64_t) -> bool {
            return true;
        },
        [](const std::string&, int64_t) {},
        /*takeoverFn=*/nullptr,
        /*configFileName=*/"",
        /*heartbeatFn=*/[&](const std::string&, int64_t, int64_t) {
            heartbeatCount.fetch_add(1);
        },
        /*heartbeatIntervalMs=*/30,
        /*baselineElapsedMs=*/0);

    // Let at least one heartbeat cycle fire so we know the thread is running.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_GE(heartbeatCount.load(), 1)
        << "watcher thread should have fired at least one heartbeat before shutdown";

    // Shutdown must join the watcher thread. If shutdown() were still the old
    // detach+clear, the detached thread would keep touching freed Watcher
    // memory; with the join fix, the thread is dead once shutdown() returns.
    listener.shutdown();
    const int joinedCount = heartbeatCount.load();

    // A detached thread (if any) would keep firing beats on freed Watcher
    // memory; a joined thread cannot. Give ample time to observe either.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // No heartbeat may fire after shutdown() returned: the watcher thread was
    // joined inside shutdown() and is provably dead.
    EXPECT_EQ(heartbeatCount.load(), joinedCount)
        << "heartbeat fired after shutdown() returned => thread outlived shutdown";

    // shutdown() must be idempotent (also called again by TearDown).
    listener.shutdown();
    CloseHandle(h);
}

// shutdown() followed by listener destruction must be safe even while a
// watcher is mid-wait on a long heartbeat interval (close-time scenario:
// user closes the app while standalone proxies are being watched).
TEST_F(ProcessExitListenerTest, ShutdownThenDestroy_LongIntervalWatch_NoCrash) {
    proc::ProcessExitListener* dyn = new proc::ProcessExitListener();
    HANDLE h = makeSignaledHandle();
    ResetEvent(h);
    std::atomic<int> heartbeatCount{0};

    // Long interval: the watcher thread sits in WaitForSingleObject(30000ms)
    // when shutdown() arrives — join must still complete promptly because
    // the fix must not rely on the wait timing out.
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    dyn->watch(
        h, "idx-long", 100,
        [](sqlite3*, const std::string&, const std::string&) -> int64_t { return -1; },
        [](int64_t, sqlite3*) -> bool { return true; },
        []() -> std::vector<std::pair<int64_t, std::string>> { return {}; },
        [](sqlite3*, const std::string&, int64_t, const std::string&, int, int64_t) -> bool {
            return true;
        },
        [](const std::string&, int64_t) {},
        nullptr, "",
        [&](const std::string&, int64_t, int64_t) { heartbeatCount.fetch_add(1); },
        /*heartbeatIntervalMs=*/30000,
        /*baselineElapsedMs=*/0);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    dyn->shutdown();
    const std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
    const long long shutdownMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    // If shutdown relied on WaitForSingleObject timing out, it would block
    // ~30s. The join must complete via the running=false flag path far
    // sooner. Allow generous slack (e.g. 5s) for CI scheduling jitter.
    EXPECT_LT(shutdownMs, 5000)
        << "shutdown() blocked too long; watcher join must not wait out the full interval";

    delete dyn;  // ~ProcessExitListener -> shutdown() again (idempotent path)
    CloseHandle(h);
}
