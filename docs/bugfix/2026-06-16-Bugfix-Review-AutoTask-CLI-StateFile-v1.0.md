# Bug Fix: AutoTask CLI Cancel, State File Path, and Config SQL Threshold

- **Date**: 2026-06-16
- **Affected modules**: `main_cli`, `AutoTaskManager`, `ConfigDialog`, `NetworkMonitor`, `MainFrame`, `icons.rc`
- **Root cause**: Multiple issues found during uncommitted code review of AutoTask integration.

---

## Problems Found

| # | Severity | Module | Issue |
|---|----------|--------|-------|
| 1 | **WARNING** | `main_cli.cpp` | CLI Ctrl+C cancel never reaches AutoTaskManager — `nullptr` passed for `externalCancel` |
| 2 | **WARNING** | `AutoTaskManager.cpp` | State file path creates double-nested `worker/worker/` in GUI mode |
| 3 | **WARNING** | `config.json` | SQL `consecutive_failures` filter changed from `between 1 and 1` to `between 0 and 0`, reducing eligible proxy pool by ~99% |
| 4 | **WARNING** | `icons.rc` | Status bar icon RCDATA entries unused + filename typo `gree` vs `green` |
| 5 | SUGGESTION | `AppController.cpp` | `doRunAutoTask`/`doResumeAutoTask` are ~30-line near-identical methods |
| 6 | SUGGESTION | `AutoTaskManager.cpp` | State file `version` field written but never validated on load |

---

## Detailed Findings

### 1. CLI Ctrl+C Cancel Never Reaches AutoTaskManager

**Files**: `src/main_cli.cpp:736,829` + `src/AutoTaskManager.cpp:318-321`

**Problem**: Both `-AT` and `-RS` CLI handlers construct `AutoTaskManager` with `nullptr` for `externalCancel`:

```cpp
AutoTaskManager manager(db, *appConfig, exeDir, nullptr);  // line 736 & 829
```

The Ctrl+C handler sets global `g_cancelRequested`, but `isCancelled()` guards against the null pointer:

```cpp
bool AutoTaskManager::isCancelled() const {
    if (externalCancel_ && externalCancel_->load()) return true;  // nullptr → skipped
    return cancelRequested_.load();  // never set by CLI code
}
```

**Result**: Ctrl+C during a CLI auto-task is silently ignored. The pipeline runs to completion until a step fails on its own.

**Fix**: Pass `&g_cancelRequested` at both construction sites:
```cpp
AutoTaskManager manager(db, *appConfig, exeDir, &g_cancelRequested);
```

---

### 2. State File Path Double-Nested in GUI Mode

**Files**: `src/AutoTaskManager.cpp:30-34`, `src/ui/AppController.cpp:956`, `src/main_cli.cpp:773-774`

**Problem**: Three independent fallback computations for the state file path:

| Location | `baseDir` | Resulting path |
|----------|-----------|----------------|
| `AutoTaskManager.cpp:33` | `baseDir_` (constructor arg) | `{baseDir_}/worker/autotask_state.json` |
| `AppController.cpp:956` | `config_.database_path.parent_path()` = `.../bin/worker` | `.../bin/worker/worker/autotask_state.json` |
| `main_cli.cpp:773` | `exeDir + "/worker/..."` where `exeDir` = `.../bin/` | `.../bin/worker/autotask_state.json` |

The GUI mode uses `database_path.parent_path()` (already `bin/worker/`) as `baseDir`, producing a double-nested `worker/worker/` subdirectory. CLI mode uses `exeDir` (`bin/`) which is correct. The GUI writes state files to a location the CLI cannot find for `-CA`/`-RS`.

**Fix**: Centralize fallback into a single helper:
```cpp
// AutoTaskManager.h
static std::string defaultStateFilePath(const std::string& baseExeDir);
```
Use `exeDir` (not database parent) in GUI mode. Remove the duplicate fallback logic from `ConfigDialog.cpp`.

---

### 3. SQL `consecutive_failures` Threshold Change

**File**: `bin/config.json` (database.sql field)

**Problem**: The SQL WHERE clause changed from:
```sql
-- old: proxies with exactly 1 consecutive failure
WHERE (pe.consecutive_failures IS NULL OR pe.consecutive_failures between 1 and 1)
```
```sql
-- new: proxies with exactly 0 consecutive failures
WHERE (pe.consecutive_failures IS NULL OR pe.consecutive_failures between 0 and 0)
```

The eligible proxy pool drops from ~65,076 to ~426 in the worker DB (~99.3% reduction). The per-subscription `sql_by_subid` still uses `< {blacklist_threshold}` (default 5), creating an inconsistency — a proxy with 4 failures is testable individually but excluded from batch "test-all."

**Risk**: This change may be intentional ("only test clean proxies in batch mode"), but the inconsistency with `sql_by_subid` can silently starve batch operations.

**Fix**: Either:
- Align `sql` with `sql_by_subid` to use `< {blacklist_threshold}`, or
- Add a code comment explaining the intentional divergence (e.g., "batch mode only tests clean proxies")

---

### 4. Status Bar Icons Unused + Typo

**File**: `src/ui/icons.rc:31-32`

**Problem**: Two RCDATA entries compiled but never loaded:

```rc
statusbar_gree_png    RCDATA "docs/design/ui/icon/png/statusbar_gree.png"  // typo: missing "n"
statusbar_red_png     RCDATA "docs/design/ui/icon/png/statusbar_red.png"
```

