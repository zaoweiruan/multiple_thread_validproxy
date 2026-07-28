# Spec: Profile Deduplication Index Creation at Startup

## Metadata
- **File**: `docs/specs/2026-07-28-Spec-ProfileDedupIndexOnStartup-v1.0.md`
- **Status**: finalized
- **Module**: Database / ProfileItem table
- **Version**: 1.0
- **Date**: 2026-07-28

---

## Problem Statement

The composite index `idx_profile_dedup` on the `ProfileItem` table is **not created automatically** when the application starts with a new or existing database. Instead, it is only created during:
- Synchronization operations (`SubitemUpdaterV2::syncDatabases()`)
- Manual database switching in the UI

This causes deduplication queries to perform full table scans on databases opened directly by the GUI or CLI without prior sync operations, significantly impacting performance and potentially causing timeouts on large databases (50K+ profiles).

### Observed Behavior

When running deduplication queries like:
```sql
SELECT * FROM ProfileItem 
WHERE LOWER(Address), Port, ConfigType, LOWER(Id), LOWER(Network) 
GROUP BY LOWER(Address), Port, ConfigType, LOWER(Id), LOWER(Network)
ORDER BY COUNT(*) DESC
```

Without the composite index, SQLite must scan every row and compute hash values for grouping. With the index `(LOWER(Address), Port, ConfigType, LOWER(Id), LOWER(Network))`, the database can use the index for grouping avoiding sorting and temporary tables.

---

## Design Goal

Ensure the composite index `idx_profile_dedup` is automatically created whenever any entry point (GUI or CLI) opens the primary database file for the first time.

---

## Technical Details

### Index Definition (from `DatabaseConnectionService::applyPragmas`)

```sql
CREATE INDEX IF NOT EXISTS idx_profile_dedup 
ON ProfileItem(LOWER(Address), Port, ConfigType, LOWER(Id), LOWER(Network));
```

The index covers all columns used in deduplication grouping and filtering.

### Current Code Structure

| File | Location | Role | Index Applied? |
|------|----------|------|----------------|
| `include/service/DatabaseConnectionService.h` | Line 17 | Service interface | N/A |
| `src/service/DatabaseConnectionService.cpp` | Lines 21-32 | `applyPragmas(sqlite3*)` implementation | ✅ (includes index) |
| `src/service/DatabaseConnectionService.cpp` | Lines 9-19 | `open(const std::string&)` wrapper | ✅ (calls `applyPragmas(db)`) |
| `src/main_gui.cpp` | Line 87 | Direct `sqlite3_open_v2()` call | ❌ (bypasses wrapper) |
| `src/main_cli.cpp` | Line 61-78 | Custom `openDatabase()` helper | ❌ (only pragmas, no index) |

### Root Cause Analysis

The `applyPragmas()` method exists and contains correct index logic, but it is **only called** through:
1. `DatabaseConnectionService::open()` — which is used in limited contexts (switching databases, some DAO operations)
2. Direct manual calls in SubitemUpdaterV2 sync paths

The two main execution paths (GUI app start and CLI commands) bypass this wrapper entirely, using raw SQLite API calls that skip index creation.

---

## Proposed Fix Strategy

Two-phase approach to fix both GUI and CLI entry points while maintaining code reuse and consistency.

### Phase 1: Fix `DatabaseConnectionService::open()` (already done)

✅ Already confirmed: `DatabaseConnectionService::open()` correctly calls `applyPragmas(db)` after successfully opening the database. This is the intended pattern.

### Phase 2: Update Entry Points to Use `DatabaseConnectionService::open()`

#### GUI Fix (`main_gui.cpp`)

Replace the raw `sqlite3_open_v2()` call at line 87 with a call to `DatabaseConnectionService::open()`. This ensures the standard apply-pragmas-and-index path is taken consistently.

