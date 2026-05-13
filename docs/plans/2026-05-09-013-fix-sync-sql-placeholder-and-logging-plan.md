---
title: "fix: Repair syncDatabases() — SQL placeholder mismatch, silent failures, std::cout bypass"
type: fix
status: completed
date: 2026-05-09
origin: "Log audit of sync_20260509_140333.log: 50 INSERT failures (27 values for 35 columns)"
---

# fix: Repair syncDatabases() — SQL mismatch, silent failures, logging bypass

## Summary

Log audit of `bin/worker/log/sync_20260509_140333.log` revealed a **blocking bug** in `migrateProxy()`: the INSERT statement has only **27** `?` placeholders in VALUES, but the column list and bind calls expect **35**. This causes every proxy migration INSERT to fail. Additionally, `syncDatabases()` and `migrateProxy()` bypass the Logger with `std::cout`/`std::cerr`, and `migrateProfileExItem()` silently fails without any logging.

---

## Problem

### P1: INSERT VALUES placeholder count mismatch (BLOCKER)

`bin/worker/log/sync_20260509_140333.log` — 50 identical failures:
```
ERROR: INSERT prepare failed for proxy <id>: 27 values for 35 columns
```
Final output: `[REPORT] sync failed` — with zero diagnostic detail.

**Root cause**: `src/SubitemUpdaterV2.cpp` line ~1529 — the INSERT statement in `migrateProxy()` has only 27 `?` placeholders but 35 columns in the column list and 35 bind parameters.

The correct INSERT exists in the same file at `updateProfileItems()` (~line 928) with all 35 placeholders.

### P2: std::cout/std::cerr bypassing Logger

`syncDatabases()` (lines ~1664-1741) uses raw `std::cout`/`std::cerr` in **8 locations**, meaning all sync progress and error details are lost from log files:
- Database open success/failure
- Proxy count found
- Per-proxy migration results
- Summary statistics

### P3: Silent failures in migrateProfileExItem()

Six `sqlite3_prepare_v2` and step failures in `migrateProfileExItem()` (lines ~1588-1662) silently `return false` with no logging at all. Callers only see failure and print generic messages via `std::cerr`.

### P4: getJsonValueString() silently swallows exceptions

Line ~38: `catch (...) {}` discards all JSON parsing exceptions. Corrupted values produce no diagnostic trace.

---

## Requirements

- **R1**: INSERT statement must have 35 `?` placeholders matching column count (BLOCKER)
- **R2**: All `std::cout`/`std::cerr` in `syncDatabases()` → `Logger::write()` with correct levels
- **R3**: All silent `return false` paths in `migrateProfileExItem()` → add `Logger::write(ERR)` before return
- **R4**: All `std::cerr` in `migrateProxy()` UPDATE branch → `Logger::write(ERR)`
- **R5**: Must compile on Windows/MinGW-w64 with C++17

---

## Scope Boundaries

- **Modify**: `src/SubitemUpdaterV2.cpp` only
- **Will NOT change**: Database schema, `updateProfileItems()`, `ConfigReader`, `ProxyBatchTester`
- **Related docs**: `docs/plans/2026-05-08-001-fix-sync-logger-plan.md` (earlier sync logger fix plan)

---

## Targeted Changes

### U1. Fix INSERT VALUES placeholder count — `src/SubitemUpdaterV2.cpp:~1529` (BLOCKER)

**Current (broken):**
```cpp
std::string insertSql = "INSERT INTO ProfileItem (...) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
// 35 columns, but only 27 '?'
```

**Target — add 8 missing `?`:**
```cpp
std::string insertSql = "INSERT INTO ProfileItem (...) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
// 35 columns, 35 '?'
```

**Alternative**: Refactor to reuse the INSERT SQL constant from `updateProfileItems()` to avoid duplication.

### U2. Replace std::cout/std::cerr in syncDatabases() with Logger::write()

| Line | Current | Replacement |
|------|---------|-------------|
| ~1684 | `std::cerr << "Failed to open source database: " << sqlite3_errmsg(srcDb) << std::endl;` | `Logger::write("Failed to open source database: " + std::string(sqlite3_errmsg(srcDb)) + " Path: " + sourceDbPath, LogLevel::ERR);` |
| ~1688 | `std::cout << "Source database opened successfully." << std::endl;` | `Logger::write("Source database opened", LogLevel::DEBUG);` |
| ~1692 | `std::cerr << "Failed to open target database: " << sqlite3_errmsg(dstDb) << std::endl;` | `Logger::write("Failed to open target database: " + std::string(sqlite3_errmsg(dstDb)) + " Path: " + targetDbPath, LogLevel::ERR);` |
| ~1697 | `std::cout << "Target database opened successfully." << std::endl;` | `Logger::write("Target database opened", LogLevel::DEBUG);` |
| ~1708 | `std::cout << "Found " << profiles.size() << " valid proxies to migrate" << std::endl;` | `Logger::write("Found " + std::to_string(profiles.size()) + " valid proxies to migrate", LogLevel::INFO);` |
| ~1718 | `std::cerr << "Warning: Failed to migrate subscription " << profile.subid << std::endl;` | `Logger::write("Warning: Failed to migrate subscription " + profile.subid, LogLevel::WARN);` |
| ~1726 | `std::cerr << "Failed to migrate proxy " << profile.indexid << std::endl;` | `Logger::write("Failed to migrate proxy " + profile.indexid, LogLevel::ERR);` |
| ~1732-35 | `std::cout` summary | `Logger::write("Migration Result — Total: ... Succeeded: ... Failed: ...", LogLevel::REPORT);` |