- The filename has a typo: `statusbar_gree.png` instead of `statusbar_green.png` (file on disk is named correctly)
- No code ever calls `ToolbarIcons::load("statusbar_green")` or `ToolbarIcons::load("statusbar_red")`
- The network status panel at `MainFrame.cpp:282-312` draws green/red dots entirely via GDI (`wxPaintDC::DrawCircle`)

**Fix**: Remove both entries from `icons.rc` and delete the unused PNG files from `docs/design/ui/icon/png/`.

---

### 5. `doRunAutoTask`/`doResumeAutoTask` Duplication

**File**: `src/ui/AppController.cpp:951-1015`

**Problem**: Two 30-line methods with identical structure (ResetGuard, baseDir computation, manager construction, progress callback lambda, wxQueueEvent, exception handler). The only difference is:

```cpp
// line 969
bool ok = manager.run(config_.auto_task.steps);
```
```cpp
// line 1002
bool ok = manager.resume();
```

Any behavioral change must be applied in both places.

**Fix**: Merge into a single helper:
```cpp
void AppController::doAutoTaskImpl(wxEvtHandler* wxHandler,
    std::function<bool(AutoTaskManager&)> runFn) {
    struct ResetGuard { std::atomic<bool>& flag; ~ResetGuard() { flag = false; } };
    ResetGuard _rg{isRunning_};
    try {
        std::string baseDir = ...;
        AutoTaskManager manager(db_, config_, baseDir, &cancelRequested_, &netMon_);
        manager.setProgressCallback([wxHandler](const AutoTaskProgress& p) { ... });
        bool ok = runFn(manager);
        ...
    } catch (...) { ... }
}
```

---

### 6. State File Version Not Validated

**File**: `src/AutoTaskManager.cpp:404,443-506`

**Problem**: `saveStateFile` writes `"version": 1` but `loadStateFile` never checks it. If a future version bumps the format, a rollback would silently interpret v2 data under v1 assumptions — potentially corrupting step indices or status arrays.

**Fix**: Add version validation:
```cpp
int fileVersion = 0;
if (root.contains("version") && root["version"].is_int64())
    fileVersion = static_cast<int>(root["version"].as_int64());
if (fileVersion > 1) {
    Logger::write("AutoTask: state file version " + std::to_string(fileVersion)
        + " > expected 1, resetting to default", LogLevel::WARN);
    return AutoTaskState();  // return default (empty) state
}
```

---

## Implemented Fixes (this session)

| # | Status | Files changed |
|---|--------|---------------|
| 1 | **DONE** — Changed `nullptr` → `&g_cancelRequested` for both `-AT` and `-RS` CLI handlers, enabling Ctrl+C to cancel AutoTask | `main_cli.cpp` |
| 2 | **DONE** — Added `AutoTaskManager::defaultStateFilePath()` centrally, removed ConfigDialog `autotask_state_file` property, updated `main_cli.cpp` to use static helper | `AutoTaskManager.h`, `AutoTaskManager.cpp`, `ConfigDialog.cpp`, `main_cli.cpp` |
| 3 | **ABANDONED** — `consecutive_failures between 0 and 0` is intentional design (clean-only), not a bug | — |
| 4 | **DONE** — Removed `statusbar_gree_png`/`statusbar_red_png` from `icons.rc`, deleted both PNG files | `icons.rc`, `statusbar_green.png`, `statusbar_red.png` |
| 5 | **DONE** — Merged `doRunAutoTask`/`doResumeAutoTask` into `doAutoTaskImpl(wxHandler, resume)` with `bool resume` parameter, eliminating ~30-line duplication | `AppController.h`, `AppController.cpp` |
| 6 | **DONE** — Added version check in `loadStateFile()`: rejects version > 1 with WARN log, returns default state | `AutoTaskManager.cpp` |

## Additional Fixes (this session)

| # | Status | Description | Files changed |
|---|--------|-------------|---------------|
| 7 | **DONE** — Added `firstCheckDone_` atomic to suppress spurious "RESTORED" log on first poll | `NetworkMonitor.cpp` |
| 8 | **DONE** — Added `controller_->isRunning()` guard + message box in 5 handlers to prevent re-entry | `MainFrame.cpp` |
| 9 | **DONE** — Changed `REALITY publicKey` CONFIG_ERROR log from ERR to INFO | `ProxyBatchTester.cpp`, `ProxyFinder.cpp` |
| 10 | **DONE** — Renamed `onUpdateType` → `updateMethod` across ConfigDialog, AppController, main_cli, ConfigReader.h, SubitemUpdaterV2 | Multiple files |
| 11 | **DONE** — Moved `autotask_notify` property to "通知" category in ConfigDialog | `ConfigDialog.cpp` |
| 12 | **DONE** — Added `IsEnabled()` check in `onNetMonTimer` to skip unnecessary work when disabled | `MainFrame.cpp` |
| 13 | **DONE** — Added `restartNetworkMonitor()` method; restarts thread + redraws status bar on config switch | `AppController.h`, `AppController.cpp`, `MainFrame.cpp` |
| 14 | **DONE** — Added Network Monitor config section to ConfigDialog with enabled/URLs/interval/timeout fields | `ConfigDialog.cpp` |
| 15 | **DONE** — Changed URL check logic from OR to AND: all URLs must pass for connection to be "connected"; added URL validation + at-least-one URL requirement on save | `NetworkMonitor.cpp`, `ConfigDialog.cpp` |

## Verification

1. Code review of all findings confirmed against actual source lines
2. All GoogleTest test executables pass (0 failures)
3. Build: clean, no warnings
4. Network monitor restart + status bar redraw implemented for config switch
