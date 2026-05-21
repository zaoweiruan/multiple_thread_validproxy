---
title: "fix: Ctrl+C cannot exit application normally"
type: fix
status: completed
date: 2026-05-20
---

# Ctrl+C Exit Fix

## Bug Summary

Ctrl+C signal in GUI mode cannot exit the application normally due to worker thread blocking.

## Root Cause

`workerThread_.join()` blocks indefinitely if the worker thread is blocked waiting for Xray API or network operations.

## Fix Implementation

Added 5-second timeout mechanism in `AppController` destructor:

```cpp
AppController::~AppController() {
    cancelRequested_ = true;
    
    if (workerThread_.joinable()) {
        std::future<void> fut = std::async(std::launch::async, [this]() {
            if (workerThread_.joinable()) {
                workerThread_.join();
            }
        });
        if (fut.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            workerThread_.detach();  // Timeout - detach to allow process exit
        }
    }
    XrayManager::release();
}
```

## Test Results
- [x] Build successful
- [x] All 3 unit test suites passed
- [x] Ctrl+C now exits within 5 seconds timeout