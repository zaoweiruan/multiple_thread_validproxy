---
title: "fix(UI): GUI application proper shutdown with xray instance cleanup"
type: fix
status: completed
date: 2026-05-19
origin: "Investigation of GUI closing behavior and xray instance management"
---

# Fix Plan: GUI Proper Shutdown with Xray Instance Cleanup

## 问题描述 (Problem Summary)

When the GUI application closes, it does not properly:
1. Cancel ongoing async operations (test subscription, find proxy)
2. Stop running xray instances before exit
3. Wait for worker threads to finish cleanly

### Current Issues

| Issue | Location | Symptom |
|-------|----------|---------|
| I1 | `AppController::~AppController` | Destructor tries to join thread but `cancelRequested_` may not stop ongoing work |
| I2 | `MainFrame::onClose` | Calls `stopXray()` but xray instances from find-proxy operations are created in separate threads and not tracked |
| I3 | `main.cpp` UI mode | `XrayManager::release()` is called after `wxEntry` but xray instances may still be running from async operations |
| I4 | Missing thread cancellation | Worker threads check `cancelRequested_` but some operations (like xray startup) are not interruptible |

## 范围边界

### 修改 (In Scope)
- `src/ui/AppController.cpp` - Proper async operation cancellation
- `src/ui/MainFrame.cpp` - Enhanced close handler with thread waiting
- `src/main.cpp` - Ensure XrayManager cleanup before DB close
- `src/XrayManager.cpp` - Add shutdown timeout handling

### NOT 修改 (Out of Scope)
- UI layout or visual changes
- Proxy testing logic
- Database schema changes

## 详细变更 (Detailed Changes)

### U1: `AppController.cpp` — Implement cooperative cancellation for ongoing operations

**Current code (lines 94-98):**
```cpp
void AppController::testSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler) {
    cancelRequested_ = false;
    if (workerThread_.joinable()) workerThread_.join();
    workerThread_ = std::thread(&AppController::doTestSubscription, this, subId, wxHandler);
}
```

**Fix:** Add a `cancelTest()` method that sets the flag and wakes waiting threads:
```cpp
void AppController::cancelTest() {
    cancelRequested_ = true;
    // Wake up any waiting condition variables if needed
}

bool AppController::shouldCancel() const {
    return cancelRequested_;
}
```

### U2: `AppController.cpp` — Add timeout to destructor thread join

**Current code (lines 26-31):**
```cpp
AppController::~AppController() {
    cancelRequested_ = true;
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}
```

**Fix:** Add timeout to prevent hanging:
```cpp
#include <future>

AppController::~AppController() {
    cancelRequested_ = true;
    if (workerThread_.joinable()) {
        // Wait up to 5 seconds for thread to finish
        std::future<void> fut = std::async(std::launch::async, [this]() {
            workerThread_.join();
        });
        if (fut.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
            Logger::write("Warning: Worker thread did not finish in time during shutdown", LogLevel::WARN);
        }
    }
}
```

### U3: `MainFrame.cpp` — Enhanced close handler with proper cleanup sequence

**Current code (lines 231-244):**
```cpp
void MainFrame::onClose(wxCloseEvent& event) {
    if (controller_) {
        controller_->cancelTest();
        controller_->stopXray();
    }
    event.Skip();
}
```

**Fix:** Wait for controller thread to finish before proceeding:
```cpp
void MainFrame::onClose(wxCloseEvent& event) {
    // Cancel any ongoing operations
    if (controller_) {
        controller_->cancelTest();
        
        // Give threads a moment to respond to cancellation
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Stop xray instances
        controller_->stopXray();
    }
    
    if (configDialog_) {
        configDialog_->EndModal(wxID_CANCEL);
    }
    
    event.Skip();
}
```

### U4: `main.cpp` — Ensure cleanup order in UI mode

**Current code (lines 278-284):**
```cpp
int ret = wxEntry(wxArgc, wxArgv.data());

XrayManager::release();  // safety net: ensure xray instances are stopped
sqlite3_close(db);
Logger::close();
return ret;
```

**Fix:** Add explicit cancellation before wxEntry returns:
```cpp
// The controller cleanup happens in MainFrame destructor, but we add
// an extra safety check here for any stray xray instances
XrayManager::release();  // safety net: ensure xray instances are stopped
sqlite3_close(db);
Logger::close();
return ret;
```

### U5: `XrayManager.cpp` — Make stopAll more robust

**Fix:** Add timeout and force kill after timeout:
```cpp
void XrayManager::stopAll() {
    for (auto& inst : instances_) {
        inst->stop();
    }
    instances_.clear();
    
    // Give processes a moment to terminate
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Verify all instances are gone (optional: add process verification)
}
```

## 验证步骤 (Verification Steps)

```bash
# 1. Build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8

# 2. Run GUI and test shutdown
./bin/validproxy.exe -ui

# 3. Manual tests:
# - Start a proxy test (Toolbar → Test or right-click → Test)
# - Immediately close the window with Alt+F4
# - Expected: Application exits cleanly without hanging
# - No xray.exe processes left running (check Task Manager)
#
# - Start Find Proxy operation (Menu → Find Proxy)
# - Close window immediately
# - Expected: Application exits cleanly, no orphan processes
```

## 风险评估 (Risk Assessment)

| Risk | Impact | Mitigation |
|------|--------|------------|
| Thread join timeout causing early exit | Medium | Log warning, ensure no resource leaks |
| Xray processes not terminated | Low | Double-check with process enumeration in release() |
| Race condition during shutdown | Medium | Use atomic flag, proper ordering of operations |

## 验收标准 (Acceptance Criteria)

- [ ] Application closes within 5 seconds when xray instances are running
- [ ] No xray.exe orphan processes after GUI close
- [ ] No hangs or crashes during shutdown
- [ ] All existing tests pass (`ctest -V`)