### U3. Add error logging to migrateProfileExItem() silent returns

| Line | Change |
|------|--------|
| ~1597 | Before `return false`: `Logger::write("migrateProfileExItem: Prepare source select failed for " + indexid + ": " + sqlite3_errmsg(srcDb), LogLevel::ERR);` |
| ~1617 | Before `return false`: `Logger::write("migrateProfileExItem: Prepare target check failed for " + indexid + ": " + sqlite3_errmsg(dstDb), LogLevel::ERR);` |
| ~1629 | Before `return false`: `Logger::write("migrateProfileExItem: Prepare UPDATE failed for " + indexid + ": " + sqlite3_errmsg(dstDb), LogLevel::ERR);` |
| ~1640 | Before `return false`: `Logger::write("migrateProfileExItem: UPDATE step failed for " + indexid + ": " + sqlite3_errmsg(dstDb), LogLevel::ERR);` |
| ~1648 | Before `return false`: `Logger::write("migrateProfileExItem: Prepare INSERT failed for " + indexid + ": " + sqlite3_errmsg(dstDb), LogLevel::ERR);` |
| ~1658 | Before `return false`: `Logger::write("migrateProfileExItem: INSERT step failed for " + indexid + ": " + sqlite3_errmsg(dstDb), LogLevel::ERR);` |

### U4. Replace std::cerr with Logger in migrateProxy() UPDATE branch

| Line | Current | Replacement |
|------|---------|-------------|
| ~1473 | `std::cerr << "UPDATE prepare failed: " << sqlite3_errmsg(dstDb) << std::endl;` | `Logger::write("UPDATE prepare failed for " + proxy.indexid + ": " + std::string(sqlite3_errmsg(dstDb)), LogLevel::ERR);` |
| ~1516 | `std::cerr << "UPDATE failed for proxy " << proxy.indexid << ": " << sqlite3_errmsg(dstDb) << std::endl;` | `Logger::write("UPDATE step failed for " + proxy.indexid + ": " + std::string(sqlite3_errmsg(dstDb)), LogLevel::ERR);` |

### U5. (Optional) Fix getJsonValueString() silent exception swallowing

| Line | Current | Replacement |
|------|---------|-------------|
| ~38 | `catch (...) {}` | `catch (...) { Logger::write("getJsonValueString: exception for key", LogLevel::WARN); return ""; }` |

---

## Verification

- **Build**: `cmake --build build --parallel 8` — must pass with 0 warnings
- **Unit test**: Run existing `test_dedup` to confirm no regression
- **Integration test**: Run `-S test/guindb.db:bin/worker/guindb.db` and confirm:
  - INSERT succeeds (no "27 values for 35 columns" error)
  - Sync summary appears in log file at REPORT level
  - Per-proxy errors appear at ERR level
- **Log level audit**: Confirm `console_level: INFO` still shows ERR/INFO messages; `file_level: REPORT` only shows summaries

---

## Risks

| Risk | Mitigation |
|------|------------|
| Overlapping INSERT SQL between `updateProfileItems()` and `migrateProxy()` — potential for future inconsistency | Consider extracting INSERT SQL to a shared constant |
| Changing `std::cout` to `Logger::write(INFO)` may suppress console output if `console_level` is later changed above INFO | Document this dependency in code comments |
| Silent failure count (U3) may produce too many log entries for large syncs | Consider batching log output or adding a `logNetworkFailures` guard (like ProxyBatchTester) |

---

## Related Plans

- `docs/plans/2026-05-08-001-fix-sync-logger-plan.md` — Earlier sync logger unification plan (partially implemented)
- `docs/plans/2026-05-09-012-fix-log-level-mismatches-from-audit-plan.md` — General log level standardization

---

## Acceptance Criteria

- [ ] `sync_*.log` contains full migration details (source/target DB open, proxy count, per-proxy results)
- [ ] `sync_*.log` contains REPORT-level summary at end (Total/Succeeded/Failed)
- [ ] No `std::cout` or `std::cerr` remaining in `syncDatabases()` or `migrateProxy()`
- [ ] All `return false` in `migrateProfileExItem()` have corresponding `Logger::write(ERR)` calls
- [ ] Build passes with 0 warnings
- [ ] Existing tests still pass