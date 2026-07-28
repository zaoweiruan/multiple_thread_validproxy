# Subscription Write Performance Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Optimize subscription item database write performance in SubitemUpdaterV2 by implementing pragma tuning, batch INSERTs with in-memory hash deduplication, composite index, and Deduplicator optimizations — targeted 10-50x speedup on large subscriptions (1K+ items).

**Architecture:** Four phased approach: (A) PrAGMA tuning for SQLite write performance; (B) Rewrite updateProfileItems() with prepared statement batching and unordered_set deduplication; (C) Add composite index on dedup columns and optimize Deduplicator Phase 3/4; (D) Optional memory-DB merge if later phases still insufficient. All changes maintain full backward compatibility and pass existing tests.

**Tech Stack:** C++17, SQLite3, Google Test, CMake/Ninja, Windows/MinGW.

---

## Phase A: Pragma Tuning — Low-Risk Immediate Gain

### File: `src/DatabaseConnectionService.cpp` (function `applyPragmas()`, lines ~160-185)

#### Step 1: Read current `applyPragmas()` implementation

- [ ] Read file to verify current pragmas only include `PRAGMA journal_mode=WAL; PRAGMA busy_timeout=5000;`

```cpp
// Current state (placeholder for reference)
void DatabaseConnectionService::applyPragmas() {
    execSql("PRAGMA journal_mode=WAL;");
    execSql("PRAGMA busy_timeout=5000;");
}
```

#### Step 2: Extend `applyPragmas()` with 5 additional PRAGMAs

- Modify to add:
```cpp
PRAGMA cache_size=-100000;    // ~100MB page cache
PRAGMA synchronous=NORMAL;  // faster writes, still safe
PRAGMA temp_store=MEMORY;   // keep temp tables in RAM
PRAGMA mmap_size=268435456; // 268MB address space for mmap
PRAGMA journal_size_limit=104857600; // 100MB max journal size
```

**Note:** `cache_size=-N` means negative number of pages (≈N×8KB for default page size). `-100000` = ~800MB; adjust based on actual page size observed in code (`PRAGMA page_size;`).

#### Step 3: Verify no regressions

- Run `ctest -V` after applying changes — all tests must pass

#### Step 4: Apply consistency fix to CLI and GUI entry points

- Also add same pragmas to:
  - `main_cli.cpp` lines 155-156 (after opening db, before main work)
  - `AppController.cpp` line 138-139 (GUI initialization path)

### Validation Metric
- Before/after wall-clock time for a sample subscription (e.g., 500 items) using same hardware; expect 1.2–2x improvement from journal_mode alone plus additional gains from other pragmas.

---

## Phase B: Batch INSERT + In-Memory Hash Dedup — Core Bottleneck Fix

### Critical Files
- `src/SubitemUpdaterV2.h` — interface declaration for `updateProfileItems()`
- `src/SubitemUpdaterV2.cpp` — implementation of `updateProfileItems()` (lines 546-709)
- `src/Dao/ProfileItemDAO.h` / `ProfileItemDAO.cpp` — may need new batch insert method

### Baseline Understanding
Current per-profile logic in `updateProfileItems()`:
1. For each profile row: `SELECT idx FROM ... WHERE ...` (dedup check) → 1 SQL round-trip
2. If not exists: `INSERT INTO ProfileItem (...) VALUES (...)` → 1 SQL round-trip  
3. Same pattern repeated for ProfileExItem
Total: up to 3 SQL statements per profile row. For 10K profiles = 30K transactions/round-trips.

### Refactor Strategy
Rewrite `updateProfileItems()` into single transaction with batch operations:

#### Phase B1: Prepare data structures in memory (0 SQL reads first)

1. Read all subscriptions once: load all profiles from DB via `ProfileItemDAO::getAll()` into a single `unordered_set<string>` keyed by `(lower(address), port, config_type, lower(id), lower(network))`. This is ONE query instead of M queries per profile.

2. Collect incoming subscription rows in a `vector<ProfileItemBatch>` structure.

#### Phase B2: Write all new rows in one batch insert

- Prepare SQL: `INSERT OR IGNORE INTO ProfileItem (IndexId, Address, Port, ConfigType, Id, Network, ...) VALUES (?, ?, ?, ..., ?);`
- Bind vectors: one call per column type, execute with bulk bind if libsqlite supports it, else loop with `sqlite3_bind_*` and `sqlite3_step()` once per group.
- Use `OR IGNORE` rather than checking existence beforehand — dedup happens inside SQLite now.

