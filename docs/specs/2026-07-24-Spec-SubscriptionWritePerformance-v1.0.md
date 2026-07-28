---
title: "Subscription Write Performance Optimization - Pragma tuning, batch insert, phased dedup"
type: spec
status: draft
date: 2026-07-24
---

# Subscription Write Performance Optimization Spec

## 1. Background & Problem Statement

### 1.1 Current Behavior

When updating subscriptions, the system writes new proxy profiles to the SQLite database via `SubitemUpdaterV2::updateProfileItems()` (lines 546-709). For each subscription containing N profiles, the write path executes:

```
BEGIN TRANSACTION
FOR EACH profile:
  1. SELECT (dedup check) — full table scan, no index
  2. INSERT ProfileItem (35 columns)
  3. INSERT ProfileExItem (6 columns)
COMMIT
```

**Key bottleneck**: 3 SQL round-trips per profile × N profiles = 3N database operations within a single transaction.

### 1.2 Measured Impact

- **Small subscription** (100 profiles): ~300 DB operations → seconds
- **Large subscription** (1000+ profiles): ~3000+ DB operations → 10-30+ seconds
- **Batch update** (44 subscriptions × ~1200 avg profiles): ~158,000 DB operations → minutes

### 1.3 Root Causes

| Cause | Location | Impact |
|-------|----------|--------|
| **No secondary indexes** | Schema: ProfileItem has only PK `IndexId` | Every dedup SELECT does full table scan |
| **Per-row dedup query** | `updateProfileItems()` lines 574-580 | `lower(Address)=lower(?) AND Port=? AND ConfigType=? AND lower(Id)=lower(?) AND lower(Network)=lower(?)` — 5-column comparison, no index |
| **No batch INSERT** | Lines 642-694 | Individual prepared statements per row, no `sqlite3_bind_blob` batch |
| **Missing PRAGMA optimizations** | `DatabaseConnectionService::applyPragmas()` | Only WAL + busy_timeout; missing synchronous, cache_size, mmap_size, temp_store |
| **ALL columns TEXT** | ProfileItem: 35 TEXT columns | Port, Enabled stored as TEXT → wasted comparison cycles |
| **Deduplicator Phase 3 loads ALL** | `Deduplicator.cpp` line 241 | `dao.getAll()` loads entire table into memory for C++ iteration |

---

## 2. Optimization Proposals

### 2.1 Phase A: Pragma Tuning (Low Risk, High Impact)

**Goal**: Reduce SQLite overhead with zero schema changes.

**Changes** to `DatabaseConnectionService::applyPragmas()`:

```cpp
// Existing (keep)
sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
sqlite3_exec(db, "PRAGMA busy_timeout=5000;", nullptr, nullptr, nullptr);

// New additions
sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);
sqlite3_exec(db, "PRAGMA cache_size=-8000;", nullptr, nullptr, nullptr);  // 8MB page cache
sqlite3_exec(db, "PRAGMA mmap_size=268435456;", nullptr, nullptr, nullptr);  // 256MB mmap
sqlite3_exec(db, "PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);
sqlite3_exec(db, "PRAGMA journal_size_limit=67108864;", nullptr, nullptr, nullptr);  // 64MB WAL limit
```

**Expected improvement**: 2-5x write throughput.

**Risk**: LOW — All are standard SQLite performance pragmas. `synchronous=NORMAL` is safe with WAL (only risks last transaction on power loss, not data corruption).

**Affected files**:
- `src/DatabaseConnectionService.cpp` — `applyPragmas()` method
- Verify same pragmas in `src/main_cli.cpp:155-156` and `src/ui/AppController.cpp:138-139`

---

### 2.2 Phase B: Batch INSERT + In-Memory Dedup (Medium Risk, High Impact)

**Goal**: Reduce per-row overhead from 3 SQL operations to 1 batched operation.

#### 2.2.1 Design

Replace per-row INSERT with prepared statement + batch binding:

