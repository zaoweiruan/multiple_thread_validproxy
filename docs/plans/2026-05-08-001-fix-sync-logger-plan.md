---
title: fix: unify logger usage in sync function (-S command) Phase 1
type: fix
status: completed
date: 2026-05-08
---

# Fix: Unify Logger Usage in Sync Function (-S Command) Phase 1

## Summary

Phase 1 of unifying the project's logger output to use `Logger::write()` instead of `std::cerr`/`std::cout`. This plan focuses on fixing the sync function (`-S` command) in `SubitemUpdaterV2.cpp` where all output currently bypasses the unified logging system. The sync function's output will be migrated to use `Logger::write()` with appropriate log levels, and missing `sqlite3_errmsg()` calls will be added to failure paths.

---

## Problem Frame

The `ce-code-review` (code review) identified that `src/SubitemUpdaterV2.cpp`'s `syncDatabases()` function (lines 1653-1730) completely bypasses the unified logging system established in `memory/project_knowledge.md`. The function uses `std::cout` for normal output and `std::cerr` for errors, which means:
- Sync output does not write to log files (only console)
- Inconsistent with the rest of `SubitemUpdaterV2` which correctly uses `Logger::write()`
- Missing `sqlite3_errmsg()` calls make diagnosis difficult when sync fails
- Violates the project's logging standards documented in `memory/project_knowledge.md`

This is Phase 1 of a staged fix — focusing only on the sync function (`-S` command). Other modules (`main.cpp`, `ProxyFinder.cpp`, `ProxyBatchTester.cpp`, etc.) will be addressed in subsequent phases.

---

## Requirements

- R1. All `std::cout` output in `syncDatabases()` must be replaced with `Logger::write(msg, LogLevel::INFO)`
- R2. All `std::cerr` output in `syncDatabases()` must be replaced with `Logger::write(msg, LogLevel::ERR)`
- R3. Missing `sqlite3_errmsg()` calls must be added to failure paths in `insertSubItem()`, `migrateProxy()`, `migrateSubscription()`, and `migrateProfileExItem()`
- R4. Log messages must follow the established format: `"INFO: message"` or `"ERROR: message"` (matching existing patterns in `SubitemUpdaterV2.cpp`)
- R5. All sync output must be written to both console and log file (via `Logger::write()`)

---

## Scope Boundaries

- Modify: `src/SubitemUpdaterV2.cpp` (syncDatabases, insertSubItem, migrateProxy, migrateSubscription, migrateProfileExItem functions)
- **Not modifying**: `main.cpp`, `ProxyFinder.cpp`, `ProxyBatchTester.cpp`, `ConfigReader.cpp`, `ShareLink.cpp`, header files — these are deferred to Phase 2/3
- **Not modifying**: Logger class itself (`include/Logger.h`, `src/Logger.cpp`) — the infrastructure is complete

### Deferred to Follow-Up Work

- Phase 2: Fix `main.cpp` remaining `std::cerr` usage (~18 locations)
- Phase 3: Fix `ProxyFinder.cpp`, `ProxyBatchTester.cpp`, `ConfigReader.cpp`, `ShareLink.cpp`, and header files
- Phase 4: Clean up any remaining DEBUG output in `ConfigReader.cpp`

---

## Context & Research

### Relevant Code and Patterns

- **Logger class**: `include/Logger.h` defines 5 levels: TRACE(0), DEBUG(1), INFO(2), WARN(3), ERR(4)
- **Correct usage pattern** (from existing `SubitemUpdaterV2.cpp` lines 91-231):
  ```cpp
  Logger::write("========================================", LogLevel::INFO);
  Logger::write("INFO: Starting SubitemUpdaterV2", LogLevel::INFO);
  Logger::write("ERROR: Subscription not found: " + subId, LogLevel::ERR);
  Logger::write("WARN: Direct connection failed", LogLevel::WARN);
  ```
- **Problem function**: `syncDatabases()` at lines 1653-1730 uses `std::cout`/`std::cerr` exclusively
- **Helper functions affected**: `insertSubItem()` (lines 45-78), `migrateProxy()` (lines 1443-1575), `migrateSubscription()` (lines 1397-1441), `migrateProfileExItem()` (lines 1577+)

### Institutional Learnings

- **From `memory/project_knowledge.md` (lines 375-405)**: 
  - Unified logger system uses `Logger::write(msg, level)`
  - Log format: `[timestamp] [LEVEL] message` (handled by Logger)
  - Enum is `LogLevel::ERR` (not `ERROR` — Windows macro conflict)
  - All new code should use `Logger::write()`, not `SubitemUpdaterV2::log()` (deprecated)
- **From code review**: Sync function failure messages lack `sqlite3_errmsg()` calls, making diagnosis difficult

### External References

- None required — this is an internal refactoring following established patterns

---

## Key Technical Decisions

