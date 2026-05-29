# Config Transaction Batch - Requirements Document

**Date**: 2026-05-07  
**Source**: Brainstorm session for improvement idea #2 from `docs/ideation/2026-05-07-improvement-ideas-ideation.md`  
**Status**: Draft

---

## 1. Problem Statement

The `SubitemUpdaterV2` class performs database operations (INSERT/UPDATE/DELETE on `SubItem` table) without consistent transaction protection. While there is one `BEGIN TRANSACTION`/`COMMIT` pair (line ~1660), the critical `insertSubItem()` function performs **single-row inserts without transaction batching**. This leads to:

1. **Performance issue**: Each `INSERT` is a separate transaction (disk flush per row)
2. **Data inconsistency risk**: Batch import (`importSubitemsFromFile`) can partially fail, leaving partial data
3. **No rollback capability**: If 100th item fails, first 99 remain committed

### Current State Analysis

| Location | Operation | Transaction? | Issue |
|----------|-------------|---------------|-------|
| `insertSubItem()` (line 49-82) | Single INSERT | ❌ No | Called in loop = N transactions |
| `importSubitemsFromFile()` | Batch import | ❌ No | Partial failure = partial data |
| Line ~1660 | Some operation | ✅ Yes | Only one place uses transaction |
| `updateProfileItems()` | Batch update | ❌ Unknown | Needs investigation |

---

## 2. Requirements

### 2.1 Functional Requirements

| ID | Requirement | Notes |
|----|-------------|-------|
| FR-1 | Add `TransactionScope` RAII class for SQLite transactions | Ensures rollback on exception |
| FR-2 | Wrap batch operations (import, update) in transactions | Use `TransactionScope` |
| FR-3 | Support explicit commit/rollback (RAII: rollback on dtor if not committed) | Exception-safe |
| FR-4 | Measure performance improvement (before/after) | Quantify benefit |
| FR-5 | Log transaction begin/commit/rollback events | Debuggability |
| FR-6 | Handle nested transaction attempts (reject or savepoint) | Decide: reject or SAVEPOINT |
| FR-7 | Update `insertSubItem()` to support batch inserts (multiple items per transaction) | Performance |

### 2.2 Non-Functional Requirements

| ID | Requirement | Notes |
|----|-------------|-------|
| NFR-1 | Must compile on Windows/MinGW-w64 with C++17 | Project constraint |
| NFR-2 | No new third-party dependencies | SQLite3 is already linked |
| NFR-3 | Must use existing `Logger::write()` for transaction logging | Consistent with project |
| NFR-4 | Transaction RAII class should be reusable (not SubitemUpdaterV2-specific) | Put in `include/DatabaseHelper.h` or new `include/TransactionScope.h` |

---

## 3. Design Decisions

### 3.1 Transaction RAII Class
**Decision**: Create `db::TransactionScope` class in `include/DatabaseHelper.h` (extend existing `db::Database` namespace)  
**Rationale**: Follows existing pattern (`db::Database` already in `DatabaseHelper.h`)  
**Alternative**: New file `include/TransactionScope.h` (if class grows large)

**Proposed API**:
```cpp
namespace db {
class TransactionScope {
public:
    explicit TransactionScope(sqlite3* db);  // BEGIN TRANSACTION
    ~TransactionScope();  // ROLLBACK if not committed
    
    void commit();   // COMMIT
    void rollback(); // ROLLBACK
    
    // Disable copy
    TransactionScope(const TransactionScope&) = delete;
    TransactionScope& operator=(const TransactionScope&) = delete;
    
private:
    sqlite3* db_;
    bool committed_;
    bool rollbacked_;
};
}
```

### 3.2 Nested Transaction Handling
**Decision**: Use SQLite **SAVEPOINT** mechanism (not flat BEGIN/COMMIT)  
**Rationale**: Allows nested transaction scopes (caller may start tx, callee also wants tx)  
**Implementation**:
- Outermost: `BEGIN TRANSACTION`
- Nested: `SAVEPOINT sp1`, `SAVEPOINT sp2`, ...
- Commit/Rollback at each level

**Alternative**: Reject nested transactions (simpler, may be sufficient)