```cpp
// BEFORE: Per-row finalize (current)
sqlite3_prepare_v2(db, insertSQL, -1, &stmt, nullptr);
sqlite3_bind_text(stmt, 1, profile.address.c_str(), ...);
// ... 35 binds ...
sqlite3_step(stmt);
sqlite3_finalize(stmt);  // ← finalize per row = expensive

// AFTER: Single prepare + batch bind + reset
sqlite3_prepare_v2(db, insertSQL, -1, &stmt, nullptr);
for (const auto& profile : profiles) {
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_bind_text(stmt, 1, profile.address.c_str(), ...);
    // ... 35 binds ...
    sqlite3_step(stmt);
}
sqlite3_finalize(stmt);  // ← finalize once after batch
```

#### 2.2.2 In-Memory Dedup Hash

Replace per-row SELECT with `std::unordered_set` dedup key:

```cpp
// Dedup key: hash(Address, Port, ConfigType, Id, Network)
struct DedupKey {
    std::string address_lower;
    int port;
    std::string config_type;
    std::string id_lower;
    std::string network_lower;
    
    bool operator==(const DedupKey& o) const { /* field-wise compare */ }
};

struct DedupKeyHash {
    size_t operator()(const DedupKey& k) const {
        // Combine std::hash for each field
        size_t h = std::hash<std::string>{}(k.address_lower);
        h ^= std::hash<int>{}(k.port) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>{}(k.config_type) << 1;
        h ^= std::hash<std::string>{}(k.id_lower) << 2;
        h ^= std::hash<std::string>{}(k.network_lower) << 3;
        return h;
    }
};
```

**Algorithm**:
1. `BEGIN TRANSACTION`
2. Load existing dedup keys into `unordered_set` via single SELECT (batch read, not per-row)
3. Filter new profiles against set (O(1) per check)
4. Batch INSERT non-duplicate profiles
5. `COMMIT`

**Expected improvement**: 10-50x for the write phase. Dedup check goes from O(N×M) table scans to O(N) set lookups.

**Risk**: MEDIUM — Requires verifying memory usage for large datasets (100K profiles ≈ 50-100MB in-memory set).

**Affected files**:
- `src/SubitemUpdaterV2.cpp` — `updateProfileItems()` rewrite
- `include/SubitemUpdaterV2.h` — Add dedup set type

---

### 2.3 Phase C: Composite Index + Phased Dedup (Medium Risk, Very High Impact)

**Goal**: Optimize both new-write dedup and post-write Deduplicator phases.

#### 2.3.1 Composite Index

Add index for the dedup query pattern:

```sql
CREATE INDEX IF NOT EXISTS idx_profile_dedup 
ON ProfileItem(lower(Address), Port, ConfigType, lower(Id), lower(Network));
```

**Note**: SQLite supports expressions in indexes (covering index). This directly accelerates the dedup SELECT.

**Expected improvement**: Dedup SELECT goes from O(M) full scan to O(log M) index seek.

#### 2.3.2 Deduplicator Phase 3 Optimization

Current Phase 3 (`Deduplicator.cpp` line 241):
```cpp
auto allProfiles = dao.getAll();  // ← loads ALL profiles into memory
for (auto& p : allProfiles) {
    if (!p.checkRequired()) {
        dao.deleteByIndexIdNoTx(p.indexId);  // ← individual DELETE per row
    }
}
```

**Proposed**: SQL-based batch delete:
```sql
DELETE FROM ProfileItem WHERE IndexId IN (
    SELECT IndexId FROM ProfileItem 
    WHERE ... checkRequired conditions ...
);
```

Or if C++ logic is needed, use batch DELETE:
```cpp
std::vector<std::string> toDelete;
for (auto& p : dao.getAll()) {  // Still loads, but DELETE is batched
    if (!p.checkRequired()) toDelete.push_back(p.indexId);
}
if (!toDelete.empty()) {
    dao.deleteByIndexIdsNoTx(toDelete);  // Batch delete via IN clause
}
```

#### 2.3.3 Deduplicator Phase 4 CTE Optimization

Current Phase 4 uses window function:
```sql
WITH ranked AS (
    SELECT IndexId, ROW_NUMBER() OVER (
        PARTITION BY lower(Address), Port, ConfigType, lower(Id), lower(Network)
        ORDER BY ...
    ) as rn
    FROM ProfileItem
)
DELETE FROM ProfileItem WHERE IndexId IN (
    SELECT IndexId FROM ranked WHERE rn > 1
);
```

**Proposed**: With composite index, this CTE becomes index-backed. Additional optimization: use `DELETE ... USING` instead of `IN` subquery:

