---
title: "Plan: 独立代理周期自动探活（v1.0）"
module: src/ui（AppController / MainFrame）
status: 待实施
date: 2026-09-15
supersedes: （无）
---

# 实施计划：独立代理周期自动探活（v1.0）

规格：`docs/specs/2026-09-15-Spec-StandalonePeriodicProbe-v1.0.md`

## 任务清单

### Task 1：`AppController` 增强（silent 参数 + 失败 WARN 日志）

**位置**：`src/ui/AppController.h`、`src/ui/AppController.cpp:484/2136`

**变更**：
1. `AppController.h`：`void testOnlineProxiesAsync(wxEvtHandler* wxHandler, bool silent = false);`
2. `AppController.cpp`：
   - `testOnlineProxiesAsync`：透传 `silent`。
   - `doTestOnlineProxies(wxEvtHandler* wxHandler, bool silent)`：失败分支加 WARN 日志 `"[OnlineProbe] test failed: <indexId>: <errorMsg>"`；`silent=true` 时跳过 `StatusUpdateEvent` 与 `TestOnlineProxiesEvent`；其余不变。

**验收**：手动调用（默认 false）行为不变；silent=true 静默 + 失败 WARN 日志；updateTestResult 均执行。

### Task 2：`MainFrame` 周期触发

**位置**：`src/ui/MainFrame.cpp:934`（`onProxyMonTimer`）

**变更**：`onProxyMonTimer` 尾部追加 `controller_->testOnlineProxiesAsync(this, true);`（静默周期探活）。

**验收**：启用进程监控后周期静默探活；不新增 timer/配置。

### Task 3：测试 + 构建验证 + 文档登记

**位置**：`tests/` + 构建产物 + `docs/INDEX.md` + `docs/plans/project-plans-tracker.md`

**变更**：
1. 若可测：新增 `silent` 参数不影响数据更新的单测（复用现有 online-test 或 DAO 链路）；否则以构建 + 手动链路回归验证。
2. `cmake --build build --parallel 8` 0 error。
3. `ctest --test-dir build -E "UI_"` 全量回归无新增失败。
4. `docs/INDEX.md` §8.2 登记本计划；`docs/plans/project-plans-tracker.md` 新增引用行。

**验收**：构建 0 error；ctest 全绿；文档登记完成。

## 依赖关系

- Task 1 → Task 2（AppController 接口先行）
- Task 3 最后执行

## 风险与对策

| 风险 | 对策 |
| --- | --- |
| 周期探活与手动/批量测试争用 `isRunning_` | 现有 `isRunning_` + `ScopeGuard` 防重入：未完成则跳过本轮 |
| `workerThread_` 被周期触发覆盖 | 与现有 `testOnlineProxiesAsync` 相同用法，`isRunning_` 保证不并发 |
| 周期探活状态栏/弹窗打扰 | `silent=true` 跳过 UI 事件（仅 WARN + 数据更新） |
| 进程监控未启用时无探活 | 与需求一致（总开关 = `proxy_process_monitor.enabled`） |