### 3.3 Batch Insert Strategy
**Decision**: Modify `insertSubItem()` to accept `std::vector<Subitem>` + `TransactionScope&`  
**Rationale**: Caller controls transaction boundary  
**Example**:
```cpp
bool insertSubItems(sqlite3* db, const std::vector<db::models::Subitem>& items, db::TransactionScope& tx) {
    for (const auto& item : items) {
        // INSERT binding...
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            tx.rollback();  // Rollback entire batch
            return false;
        }
    }
    // Caller decides when to commit
    return true;
}
```

### 3.4 Performance Measurement
**Decision**: Add optional benchmark mode (log time before/after batch operation)  
**Rationale**: Quantify improvement, validate requirement FR-4  
**Implementation**: Use `std::chrono::high_resolution_clock` in debug builds

---

## 4. Usage Examples (After Implementation)

### 4.1 Batch Import with Transaction
**Before** (current, no transaction):
```cpp
bool SubitemUpdaterV2::importSubitemsFromFile(const std::string& filePath, ...) {
    for (const auto& subitem : items) {
        if (!insertSubItem(db_, subitem)) {
            // Oops: previous items already committed!
            return false;
        }
    }
    return true;
}
```

**After** (with `TransactionScope`):
```cpp
bool SubitemUpdaterV2::importSubitemsFromFile(const std::string& filePath, ...) {
    try {
        db::TransactionScope tx(db_);  // BEGIN TRANSACTION
        
        for (const auto& subitem : items) {
            if (!insertSubItem(db_, subitem)) {
                return false;  // tx dtor will ROLLBACK
            }
        }
        
        tx.commit();  // COMMIT
        return true;
    } catch (const std::exception& e) {
        Logger::write("ERROR: Import failed, transaction rolled back: " + std::string(e.what()), LogLevel::LOG_ERROR);
        return false;  // tx dtor will ROLLBACK
    }
}
```

### 4.2 Nested Transaction (SAVEPOINT)
```cpp
void outer() {
    db::TransactionScope tx1(db_);  // BEGIN TRANSACTION
    
    inner();  // Calls inner(), which starts its own scope
    
    tx1.commit();
}

void inner() {
    db::TransactionScope tx2(db_);  // SAVEPOINT sp1
    
    // Do work...
    
    tx2.commit();  // RELEASE sp1
}
```

---

## 5. Acceptance Criteria

- [ ] `db::TransactionScope` class implemented and compiles
- [ ] RAII: Destructor rolls back if not committed
- [ ] Support nested transactions via SAVEPOINT
- [ ] All batch operations in `SubitemUpdaterV2` wrapped in `TransactionScope`
- [ ] `insertSubItem()` supports batch mode (vector input)
- [ ] Performance improvement measured (log before/after time)
- [ ] Logger::write() logs transaction events (begin/commit/rollback)
- [ ] Exception safety: rollback on `std::runtime_error`
- [ ] Unit tests for `TransactionScope` (create `tests/test_transaction_scope.cpp`)

---

## 6. Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| SAVEPOINT syntax errors | Medium | Test on SQLite 3.x (project version) |
| Nested transaction complexity | Medium | Start with flat BEGIN/COMMIT, add SAVEPOINT later |
| Performance regression (if implemented wrong) | Low | Benchmark before/after |
| Breaking existing single-insert callers | Medium | Keep `insertSubItem()` single-item overload, add batch overload |

---

## 7. Next Steps

1. Run `ce-plan` to create implementation plan
2. Implement `db::TransactionScope` in `include/DatabaseHelper.h`
3. Modify `insertSubItem()` to support batch inserts
4. Wrap all batch operations in `SubitemUpdaterV2` with transactions
5. Add SAVEPOINT support for nested transactions
6. Add tests for `TransactionScope`
7. Benchmark and measure performance improvement

---

## 8. References

- **Existing database helper**: `include/DatabaseHelper.h` (`db::Database` class)
- **SQLite transaction docs**: https://www.sqlite.org/lang_transaction.html
- **SQLite savepoint docs**: https://www.sqlite.org/lang_savepoint.html
- **SubitemUpdaterV2.cpp**: Bulk operations at lines 49-82, ~1660