```sql
DELETE FROM ProfileItem USING ranked 
WHERE ProfileItem.IndexId = ranked.IndexId AND ranked.rn > 1;
```

**Expected improvement**: 50-100x combined with Phase A+B.

**Risk**: MEDIUM — Index adds ~10-20MB to database size, slight INSERT overhead for maintaining index.

**Affected files**:
- `src/update/Deduplicator.cpp` — Phase 3, 4 optimizations
- `src/DatabaseHelper.h` or DAO — Add `deleteByIndexIdsNoTx()` batch method
- Schema migration or startup — Create composite index

---

### 2.4 Phase D: Memory Database for New Subscriptions (High Risk, Highest Impact)

**Goal**: Write to `:memory:` SQLite first, then merge to disk in single operation.

#### 2.4.1 Design

```
Disk DB (existing)          Memory DB (ephemeral)
┌──────────────┐           ┌──────────────┐
│ ProfileItem  │           │ ProfileItem  │
│ (50K rows)   │           │ (200 new)    │
│              │     ←───  │              │
│              │  INSERT   │              │
│              │  OR IGNORE│              │
└──────────────┘           └──────────────┘
         │                         │
         └─────────────────────────┘
              MERGE via INSERT OR IGNORE ... SELECT
```

**Algorithm**:
1. Create `:memory:` SQLite database with same schema
2. Parse subscription → write profiles to memory DB
3. Use `INSERT OR IGNORE INTO main_db.ProfileItem SELECT * FROM mem_db.ProfileItem` for merge
4. Single atomic operation replaces 3N round-trips

**Expected improvement**: 100x+ for write phase (single INSERT ... SELECT vs 3N individual operations).

**Risk**: HIGH — Complex to implement, requires maintaining two SQLite handles, transaction coordination, error recovery.

**Affected files**:
- `src/SubitemUpdaterV2.cpp` — `updateProfileItems()` complete rewrite
- `src/DatabaseConnectionService.cpp` — Memory DB connection factory
- `include/SubitemUpdaterV2.h` — New method signatures

---

## 3. Recommended Implementation Order

| Phase | Effort | Impact | Risk | Dependencies |
|-------|--------|--------|------|--------------|
| **A: Pragma Tuning** | ~10 lines | 2-5x | LOW | None |
| **B: Batch INSERT + Hash Dedup** | ~100 lines | 10-50x | MEDIUM | Phase A |
| **C: Index + Phased Dedup** | ~200 lines | 50-100x | MEDIUM | Phase A |
| **D: Memory DB Merge** | ~300 lines | 100x+ | HIGH | Phase A, B |

**Recommended path**: **A → B → C** (skip D unless extreme scale needed)

Phase A is zero-risk and provides immediate benefit. Phase B eliminates the dominant per-row overhead. Phase C optimizes the post-write Deduplicator. Phase D is only needed if subscription counts exceed 10K+ per batch.

---

## 4. Detailed Implementation Plan

### 4.1 Phase A: Pragma Tuning

**File**: `src/DatabaseConnectionService.cpp`

**Change**: Add 5 PRAGMA statements to `applyPragmas()`.

**Testing**: Run `ctest -V` — all existing tests must pass. No behavioral change expected.

**Rollback**: Remove added PRAGMA lines.

---

### 4.2 Phase B: Batch INSERT + Hash Dedup

**Files**:
- `src/SubitemUpdaterV2.cpp` — Rewrite `updateProfileItems()`
- `include/SubitemUpdaterV2.h` — Add `DedupKey`, `DedupKeyHash` structs

**Change details**:

1. Add `DedupKey` struct (see §2.2.2)
2. In `updateProfileItems()`:
   a. Single SELECT to load existing keys into `unordered_set<DedupKey>`
   b. Filter incoming profiles against set
   c. Single prepared statement, `reset()` + `clear_bindings()` per row (no `finalize()` until done)
   d. Commit batch

**Testing**: 
- Add unit test: `TEST_F(SubitemUpdaterV2Test, BatchInsertPerformance)` — insert 1000 profiles, verify < 1 second
- Add unit test: `TEST_F(SubitemUpdaterV2Test, DedupHashSetAccuracy)` — verify hash correctness vs SQL dedup

