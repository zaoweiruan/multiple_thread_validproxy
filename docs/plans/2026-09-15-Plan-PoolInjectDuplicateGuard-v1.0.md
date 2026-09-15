---
title: "Plan: 代理池重复注入防护（v1.0）"
module: src（StandaloneProxyPool）+ tests
status: 待实施
date: 2026-09-15
supersedes: （无）
---

# 实施计划：代理池重复注入防护（v1.0）

规格：`docs/specs/2026-09-15-Spec-PoolInjectDuplicateGuard-v1.0.md`

## 任务清单

### Task 1：`injectMember` 显式重复检查 + WARN 日志

**位置**：`src/StandaloneProxyPool.cpp`（`injectMember`，L75 起）

**变更**：
1. `running_` 检查后，锁内查 `members_` 是否已有 `std::atoll(profile.indexid.c_str())`。
2. 已存在 → `Logger::write("[StandaloneProxyPool] inject rejected: <indexId> already in pool", LogLevel::WARN)` + `return false`（不调用 `addOutboundDirect`）。
3. 其余逻辑不变（保持 bool 返回）。

**验收**：重复注入返回 false + WARN 日志；非重复注入行为不变。

### Task 2：测试（Live opt-in）

**位置**：`tests/TestStandaloneProxyPool.cpp`

**变更**：新增 Live 用例（`XRAY_REAL_EXE` opt-in，无则 `GTEST_SKIP`）：
1. 注入成员 A（indexId=X）→ 再次注入 X → 第二次返回 false。
2. 注入不同 indexId → 返回 true。

**验收**：有 `XRAY_REAL_EXE` 时通过；无则跳过（CI 保持绿）。

### Task 3：构建验证 + 文档登记

**位置**：构建产物 + `docs/INDEX.md` + `docs/plans/project-plans-tracker.md`

**变更**：
1. `cmake --build build --parallel 8` 0 error。
2. `ctest --test-dir build -E "UI_"` 全量回归无新增失败。
3. `docs/INDEX.md` §8.2 登记本计划；`docs/plans/project-plans-tracker.md` 新增引用行。

**验收**：构建 0 error；ctest 全绿；文档登记完成。

## 依赖关系

- Task 1 → Task 2（重复检查先行）
- Task 3 最后执行

## 风险与对策

| 风险 | 对策 |
| --- | --- |
| 重复检查与 `members_` 写入竞态 | 检查与写入同锁（`membersMutex_`） |
| Live 测试依赖真实 xray | `XRAY_REAL_EXE` opt-in，无则跳过（CI 保持绿） |