- **K1. Use `Logger::write()` with explicit levels**: Replace `std::cout` → `Logger::write(msg, LogLevel::INFO)`, `std::cerr` → `Logger::write(msg, LogLevel::ERR)` — follows existing patterns in the same file
- **K2. Add `sqlite3_errmsg()` to failure paths**: When `sqlite3_prepare_v2()` or `sqlite3_step()` fails, append `sqlite3_errmsg(db)` to the log message for diagnosability
- **K3. Preserve message format**: Keep the `"INFO: "` and `"ERROR: "` prefixes to match the established convention in `SubitemUpdaterV2.cpp`
- **K4. No functional changes**: This is a pure logging refactoring — the sync logic itself remains unchanged

---

## Open Questions

### Resolved During Planning

- **Q1. Should we add `sqlite3_errmsg()` to `migrateProfileExItem()` failure returns?**  
  **Resolution**: Yes — K2 decision covers all failure paths in sync-related functions

### Deferred to Implementation

- **Q1. Should `insertSubItem()` success be logged?**  
  **Deferred**: Not required for this fix — only failure paths need `sqlite3_errmsg()` for now

---

## Implementation Units

- U1. **Fix syncDatabases() std::cout/cerr output**

**Goal:** Replace all `std::cout` and `std::cerr` in `syncDatabases()` with `Logger::write()` calls

**Requirements:** R1, R2, R4, R5

**Dependencies:** None

**Files:**
- Modify: `src/SubitemUpdaterV2.cpp` (lines 1653-1730)

**Approach:**
- Replace `std::cout << "========================================"` → `Logger::write("========================================", LogLevel::INFO);`
- Replace `std::cout << "Proxy Database Sync"` → `Logger::write("INFO: Proxy Database Sync", LogLevel::INFO);`
- Replace `std::cout << "Source: " << path` → `Logger::write("INFO: Source: " + path, LogLevel::INFO);`
- Replace `std::cerr << "Error: ..."` → `Logger::write("ERROR: Error: ..." , LogLevel::ERR);` (preserve existing "Error: " prefix for compatibility)
- Replace statistics output (`std::cout << "\nMigration complete:"` etc.) → `Logger::write("INFO: Migration complete:", LogLevel::INFO);` followed by individual lines

**Patterns to follow:**
- Lines 91-93 in same file: `Logger::write("========================================", LogLevel::INFO);`
- Lines 148-231: `Logger::write("INFO: " + msg, LogLevel::INFO);`

**Test scenarios:**

- Happy path: Run `validproxy.exe -S source.db:target.db` with valid databases, verify log file contains:
  - `[INFO] ========================================`
  - `[INFO] INFO: Proxy Database Sync`
  - `[INFO] INFO: Source: ...`
  - `[INFO] INFO: Found N valid proxies to migrate`
  - `[INFO] INFO: Migration complete:`
- Error path: Run with same source and target, verify log contains:
  - `[ERR] ERROR: Error: Source and target databases are the same`
  - `[ERR] ERROR: Please specify different databases for sync.`
- Error path: Run with non-existent source DB, verify log contains:
  - `[ERR] ERROR: Failed to open source database: ...` (message includes sqlite3_errmsg)

**Verification:**
- All output from `-S` command appears in both console and log file (`bin/log/sync_*.log`)
- No `std::cout` or `std::cerr` remains in `syncDatabases()`

---

- U2. **Add sqlite3_errmsg() to sync failure paths**

**Goal:** Add detailed SQLite error messages to all failure paths in sync-related functions

**Requirements:** R2, R3

**Dependencies:** U1

**Files:**
- Modify: `src/SubitemUpdaterV2.cpp` (insertSubItem, migrateProxy, migrateSubscription, migrateProfileExItem functions)

**Approach:**
- **insertSubItem()** (lines 45-78):
  - Line 52-53: Add on prepare failure: `Logger::write("ERROR: insertSubItem prepare failed: " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);` before `return false;`
  - Line 75-77: Add on step failure: `Logger::write("ERROR: insertSubItem failed for id=" + subitem.id + ": " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);` before `return success;`
- **migrateProxy() UPDATE path** (lines 1443-1518):
  - Line 1463-1464: Already has output ✅
  - Line 1506-1507: Already has output ✅
  - Line 1512-1514: Add on `migrateProfileExItem` failure: `Logger::write("ERROR: migrateProfileExItem failed for proxy " + proxy.indexid, LogLevel::ERR);` before `return false;`
- **migrateProxy() INSERT path** (lines 1519-1575):
  - Line 1523-1524: Add on prepare failure: `Logger::write("ERROR: INSERT prepare failed for proxy " + proxy.indexid + ": " + std::string(sqlite3_errmsg(dstDb)), LogLevel::ERR);` before `return false;`
  - Line 1564-1565: Add on step failure: `Logger::write("ERROR: INSERT failed for proxy " + proxy.indexid + ": " + std::string(sqlite3_errmsg(dstDb)), LogLevel::ERR);` before `return result;`
