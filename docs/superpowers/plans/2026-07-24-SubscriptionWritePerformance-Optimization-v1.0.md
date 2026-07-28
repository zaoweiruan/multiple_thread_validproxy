# Subscription Write Performance Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Optimize subscription item database write performance in SubitemUpdaterV2 by implementing pragma tuning, batch INSERTs with in-memory hash deduplication, composite index, and Deduplicator optimizations — targeted 10-50x speedup on large subscriptions (1K+ items).

**Architecture:** Four phased approach: (A) PrAGMA tuning for SQLite write performance; (B) Rewrite updateProfileItems() with prepared statement batching and unordered_set deduplication; (C) Add composite index on dedup columns and optimize Deduplicator Phase 3/4; (D) Optional memory-DB merge if later phases still insufficient. All changes maintain full backward compatibility and pass existing tests.

**Tech Stack:** C++17, SQLite3, Google Test, CMake/Ninja, Windows/MinGW.

---

## Phase A: Pragma Tuning — Low-Risk Immediate Gain

### File: `src/DatabaseConnectionService.cpp` (function `applyPragmas()`, lines ~160-185)

#### Step 1: Read current `applyPragmas()` implementation

Verify current pragmas only include `PRAGMA journal_mode=WAL; PRAGMA busy_timeout=5000;`:

```cpp
// Current state (placeholder for reference)
void DatabaseConnectionService::applyPragmas() {
    execSql("PRAGMA journal_mode=WAL;");
    execSql("PRAGMA busy_timeout=5000;");
}
```

#### Step 2: Extend `applyPragmas()` with 5 additional PRAGMAs

Add these lines after the existing pragmas:

```cpp
PRAGMA cache_size=-100000;      // ~100MB page cache (adjust based on actual page size)
PRAGMA synchronous=NORMAL;     // faster writes, still safe for most use cases
PRAGMA temp_store=MEMORY;      // keep temp tables in RAM
PRAGMA mmap_size=268435456;    // 268MB address space for mmap
PRAGMA journal_size_limit=104857600; // 100MB max journal size
```

**Note:** `cache_size=-N` means negative number of pages. For default page size 8KB, `-100000` ≈ 800MB. Adjust value based on actual page size observed in code (query via `PRAGMA page_size;` before applying).

#### Step 3: Apply consistency fix to CLI and GUI entry points

Also add same pragmas to ensure all DB connections have identical settings:

- **File:** `main_cli.cpp` lines 155-156 — add pragmas after `db.Open(...)`, before any data operations
- **File:** `AppController.cpp` line 138-139 — add pragmas in GUI initialization after opening DB

#### Step 4: Verify no regressions

Run full test suite: `ctest -V`. All existing tests must pass without modification.

### Validation Metric
- Before/after wall-clock time for sample subscription (500 items). Expected improvement: ≥2× from journal_mode alone plus additional gains from other pragmas.

---

## Phase B: Batch INSERT + In-Memory Hash Dedup — Core Bottleneck Fix

### Critical Files
- `src/SubitemUpdaterV2.h` — interface declaration for `updateProfileItems()`
- `src/SubitemUpdaterV2.cpp` — implementation of `updateProfileItems()` (lines 546-709)  
- `src/Dao/ProfileItemDAO.h` / `ProfileItemDAO.cpp` — will need new `insertBatch()` method
- `src/Dao/ProfileExItemDAO.h` / `ProfileExItemDAO.cpp` — similarly extend with `insertBatch()`

### Baseline Understanding
Current per-profile logic in `updateProfileItems()`:
1. For each profile row: `SELECT idx FROM ... WHERE ...` (dedup check) → 1 SQL round-trip
2. If not exists: `INSERT INTO ProfileItem (...) VALUES (...)` → 1 SQL round-trip
3. Same pattern repeated for ProfileExItem
Total: up to 3 SQL statements per profile row. For 10K profiles = 30K transactions/round-trips.

### Refactor Strategy
Rewrite `updateProfileItems()` into single transaction with batch operations:

**Step 1: Collect incoming subscription data in memory first** (0 SQL reads for new items)
**Step 2: Single SELECT** for all existing dedup keys from DB
**Step 3: Build `unordered_set`** of existing dedup keys for O(1) lookup
**Step 4: Build vector** of new ProfileItem rows that don't exist (via hash lookup)
**Step 5: Batch INSERT** using `INSERT OR IGNORE` on the new vector
**Step 6: Same process** for ProfileExItem
**Step 7: Update counters** at end of single transaction

### Task Breakdown

**Task B1 — Extend ProfileItemDAO with batch insert**

- [ ] Modify `ProfileItemDAO.h`:
  ```cpp
  void insertBatch(const std::vector<ProfileItem>& items);
  ```

