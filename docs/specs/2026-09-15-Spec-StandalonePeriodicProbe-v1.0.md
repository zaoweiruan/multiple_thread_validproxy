---
title: "Spec: 独立代理周期自动探活（复用池评估机制）（v1.0）"
module: src/ui（AppController / MainFrame）
status: 待实施
date: 2026-09-15
supersedes: （无 — 新增规格）
---

# 规格说明：独立代理周期自动探活（v1.0）

## 1. 目标

| 目标 | 说明 | 验收标准 |
| --- | --- | --- |
| G1 周期自动探活 | 独立代理进程运行时，周期性自动执行「测试在线代理」（复用池评估机制语义），无需手动触发 | 启用 `proxy_process_monitor` 后，在线独立代理按 `checkIntervalMs` 周期自动探活 |
| G2 失败更新测试结果 | 探活失败时更新 `ProfileExItem`（与现有手动「测试在线代理」一致） | 失败代理 `delay=-1`、历史计数重置（健康度归 0）、`consecutive_failures+1` |
| G3 WARN 日志 | 探活失败时输出 WARN 级别日志，明确失败原因 | 日志含 indexId 与失败原因（errorMsg），级别 WARN |
| G4 不主动关闭进程 | 探活失败仅更新数据与日志，**不主动关闭**独立代理进程（区别于池的 autoPruneDead） | 探活失败后进程仍运行，`watch`/心跳不受影响 |
| G5 静默周期触发 | 周期触发不发 UI 弹窗/事件（手动触发保持现有反馈） | 周期探活静默执行；手动「测试在线代理」行为不变 |
| G6 文档交付 | 本规格含复用功能清单与配置清单，登记至 `docs/INDEX.md` | 本文件写入 `docs/specs/`，`docs/INDEX.md` §8.2 与 `docs/plans/project-plans-tracker.md` 登记 |

## 2. 复用功能清单

**零新增逻辑，全部复用现有组件**：

| 复用项 | 位置 | 用途 |
| --- | --- | --- |
| `AppController::getWatchedStandaloneMonitors()` | `src/ui/AppController.cpp:1333` | 在线独立代理枚举（含 `socksPort` / `indexId`） |
| `ProxyTester::test(port, cancel, handler)` | `src/ProxyTester.cpp` | 本地 SOCKS 端口端到端探活（本地端口→xray→上游→目标，真实用户流量路径） |
| `ProfileExItemDAO::updateTestResult` | `src/ProfileExItemDAO.cpp:243` | 结果写回：成功 `delay=latency/10`、失败 `delay=-1` + 历史计数重置（健康度归 0）+ `consecutive_failures+1` |
| `AppController::testOnlineProxiesAsync` + `doTestOnlineProxies` | `src/ui/AppController.cpp:484/2136` | 后台线程执行 + `isRunning_` / `ScopeGuard` 防重入 |
| `Logger::write(..., LogLevel::WARN)` | `src/Logger.cpp` | 失败 WARN 日志 |
| `MainFrame::onProxyMonTimer`（`ID_PROXYMON_TIMER`） | `src/ui/MainFrame.cpp:934` | 周期触发载体（进程监控 timer 顺带触发探活） |

## 3. 配置清单

**零新增配置，全部复用现有 `config.json` 项**：

| 配置 | 路径 | 默认值 | 用途 |
| --- | --- | --- | --- |
| `proxy_process_monitor.enabled` | 顶层 | false | 周期探活总开关（随进程监控启用/停用） |
| `proxy_process_monitor.checkIntervalMs` | 顶层 | 30000 | 探活间隔（范围 5000-300000） |
| `test.url` | 顶层 | — | 探活目标 URL |
| `test.timeout_ms` | 顶层 | 5000 | 探活超时（毫秒） |

## 4. 设计

### 4.1 `AppController`：`testOnlineProxiesAsync` / `doTestOnlineProxies` 增强

**`src/ui/AppController.h`**：