- **migrateSubscription()** (lines 1397-1441):
  - Line 1408-1409: Add on prepare failure: `Logger::write("ERROR: migrateSubscription prepare failed: " + std::string(sqlite3_errmsg(dstDb)), LogLevel::ERR);` before `return false;`
  - Line 1432-1433: Add on prepare failure: `Logger::write("ERROR: migrateSubscription prepare failed for src: " + std::string(sqlite3_errmsg(srcDb)), LogLevel::ERR);` before `return false;`

**Patterns to follow:**
- Line 1464: `std::cerr << "UPDATE prepare failed: " << sqlite3_errmsg(dstDb)` (existing correct pattern)

**Test scenarios:**

- Error path: Trigger INSERT prepare failure (corrupt target DB), verify log contains:
  - `[ERR] ERROR: INSERT prepare failed for proxy XXXXX: ...` (includes sqlite3_errmsg)
- Error path: Trigger UPDATE step failure, verify log contains:
  - `[ERR] ERROR: UPDATE failed for proxy XXXXX: ...` (includes sqlite3_errmsg)
- Error path: Trigger migrateProfileExItem failure, verify log contains:
  - `[ERR] ERROR: migrateProfileExItem failed for proxy XXXXX`

**Verification:**
- All failure paths in sync functions output `sqlite3_errmsg()` where applicable
- Log messages clearly identify which function and proxy/subscription failed

---

- U3. **Verify sync function logging**

**Goal:** End-to-end verification that sync function logging works correctly

**Requirements:** R1, R2, R4, R5

**Dependencies:** U1, U2

**Files:**
- Test: Manual testing with `validproxy.exe -S`

**Approach:**
- Build the project after U1+U2 changes
- Test sync with various scenarios
- Check log file output (not just console)

**Test scenarios:**

- Happy path: Run `validproxy.exe -c config.json -S test/guindb.db:E:\temp\test_sync_target.db`
  - Expected: Log file (`bin/log/sync_*.log`) contains all sync output
  - Expected: Console shows same output as log file
  - Expected: No `std::cout`/`std::cerr` visible in console (only Logger output)
- Error path: Run with same source and target: `validproxy.exe -c config.json -S test/guindb.db:test/guindb.db`
  - Expected: Log contains `[ERR] ERROR: Error: Source and target databases are the same`
  - Expected: Program returns exit code 1
- Error path: Run with non-existent source: `validproxy.exe -c config.json -S nonexistent.db:target.db`
  - Expected: Log contains `[ERR] ERROR: Failed to open source database: unable to open database file`
- Error path: Run with URL-less subscription (triggers migrateSubscription failure)
  - Expected: Log contains `Warning: Failed to migrate subscription` (from line 1707)

**Verification:**
- `-S` command produces no `std::cout`/`std::cerr` output in console
- All output appears in log file with correct format `[timestamp] [LEVEL] INFO/ERROR: message`
- Failure messages include `sqlite3_errmsg()` where applicable

---

## System-Wide Impact

- **Interaction graph:** The sync function (`-S`) is called from `main.cpp` when `-S` argument is provided. No callbacks or middleware affected.
- **Error propagation:** Errors now propagate through `Logger::write()` instead of `std::cerr` — ensures errors are logged to file for later diagnosis.
- **State lifecycle risks:** None — this is a pure logging change, no state modifications.
- **API surface parity:** None — no external API changes.
- **Integration coverage:** The `-S` command should be tested end-to-end after changes.
- **Unchanged invariants:** 
  - `Logger::write()` behavior unchanged
  - Sync logic in `syncDatabases()` unchanged
  - Other commands (`-U`, `-UA`, `-T`, etc.) unaffected by this change

---

## Risks & Dependencies

| Risk | Mitigation |
|------|------------|
| Incorrect log level chosen for certain messages | Follow existing patterns in same file (lines 91-231) — use INFO for normal flow, ERR for errors |
| sqlite3_errmsg() called on wrong database handle | Verify each call uses the correct db handle (srcDb vs dstDb) by checking function parameters |
| Breaking output format for log parsing tools | Preserve existing "INFO: " and "ERROR: " prefixes to maintain compatibility |

---

## Sources & References

- **Origin document:** `memory/project_knowledge.md` (lines 375-405, 507-530) — Logger system specification
- **Code review:** `ce-code-review` output identifying sync function as P1 issue
- **Related code:** `src/SubitemUpdaterV2.cpp` lines 91-231 (correct Logger::write usage patterns)
- **Related code:** `src/SubitemUpdaterV2.cpp` lines 1653-1730 (problem function syncDatabases())
- **Logger class:** `include/Logger.h`, `src/Logger.cpp`

---

Plan written to: E:\eclipse_workspace\multiple_thread_validproxy\docs\plans\2026-05-08-001-fix-sync-logger-plan.md
