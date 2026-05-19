---
title: "docs: Project Plans Tracker — global plan index and progress tracker"
type: docs
status: in_progress
date: 2026-05-11
origin: "Continuation of project memory consolidation task"
---

# Project Plans Tracker

## Status: In Progress

> 📝 **2026-05-14** — UI 实现计划已创建 [`docs/plans/2026-05-14-002-ui-implementation-plan.md`](./2026-05-14-002-ui-implementation-plan.md)，共 11 个实施单元 (U1-U11)，分 4 阶段执行。参见下方 UI Implementation 跟踪表。

## Completed Items

### Plan Document Consolidation
- [x] All 20+ plan documents migrated to `docs/plans/`
- [x] Plan 003 (standardize-proxy-test-error-levels) marked `superseded` by merge plan
- [x] Plan 012 (fix-log-level-mismatches-from-audit) marked `superseded` by merge plan
- [x] Plan 004 (cleanup-dead-config-fields) — completed in commit `709972e`, frontmatter added
- [x] Plan 013 reference updated (was in plan 013 doc, now in merge plan)
- [x] DEV-PROCESS.md created with 7-step workflow

### Log Level Standardization (Merge Plan — Completed)
- [x] U1: `ProxyBatchTester.cpp` — 4× `LogLevel::WARN` added for boundary conditions
- [x] U1: `ProxyBatchTester.cpp` — 4× `LogLevel::INFO` replaced REPORT (xray lifecycle + network)
- [x] U2: `SubitemUpdaterV2.cpp` — sync failure summary log added (`LogLevel::ERR`)
- [x] U3: `XrayInstance.cpp` — 4× `LogLevel::ERR` added, 3× REPORT→INFO
- [x] U4: `XrayManager.cpp` — `LogLevel::ERR` (port fail) + `LogLevel::WARN` (instance fail)
- [x] U5: `ConfigGenerator.cpp` — 4× missing `LogLevel` params added (2×INFO, 2×WARN)
- [x] U6: Residual `std::cerr`/`std::cout` removed from `ProxyBatchTester.cpp`

### Notification Config (Plan 009 — Completed)
- [x] `bin/config.json` notification block has `enabled`/`on_update`/`on_test`
- [x] `bin/worker/config_test.json` notification block complete
- [x] `bin/test_config.json` notification block added
- [x] `bin/test_config_full.json` notification block added

### Production Sync Fix
- [x] `bin/worker/config.json` `sync.target_db` corrected
- [x] `bin/worker/config.json` `database.path` corrected to absolute path
- [x] Sync verified: 706/706 proxies migrated successfully
- [x] `docs/plans/2026-05-11-003-fix-sync-config-path-errors-plan.md` created

### Config File Fixes
- [x] `bin/config.json` `log.file_level` changed from `"ERROR"` to `"DEBUG"`
- [x] `bin/config.json` `log.network_failures` changed from `false` to `true`

### Documentation
- [x] AGENTS.md — "核心开发规则" section added
- [x] memory/project_knowledge.md — "开发流程规则" section added
- [x] DEV-PROCESS.md — created with workflow + template + status definitions

## Execution History (post-tracker-creation)

### Recently Completed Plans

| # | Plan | Description | Status |
|---|------|-------------|--------|
| 1 | 13-001 | Sub-update log level INFO→REPORT | ✅ completed |
| 3 | 14-001 | Dedup: filter invalid proxies (REALITY missing key/sni, dirty Network) | ✅ completed
| 2 | 13-002 | Profile progress/inserted log level INFO→REPORT | ✅ completed |
| 3 | 30 (P0) | Fix silent sqlite3_exec + finalize audit | ✅ completed |
| 4 | 31 (P1) | Extract sqlite3_open helper (U1 done, U2 kept as-is) | ✅ completed |
| 5 | 32 (P3) | Extract logInfo/logError helper functions | ✅ completed |
| 6 | 33 (P4) | Extract sqlite::exec helper + Transaction RAII guard | 📝 draft |
| 7 | 14-001 | 在去重功能中去除无效代理 | ✅ completed |
| 8 | 34 (P5) | Fix LogLevel ordering REPORT below ERR | ✅ completed |

### In Progress Plans

| # | Plan | Description | Status |
|---|------|-------------|--------|
| 1 | 14-002 | UI 图形界面实现 — 基于 wxWidgets (见下方 UI Implementation 详表) | 📝 draft |

### UI Implementation — Phase Tracking

| Phase | U# | Description | Status |
|-------|----|-------------|--------|
| Phase 1 | U1 | CMakeLists.txt wxWidgets 集成 | 📝 |
| Phase 1 | U2 | 自定义事件系统 (Events.h) | 📝 |
| Phase 1 | U3 | AppController 控制器层 | 📝 |
| Phase 2 | U8 | 日志面板 (LogPanel) | 📝 |
| Phase 2 | U9 | 配置对话框 (ConfigDialog) | 📝 |
| Phase 2 | U10 | 系统托盘 (TrayIcon) | 📝 |
| Phase 4 | U11 | main.cpp `-ui` 入口修改 | 📝 |
| Phase 3 | U5 | 订阅面板 (SubscriptionPanel) | 📝 |
| Phase 3 | U6 | 代理列表面板 (ProxyListPanel) | 📝 |
| Phase 3 | U7 | 测试面板 (TestPanel) | 📝 |
| Phase 4 | U4 | 主窗口布局 (MainFrame) — 依赖前述所有面板 | 📝 |
| Phase 4 | INT | 集成测试 + 构建验证 | 📝 |

**状态标记:** ✅ completed ｜ 🔄 in_progress ｜ 📝 draft ｜ ❌ blocked