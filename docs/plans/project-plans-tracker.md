---
title: "docs: Project Plans Tracker — global plan index and progress tracker"
type: docs
status: in_progress
date: 2026-05-11
origin: "Continuation of project memory consolidation task"
---

# Project Plans Tracker

## Status: In Progress

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
| 2 | 13-002 | Profile progress/inserted log level INFO→REPORT | ✅ completed |
| 3 | 30 (P0) | Fix silent sqlite3_exec + finalize audit | ✅ completed |
| 4 | 31 (P1) | Extract sqlite3_open helper (U1 done, U2 kept as-is) | ✅ completed |
| 5 | 32 (P3) | Extract logInfo/logError helper functions | ✅ completed |
| 6 | 33 (P4) | Extract sqlite::exec helper + Transaction RAII guard | 📝 draft |
| 7 | 34 (P5) | Fix LogLevel ordering REPORT below ERR | ✅ completed |

- [x] **Plan 004** (2026-05-11-004): SubitemUpdaterV2 optimization — executed, build+test pass, marked completed
- [x] **Plan 005** (2026-05-11-005): Config transaction batch — analyzed as unnecessary (single query already), cancelled
- [x] **Plan 006** (2026-05-11-006): ShareLinkParser — analyzed as unnecessary (parseSubscription handles it), cancelled
- [x] **Plan 007** (2026-05-12-007): Log config SQL queries — implemented, verified, completed
- [x] **Plan 009** (2026-05-12-009): Remove dead log level field — completed earlier
- [x] **Plan 013** (2026-05-13-001): Adjust sub-update log level INFO→REPORT — SubitemUpdaterV2.cpp lines 165/211, build+test pass, completed
- [x] **Plan 014** (2026-05-13-002): Adjust profile progress/inserted log level INFO→REPORT — SubitemUpdaterV2.cpp lines 969/1052, build+test pass, completed
- [x] **Plan 030** (2026-05-13-P0): Fix silent sqlite3_exec + finalize audit — U1: 4 sqlite3_exec error checks added in SubitemUpdaterV2.cpp, U2: main.cpp finalize audit clean, build+test pass, completed

### Validation
- [x] Full Logger::write audit (clean)
- [x] Build verification (CI mode) — verified on Plan 004/013/014 execution
- [x] Full test suite run (dedup + model + curl_easy_handle) — all passed

## Pending Work

- [ ] **P4** (33): Extract sqlite::exec helper + Transaction RAII guard — see plan doc

## Plan Document Index

| # | File | Type | Status |
|---|------|------|--------|
| 01 | 2026-04-13-module-refactoring-plan.md | refactor | cancelled |
| 02 | 2026-04-17-subscription-url-proxy-fallback-plan.md | feat | cancelled |
| 03 | 2026-04-24-proxy-sync.md | feat | cancelled |
| 04 | 2026-05-07-001-refactor-curl-raii-wrapper-plan.md | refactor | completed |
| 05 | 2026-05-07-002-refactor-dedup-optimization-plan.md | refactor | completed |
| 06 | 2026-05-08-001-fix-sync-logger-plan.md | fix | completed |
| 07 | 2026-05-08-002-fix-cli-argument-handling-plan.md | fix | completed |
| 08 | 2026-05-08-003-standardize-proxy-test-error-levels-plan.md | fix | superseded |
| 09 | 2026-05-08-004-cleanup-dead-config-fields-plan.md | fix | completed |
| 10 | 2026-05-08-005-restore-sub-update-interval-check-plan.md | fix | completed |
| 11 | 2026-05-09-006-fix-sub-update-interval-not-skipping-plan.md | fix | completed |
| 12 | 2026-05-09-007-add-report-loglevel-for-summary-output-plan.md | feat | completed |
| 13 | 2026-05-09-008-fix-skip-not-working-in-proxy-first-mode-plan.md | fix | completed |
| 14 | 2026-05-09-009-fix-notification-config-and-add-missing-notifications-plan.md | fix | completed |
| 15 | 2026-05-09-010-fix-log-file-nul-header-bug-plan.md | fix | completed |
| 16 | 2026-05-09-011-fix-write-after-close-log-bug-plan.md | fix | completed |
| 17 | 2026-05-09-012-fix-log-level-mismatches-from-audit-plan.md | fix | superseded |
| 18 | 2026-05-09-013-fix-sync-sql-placeholder-and-logging-plan.md | fix | completed |
| 19 | 2026-05-10-merge-log-level-standardization-plan.md | fix | completed |
| 20 | 2026-05-11-001-agents-md-improvement-plan.md | docs | completed |
| 21 | 2026-05-11-003-fix-sync-config-path-errors-plan.md | fix | completed |
| 22 | 2026-05-11-004-subitem-updater-v2-optimization-plan.md | refactor | completed |
| 23 | 2026-05-11-005-config-transaction-batch-plan.md | feat | cancelled |
| 24 | 2026-05-11-006-sharelinkparser-plan.md | feat | cancelled |
| 25 | 2026-05-12-007-log-config-sql-queries-plan.md | feat | completed |
| 26 | 2026-05-12-009-remove-dead-log-level-field-plan.md | fix | completed |
| 27 | 2026-05-13-001-adjust-sub-update-log-level-to-report-plan.md | feat | completed |
| 28 | 2026-05-13-002-adjust-profile-progress-inserted-log-level-to-report-plan.md | feat | completed |
| 29 | ~~todo-tracker.md~~ | ~~meta~~ | ~~in_progress~~ (merged into tracker) |
| 30 | 2026-05-13-P0-fix-silent-sqlite3-exec-and-finalize-plan.md | fix | completed |
| 31 | 2026-05-13-P1-extract-sqlite3-open-helper-and-exec-template-plan.md | refactor | completed |
| 32 | 2026-05-13-P3-extract-log-helper-functions-plan.md | refactor | completed |
| 33 | 2026-05-13-P4-extract-sqlite-helper-and-transaction-plan.md | refactor | draft |
| 34 | 2026-05-13-P5-fix-loglevel-ordering-report-below-err-plan.md | fix | draft |