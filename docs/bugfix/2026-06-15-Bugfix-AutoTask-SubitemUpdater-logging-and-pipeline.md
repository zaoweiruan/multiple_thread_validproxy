# Bug Fix: AutoTask Pipeline Failure & Invisible Diagnostics Under `file_level: ERROR`

- **Date**: 2026-06-15
- **Affected modules**: `SubitemUpdaterV2`, `ProxyBatchTester`, `AutoTaskManager`, `ConfigDialog`, `UrlFetcher`
- **Root cause**: Three interacting issues caused the AutoTask "update_all" step to fail silently without visible diagnostic logs.

---

## Problem

User configured `bin/config.json` with `file_level: ERROR` and `check_auto_update_interval: true`. Running AutoTask produced:
```
step [update_all] failed
```
with **no visible error details** in the log output. The pipeline halted entirely (no subsequent steps ran).

## Root Cause Analysis

| Layer | Issue | Visibility |
|-------|-------|------------|
| **Log level** | Connection-failure messages (`fetchUrl failed`, `accelerator connection failed`) were at INFO level | Hidden under `file_level: ERROR` |
| **Update interval** | All subscriptions were skipped by `shouldSkipUpdate()` — only at INFO level | Hidden under `file_level: ERROR` |
| **Pipeline return** | `SubitemUpdaterV2::run()` returned `false` when `successCount == 0` — but all-skipped is not a failure | Triggered the "failed" status |

The chain: `check_auto_update_interval: true` → all subs skip → INFO-level log invisible → `successCount == 0` → `run()` returns false → AutoTaskManager reports "step [update_all] failed" with no explanation.

## Fixes Applied

### 1. Log Level Promotions (all → ERR)

Promoted critical diagnostic messages from INFO/DEBUG to ERR so they survive `file_level: ERROR`:

- `"fetchUrl failed - {exception}"` — `src/UrlFetcher.cpp` — ERR
- `"accelerator_url empty, falling back to direct fetch"` — `src/SubitemUpdaterV2.cpp` — ERR
- `"accelerator connection failed"` — `src/SubitemUpdaterV2.cpp` — ERR
- New: `"All subscriptions failed to update - check network connectivity"` — `src/SubitemUpdaterV2.cpp` — ERR
- New: `"All subscriptions skipped by update interval - nothing to update"` — `src/SubitemUpdaterV2.cpp` — ERR

### 2. `SubitemUpdaterV2::run()` Return Path Fix

`src/SubitemUpdaterV2.cpp` — added three distinct return paths:

| Scenario | New behavior | Old behavior |
|----------|-------------|--------------|
| `enabledSubs.empty()` | `return true` (no subscriptions = success, nothing to update) | `return false` |
| All skipped by `shouldSkipUpdate()` | `return true` with ERR log (skipped by interval, not a failure) | `return false` (silently) |
| All attempted but all failed | `return false` with ERR log (genuine failure, with visibility) | `return false` (silent) |

Tracking: added `attemptedCount` to distinguish "skipped" from "attempted but failed".

### 3. AutoTask Step Name Aliases

`AutoTaskManager::stepNameToType()` — added `"update_all"` → `UPDATE_ALL` and `"test_all"` → `TEST_ALL` aliases so config strings match the actual step names in the spec.

### 4. ConfigDialog Refinements

| Change | File | Detail |
|--------|------|--------|
| No hardcoded default step | `ConfigDialog::saveConfig()` | Removed `push_back("update_all")` when steps empty |
| No default chain display text | `ConfigDialog::refreshAutoTaskChainDisplay()` | Show empty instead of `"(无)"` |
| State file path editable | `ConfigDialog::loadConfig()` | Removed `SetPropertyReadOnly("autotask_state_file")` |
| State file default from exeDir | `ConfigDialog::loadConfig()` | Use `wxStandardPaths::Get().GetExecutablePath()` instead of `database_path.parent_path()` |
| Step order follows click | `ConfigDialog` | Added `stepOrder_` vector; `onPropertyChanged` appends/removes |

### 5. Empty Subscription Edge Case

`SubitemUpdaterV2::run()` — return `true` when `enabledSubs.empty()` (no subscriptions configured is not an error condition).

### 6. `ProxyBatchTester::run()` Zero-Proxy Return Fix

`src/ProxyBatchTester.cpp` — changed `return false` to `return true` when `totalProxies_ == 0`:

| Scenario | New behavior | Old behavior |
|----------|-------------|--------------|
| No proxies to test (SQL returns empty) | `return true` (nothing to test = success, pipeline continues) | `return false` (halts chain with `[ERROR] step [test_all] failed`) |

Same pattern as fix #2: an empty result set is not a pipeline failure.

## Verification

1. All 9 ctest test executables pass
2. Logs under `file_level: ERROR` now show clear ERR-level messages for skip/connection failures
3. `run()` returns `true` for empty/all-skipped scenarios, allowing subsequent AutoTask steps to proceed
4. `ProxyBatchTester::run()` returns `true` when no proxies need testing