- [ ] Implement `ProfileItemDAO::insertBatch()` in `.cpp`:
  ```cpp
  void ProfileItemDAO::insertBatch(const std::vector<ProfileItem>& items) {
      if (items.empty()) return;
      BeginTransaction();
      try {
          const auto stmt = prepareStmt("INSERT OR IGNORE INTO ProfileItem "
            "(IndexId,Address,Port,Enabled,ConfigType,Network,Id,Protocol,BypassProxy,Rule,Remark,AddedAt,UpsertAt,"
            "Delay,Rtt,Jitter,Loss,Country,City,DnsHost,UserAgent,ShadowsocksMethod,ShadowsocksPassword,"
            "TrojanPassword,SsnodocSocks5Username,SsnodocSocks5Password,MetoxAuth,VmessUsername,VmessPassword,"
            "HysteriaAuth,HystericaPassword,SSHUsername,SSHPassword,XTlsMode,Remarks) VALUES "
            "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
          
          for (const auto& item : items) {
              bindStmt(stmt, 1, item.IndexId);
              bindStmt(stmt, 2, item.Address);
              bindStmt(stmt, 3, item.Port);
              // ... bind all remaining parameters (37 total)
              stepStmt(stmt); // commit this row
              resetStmt(stmt); // reuse for next row
          }
          CommitTransaction();
      } catch (...) {
          RollbackTransaction();
          throw;
      }
  }
  ```

**Task B2 — Extend ProfileExItemDAO identically** with matching column list.

**Task B3 — Rewrite `SubitemUpdaterV2::updateProfileItems()`**

Replace the existing loop-based code with:

```cpp
bool SubitemUpdaterV2::updateProfileItems(/* same params */) {
    // --- Step 1: Load ALL existing dedup keys once ---
    auto all_items = m_profile_item_dao->getAll(); // ONE SELECT
    std::unordered_set<string> existing_keys;
    for (const auto& item : all_items) {
        string key = buildDedupKey(item); // lower(Address)+|+Port+|+ConfigType+|+lower(Id)+|+lower(Network)
        existing_keys.insert(key);
    }

    // --- Step 2: Collect new items from subscription parsing ---
    vector<ProfileItem> new_profiles;
    vector<ProfileItemEx> new_profile_exes;

    for (const auto& sub_item : sub_items) {
        // Parse subscription... get item.ProfileItem & item.ProfileExItem

        string key = buildDedupKey(profile_item);
        if (existing_keys.find(key) == existing_keys.end()) {
            new_profiles.push_back(profile_item);
            new_profile_exes.push_back(profile_ex_item);
        } else {
            // Update existing (if needed): set Modified flag and store for later UPDATE batch
            // Original code handles updates — preserve this logic carefully
        }
    }

    // --- Step 3: Batch INSERT new items in single transaction ---
    BeginTransaction();
    try {
        if (!new_profiles.empty()) {
            m_profile_item_dao->insertBatch(new_profiles); // ONE batch INSERT
        }
        if (!new_profile_exes.empty()) {
            m_profile_ex_item_dao->insertBatch(new_profile_exes); // ONE batch INSERT
        }
        CommitTransaction();
    } catch (...) {
        RollbackTransaction();
        throw;
    }

    // --- Step 4: Update counters and status ---
    // Preserve exact logic from original for profile_item_count, sub_item_count, etc.

    return true;
}
```

**Task B4 — Remove redundant per-row SELECT EXISTS**

Delete all instances of:
```cpp
if (m_profile_item_dao->existsByDedupKey(lower_addr, port, config_type, lower_id, lower_network)) {
    continue;
}
```
The `INSERT OR IGNORE` and pre-existing key set handle deduplication.

**Task B5 — Handle existing item updates correctly**

Original code may modify existing items (when `Modified` flag is set). Need to:
- Detect modified items during iteration (compare against existing DB values or track changes)
- Separate `new_profiles` (INSERT) from `updated_profiles` (UPDATE in separate batch)
- Create `updateBatch()` method in DAO to handle multiple UPDATE statements in one transaction

### Testing Requirements

1. **Unit test for dedup correctness**: Create test with duplicate keys inserted both sequentially and randomly — verify final DB row count matches expected (no duplicates, no lost inserts).

2. **Integration test**: Full subscription update flow with mixed new, duplicate, and modified items — compare output row counts and content against baseline.

3. **Benchmark**: Measure wall-clock time for inserting 1K and 10K items (with ~50% duplicates) comparing old vs new code. Target: ≥10× speedup at 1K, ≥50× at 10K.

---

## Phase C: Composite Index + Deduplicator Optimization

### Files
- `src/Dao/ProfileItemDAO.h/.cpp` — add index creation during DB init/update
- `src/update/Deduplicator.cpp` — Phase 3 (`removeDuplicateProfilesViaFullLoad`) and Phase 4 (`deduplicateConfigErrorPhase`)
- `src/Dao/ProfileExItemDAO.h/.cpp` — possibly similar treatment

#### Step C1: Add composite index on dedup columns

In `ProfileItemDAO::initialize()` or dedicated migration function:

```cpp
void ProfileItemDAO::initialize() {
    // ... existing init code
    execSql("CREATE INDEX IF NOT EXISTS idx_profile_dedup ON ProfileItem("
            "LOWER(Address), Port, ConfigType, LOWER(Id), LOWER(Network));");
}
```

This speeds up the `SELECT COUNT(*) WHERE LOWER(...) AND ...` queries used in Deduplicator Phase 4 from table scan to indexed seek.