**Before:**
```cpp
sqlite3* db = nullptr;
if (sqlite3_open_v2(appConfig->database_path.c_str(), &db, ...) != SQLITE_OK) { ... }
```

**After:**
```cpp
service::DatabaseConnectionService dbService;
sqlite3* db = dbService.open(appConfig->database_path);
if (!db) { ... /* handle error */ }
```

#### CLI Fix (`main_cli.cpp`)

Option A (Recommended): Refactor `openDatabase()` helper to call `DatabaseConnectionService::applyPragmas()` after opening.

Option B: Replace direct usage with `DatabaseConnectionService::open()` throughout CLI (requires restructuring).

**Current `openDatabase()` fragment (lines 61-78):**
```cpp
static bool openDatabase(const config::AppConfig& config, sqlite3*& db, const std::string& context) {
    if (sqlite3_open_v2(config.database_path.c_str(), &db, ...) != SQLITE_OK) { ... }
    // Applies only pragmas, no index!
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", ...);
    // ... other pragmas
    return true;
}
```

**Fix:** Add index creation after opening, either by:
- Calling `DatabaseConnectionService::applyPragmas(db)` directly, OR
- Reusing the pragma SQL strings plus adding the index statement

---

## Implementation Plan

### Step 1: Update `main_gui.cpp`
1. Include `service/DatabaseConnectionService.h`
2. Replace raw `sqlite3_open_v2()` with `DatabaseConnectionService::open()`
3. Handle potential `nullptr` return gracefully (same as current error handling)

### Step 2: Update `main_cli.cpp`
1. For each CLI command that opens a database:
   - Either add `DatabaseConnectionService::applyPragmas(db)` immediately after `openDatabase()` returns true, OR
   - Modify `openDatabase()` itself to include index creation (preferred for code hygiene)
2. Ensure all CLI entry points cover this: test-all, generator, show-sub, find-proxy, tourl, sync, import-sub, auto-task, resume-task, dedup, test-sub, update

### Step 3: Build and Verify
1. Clean build: `cmake --build build --clean-first`
2. Compile with: `cmake --build build --parallel 8`
3. Test with fresh database (or run on test guindb.db): Check index exists via `.schema idx_profile_dedup`
4. Run unit tests: `ctest -V`

### Step 4: Documentation Update
Update `docs/plans/project-plans-tracker.md` to mark this change as completed.

---

## Expected Outcome

1. **GUI mode**: Every time the application opens its configured database, the composite index is created if not already present (via `CREATE INDEX IF NOT EXISTS`), ensuring optimal deduplication query performance from first use.
2. **CLI mode**: All CLI commands that operate on the database benefit from the index, improving speed of deduplication, finding proxies, testing subscriptions, and other operations that rely on grouped profile data.
3. **No regressions**: The `IF NOT EXISTS` clause ensures backward compatibility with existing databases that already have the index.
4. **Code consistency**: Single source of truth for database initialization pragmas + index resides in `DatabaseConnectionService::applyPragmas()`, called consistently from all entry points.

---

## Verification Commands

After building and running, verify index creation:

```bash
# On a fresh or empty DB, check that index exists
sqlite3 bin/worker/guindb.db ".schema idx_profile_dedup"
# Should output: CREATE INDEX IF NOT EXISTS idx_profile_dedup ON ProfileItem(...)

# Or inspect indexes directly
sqlite3 bin/worker/guindb.db "SELECT name, sql FROM sqlite_master WHERE type='index' AND tbl_name='ProfileItem'"
```

Check query plan to confirm index usage:
```sql
EXPLAIN QUERY PLAN SELECT COUNT(*), LOWER(Address), Port, ConfigType, LOWER(Id), LOWER(Network)
FROM ProfileItem GROUP BY LOWER(Address), Port, ConfigType, LOWER(Id), LOWER(Network) ORDER BY COUNT(*) DESC;
```
Should show `USING INDEX idx_profile_dedup` rather than `SCAN TABLE ProfileItem`.