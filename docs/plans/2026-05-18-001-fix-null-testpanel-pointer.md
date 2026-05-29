---
title: "fix(MainFrame): fix null testPanel_ pointer in initPanels ordering"
type: fix
status: draft
date: 2026-05-18
origin: "User reported \"右键test this proxy没有结果\""
---

# Fix Null testPanel_ Pointer Causes Empty Test Results

## 问题描述

Right-click "Test This Proxy" on a proxy in the list shows no results in the TestPanel.

## 根因分析

In `MainFrame::initPanels()` (MainFrame.cpp:184–212), `testPanel_` is passed to `ProxyListPanel`'s constructor **before** `testPanel_` is instantiated:

```cpp
void MainFrame::initPanels() {
    subPanel_    = new SubscriptionPanel(this, controller_);
    proxyPanel_  = new ProxyListPanel(this, controller_, db_, testPanel_); // ← nullptr here
    testPanel_   = new TestPanel(this);            // ← created too late
    ...
}
```

This means `ProxyListPanel::testPanel_` is nullptr for the entire lifetime of the application. When `onTestProxy()` in ProxyListPanel.cpp checks `if (testPanel_)`, the condition is always false, so **all wxQueueEvent calls are silently skipped** — no event ever reaches TestPanel, hence no result rows.

## 范围边界

- **唯一修改文件**: `src/ui/MainFrame.cpp` — `initPanels()` construction order
- **NOT 修改**: ProxyListPanel.cpp, TestPanel.cpp, Events.h, ProxyTester

## 详细变更

### U1: `src/ui/MainFrame.cpp` — Fix construction order in initPanels()

Swap lines 186 and 187 so `testPanel_` is created before being passed to `ProxyListPanel`:

```diff
  void MainFrame::initPanels() {
      subPanel_   = new SubscriptionPanel(this, controller_);
-     proxyPanel_ = new ProxyListPanel(this, controller_, db_, testPanel_);
-     testPanel_  = new TestPanel(this);
+     testPanel_  = new TestPanel(this);
+     proxyPanel_ = new ProxyListPanel(this, controller_, db_, testPanel_);
      logPanel_   = new LogPanel(this);
      ...
  }
```

## 关联修正

Prior pending fix (plan `1778814717912-tidy-meadow.md`) on `ProxyListPanel.cpp` -- previously changed intermediate error events from `isCompleted=true` to `false` and split the final result into two events (result + completion). **This fix is a prerequisite**: `testPanel_` being non-null is required for any of the event-queueing code to execute.

## 验证步骤

1. Build: `cmake --build build --parallel 8` (passes)
2. Run `bin/validproxy.exe`, select a subscription, right-click a proxy → **"Test This Proxy"**
3. Expected: Test Results panel shows the proxy's test row with Delay/Message
