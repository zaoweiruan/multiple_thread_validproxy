---
title: "Spec: 代理池重复注入防护（WARN 日志）（v1.0）"
module: src（StandaloneProxyPool）+ tests
status: 待实施
date: 2026-09-15
supersedes: （无 — 新增规格）
---

# 规格说明：代理池重复注入防护（v1.0）

## 1. 目标

| 目标 | 说明 | 验收标准 |
| --- | --- | --- |
| G1 显式重复检查 | `StandaloneProxyPool::injectMember` 注入前显式检查 `members_` 是否已有相同 indexId | 重复注入返回 false，且不调用 `api_->addOutboundDirect`（避免 Xray 端 "existing tag found" 错误兜底） |
| G2 WARN 日志 | 重复注入时生成 WARN 级别日志，明确「已在池中」 | 日志含 indexId 与 "already in pool" 字样，级别 WARN |
| G3 无行为回归 | 非重复注入行为不变 | 正常注入仍返回 true；`members_` 覆盖语义不变（仅 addOutboundDirect 成功后写入） |
| G4 文档交付 | 本规格登记至 `docs/INDEX.md` | 本文件写入 `docs/specs/`，`docs/INDEX.md` §8.2 与 `docs/plans/project-plans-tracker.md` 登记 |

## 2. 现状与差距

### 2.1 现状

**`StandaloneProxyPool::injectMember`（`src/StandaloneProxyPool.cpp:75-122`）**：

- `if (!running_) return false;` → 构造 outbound JSON → `api_->addOutboundDirect(json, tag)` → 成功则 `members_[m.indexId] = m`。
- **无显式重复检查**：重复注入依赖 Xray 端兜底——Xray-core `outbound.Manager.AddHandler`（`app/proxyman/outbound/outbound.go:115-117`）对重复 tag 返回 `"existing tag found: px-<indexId>"` → `addOutboundDirect` false → `injectMember` false。
- 失败日志：`"[StandaloneProxyPool] inject failed for " + tag`（ERR 级别），不区分「重复」与「真失败」。

### 2.2 差距

| # | 差距 | 影响 |
| --- | --- | --- |
| D1 | 重复注入无显式检查，依赖 Xray 端报错兜底 | 日志无法区分「已在池中」与「注入失败」；多一次无效 gRPC 调用 |
| D2 | 失败日志级别 ERR | 重复注入是预期内拒绝（用户重复选择），不应按错误告警 |

## 3. 设计

### 3.1 `StandaloneProxyPool::injectMember` 显式重复检查

**`src/StandaloneProxyPool.cpp`**（`injectMember` 开头，`running_` 检查后）：

```cpp
bool StandaloneProxyPool::injectMember(const db::models::Profileitem& profile) {
    if (!running_) return false;
    const long long indexId = std::atoll(profile.indexid.c_str());
    {
        std::lock_guard<std::mutex> lock(membersMutex_);
        if (members_.find(indexId) != members_.end()) {
            Logger::write("[StandaloneProxyPool] inject rejected: " + profile.indexid
                          + " already in pool", LogLevel::WARN);
            return false;
        }
    }
    // ... 原有 addOutboundDirect + members_[m.indexId] = m 逻辑不变
}
```

- 保持 `bool` 返回（调用方 `AppController::injectProxyToPool` 与 UI 提示逻辑不变）。
- 重复检查在锁内进行（与 `members_` 写入同锁，避免竞态）。
- 重复时**不调用** `api_->addOutboundDirect`（省一次无效 gRPC，日志语义清晰）。

### 3.2 测试策略（TDD）

| 测试 | 位置 | 覆盖 |
| --- | --- | --- |
| 重复注入返回 false | `tests/TestStandaloneProxyPool.cpp`（Live，`XRAY_REAL_EXE` opt-in） | 注入成员 → 再次注入同 indexId → 第二次返回 false |
| 非重复注入不受影响 | 同上（Live） | 注入不同 indexId 仍返回 true |

## 4. 验收标准

1. 构建 0 error（`cmake --build build --parallel 8`）。
2. 新增测试全绿（Live opt-in）；`ctest` 全量回归无新增失败。
3. 重复注入返回 false + WARN 日志（含 indexId 与 "already in pool"）；不调用 `addOutboundDirect`。
4. `docs/INDEX.md` §8.2 与 `docs/plans/project-plans-tracker.md` 登记本规格与实施计划。

## 5. 相关文档

- `docs/plans/2026-09-15-Plan-PoolInjectDuplicateGuard-v1.0.md`（实施计划）
- `docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md`（统一池监控，池生命周期上下文）