#### Step C2: Optimize Deduplicator Phase 3 (`removeDuplicateProfilesViaFullLoad`)

Currently loads ALL profiles into memory to compare. Replace with:

- Query database directly for duplicates:
```sql
SELECT IndexId FROM ProfileItem 
WHERE LOWER(?)=? AND LOWER(?)=? GROUP BY IndexId HAVING COUNT(*)>1
```
- Delete matching rows via batch delete instead of逐行 delete.
- Add `deleteByIndexIds(const vector<string>& indices)` to DAO layer.

#### Step C3: Optimize Deduplicator Phase 4 (`deduplicateConfigErrorPhase`)

Replace heavy CTE with ROW_NUMBER with simpler query using the new composite index:

```sql
-- OLD (expensive):
WITH Ranked AS (
    SELECT IndexId, ROW_NUMBER() OVER (PARTITION BY Address, Port, ConfigType, Id, Network ORDER BY AddedAt) as rn
    FROM ProfileItem
)
SELECT IndexId FROM Ranked WHERE rn > 1;

-- NEW (index-friendly):
SELECT p2.IndexId FROM ProfileItem p1
JOIN ProfileItem p2 ON LOWER(p1.Address)=LOWER(p2.Address) AND p1.Port=p2.Port 
                   AND p1.ConfigType=p2.ConfigType AND LOWER(p1.Id)=LOWER(p2.Id) AND LOWER(p1.Network)=LOWER(p2.Network)
                    AND p1.AddedAt < p2.AddedAt
WHERE p1.IndexId = (SELECT MIN(IndexId) FROM ProfileItem 
                     WHERE LOWER(Address)=LOWER(p1.Address) AND Port=p1.Port 
                     AND ConfigType=p1.ConfigType AND LOWER(Id)=LOWER(p1.Id) AND LOWER(Network)=LOWER(p1.Network));
```

Or alternatively: select all candidate duplicates via indexed filter, then apply top-N logic in application layer.

#### Step C4: Add batch-delete helper in DAO

```cpp
void ProfileItemDAO::deleteByIndexIds(const vector<string>& indices) {
    if (indices.empty()) return;
    BeginTransaction();
    try {
        auto stmt = prepareStmt("DELETE FROM ProfileItem WHERE IndexId IN (" + 
            createPlaceholders(indices.size()) + ");");
        // Bind each index to placeholder
        CommitTransaction();
    } catch (...) {
        RollbackTransaction();
        throw;
    }
}
```

**Validation:** Run Deduplicator unit tests — confirm identical output shape (only 1 copy per unique proxy remains). Benchmarks on high-duplication subscriptions (≥80%) should show Phase 4 speedup 5-10× from indexed scan vs full table scan.

---

## Phase D (Optional): Memory-DB Merge Path

Only needed if total write time remains above threshold after Phases A-C for subscriptions exceeding 10K items.

**Strategy:** Open temporary in-memory SQLite instance, do all batch INSERTs there (fastest possible), then attach to main DB and copy results:

```sql
ATTACH MEMORY ':memory:' AS mem_db; -- in-memory DB
-- All INSERTs go to mem_db.ProfileItem
INSERT INTO main.ProfileItem SELECT * FROM mem_db.ProfileItem;
DETACH mem_db;
```

**Implementation notes:**
- Requires coordinating with transaction handling in SubitemUpdaterV2 to route to memory DB
- Only considered if benchmarking shows remaining bottleneck after A+B+C
- Deferred per optimization path recommendation A→B→C→(optional D)

---

## Integration & Release Sequence

1. **Precondition**: Baseline git checkout at current stable tag. All ctests pass.
2. **Implement Phase A** → compile → `ctest -V` → measure baseline perf.
3. **Implement Phase B** → recompile → run dedup unit tests → integration test → benchmark → verify no functional regression.
4. **Implement Phase C** → run full test suite → validate dedup results match baseline.
5. **Documentation**: Update `docs/superpowers/plans/2026-07-24-SubscriptionWritePerformance-Optimization-v1.0.md` with post-mortem benchmark numbers and lessons learned.

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

- [ ] Phase A: Pragmas added to `DatabaseConnectionService::applyPragmas()`, CLI (`main_cli.cpp`), and AppController
- [ ] Phase A: All `ctest` cases pass
- [ ] Phase B: `ProfileItemDAO::insertBatch()` implemented with transaction safety and proper bind handling
- [ ] Phase B: `updateProfileItems()` rewritten with single SELECT, batch INSERT, in-memory hash dedup
- [ ] Phase B: Deduplication still correct — final DB row counts match baseline
- [ ] Phase B: ASAN/UBSan verification passed (run with `-DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON`)
- [ ] Phase C: Composite index `idx_profile_dedup` created successfully, no conflicts
- [ ] Phase C: Deduplicator Phase 3 & 4 modified to use indexed queries, results match pre-change
- [ ] Phase C: All ctests pass including Deduplication tests
- [ ] Documentation: `docs/INDEX.md`, `project-plans-tracker.md` updated with completion status
- [ ] Release Notes: Summary of performance improvements and known trade-offs added to changelog