#### Phase B3: Insert ProfileExItem similarly (batch)

#### Phase B4: Update SubItem count and status markers

Original code tracked `sub_item_count`, `profile_item_count` etc. Keep these counters updated per transaction boundary.

#### Step-by-step task breakdown

**Task B1 — Extract and store current dedup keys**

- [ ] Modify `ProfileItemDAO::getAll()` to return `vector<ProfileItemEx>` where `ProfileItemEx` includes all dedup-relevant columns (already present) plus an internal dedup_key = lower(Address) + "|" + Port + "|" + ConfigType + "|" + lower(Id) + "|" + lower(Network) + "|" + Sha256(...)

- [ ] In `SubitemUpdaterV2::updateProfileItems()`, at start:
  ```cpp
  auto all_items = m_profile_item_dao->getAll(); // 1 SELECT
  unordered_set<string> existing_keys;
  for(const auto& item : all_items) {
      string key = buildDedupKey(item);
      existing_keys.insert(key);
  }
  ```

**Task B2 — Build batch inserts with `OR IGNORE`**

- [ ] Change `ProfileItemDAO::insert()` from single-row to accept `vector<ProfileItem>&` and perform batch insert inside one transaction.

- [ ] New signature in header:
  ```cpp
  void insertBatch(const vector<ProfileItem>& items);
  ```

- [ ] Implementation opens transaction, prepares one stmt, iterates items, binds each, steps, rolls back on error, commits.

- [ ] Similarly for `ProfileExItemDAO::insertBatch()`.

- [ ] Replace old per-row inserts in `updateProfileItems()` with calls to `insertBatch(new_profiles)` and `insertBatch(new_profile_exes)`.

**Task B3 — Remove redundant SELECT dedup checks**

- Original per-row code:
  ```cpp
  if (!m_profile_item_dao->existsByDedupKey(...)) {
      m_profile_item_dao->insert(item);
  }
  ```
  Remove entirely. The `INSERT OR IGNORE` handles uniqueness.

**Task B4 — Preserve original semantics**

- Ensure that if any ProfileItem fails to insert (constraint violation after careful review), the whole transaction rolls back identically to previous behavior.
- Maintain counter updates (`profile_item_count`, `sub_item_count`, etc.) at the transaction commit point, matching original logic exactly.

### Testing Requirements

1. **Unit test for dedup correctness**: Create test case with duplicate keys inserted both ways — verify final row count matches expected without duplicates.
2. **Integration test**: Full subscription update flow with mixed new + duplicate + modified items — compare output row counts and content against baseline.
3. **Benchmark**: Measure wall-clock time for inserting 1K and 10K items (with ~50% duplicates) comparing old vs new code. Expect 10-50x improvement.

---

## Phase C: Composite Index + Deduplicator Optimization

### File: `Dao/ProfileItemDAO.h/.cpp`, `update/Deduplicator.cpp`

#### Step C1: Add composite index on dedup columns

- [ ] In `ProfileItemDAO::initialize()` or dedicated migration function, create index:
  ```sql
  CREATE INDEX IF NOT EXISTS idx_profile_dedup ON ProfileItem(
      LOWER(Address), Port, ConfigType, LOWER(Id), LOWER(Network)
  );
  ```
- This directly speeds up `SELECT COUNT(*) WHERE LOWER(...) AND ...` queries used in Deduplicator Phase 4.

#### Step C2: Optimize Deduplicator Phase 3 (remove massive ALL load)

Currently Phase 3 loads ALL profiles into memory to compare. Instead:
- Use `SELECT IndexId FROM ProfileItem WHERE lower(?)=? AND lower(?)=? ... GROUP BY IndexId HAVING COUNT(*)>1` to find duplicates directly in DB.
- Delete them via batch delete rather than逐行 delete.

#### Step C3: Optimize Deduplicator Phase 4 (expensive ROW_NUMBER CTE)

Replace the heavy CTE-based top-n selection with:
- Simpler subquery: `SELECT * FROM ProfileItem WHERE IndexId IN (SELECT min(IndexId) FROM ... GROUP BY dedup_key)`
- Or alternatively perform dedup in application memory after selecting fewer candidate rows via indexed filter.

#### Step C4: Batch-delete utility in DAO

