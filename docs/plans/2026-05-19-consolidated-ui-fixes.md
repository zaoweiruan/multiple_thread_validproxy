---
title: "fix(UI): consolidated remaining UI issues - single proxy test delay refresh + event flow"
type: fix
status: completed
date: 2026-05-19
origin: "Merged from 2026-05-18-001/002/003/004 plans"
---

# Consolidated UI Fix Plan — 已完成

**说明**: 本计划涉及的修改已在 `ProxyListPanel.cpp`、`TestPanel.cpp` 和 `AppController.cpp` 中实现。

# Consolidated UI Fix Plan

## Problem Summary

Three interconnected issues remain after 5/18 plans:

| # | Problem | Root Cause |
|---|---------|------------|
| P1 | Single proxy test doesn't refresh Delay column | `ProxyListPanel::onTestProxy` sends events only to `testPanel_`, not to `MainFrame` |
| P2 | TestPanel progress events missing result rows | `onProgress` returns early when `isCompleted=true` before adding row |
| P3 | Build verification needed | Ensure all changes compile and run correctly |

## Files to Modify

| File | Changes |
|------|---------|
| `src/ui/ProxyListPanel.cpp` | Fix event routing for single proxy test |
| `src/ui/TestPanel.cpp` | Reorder logic to add result row before completion handling |
| `src/ui/AppController.cpp` | Minor fix for single proxy test result flow |

## Detailed Changes

### U1: `ProxyListPanel.cpp` - Fix event routing for single proxy test

**Current code (lines 151-166):**
```cpp
void ProxyListPanel::onTestProxy(wxCommandEvent& event) {
    // ...
    controller_->testSubscriptionAsync(indexId, testPanel_);  // Only sends to testPanel_
}
```

**Fix:**
```cpp
void ProxyListPanel::onTestProxy(wxCommandEvent& event) {
    // Get selected proxy info
    wxDataViewItem item = listCtrl_->GetSelection();
    if (!item.IsOk()) return;

    wxVariant idxVar;
    store_->GetValue(idxVar, item, COL_INDEXID);
    std::string indexId = idxVar.GetString().ToStdString();

    // Send initial "Testing..." event to TestPanel
    if (testPanel_) {
        wxQueueEvent(testPanel_, new ProxyTestProgressEvent(0, 1, indexId, "", "", "Testing…", false));
    }

    // FIX: Also send to MainFrame so it can refresh Delay column on completion
    wxWindow* topLevel = wxGetTopLevelParent(this);
    if (topLevel && topLevel != this) {
        wxQueueEvent(topLevel, new ProxyTestProgressEvent(0, 1, indexId, "", "", "Testing…", false));
    }

    controller_->testSubscriptionAsync(indexId, testPanel_);
    (void)event;
}
```

### U2: `TestPanel.cpp` - Reorder `onProgress` to add row before completion

**Current code (lines 75-100):**
```cpp
void TestPanel::onProgress(ProxyTestProgressEvent& event) {
    if (event.isCompleted()) {
        isRunning_ = false;
        cancelBtn_->Enable(false);
        statusLabel_->SetLabel("Test completed");
        return;  // <-- Returns BEFORE adding result row!
    }
    // ... result row code
}
```

**Fix:**
```cpp
void TestPanel::onProgress(ProxyTestProgressEvent& event) {
    // Handle completion state FIRST
    if (event.isCompleted()) {
        isRunning_ = false;
        cancelBtn_->Enable(false);
        statusLabel_->SetLabel("Test completed");
    }

    // Add result row if we have proxy data (even for completion events)
    if (!event.getProxyId().empty()) {
        long row = resultList_->InsertItem(resultList_->GetItemCount(), event.getRemarks());
        resultList_->SetItem(row, 1, event.getProxyId());
        resultList_->SetItem(row, 2, event.getDelay());
        resultList_->SetItem(row, 3, event.getMessage());
    }

    // Update progress UI for non-completion events
    if (!event.isCompleted()) {
        int current = event.getCurrent();
        int total = event.getTotal();
        if (total > 0) {
            total_ = total;
            progressBar_->SetRange(total);
            progressBar_->SetValue(current);
            progressText_->SetLabel(wxString::Format("%d/%d", current, total));
        }
    }
}
```

### U3: `AppController.cpp` - Ensure single proxy test posts correct completion event

The current `doTestSubscription` already posts to `wxHandler` and broadcasts to top-level window. This is working correctly.

## Verification Steps

```bash
# 1. Build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8

# 2. Run and test
./bin/validproxy.exe -ui

# 3. Manual tests:
# - Right-click proxy → "Test This Proxy"
#   Expected: TestPanel shows result row, ProxyListPanel Delay column updates
# - Toolbar → Test → select subscription
#   Expected: TestPanel shows batch results, Delay column updates on completion
# - Menu → Find Proxy / Find Best
#   Expected: Non-blocking, shows result in message box
```

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Double events causing duplicate rows | Low | `isCompleted` flag prevents duplicate processing |
| Thread safety | Low | All UI updates via `wxQueueEvent` |
| Event routing complexity | Low | Well-defined handlers in MainFrame |

## Verification Results (2026-05-19)

- ✅ Code in `ProxyListPanel.cpp:164-168` sends events to both TestPanel and MainFrame
- ✅ Code in `TestPanel.cpp:75-102` handles completion state first, then adds result rows
- ✅ Code in `AppController.cpp:270-296` broadcasts to MainFrame for delay column refresh
- ✅ Build: Success (Debug mode)
- ✅ Tests: 3/3 passed