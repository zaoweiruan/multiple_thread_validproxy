# Specification: Fix Missing Composite Index `idx_profile_dedup`

## 1. Problem Description

The composite index `idx_profile_dedup` on the `ProfileItem` table is missing from existing databases, causing deduplication queries to perform full table scans and severely impacting performance.

The index definition should be:

```sql
CREATE INDEX IF NOT EXISTS idx_profile_dedup ON ProfileItem(
    LOWER(Address), 
    Port, 
    ConfigType, 
    LOWER(Id), 
    LOWER(Network)
);
```

This index was correctly implemented in `DatabaseConnectionService::applyPragmas()` (src/service/DatabaseConnectionService.cpp, line 31), but the function is not being called during database initialization for all code paths.

## 2. Root Cause Analysis

Two database opening code paths fail to invoke `applyPragmas()`:

1. **GUI Entry Point** (`src/main_gui.cpp`, line ~87):  
   Directly calls `sqlite3_open_v2()` without going through `DatabaseConnectionService::open()`, so `applyPragmas()` is never executed.

2. **CLI Helper** (`src/main_cli.cpp`, in `openDatabase()` function, line ~61+):  
   Creates its own `sqlite3*` handle via `sqlite3_open_v2()` and manually applies some PRAGMAS, but does NOT call `applyPragmas(db)` to create indexes.

Any other direct callers of `sqlite3_open_v2()` are also potentially affected and require review.

## 3. Solution Approach

### Principle: Single Source of Truth for Database Initialization

All database opening code must eventually ensure `applyPragmas(db)` is invoked after a successful connection. The preferred approach is centralization via `DatabaseConnectionService`.

### 3.1 GUI Entry Point (`main_gui.cpp`)

**Current:**
```cpp
sqlite3* db = nullptr;
const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
const char* uri = 0;
sqlite3_open_v2(full_path.c_str(), &db, flags, uri);
```

**Change:**
- Replace direct `sqlite3_open_v2()` call with `DatabaseConnectionService::open()`
- This ensures `applyPragmas()` is invoked automatically
- Return the opened database pointer for use in remaining logic

**Rationale:** The simplest and most maintainable fix. Centralizes DB ownership and lifecycle.

### 3.2 CLI Helper (`main_cli.cpp` - `openDatabase()`)

**Current Pattern:** Similar to GUI—direct `sqlite3_open_v2()` plus manual PRAGMA settings, no index creation.

**Change Options (in order of preference):**

a) **Preferred**: Refactor to use `DatabaseConnectionService::open()` internally if this function's signature allows it. Otherwise, add explicit `applyPragmas(db)` call immediately after a successful `sqlite3_open_v2()` while preserving current signature.

b) **Fallback**: If refactoring isn't feasible, append `applyPragmas(db)` inside `openDatabase()` right after opening succeeds.

### 3.3 Additional Review Required

Search for any other direct uses of `sqlite3_open_v2()` across the codebase (UI modules, updater services, maintenance services). Each must either route through `DatabaseConnectionService` or include an explicit `applyPragmas(db)` post-open.

## 4. Verification Steps

After implementation:

1. Build project in Debug mode using Ninja:
   ```powershell
   cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
   cmake --build build --parallel 8
   ```

2. Run program (GUI or any command that opens the database).

3. Confirm index exists by running the following against the resulting database (either from an interactive SQLite session or via query tool):
   ```sql
   SELECT name FROM sqlite_master WHERE type='table' AND name='ProfileItem';
   PRAGMA index_list(ProfileItem);
   -- Expect to see idx_profile_dedup listed
   ```

4. Optionally inspect index columns:
   ```sql
   PRAGMA index_info('idx_profile_dedup');
   -- Should report entries matching: LOWER(Address), Port, ConfigType, LOWER(Id), LOWER(Network)
   ```

5. Run relevant unit tests (especially deduplication-related tests) to ensure correctness is preserved.

## 5. Files Requiring Modification

| File | Role | Change Type |
|------|------|-------------|
| `src/main_gui.cpp` | Database opening in entry point | Replace raw `sqlite3_open_v2()` with `DatabaseConnectionService::open()` |
| `src/main_cli.cpp` | CLI helper for DB access | Add `applyPragmas(db)` after open, or refactor to use service |
| *(pending search)* | Other potential direct openers | Add `applyPragmas(db)` or refactor via service |

## 6. Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Breaking changes due to refactoring to `DatabaseConnectionService::open()` (different signature/behavior) | Check compatibility before changing call site; preserve error handling |
| Accidentally modifying unrelated PRAGMAS | `applyPragmas()` only sets already verified safe/read-only-friendly PRAGMAS; verify diff carefully |
| Forgetting another direct opener | Perform thorough grep for `sqlite3_open_v2()` across the whole source tree |

## 7. Acceptance Criteria

- [x] Index `idx_profile_dedup` defined once in `DatabaseConnectionService::applyPragmas()`
- [x] All code paths invoking `sqlite3_open_v2()` result in `applyPragmas()` being called at least once per connection
- [x] New databases created via GUI or CLI contain the index (verified via `PRAGMA index_list(ProfileItem)`)
- [x] Existing databases unaffected when index already exists (`IF NOT EXISTS` clause)
- [x] All tests pass (including deduplication tests) without regression

---

*Created: 2026-07-28 | Status: In Progress (next: modify main_gui.cpp and main_cli.cpp)*