- Add `deleteByIndexIds(const vector<string>& indices)` to DAO layer for efficient multi-delete in Deduplication cleanup.

### Validation

- Run Deduplicator unit tests — confirm identical output set shape (only 1 copy per unique proxy remains)
- Benchmarks on subscription with high duplication rate (≥80%) should show Phase 4 speedup 5-10x from indexed scan vs table scan.

---

## Phase D (Optional): Memory-DB Merge Path

Only needed if total write time > threshold after Phases A-C for 10K+ items.

Strategy: Open temporary `:memory:` SQLite instance, do all inserts/batching there, then `ATTACH DATABASE` to copy results into persistent DB via `INSERT INTO main.Table SELECT * FROM TempTable`. Not recommended unless profiling shows remaining bottleneck.

---

## Integration & Release Sequence

1. **Precondition**: Baseline `git checkout` at current stable tag. All ctests pass.
2. **Implement Phase A** (pragma tuning) → compile → `ctest -V` → measure baseline perf.
3. **Implement Phase B** (batch inserts + dedup set) → recompile → run dedup unit tests → integration test → benchmark → verify no functional regression.
4. **Implement Phase C** (index + dedup optimization) → run full test suite → validate dedup results match baseline.
5. **Documentation**: Update `docs/superpowers/plans/2026-07-24-SubscriptionWritePerformance-Optimization-v1.md` with post-mortem benchmark numbers and lessons learned.

---

## Potential Risk Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|----------|
| `INSERT OR IGNORE` silently skips intended inserts (data loss) | LOW | HIGH | Add audit log: count rows affected per batch insert, match against expected new counts. Cross-check after insertion with DB query. |
| Transaction rollback on partial failure leaves DB inconsistent | MEDIUM | HIGH | Wrap entire `updateProfileItems()` in single explicit transaction; rollback on any error; ensure counters reset before abort. |
| Composite index creation blocks long-running DB writes on live DB | LOW | MEDIUM | Run schema migrations during initial DB setup or offline, never on active user session. Use `CREATE INDEX CONCURRENTLY` if SQLite version supports. |
| Dedupulator Phase 3 returns different results due to case-sensitivity | MEDIUM | MEDIUM | Lowercase comparison in both query and application layer; ensure `LOWER(Address)` used consistently. |
| Performance regression on small subscriptions (<100 items) due to batch overhead | LOW | LOW | For very small batches, consider fallback to single-row insert if batch size < threshold (e.g., 50). Benchmark both paths. |

---

## Benchmarks and Acceptance Criteria

All benchmarks performed on identical hardware, same subscription feeds, debug builds disabled (`-O2`/`-O3`).

| Criterion | Threshold | Measurement Method |
|-----------|-----------|-------------------|
| Phase A speedup (vs raw WAL) | ≥2× for 1K items | `time` command around `./validproxy -U <sub_id>` |
| Phase B speedup (vs old per-row INSERT) | ≥10× for 1K items, ≥50× for 10K items | Wall-clock of full subscription update |
| Phase C index lookup speedup | ≥5× on large dup-heavy DBs | Time taken for Deduplicator Phase 4 step |
| Functional correctness | All existing ctests pass, new dedup results identical to baseline | Unit tests + manual DB row-count validation |
| Memory usage | <500MB peak for 10K-item subscription | Monitor RSS during update |

---

## Checklist (Post-Implementation Review)

- [ ] Phase A: Pragmas added to `DatabaseConnectionService::applyPragmas()`, CLI, and AppController
- [ ] Phase A: All `ctest` cases pass
- [ ] Phase B: `ProfileItemDAO::insertBatch()` implemented with transaction safety
- [ ] Phase B: `updateProfileItems()` rewritten — single SELECT for existing keys, batch INSERTs for new, no per-row SELECT EXISTS
- [ ] Phase B: Deduplication still correct — compare final DB row counts before/after against known-good output
- [ ] Phase B: Memory leak verified via ASAN or Valgrind (if available on Windows)
- [ ] Phase C: Composite index created successfully, no conflicts with existing indexes
- [ ] Phase C: Deduplicator Phase 3 & 4 modified to use indexed queries, results match pre-change
- [ ] Phase C: All ctests pass including Deduplication tests
- [ ] Documentation: `docs/INDEX.md`, `project-plans-tracker.md` updated with implementation completion notes
- [ ] Release notes: Summary of performance improvements and known trade-offs added to changelog
