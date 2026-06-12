---
title: "fix: 批量更新全部订阅后未刷新订阅面板（含全部项）"
type: plan
status: draft
date: 2026-06-11
---

## 问题描述

- 点击“更新全部”订阅后，SubscriptionPanel（包括“全部”子项）的有效代理计数不会自动刷新。
- 用户必须手动右键刷新才能看到最新统计。

## 根本原因

### 代码路径
1. `MainFrame.cpp:568` → `controller_->updateAllSubscriptionsAsync(this)`
2. `AppController::updateAllSubscriptionsAsync()` 启动后台线程执行 `doUpdateAllSubscriptions`
3. 完成后仅发送 `StatusUpdateEvent`（用于状态栏文本）
4. `MainFrame::onStatusUpdate()` 仅调用 `setStatusText()`，不触发订阅面板刷新

### 对比基准
- 批量测试完成路径已修复：`wxEVT_PROXY_TEST_PROGRESS` completion 分支已补 `subPanel_->loadSubscriptions()`
- 但更新订阅完成路径等价刷新缺失

## 修复方案

在 AppController 完成更新后发送订阅刷新事件，由 MainFrame 统一处理。

步骤：
1. 在 `include/Events.h` 新增 `SubscriptionRefreshEvent`
2. `AppController::doUpdateAllSubscriptions()` 完成时，对 `wxHandler` 发送 `SubscriptionRefreshEvent`
3. `MainFrame` 注册 handler，收到后调用 `subPanel_->loadSubscriptions()`

涉及文件:
- `include/Events.h`
- `src/ui/MainFrame.cpp`
- `src/ui/AppController.cpp`

## 验证

- 更新全部订阅后，观察“全部”项有效计数是否即时更新
- 确认各订阅项代理数统计同步刷新

## 优先级

P2 - 影响用户体验但不致命
