---
title: "docs: Project Plans Tracker — global plan index and progress tracker"
type: docs
status: in_progress
date: 2026-05-11
origin: "Continuation of project memory consolidation task"
---

# Project Plans Tracker

## Status: In Progress

> 📝 **2026-05-19** — 批量状态更新: P4→❌ cancelled; 003→✅ completed; DLL fix→✅ completed; libpng16d/libtiffd fix→✅ completed; consolidated-ui-fixes→✅ completed; 007 frontmatter repaired.

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
| 3 | 14-001 | Dedup: filter invalid proxies (REALITY missing key/sni, dirty Network) | ✅ completed |
| 2 | 13-002 | Profile progress/inserted log level INFO→REPORT | ✅ completed |
| 3 | 30 (P0) | Fix silent sqlite3_exec + finalize audit | ✅ completed |
| 4 | 31 (P1) | Extract sqlite3_open helper (U1 done, U2 kept as-is) | ✅ completed |
| 5 | 32 (P3) | Extract logInfo/logError helper functions | ✅ completed |
| 6 | 33 (P4) | Extract sqlite::exec helper + Transaction RAII guard | ❌ cancelled |
| 7 | 14-001 | 在去重功能中去除无效代理 | ✅ completed |
| 8 | 34 (P5) | Fix LogLevel ordering REPORT below ERR | ✅ completed |
| 9 | conse ui-fixes **consolidated-ui-fixes** | Fix single proxy test delay refresh + event flow | ✅ completed |
| 10 | DLL-fix **DLL Issue** | Fix wxmsw32ud_aui_gcc_custom.dll missing (Build type mismatch) | ✅ completed |
| 11 | DLL-fix **libpng16d/libtiffd** | Fix libpng16d.dll and libtiffd.dll missing for Debug build | ✅ completed |
| 12 | **Single proxy test report** | Technical report: TestSubId→IndexId, Delay refresh, event flow | ✅ completed |
| 13 | **UI enhancements plan** | Column sorting + Find individual proxy + Subscription linkage | 📝 draft |
| 14 | **UI close hang fix** | Fix AppController/XrayInstance destructor race + process handle bugs | ✅ completed |
| 15 | **Cancel support for update** | Add runtime cancel support for subscription update (SubitemUpdaterV2 + MainFrame dynamic cancel button) | ✅ completed |
| 16 | **ProxyFinder cancel propagation** | Wire externalCancel_ to ProxyFinder in SubitemUpdaterV2::getProxyPorts() | ✅ completed |

### In Progress Plans

| # | Plan | Description | Status |
|---|------|-------------|--------|
| 1 | 14-002 | UI 图形界面实现 — 基于 wxWidgets (见下方 UI Implementation 详表) | 📝 draft |

---

### `.kilo/plans/` 迁移条目

> 来源: `.kilo/plans/` → `docs/plans/` 合并迁移 (已格式化为规范命名)
> 日期: 2026-05-19

#### 已完成

| # | Plan File | Original Source | Description | Status |
|---|-----------|----------------|-------------|--------|
| 1 | `2026-05-14-003-proxy-validation-tool-architecture.md` | `1776914861549-gentle-panda.md` | 项目整体架构设计 + 构建系统 + 阶段的实现顺序 | 📝 参考文档 |
| 2 | `2026-05-18-002-fix-empty-test-results.md` | `1778814717912-tidy-meadow.md` | 修复单代理测试时 TestPanel 结果列表为空 | ✅ completed |
| 3 | `2026-04-23-001-fix-sql-delay-filter.md` | `1776415141516-jolly-mountain.md` | main.cpp SQL Delay>0 条件修正 + ShareLink 导出修复（2026-04-23） | ✅ completed |
| 4 | `2026-05-19-consolidated-ui-fixes.md` | 合并计划 | 单代理测试 Delay 列刷新 + 事件流程修复 | ✅ completed |

