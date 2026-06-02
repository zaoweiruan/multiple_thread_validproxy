---
title: "fix: CLI Ctrl+C cannot interrupt test/update operations"
type: fix
status: completed
date: 2026-06-01
---

# CLI Ctrl+C Interrupt Fix

## Bug Summary

CLI program (`validproxy-cli.exe`) cannot be interrupted by Ctrl+C during proxy testing or subscription updates. The handler stops Xray instances but worker threads continue running until curl timeouts (2+ seconds).

## Root Cause Analysis

### Phase 1 Findings

1. **`main_cli.cpp:87-97`** — `consoleCtrlHandler` only calls `g_xrayManager->stopAll()`, **does not set any cancellation flag**

2. **`main_cli.cpp:144`** — `ProxyBatchTester tester(db, *appConfig, exeDir)` created **without** `externalCancel` parameter → `externalCancel_ = nullptr` in ProxyBatchTester.h:64

3. **`ProxyBatchTester::isCancelled()`** — checks `externalCancel_` (null) and `cancelRequested_.load()` (never set externally)

4. **`testProxiesMultiThreaded()`** — waits on threads with join, but if `isCancelled()` never returns true, threads continue

5. **No timeout mechanism** — unlike `AppController::~AppController` which has a 5s timeout + detach pattern

### Missing Link

The CLI binary lacks the cancellation signal path that GUI mode has through AppController.

## Fix Implementation

### Changes Made

| File | Change | Lines |
|------|--------|-------|
| `src/main_cli.cpp` | Add `g_cancelRequested` atomic flag | +1 |
| `src/main_cli.cpp` | Set flag in `consoleCtrlHandler` | +1 |
| `src/main_cli.cpp` | Pass `&g_cancelRequested` to ProxyBatchTester constructor | 2 sites |
| `include/ProxyBatchTester.h` | Add `workerThreads_` member | +1 |
| `src/ProxyBatchTester.cpp` | Add `#include <future>` | +1 |
| `src/ProxyBatchTester.cpp` | Add timeout+detach in destructor | +35 |

### Code Changes

**main_cli.cpp (anonymous namespace):**
```cpp
std::atomic<bool> g_cancelRequested{false};  // Added after g_xrayManager
```

**main_cli.cpp (consoleCtrlHandler):**
```cpp
BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        std::cout << "\nCtrl+C detected, stopping xray instances..." << std::endl;
        g_cancelRequested.store(true);  // Added
        if (g_xrayManager) {
            g_xrayManager->stopAll();
        }
        return TRUE;
    }
    return FALSE;
}
```

**main_cli.cpp (constructor calls):**
```cpp
ProxyBatchTester tester(db, *appConfig, exeDir, &g_cancelRequested);  // Added &g_cancelRequested
```

**ProxyBatchTester.h:**
```cpp
std::atomic<bool>* externalCancel_{nullptr};
std::vector<std::thread> workerThreads_;  // Added
```

**ProxyBatchTester.cpp (destructor):**
```cpp
ProxyBatchTester::~ProxyBatchTester() {
    cancelRequested_ = true;
    delete proxyTester_;
    
    if (!workerThreads_.empty()) {
        for (auto& t : workerThreads_) {
            if (t.joinable()) {
                std::future<void> fut = std::async(std::launch::async, [&t]() {
                    if (t.joinable()) t.join();
                });
                if (fut.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
                    Logger::write("[ProxyBatchTester] Destructor: detach due to timeout", LogLevel::WARN);
                    t.detach();
                }
            }
        }
        workerThreads_.clear();
    }
}
```

## Test Results

- [x] Build successful (no compile errors)
- [x] All 5 unit test suites passed (CurlEasyHandleTest, DedupTest, LoggerTest, ShareLinkTest, ConfigGeneratorTest)
- [x] Ctrl+C handler now sets cancellation flag
- [x] Destructor handles blocked threads gracefully

## References

- GUI mode fix: `docs/bugfix/2026-05-20-ctrl-c-exit.md`
- AppController destructor pattern: `src/ui/AppController.cpp:30-69`
- Implementation plan: `docs/plans/2026-06-01-cli-ctrl-c-fix-plan.md`