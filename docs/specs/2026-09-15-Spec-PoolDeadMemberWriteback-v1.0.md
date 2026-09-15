---
title: "Spec: 代理池死亡成员剔除后测试数据写回（v1.0）"
module: src（StandaloneProxyPool / AppController）+ tests
status: 待实施
date: 2026-09-15
supersedes: （无 — 新增规格）
---

# 规格说明：代理池死亡成员剔除后测试数据写回（v1.0）

## 1. 目标

| 目标 | 说明 | 验收标准 |
| --- | --- | --- |
| G1 时延写回 | 代理池中被剔除的死亡代理，时延按独立代理测试失败方式更新 | 死亡成员被剔除时，`ProfileExItem.delay` 置为 `-1`（代理列表时延列显示无效） |
| G2 健康度写回 | 死亡代理健康度按独立代理测试失败方式更新 | `ProfileExItem.start_count/crash_count` 重置为 0（健康度归 0.0）；`consecutive_failures` +1 |
| G3 触发范围 | 仅死亡成员（`failStreak >= pruneFailStreak` 或 `lastAlive == false`）被剔除时写回 | 手动移除健康成员不触发写回；死亡成员经 autoPruneDead 或优雅移除路径剔除时触发 |
| G4 解耦 | 池不直接访问数据库 | `StandaloneProxyPool` 通过 `onMemberRemoved` 回调通知；`AppController` 注入回调执行 `ProfileExItemDAO::updateTestResult` |
| G5 文档交付 | 本规格登记至 `docs/INDEX.md` | 本文件写入 `docs/specs/`，`docs/INDEX.md` §8.2 与 `docs/plans/project-plans-tracker.md` 登记 |

## 2. 现状与差距

### 2.1 现状

**2.1.1 代理池死亡剔除路径（`src/StandaloneProxyPool.cpp`）**

- `evaluatorLoop`（L290-349）：每 `intervalSec` 一轮 `relaunchIfNeeded → doProbe → mergeHealth → 策略 → 两阶段移除 → notifyChanged`。
- autoPruneDead（L303-311）：`failStreak >= cfg_.evaluate.pruneFailStreak` 的 ACTIVE 成员标记 `REMOVE_REQUESTED`。
- 两阶段移除（L325-338）：`REMOVE_REQUESTED` → `api_->removeOutbound(tag)` 成功 → `DRAINING` → `members_.erase`。
- `removeMember`（L124-141）：`graceful=false` 立即 `removeOutbound + erase`；`graceful=true` 标记 `REMOVE_REQUESTED` 由 evaluatorLoop 处理。
- 现有回调：`onMembersChanged`（每轮评估后携带快照，UI 刷新用）。

**2.1.2 独立代理测试后数据更新（`src/ProfileExItemDAO.cpp:243` `updateTestResult`）**

- 成功：`delay = latencyMs/10`，`message = 测试时间`，`consecutive_failures = 0`，保留历史计数。
- 失败：`delay = "-1"`，`message` 保持原样，`consecutive_failures + 1`，**重置 `start_count/total_runtime_ms/crash_count` 为 0**（健康度归 0）。
- 独立代理（standalone proxy）测试失败调用点：`AppController.cpp:978/1013/1046/2150/2162`。

### 2.2 差距

| # | 差距 | 影响 |
| --- | --- | --- |
| D1 | 池死亡成员被剔除后，`ProfileExItem` 无任何写回 | 代理列表中该代理时延仍显示旧有效值、健康度不反映池中死亡状态，与实际不符 |
| D2 | 池与 DB 无解耦通道 | 池内部无法（也不应）直接访问数据库 |

## 3. 设计

### 3.1 `StandaloneProxyPool` 新增回调与死亡判断

**`include/StandaloneProxyPool.h`**：

```cpp
// Invoked when a dead member is removed from the pool (autoPruneDead or
// graceful removal). The caller (AppController) writes the failure back to
// ProfileExItem so the proxy list reflects the pool's death verdict.
std::function<void(long long indexId)> onMemberRemoved;

// Pure predicate: a member is "dead" when its fail streak reached the prune
// threshold or its last probe was not alive. Exposed for tests.
static bool isDeadMember(const PoolMember& m, int pruneFailStreak);
```

**`src/StandaloneProxyPool.cpp`**：

- `isDeadMember` 实现：`m.failStreak >= pruneFailStreak || !m.lastAlive`。
- `evaluatorLoop` 两阶段移除 erase 处（L336-338）：erase 前判断 `isDeadMember`，死亡则调用 `onMemberRemoved(indexId)`（回调非空时）。
- `removeMember` 非优雅路径（L131-133）：erase 前同样判断死亡并触发回调；同时补 `api_` null 安全（未 start 的池移除成员不崩溃）。

### 3.2 `AppController` 注入回调写回

**`src/ui/AppController.cpp` `startProxyPool`（L1855 后）**：

```cpp
pool->onMemberRemoved = [this](long long indexId) {
    if (!db_) return;
    db::models::ProfileExItemDAO dao(db_);
    dao.updateTestResult(std::to_string(indexId), -1, false,
                         "pool member removed (dead)");
};
```

- 写回语义与独立代理测试失败完全一致（`updateTestResult` 失败路径）。
- `message` 保持原样（`updateTestResult` 失败不覆盖 message，与独立代理一致）。

### 3.3 测试策略（TDD）

| 测试 | 位置 | 覆盖 |
| --- | --- | --- |
| `isDeadMember` 纯函数 | `tests/TestStandaloneProxyPool.cpp`（离线） | failStreak 达阈值 / lastAlive=false / 健康成员 false / 组合 |
| removeMember 不触发回调 | 同上（离线） | 手动移除健康成员不触发 `onMemberRemoved` |
| 死亡剔除触发回调 | 同上（Live，`XRAY_REAL_EXE` opt-in） | 注入不可达成员 → 等 evaluatorLoop 剔除 → 断言回调收到 indexId |
| 写回语义 | 复用 `ProfileExItemDAOTest` | `updateTestResult` 失败路径 delay=-1 / 历史重置（既有覆盖） |

## 4. 验收标准

1. 构建 0 error（`cmake --build build --parallel 8`）。
2. 新增 GTest 单测全绿；`ctest` 全量回归无新增失败。
3. 死亡成员被剔除时 `onMemberRemoved` 触发；健康成员手动移除不触发。
4. `AppController` 回调写回 `ProfileExItem`：delay=-1、start_count/crash_count=0、consecutive_failures+1。
5. `docs/INDEX.md` §8.2 与 `docs/plans/project-plans-tracker.md` 登记本规格与实施计划。

## 5. 相关文档

- `docs/plans/2026-09-15-Plan-PoolDeadMemberWriteback-v1.0.md`（实施计划）
- `docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md`（统一池监控，池生命周期上下文）
- `docs/specs/2026-08-11-Spec-ProfileExMessage-v1.0.md`（message 双时间戳格式，失败不覆盖语义）