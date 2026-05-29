---
title: "fix: single proxy test delay refresh and event flow repair"
type: report
status: completed
date: 2026-05-19
origin: "Consolidation of 002-fix-empty-test-results and UI fixes"
---

# Technical Report: Single Proxy Test Fix

## Problem Summary

Three interconnected issues in the GUI proxy testing flow:

| # | Problem | Root Cause |
|---|---------|------------|
| P1 | Right-click "Test This Proxy" fails with "No proxies to test for subscription" | `onTestProxy` passed `indexId` to `testSubscriptionAsync` which queried by `subId` |
| P2 | Delay column never refreshes after test completion | `ProxyListPanel` events sent only to `TestPanel`, not `MainFrame` |
| P3 | `ProxyBatchTester` destructor deleted singleton `XrayManager` | Double-free potential crash |

## Solution Architecture

### Core Fix: `runWithIndexId()` Method

Added in `ProxyBatchTester.cpp:257-284` — queries proxy by `IndexId` instead of `Subid`:

```cpp
bool ProxyBatchTester::runWithIndexId(const std::string& indexId) {
    db::models::ProfileitemDAO dao(db_);
    auto profiles = dao.getAll();
    std::vector<db::models::Profileitem> filtered;
    std::copy_if(profiles.begin(), profiles.end(), std::back_inserter(filtered),
        [&indexId](const db::models::Profileitem& p) { return p.indexid == indexId; });
    // ... test the single proxy
}
```

This avoids the SQL placeholder confusion between `{subid}` and actual `IndexId` values.

### Change List

| File | Changes |
|------|---------|
| `src/ProxyBatchTester.cpp` | Added `runWithIndexId()`; Fixed destructor comment to not delete singleton |
| `include/ProxyBatchTester.h` | Added `runWithIndexId()` declaration |
| `src/ui/AppController.h` | Added `testSingleProxyAsync()` declaration |
| `src/ui/AppController.cpp` | Added `testSingleProxyAsync()` + `doTestSingleProxy()`; Event broadcast to MainFrame |
| `src/ui/ProxyListPanel.h` | Added `refreshResults()` + `selectProxyByIndexId()` declarations |
| `src/ui/ProxyListPanel.cpp` | Implemented `refreshResults()`; Changed to use `testSingleProxyAsync()` |
| `src/ui/TestPanel.cpp` | Fixed `onProgress()` to add result row before early return |

## Technical Details

### 1. `ProxyBatchTester` - Single Proxy Test Path

```cpp
// ProxyBatchTester.h
class ProxyBatchTester {
public:
    bool runWithIndexId(const std::string& indexId);
};

// ProxyBatchTester.cpp:257-284
bool ProxyBatchTester::runWithIndexId(const std::string& indexId) {
    // Query ALL profiles and filter by indexId
    // This avoids SQL placeholder mismatch issues
}
```

### 2. `AppController` - Async Event Broadcasting

```cpp
// AppController.cpp:304-326
void AppController::doTestSingleProxy(const std::string& indexId, wxEvtHandler* wxHandler) {
    ProxyBatchTester tester(db_, config_, "");
    bool ok = tester.runWithIndexId(indexId);

    // Broadcast completion to both TestPanel AND MainFrame
    if (ok && wxHandler) {
        wxQueueEvent(topLevel, new ProxyTestProgressEvent(0, 0, "", "", "", "Single test finished", true));
    }
}
```

### 3. `ProxyListPanel` - Refresh Implementation

```cpp
// ProxyListPanel.cpp:101-120
void ProxyListPanel::refreshResults() {
    exItems_ = controller_->loadProxyResults();
    
    std::unordered_map<std::string, std::string> delayMap;
    for (const auto& ex : exItems_) {
        delayMap[ex.indexid] = ex.delay;
    }

    // Update existing rows in-place (preserves selection)
    for (unsigned row = 0; row < store_->GetCount(); ++row) {
        // ... update delay value
    }
}
```

### 4. `TestPanel` - Result Row Fix

```cpp
// TestPanel.cpp:75-103
void TestPanel::onProgress(ProxyTestProgressEvent& event) {
    if (event.isCompleted()) {
        isRunning_ = false;
        cancelBtn_->Enable(false);
        statusLabel_->SetLabel("Test completed");
    }

    // NOW adds result row even for completion events
    if (!event.getProxyId().empty()) {
        long row = resultList_->InsertItem(...);
        // ... populate row
    }
}
```

## Verification Results

```
Build: cmake --build build --parallel 8
Result: ✅ SUCCESS (0 errors)

Tests: ctest -V
Result: ✅ 3/3 passed

Manual Verification:
- Single proxy test: Delay column updates after test
- Batch test: Delay column updates after completion  
- TestPanel shows result rows for both paths
- No crashes or duplicate events
```

## Event Flow Diagram

```
Right-click "Test This Proxy"
           ↓
ProxyListPanel::onTestProxy()
           ↓
testSingleProxyAsync(indexId, testPanel_)
           ↓
   doTestSingleProxy() [worker thread]
           ↓
runWithIndexId() → tests single proxy
           ↓
wxQueueEvent(testPanel_, ...)     ← TestPanel gets result
wxQueueEvent(topLevel, ...)      ← MainFrame gets completion
           ↓
MainFrame: refreshResults()      ← Delay column updated
TestPanel: onProgress()          ← Result row added
```

## Files Changed Summary

| File | Lines Added | Lines Modified |
|------|-------------|----------------|
| `src/ProxyBatchTester.cpp` | 27 | 1 |
| `include/ProxyBatchTester.h` | 1 | - |
| `src/ui/AppController.cpp` | 32 | - |
| `src/ui/AppController.h` | 1 | - |
| `src/ui/ProxyListPanel.cpp` | 94 | - |
| `src/ui/ProxyListPanel.h` | 2 | - |
| `src/ui/TestPanel.cpp` | 1 | 26 |

**Total: ~158 lines added/modified**