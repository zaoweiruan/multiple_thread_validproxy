---
title: "Plan: 代理池死亡成员剔除后测试数据写回（v1.0）"
module: src（StandaloneProxyPool / AppController）+ tests
status: 待实施
date: 2026-09-15
supersedes: （无）
---

# 实施计划：代理池死亡成员剔除后测试数据写回（v1.0）

规格：`docs/specs/2026-09-15-Spec-PoolDeadMemberWriteback-v1.0.md`

## 任务清单

### Task 1：`StandaloneProxyPool` 回调 + 死亡判断 + 剔除触发

**位置**：`include/StandaloneProxyPool.h`、`src/StandaloneProxyPool.cpp`

**变更**：
1. `StandaloneProxyPool.h` 新增：
   - `std::function<void(long long indexId)> onMemberRemoved;`（public，`onMembersChanged` 旁）
   - `static bool isDeadMember(const PoolMember& m, int pruneFailStreak);`（public，可测试）
2. `StandaloneProxyPool.cpp`：
   - 实现 `isDeadMember`：`m.failStreak >= pruneFailStreak || !m.lastAlive`。
   - `evaluatorLoop` erase 处（L336-338）：erase 前 `isDeadMember` 判断，死亡则 `onMemberRemoved(indexId)`（回调非空时）。
   - `removeMember` 非优雅路径（L131-133）：`api_` null 安全（`if (api_)`）+ erase 前死亡判断触发回调。

**验收**：死亡成员剔除触发回调；健康成员移除不触发；未 start 池移除成员不崩溃。

### Task 2：`AppController` 注入回调写回

**位置**：`src/ui/AppController.cpp` `startProxyPool`（L1855 后）

**变更**：`pool->onMemberRemoved` 注入 lambda：`db_` 非空时 `ProfileExItemDAO(db_).updateTestResult(std::to_string(indexId), -1, false, "pool member removed (dead)")`。

**验收**：死亡成员剔除后 `ProfileExItem` 写回（delay=-1、历史重置、consecutive_failures+1）。

### Task 3：GTest 单测（离线）

**位置**：`tests/TestStandaloneProxyPool.cpp`

**变更**：
1. `isDeadMember` 用例：failStreak 达阈值 true / 未达阈值但 lastAlive=false true / 健康成员 false / 组合。
2. removeMember 不触发回调：构造池（不 start）→ 注入成员 → 设 `onMemberRemoved` 计数 → `removeMember(id, false)` → 断言回调未触发（健康成员）。

**验收**：新用例全绿。

### Task 4：Live 测试（opt-in）

**位置**：`tests/TestStandaloneProxyPool.cpp`

**变更**：新增 Live 用例（`XRAY_REAL_EXE` opt-in，无则 `GTEST_SKIP`）：注入不可达成员 → 设 `onMemberRemoved` 捕获 → 等 evaluatorLoop 多周期（intervalSec 调小）→ 断言回调收到 indexId。

**验收**：有 `XRAY_REAL_EXE` 时通过；无则跳过（CI 保持绿）。

### Task 5：构建验证 + 文档登记

**位置**：构建产物 + `docs/INDEX.md` + `docs/plans/project-plans-tracker.md`

**变更**：
1. `cmake --build build --parallel 8` 0 error。
2. `ctest --test-dir build -E "UI_"` 全量回归无新增失败。
3. `docs/INDEX.md` §8.2 登记本计划；`docs/plans/project-plans-tracker.md` 新增引用行。

**验收**：构建 0 error；ctest 全绿；文档登记完成。

## 依赖关系

- Task 1 → Task 2（回调先行）
- Task 3 可与 Task 1 并行（isDeadMember 纯函数）
- Task 4 依赖 Task 1（Live 验证回调触发）
- Task 5 最后执行

## 风险与对策

| 风险 | 对策 |
| --- | --- |
| evaluatorLoop 是后台线程，离线测试难触发剔除 | isDeadMember 纯函数离线测；回调触发用 Live 测试（opt-in） |
| removeMember 非优雅路径 `api_` 可能为 null（未 start） | 补 `if (api_)` 安全守卫（顺带加固） |
| 死亡判断与 pruneFailStreak 语义耦合 | `isDeadMember` 接收 pruneFailStreak 参数，测试显式传值 |