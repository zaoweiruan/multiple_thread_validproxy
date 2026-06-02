# CLI Ctrl+C Interrupt Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Ctrl+C interrupt capability to CLI binary so users can stop proxy testing and subscription updates gracefully.

**Architecture:** Add a global `std::atomic<bool> g_cancelRequested` flag, wire it to Ctrl+C handler, pass to ProxyBatchTester constructor, and add timeout+detach pattern in destructor.

**Tech Stack:** C++17, Windows API (SetConsoleCtrlHandler)

---

## File Changes

| File | Action | Lines |
|------|--------|-------|
| `src/main_cli.cpp` | Modify | +5/-2 (add flag, set in handler, pass to constructor) |
| `src/ProxyBatchTester.cpp` | Modify | +35 (destructor timeout + detach) |

---

## Phase 2: Pattern Analysis

The GUI mode fix from `docs/bugfix/2026-05-20-ctrl-c-exit.md` already solved this:

```cpp
// AppController::~AppController pattern (lines 30-69)
cancelRequested_ = true;
// 5s timeout with async join, then detach if needed
```

We apply the same pattern to CLI.

---

## Implementation Tasks

### Task 1: Add global cancellation flag and wire to Ctrl+C handler

**Files:**
- Modify: `src/main_cli.cpp`

- [ ] **Step 1: Add `g_cancelRequested` flag in anonymous namespace (after line 36)**

Add after `XrayManager* g_xrayManager = nullptr;`:
```cpp
std::atomic<bool> g_cancelRequested{false};
```

- [ ] **Step 2: Set flag in `consoleCtrlHandler` (line 87-97)**

Replace the body of `consoleCtrlHandler`:
```cpp
BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        std::cout << "\nCtrl+C detected, stopping xray instances..." << std::endl;
        g_cancelRequested.store(true);
        if (g_xrayManager) {
            g_xrayManager->stopAll();
        }
        return TRUE;
    }
    return FALSE;
}
```

- [ ] **Step 3: Pass flag to ProxyBatchTester constructor (line 144)**

Change:
```cpp
ProxyBatchTester tester(db, *appConfig, exeDir);
```
To:
```cpp
ProxyBatchTester tester(db, *appConfig, exeDir, &g_cancelRequested);
```

- [ ] **Step 4: Commit Task 1**

```bash
git add src/main_cli.cpp
git commit -m "fix: add Ctrl+C cancellation flag to CLI mode
- Add g_cancelRequested atomic flag
- Set flag in consoleCtrlHandler on CTRL_C_EVENT
- Pass flag to ProxyBatchTester for worker thread cancellation"
```

---

### Task 2: Add timeout+detach in ProxyBatchTester destructor

**Files:**
- Modify: `src/ProxyBatchTester.cpp`

- [ ] **Step 1: Write failing test case (conceptual)**

The test would verify that:
- Creating ProxyBatchTester with running threads
- Setting `cancelRequested_.store(true)` from another thread
- Destroying ProxyBatchTester within timeout (5s) succeeds

- [ ] **Step 2: Add timeout+detach to destructor (lines 25-29)**

Replace current destructor:
```cpp
ProxyBatchTester::~ProxyBatchTester() {
    cancelRequested_ = true;
    delete proxyTester_;
}
```

With:
```cpp
ProxyBatchTester::~ProxyBatchTester() {
    cancelRequested_ = true;  // Signal workers to stop
    delete proxyTester_;
    
    // Wait for worker threads with timeout (same pattern as AppController)
    // Get thread handles from testProxiesMultiThreaded - need to store them
}
```

Wait - we need to store thread references. Let me check the current implementation again.

Actually, looking at `testProxiesMultiThreaded()` (lines 227-252), threads are local to the function. The destructor doesn't have access to them.

The proper fix requires storing thread references as member variables. Let me restructure.

- [ ] **Step 2a: Add thread storage to `ProxyBatchTester.h` (line 64)**

Add after `std::atomic<bool>* externalCancel_{nullptr};`:
```cpp
std::vector<std::thread> workerThreads_;  // Store threads for join/timeout in destructor
```

- [ ] **Step 2b: Store threads in `testProxiesMultiThreaded()` (line 243)**

After `threads.emplace_back(...)`, add:
```cpp
workerThreads_ = std::move(threads);
```

- [ ] **Step 2c: Clear threads after join (line 251)**

After the join loop, add:
```cpp
workerThreads_.clear();
```

- [ ] **Step 2d: Add timeout+detach in destructor (lines 25-29)**

Replace destructor:
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
                    t.detach();
                    Logger::write("[ProxyBatchTester] Destructor: detach due to timeout", LogLevel::WARN);
                }
            }
        }
        workerThreads_.clear();
    }
}
```

- [ ] **Step 3: Add required includes (#include <future>)**

Add `#include <future>` to ProxyBatchTester.cpp includes.

- [ ] **Step 4: Commit Task 2**

```bash
git add include/ProxyBatchTester.h src/ProxyBatchTester.cpp
git commit -m "fix: add timeout+detach in ProxyBatchTester destructor for Ctrl+C
- Store worker threads as member vector
- Add 5s timeout with std::async join pattern
- Detach threads if timeout exceeded to prevent process hang"
```

---

### Task 3: Build and verify

- [ ] **Step 1: Build the CLI target**

```bash
cmake --build build --parallel 8
```

- [ ] **Step 2: Run full test suite**

```bash
ctest -V
```

- [ ] **Step 3: Manual Ctrl+C test**

```cmd
echo Start: %TIME% && bin\validproxy-cli.exe -TA && echo End: %TIME%
```
Manually press Ctrl+C after ~3 seconds. Expected: Process exits within 5-7 seconds.

---

### Task 4: Update INDEX.md

- [ ] **Step 1: Add bug fix document to INDEX.md**

Add entry to `## 9.1 Bug 修复记录` section.

---

## Self-Review Checklist

- [ ] All spec requirements covered (cancellation flag, handler wiring, destructor timeout)
- [ ] No placeholders (TBD, TODO, etc.)
- [ ] File paths are exact
- [ ] Code is complete in each step