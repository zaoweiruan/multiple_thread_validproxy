---
doc_type: Spec
date: 2026-09-17
module: AppController
version: v1.0
status: completed
scope: src/ui/AppController.cpp
risk: low
---

# Spec — 修复加入代理池未更新 ProfileExItem.message 启动时间侧

## 1. 背景

用户报告：通过 ProxyListPanel 右键「加入代理池」将代理加入独立代理池后，`ProfileExItem.message` 字段未更新启动时间侧。

`ProfileExItem.message` 双时间戳语义（2026-08-11 Spec-ProfileExMessage）：`<测试成功时间>+<启动时间>`，测试时间恒在前，`+` 分隔。加入代理池意味着代理已被启动并注入 Xray balancer，与「启动独立代理」语义一致，应更新启动时间侧。

## 2. 根因

`AppController::injectProxyToPool` L1996-2006 调用 `pool->injectMember(*profile)` 成功后**未调用 `exDao_.updateStartupTime(indexId, db_)`**。

对比既有语义一致路径：

- `AppController::startStandaloneProxy` L1162：`exDao_.updateStartupTime(indexId, db_);` — 启动独立代理后更新
- `AppController::adoptDanglingStandaloneProxies` L1766：`exDao_.updateStartupTime(indexId, db_);` — 纳管悬垂进程后更新
- `AppController::injectProxyToPool`：缺少调用

## 3. 目标 / 非目标

### 目标

1. 加入代理池成功后，`ProfileExItem.message` 的启动时间侧被刷新为当前时间。
2. 与 `startStandaloneProxy`、`adoptDanglingStandaloneProxies` 语义对齐。

### 非目标

- 不修改 `ProfileExItemDAO::updateStartupTime` 契约（仍为 `<测试时间>+<启动时间>` 格式，保留测试时间侧）。
- 不修改 `StandaloneProxyPool::injectMember` 逻辑（仍为池成员注入 + 通知）。
- 不修改 `ProxyListPanel::onAddToPool` UI 流程。
- 不修改 `removePoolMember`（退出池不更新启动时间，保持现状）。

## 4. 实现方案

在 `AppController::injectProxyToPool` 成功加入池后调用 `exDao_.updateStartupTime(indexId, db_)`：

```cpp
bool AppController::injectProxyToPool(const std::string& indexId) {
    std::shared_ptr<proxy::StandaloneProxyPool> pool;
    {
        std::lock_guard<std::mutex> lock(poolMutex_);
        pool = proxyPool_;
    }
    if (!pool) return false;
    std::optional<db::models::Profileitem> profile = getProxyByIndexId(indexId);
    if (!profile) return false;
    bool ok = pool->injectMember(*profile);
    if (ok) {
        // 加入代理池意味着代理已被启动并注入 Xray balancer，与独立代理启动
        // 语义一致：更新 ProfileExItem.message 的启动时间侧（保留测试时间侧）。
        exDao_.updateStartupTime(indexId, db_);
    }
    return ok;
}
```

放置位置：紧跟 `pool->injectMember` 成功返回之后、`return ok` 之前。

## 5. 边界与兼容性

- **`injectMember` 失败**：`ok=false`，不更新 message，保持现状。
- **重复 indexId**：`injectMember` 内部 L83 拒绝重复（`already in pool`），返回 `false`，不更新 message。
- **池未运行**：`pool` 为空或 `running_=false`，`injectMember` 返回 `false`，不更新 message。
- **message 已有测试时间**：`updateStartupTime` 内部 `formatStartupMessage(existingMessage, now)` 保留测试时间侧，仅刷新启动时间侧。
- **message 为空**：`formatStartupMessage` 处理空 existingMessage，仅写入 `+<启动时间>`。
- **DB 切换**：`db_` 是当前活动数据库指针，与 `startStandaloneProxy` 一致。
- **C++17 无 `auto`**：`bool ok` 显式类型。

## 6. 验证计划

1. `cmake --build build --parallel 8` 增量编译通过（零 error）。
2. 启动 `bin\validproxy.exe`，启动代理池，从 ProxyListPanel 右键「加入代理池」加入一个代理。
3. 确认 `ProfileExItem.message` 字段的启动时间侧被刷新为当前时间。

## 7. 变更清单

| 文件 | 变更 |
| :--- | :--- |
| `src/ui/AppController.cpp` | `injectProxyToPool` 成功加入池后追加 `exDao_.updateStartupTime(indexId, db_);` |
| `docs/INDEX.md` §8.2 | 新增 2026-09-17 本 spec 条目 |
| `docs/plans/project-plans-tracker.md` | 近期文档引用表新增 2026-09-17 条目 |
| `docs/specs/2026-09-17-Spec-ProxyPoolInjectMessageUpdate-Fix-v1.0.md` | 本 spec（新建） |