**Rollback**: Revert to per-row INSERT pattern.

---

### 4.3 Phase C: Composite Index + Dedup Optimization

**Files**:
- `src/update/Deduplicator.cpp` — Phase 3, 4 optimization
- `src/DatabaseHelper.h` — Add `deleteByIndexIdsNoTx()` method
- Schema migration — Create index on first run or via migration script

**Change details**:

1. Add `CREATE INDEX IF NOT EXISTS idx_profile_dedup` at startup or in migration
2. Phase 3: Replace individual `deleteByIndexIdNoTx()` calls with batch delete
3. Phase 4: Verify CTE uses index (EXPLAIN QUERY PLAN check)

**Testing**:
- Run full dedup test suite: `ctest -R DedupTest -V`
- Performance benchmark: measure dedup time with/without index on 50K profiles

**Rollback**: `DROP INDEX idx_profile_dedup;` and revert Phase 3/4 changes.

---

## 5. Data Flow Diagram (Current vs Proposed)

### Current Flow

```
Subscription Content
    ↓
parseSubscription() → vector<Profileitem>
    ↓
updateProfileItems()
    ├─ BEGIN TRANSACTION
    ├─ FOR EACH profile (N times):
    │   ├─ SELECT (dedup check)     ← O(M) full scan each
    │   ├─ INSERT ProfileItem       ← 35 column bind
    │   └─ INSERT ProfileExItem     ← 6 column bind
    └─ COMMIT
    Total: 3N SQL operations
```

### Proposed Flow (Phase A+B)

```
Subscription Content
    ↓
parseSubscription() → vector<Profileitem>
    ↓
updateProfileItems()
    ├─ BEGIN TRANSACTION
    ├─ SELECT ALL existing keys → unordered_set  ← 1 operation
    ├─ Filter profiles against set                ← O(N) in-memory
    ├─ FOR EACH non-duplicate profile (≤N times):
    │   └─ INSERT (batch, no finalize)            ← 1 bind + step
    ├─ COMMIT
    └─ Total: ~N+1 SQL operations (vs 3N)
```

---

## 6. Performance Estimates

| Scenario | Current | After A | After A+B | After A+B+C |
|----------|---------|---------|-----------|-------------|
| 100 profiles | ~2s | ~0.5s | ~0.1s | ~0.05s |
| 1000 profiles | ~20s | ~5s | ~0.5s | ~0.2s |
| 10K profiles | ~200s | ~50s | ~3s | ~1s |
| 44 subs × 1200 avg | ~440s | ~110s | ~15s | ~5s |

*Estimates based on SQLite benchmarks for similar workloads on SSD.*

---

## 7. Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Memory pressure from hash set | Cap at 500K entries; fall back to SQL dedup if exceeded |
| Index bloat | Monitor DB size; drop/recreate if >50MB overhead |
| Transaction lock contention | WAL mode already handles concurrent reads; single writer pattern preserved |
| Data consistency | Phase A+B maintain single transaction boundary; no partial writes |
| Rollback complexity | Each phase independently reversible; no cross-phase dependencies |

---

## 8. Acceptance Criteria

- [ ] All existing `ctest -V` pass after each phase
- [ ] Phase A: Pragma settings verified via `PRAGMA *;` queries
- [ ] Phase B: 1000-profile insert completes in < 1 second
- [ ] Phase B: Dedup accuracy matches current SQL-based dedup (100% equivalence)
- [ ] Phase C: Dedup EXPLAIN QUERY PLAN shows index usage
- [ ] Phase C: Deduplicator Phase 3+4 time reduced by 10x+
- [ ] No new compiler warnings (MinGW/GCC)
- [ ] Memory usage stays < 200MB for 100K profiles

---

## 9. References

- `docs/superpowers/specs/2026-04-16-subitem-updater-v2-optimization.md` — Proxy lifecycle optimization (different scope)
- `docs/specs/2026-06-18-Spec-SubitemUpdaterV2-Decomposition-v1.0.md` — Code decomposition (completed)
- `docs/design/dedup-blacklist-design.md` — Dedup blacklist logic reference
- `docs/design/invalid-proxy-filter-map.md` — Invalid proxy filter strategies
- SQLite Performance FAQ: https://www.sqlite.org/pragma.html#pragma_synchronous