```cpp
// silent=true: periodic probe — no TestOnlineProxiesEvent / status-bar chatter.
void testOnlineProxiesAsync(wxEvtHandler* wxHandler, bool silent = false);
```

**`src/ui/AppController.cpp`**：

- `testOnlineProxiesAsync`：透传 `silent` 给 `doTestOnlineProxies`。
- `doTestOnlineProxies(wxEvtHandler* wxHandler, bool silent)`：
  - 循环内失败分支追加 WARN 日志：
    ```cpp
    if (!r.success) {
        Logger::write("[OnlineProbe] test failed: " + mon.indexId
                      + ": " + r.errorMsg, LogLevel::WARN);
    }
    ```
  - `silent=true` 时跳过 `StatusUpdateEvent`（"Testing online proxies..." / "completed"）与 `TestOnlineProxiesEvent`（周期触发不弹窗）；手动（默认 false）行为不变。
  - 其余逻辑（`updateTestResult`、不关闭进程）不变。

### 4.2 `MainFrame`：周期触发

**`src/ui/MainFrame.cpp`**（`onProxyMonTimer` 尾部追加）：

```cpp
void MainFrame::onProxyMonTimer(wxTimerEvent&) {
    if (!controller_) return;
    // ... 现有 adopt + alive count 逻辑不变 ...
    // Periodic silent probe of watched standalone proxies (reuses the online
    // test machinery; isRunning_ inside AppController prevents overlap).
    controller_->testOnlineProxiesAsync(this, true);
}
```

- `proxy_probe` 由 `proxy_process_monitor.enabled` 控制（`onProxyMonTimer` 仅在启用时运行）。
- `isRunning_` 防重入：周期触发与手动触发、批量测试互不叠加（若上一轮未完成则跳过本轮）。
- 不新增 timer / 不新增配置。

## 5. 与池评估的差异

| 维度 | 池 `evaluatorLoop` | 独立代理（本规格） |
| --- | --- | --- |
| 探活 | 成员上游直连 / 探针池 worker | 本地 SOCKS 端口端到端（ProxyTester） |
| 失败动作 | 标记死亡 → REMOVE_REQUESTED → 剔除 | `updateTestResult` + WARN 日志，**不关闭** |
| 结果用途 | 池成员状态 / balancer 样本 | ProfileExItem 时延 / 健康度 |

## 6. 测试策略

| 测试 | 位置 | 覆盖 |
| --- | --- | --- |
| 失败 WARN 日志 + updateTestResult 不变性 | `tests/TestStandaloneProxyPool.cpp` 或现有 online-test 链路 | 失败代理 delay=-1 + WARN 日志（可复用 `doTestOnlineProxies` 单测或集成验证） |
| `silent` 参数不影响数据更新 | 同上 | silent=true 时 updateTestResult 仍执行 |
| 防重入 | 现有 `isRunning_` 机制 | 周期与手动不叠加（现有 ScopeGuard 语义） |
| config 复用 | 无新配置 | 零新增配置，无需解析测试 |

> 周期 timer 属 MainFrame UI 层，测试成本高；以构建验证 + 手动触发链路回归为主。

## 7. 验收标准

1. 构建 0 error（`cmake --build build --parallel 8`）。
2. `ctest` 全量回归无新增失败。
3. 启用进程监控后，在线独立代理按 `checkIntervalMs` 周期静默探活；失败代理 `updateTestResult`（delay=-1 + 历史重置）+ WARN 日志；进程不被关闭。
4. 手动「测试在线代理」行为不变（UI 反馈保留）。
5. `docs/INDEX.md` §8.2 与 `docs/plans/project-plans-tracker.md` 登记本规格与实施计划。

## 8. 相关文档

- `docs/plans/2026-09-15-Plan-StandalonePeriodicProbe-v1.0.md`（实施计划）
- `docs/specs/2026-08-17-Spec-StandaloneProxy-WatchLifecycle-v1.0.md`（独立代理 watch/心跳上下文）
- `docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md`（池评估机制参照）