---
title: "fix(TestPanel): Fix empty test results when testing proxies"
type: fix
status: completed
date: 2026-05-18
origin: "User reported issue \"进行代理测试时test results 空\""
---

# Fix Empty Test Results in Proxy Testing

## 问题描述

When testing a proxy via right-click context menu "Test This Proxy" in ProxyListPanel, the TestPanel shows "Test completed" but the results list remains empty - no rows are added to display the test outcome.

## 根因分析

The `TestPanel::onProgress()` method returns early when `isCompleted=true`:

```cpp
void TestPanel::onProgress(ProxyTestProgressEvent& event) {
    if (event.isCompleted()) {
        statusLabel_->SetLabel("Test completed");
        return;  // <-- Returns before adding result row!
    }
    // ... result row adding code
}
```

In `ProxyListPanel::onTestProxy()`, all progress events were sending `isCompleted=true`, causing the handler to never reach the result row insertion code.

## 范围边界

- **修改**: `src/ui/ProxyListPanel.cpp` - `onTestProxy()` method
- **NOT 修改**: TestPanel logic, Event definitions

## 详细变更

### U1: `src/ui/ProxyListPanel.cpp` - Fixed event flow

Changed 4 intermediate error events from `isCompleted=true` to `isCompleted=false`:

| Line | Error Case | Before | After |
|------|-----------|--------|-------|
| 509 | CONFIG_ERROR (generateConfig exception) | `true` | `false` |
| 521 | No available ports | `true` | `false` |
| 555 | Failed to create config file | `true` | `false` |
| 578 | Failed to start xray process | `true` | `false` |

Split the final result event into two events:
1. Result event (`isCompleted=false`) - adds the row with delay/message
2. Completion event (`isCompleted=true`) - updates status to "Test completed"

## 验证步骤

1. Build: `cmake --build build --parallel 8` (passes)
2. Tests: Run `bin/test_dedup.exe` and `bin/test_model.exe` (all pass)
3. Functional: Run `validproxy.exe -help` (executes without DLL errors)

## 风险

Low risk - only affects the event flow for single proxy testing, does not change core testing logic.