#### 报告归档 (docs/reports/ )

| # | Report File | Original Source | Description | Status |
|---|-------------|----------------|-------------|--------|
| 1 | `docs/reports/2026-04-23-sharelink-export-repair-report.md` | `1776931315746-sunny-rocket.md` | ShareLink 导出分享链接修复完成报告（5项修复已验证） | ✅ completed |

#### 草稿进行中

| # | Plan File | Original Source | Description | Status |
|---|-----------|----------------|-------------|--------|
| ~~1-5~~ | ~~See below - merged into consolidated plan~~ | ~~N/A~~ | ~~See 2026-05-19-consolidated-ui-fixes.md~~ | ✅ **MERGED** |

### UI Implementation — Phase Tracking

| Phase | U# | Description | Status |
|-------|----|-------------|--------|
| Phase 1 | U1 | CMakeLists.txt wxWidgets 集成 | ✅ |
| Phase 1 | U2 | 自定义事件系统 (Events.h) | ✅ |
| Phase 1 | U3 | AppController 控制器层 | ✅ |
| Phase 2 | U8 | 日志面板 (LogPanel) | ✅ |
| Phase 2 | U9 | 配置对话框 (ConfigDialog) | ✅ |
| Phase 2 | U10 | 系统托盘 (TrayIcon) | ✅ |
| Phase 4 | U11 | main.cpp `-ui` 入口修改 | ✅ |
| Phase 3 | U5 | 订阅面板 (SubscriptionPanel) | ✅ |
| Phase 3 | U6 | 代理列表面板 (ProxyListPanel) | ✅ |
| Phase 3 | U7 | 测试面板 (TestPanel) | ✅ |
| Phase 4 | U4 | 主窗口布局 (MainFrame) — 依赖前述所有面板 | ✅ |
| Phase 4 | INT | 集成测试 + 构建验证 | ✅ |
| **已完成** | sort/find/link | 列排序 + 查找单个代理 + 订阅联动 | ✅ completed |

**状态标记:** ✅ completed ｜ 🔄 in_progress ｜ 📝 draft ｜ ❌ blocked

---

### `.kilo/plans/` 归档清理记录

> 原始文件迁移完成后，`.kilo/plans/` 目录已删除 (2026-05-19)。
> 以下计划原存于 `.kilo/plans/`，均已按规范命名迁移至 `docs/` 相应位置。
> `.kilo/plans/` 源文件保留情况: `architecture.md` / `context.md` 已确认无需迁移(同内容在 `docs/`)，其余 8 份均为临时 plan ID 命名，已完全迁移，`docs/` 即为唯一权威存放地。

| 原计划文件 (MD5) | 规范命名 | 目标位置 | 状态 |
|-----------------|---------|---------|------|
| `1776215451920-nimble-wolf.md` | `2026-05-14-004-gh-mcp-list-top-repos.md` | `docs/plans/` | 📝 draft |
| `1776415141516-jolly-mountain.md` | `2026-04-23-001-fix-sql-delay-filter.md` | `docs/plans/` | ✅ completed |
| `1776931315746-sunny-rocket.md` | `2026-04-23-sharelink-export-repair-report.md` | `docs/reports/` | ✅ completed |
| `1776914861549-gentle-panda.md` | `2026-05-14-003-proxy-validation-tool-architecture.md` | `docs/plans/` | ✅ completed |
| `1778814717912-tidy-meadow.md` | `2026-05-18-002-fix-empty-test-results.md` | `docs/plans/` | ✅ completed |
| `1779070922736-shiny-comet.md` + `eager-moon.md` + `silent-harbor.md` | `2026-05-19-consolidated-ui-fixes.md` | `docs/plans/` | ✅ **MERGED & COMPLETED** |
| `architecture.md` | 同名 | `docs/architecture.md` | 同内容已存在，不迁移 |
| `context.md` | 同名 | `docs/context.md` | 同内容已存在，不迁移 |