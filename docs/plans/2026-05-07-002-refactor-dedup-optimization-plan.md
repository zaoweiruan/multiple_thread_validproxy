---
title: "refactor: Optimize subscription dedup by merging SQL phases"
type: refactor
status: completed
date: 2026-05-07
origin: docs/superpowers/brainstorm/2026-05-07-improvement-ideas-ideation.md (idea #4)
---

# refactor: Optimize subscription dedup by merging SQL phases

## Summary

Refactor the 5-phase deduplication in `SubitemUpdaterV2::deduplicate()` into 1-2 optimized SQL statements. Currently, Phase0-4 run sequentially with separate DELETE/UPDATE operations, causing excessive database round-trips. The optimized approach merges overlapping conditions, removes redundant phases, and wraps the operation in a single transaction for atomicity and performance.

---

## Problem Frame

`SubitemUpdaterV2.cpp` has 5 dedup phases (Phase0-4) that run sequentially during `deduplicate()`. Each phase executes separate SQL statements (DELETE/UPDATE), resulting in:
1. Excessive database round-trips (5+ separate operations)
2. No transaction protection (partial dedup can leave inconsistent state)
3. Redundant work (Phase2-4 all delete duplicates, but with different conditions)
4. Hard to maintain (5 separate functions with overlapping logic)

---

## Requirements

- R1. Merge 5 dedup phases into 1-2 optimized SQL statements (performance optimization)
- R2. Remove redundant phases (Phase2-4 overlap significantly)
- R3. Wrap dedup operation in a single SQLite transaction (atomicity)
- R4. Preserve existing behavior: keep working proxies (delay>0), remove duplicates by Address+Port+ConfigType+Id+Network
- R5. Preserve protected subscriptions (dedup_subids configuration)
- R6. Log deletion counts for each phase (for backwards compatibility / debugging)
- R7. Must compile on Windows/MinGW-w64 with C++17

**Origin actors:** N/A (internal refactor)  
**Origin flows:** N/A  
**Origin acceptance examples:** AE1 (fewer DB operations), AE2 (transaction atomicity), AE3 (preserve behavior)

---

## Scope Boundaries

- Refactor `deduplicate()` and its 5 phase functions (Phase0-4)
- Merge Phase2, Phase3, Phase4 into a single optimized DELETE statement
- Keep Phase0 (mark working proxies) and Phase1 (remove invalid addresses) as separate steps (they do different things)
- Add transaction protection to entire dedup operation
- Update logging to report merged phase results

- [Will NOT change the dedup_after_update configuration flag]
- [Will NOT change how dedup_subids configuration is interpreted]
- [Will NOT modify other parts of SubitemUpdaterV2 (like import, update)]

---

## Context & Research

### Relevant Code and Patterns

**Current dedup phases in `src/SubitemUpdaterV2.cpp`:**

| Phase | Function | Lines | Purpose | SQL Type |
|-------|----------|-------|---------|----------|
| 0 | `deduplicatePhase0()` | ~1800-1850 | Mark working proxies with protected subid | UPDATE |
| 1 | `deduplicatePhase1()` | ~1850-1900 | Remove private/invalid IP addresses | DELETE |
| 2 | `deduplicatePhase2()` | ~1900-1950 | Remove duplicates keeping delay>0 (with dedup_subids) | DELETE |
| 3 | `deduplicatePhase3()` | ~1950-2000 | Remove duplicates within dedup_subids (keep delay>0) | DELETE |
| 4 | `deduplicatePhase4()` | ~2000-2050 | Full table dedup (exclude dedup_subids, keep delay>0) | DELETE |

**Key observation:** Phase2, Phase3, Phase4 all delete duplicates based on similar criteria (Address+Port+ConfigType+Id+Network). They can be merged into a single SQL with CASE/OR conditions.

**Transaction pattern to follow:** (from brainstorm #2 requirements `2026-05-07-config-transaction-batch-requirements.md`)
- Use `db::TransactionScope` (if implemented) or raw `sqlite3_exec(BEGIN/COMMIT)`

**Dedup SQL pattern (from Phase2):**
```sql
DELETE FROM ProfileItem 
WHERE IndexId NOT IN (
    SELECT MIN(IndexId) FROM ProfileItem 
    WHERE SubId IN (...) AND Delay > 0
    GROUP BY lower(Address), Port, ConfigType, lower(Id), lower(Network)
)
AND SubId IN (...)
```

### Institutional Learnings

N/A (no `docs/solutions/` yet)

### External References

- SQLite DELETE syntax: https://www.sqlite.org/lang_delete.html
- SQLite subqueries: https://www.sqlite.org/lang_select.html
- Existing design doc: `docs/design/dedup-blacklist-design.md`

---

## Key Technical Decisions

- **Decision**: Merge Phase2+3+4 into a single optimized DELETE  
  - **Rationale**: User chose "合并 SQL（性能优化）(Recommended)" — reduces DB operations from 3 to 1  
  - **Implementation**: Use CTE (Common Table Expression) or nested subquery to handle multiple conditions in one statement
  
- **Decision**: Keep Phase0 and Phase1 separate  
  - **Rationale**: Phase0 does UPDATE (marking), Phase1 does DELETE (invalid addresses) — different operations, merging would reduce clarity
  
- **Decision**: Wrap entire `deduplicate()` in a transaction  
  - **Rationale**: Ensures atomicity — either all phases succeed or all roll back  
  - **Note**: Depends on TransactionScope (from idea #2) OR use raw BEGIN/COMMIT for now
  
- **Decision**: Use CTE for merged DELETE (Phase2+3+4)  
  - **Rationale**: CTE makes complex DELETE logic readable and maintainable  
  - **Example**:
    ```sql
    WITH ToDelete AS (
        SELECT IndexId FROM (
            SELECT IndexId,
                   ROW_NUMBER() OVER (
                       PARTITION BY lower(Address), Port, ConfigType, lower(Id), lower(Network)
                       ORDER BY CASE WHEN SubId IN (...) THEN 0 ELSE 1 END,
                                CAST(COALESCE(Delay,0) AS INTEGER) DESC
                   ) as rn
            FROM ProfileItem
            WHERE Delay > 0 OR SubId IN (...)
        )
    )
    DELETE FROM ProfileItem WHERE IndexId IN (SELECT IndexId FROM ToDelete WHERE rn > 1)
    ```

---

## Open Questions

### Resolved During Planning

- [How to merge 3 DELETE phases?]: Use CTE with window function (ROW_NUMBER) to rank duplicates, delete ranks >1
- [Should Phase0/1 be merged too?]: No, keep separate (different operations: UPDATE vs DELETE)

### Deferred to Implementation

- [Exact CTE syntax for SQLite]: Verify SQLite version supports window functions (SQLite 3.25+ does)
- [Whether to keep phase-level logging]: Can log total deleted count, or break down by sub-phase (protected vs non-protected)

---

## Implementation Units

- U1. **Create merged dedup SQL (Phase2+3+4)**

**Goal:** Replace Phase2, Phase3, Phase4 with a single optimized DELETE statement using CTE.

**Requirements:** R1, R2, R4, R5

**Dependencies:** None

**Files:**
- Modify: `src/SubitemUpdaterV2.cpp`
- Create: `tests/test_dedup_sql.cpp` (optional, for SQL validation)

**Approach:**
- Write a CTE-based DELETE that:
  1. Ranks duplicates by (Address+Port+ConfigType+Id+Network)
  2. Prioritizes protected subscriptions (dedup_subids) with lower rank
  3. Prioritizes entries with delay>0
  4. Deletes all but the highest-priority duplicate (rank 1)
- Handle edge cases: NULL Delay, empty SubId, etc.
- Test with sample data to verify correctness

**Patterns to follow:**
- Existing Phase2 SQL pattern (subquery with GROUP BY)
- SQLite window function syntax (ROW_NUMBER() OVER (PARTITION BY ... ORDER BY ...))

**Test scenarios:**
- Happy path: 10 duplicates of same (Address+Port+ConfigType+Id+Network), keeps 1 with lowest delay
- Happy path: Protected subid duplicates, keeps the one with delay>0
- Edge case: All duplicates have delay<=0, keeps first one (MIN(IndexId))
- Edge case: NULL Delay values handled correctly (treat as 0)
- Edge case: Empty dedup_subids configuration (only keeps delay>0)

**Verification:**
- SQL syntax valid in SQLite 3.x
- Correctly deletes duplicates while preserving priority rules
- No false positives (doesn't delete non-duplicates)

---

- U2. **Refactor deduplicate() to use merged SQL**

**Goal:** Update `deduplicate()` to call merged SQL instead of Phase2+3+4.

**Requirements:** R1, R2, R3, R6

**Dependencies:** U1 (merged SQL must be ready)

**Files:**
- Modify: `src/SubitemUpdaterV2.cpp` (deduplicate() function, lines ~1749-1800)

**Approach:**
- Remove calls to `deduplicatePhase2()`, `deduplicatePhase3()`, `deduplicatePhase4()`
- Add single call to new merged dedup function (e.g., `deduplicateMergedPhase()`)
- Keep Phase0 and Phase1 calls (separate operations)
- Update logging: report merged phase deletion count
- Wrap entire `deduplicate()` in transaction (BEGIN/COMMIT/ROLLBACK)

**Patterns to follow:**
- Existing transaction pattern in `src/SubitemUpdaterV2.cpp` line ~1660 (`sqlite3_exec(db_, "BEGIN TRANSACTION;", ...)`)
- Existing logging pattern (`Logger::write("INFO: Phase X completed: removed "...`, LogLevel::INFO))

**Test scenarios:**
- Happy path: `deduplicate()` runs without errors, reduces proxy count correctly
- Error path: Transaction rollback on SQL error (if we add explicit error handling)
- Happy path: Logging shows merged phase results correctly

**Verification:**
- `deduplicate()` produces same or better results than before (fewer proxies, but all valid)
- Transaction ensures atomicity (no partial dedup)
- Logging provides sufficient debug information

---

- U3. **Remove obsolete phase functions**

**Goal:** Delete `deduplicatePhase2()`, `deduplicatePhase3()`, `deduplicatePhase4()` functions.

**Requirements:** R2

**Dependencies:** U2 (must have replaced calls to these functions)

**Files:**
- Modify: `src/SubitemUpdaterV2.cpp` (remove 3 functions, ~150 lines)

**Approach:**
- Delete function bodies
- Remove function declarations (if in header, but these are likely in .cpp only)
- Verify no other code calls these functions

**Patterns to follow:**
- Clean deletion: use `git rm` or delete in editor, verify no compilation errors

**Test scenarios:**
- Happy path: Code compiles without errors after removal
- Happy path: No linker errors (functions truly unused)

**Verification:**
- `cmake --build . --parallel 8` succeeds
- No undefined reference errors

---

- U4. **Add transaction protection to deduplicate()**

**Goal:** Wrap entire `deduplicate()` in a SQLite transaction for atomicity.

**Requirements:** R3

**Dependencies:** U2 (refactored deduplicate() ready)

**Files:**
- Modify: `src/SubitemUpdaterV2.cpp` (deduplicate() function)

**Approach:**
- Add `sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr)` at start
- Add `sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr)` if all phases succeed
- Add rollback on error: `sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr)`
- Use try-catch or manual error checking (depending on whether we throw exceptions)

**Note:** This can be combined with U2 (do it in the same refactoring pass).

**Patterns to follow:**
- Existing transaction usage in `src/SubitemUpdaterV2.cpp` line ~1660
- TransactionScope pattern (if available from idea #2 implementation)

**Test scenarios:**
- Happy path: Transaction commits successfully after successful dedup
- Error path: Transaction rolls back if a phase fails (simulate error?)
- Edge case: Nested transaction attempt (should use SAVEPOINT or reject)

**Verification:**
- Transaction begins and commits in normal flow
- Transaction rolls back on error (if error injection test is possible)
- No "transaction already active" errors

---

- U5. **Update logging for merged phases**

**Goal:** Update `deduplicate()` logging to reflect merged phases.

**Requirements:** R6

**Dependencies:** U2, U4 (refactored deduplicate() with merged phases)

**Files:**
- Modify: `src/SubitemUpdaterV2.cpp` (deduplicate() logging)

**Approach:**
- Change log messages from "Phase 2/3/4 completed: removed X proxies" to "Merged dedup completed: removed X proxies"
- Keep Phase0 and Phase1 logging unchanged
- Optionally add sub-breakdown (e.g., "Merged dedup: X protected, Y non-protected removed")

**Patterns to follow:**
- Existing logging pattern in `deduplicate()` (lines ~1766-1783)

**Test scenarios:**
- Happy path: Log output shows merged phase results clearly
- Happy path: Phase0/1 logs unchanged

**Verification:**
- Log output is clear and actionable
- No confusing "Phase 2/3/4" references remain

---

- U6. **Add tests for optimized dedup**

**Goal:** Create tests to validate the optimized dedup logic.

**Requirements:** R1, R4, R5, R7

**Dependencies:** U1 (merged SQL ready), U2 (deduplicate() refactored)

**Files:**
- Create: `tests/test_dedup_optimization.cpp`
- Modify: `CMakeLists.txt` (add test target)

**Approach:**
- Use Google Test (already in project)
- Create test database with known duplicates
- Run optimized dedup
- Verify correct proxies kept/deleted
- Test edge cases (NULL delay, protected subscriptions, etc.)

**Patterns to follow:**
- `tests/test_model.cpp` (Google Test usage)
- Existing CMake test configuration

**Test scenarios:**
- (Covered in U1 test scenarios, plus:)
- Integration: Full `deduplicate()` run on test database produces expected result
- Performance: Optimized version runs faster than original (optional benchmark)

**Verification:**
- All tests pass
- Test coverage for dedup logic is adequate

---

## System-Wide Impact

- **Interaction graph:** 
  - `deduplicate()` called by `run()` and `runSingle()` after subscription update (if `dedup_after_update` config is true)
  - No other callers (internal function)
  
- **Error propagation:** Transaction rollback on error; caller sees return value (bool)

- **State lifecycle risks:** 
  - Transaction must be properly committed or rolled back
  - Merged SQL must not accidentally delete non-duplicates
  
- **Unchanged invariants:**
  - `dedup_after_update` and `dedup_enabled` config flags behavior unchanged
  - `dedup_subids` configuration interpretation unchanged
  - `cleanupProfileExItem()` still called after dedup (if present)

---

## Risks & Dependencies

| Risk | Mitigation |
|------|------------|
| Merged SQL deletes wrong proxies | Thorough testing with known datasets; use transaction for rollback |
| SQLite window functions not available | Check SQLite version (project likely uses 3.25+); fallback to nested subquery if needed |
| Transaction conflicts with existing BEGIN/COMMIT | Ensure no nested transactions; use SAVEPOINT if needed |
| Performance regression (merged SQL slower) | Benchmark before/after; SQLite EXPLAIN QUERY PLAN to verify optimization |
| Breaking existing behavior | Test with real-world subscription data; compare before/after proxy counts |

---

## Documentation / Operational Notes

- Update `docs/design/dedup-blacklist-design.md` if needed (to reflect new dedup approach)
- Consider adding comment in `SubitemUpdaterV2.cpp` explaining the merged SQL logic
- No user-facing documentation changes needed (internal refactor)

---

## Sources & References

- **Origin document:** `docs/superpowers/brainstorm/2026-05-07-improvement-ideas-ideation.md` (idea #4, 78% confidence)
- Related code: `src/SubitemUpdaterV2.cpp` (deduplicate() and 5 phase functions)
- Design doc: `docs/design/dedup-blacklist-design.md`
- SQLite window functions: https://www.sqlite.org/windowfunctions.html
- Transaction plan: `docs/superpowers/brainstorm/2026-05-07-config-transaction-batch-requirements.md` (idea #2)
