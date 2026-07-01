# Batch Write Transaction Safety Fix

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add transaction wrapping to all batch/multi-statement database write operations that currently lack it, preventing partial writes and improving performance.

**Architecture:** Wrap existing loops and multi-statement operations in `BEGIN TRANSACTION` / `COMMIT` with `ROLLBACK` on failure. Follow the established pattern from `SubitemUpdaterV2::insertProfiles()` (lines 571-704) and `Deduplicator::deduplicate()` (lines 21-50) which already demonstrate correct transaction usage.

**Tech Stack:** C++17, SQLite3 native C API (sqlite3_prepare_v2/sqlite3_step/sqlite3_exec), wxWidgets GUI (unchanged).

---

## Problem Statement

Database audit revealed 4 write paths lacking transaction protection:

| Module | Method | Risk Level | Operations |
|--------|--------|-----------|------------|
| SubitemUpdaterV2 | `syncDatabases()` proxy loop | **HIGH** | Called per-proxy in sync loop (100s of calls); each does INSERT/UPDATE ProfileItem + ProfileExItem |
| SubitemUpdaterV2 | `migrateProxy()` | **HIGH** | Each call does 2+ SQL statements (check + update/insert ProfileItem + update/insert ProfileExItem) |
| ProfileExItemDAO | `updateTestResult()` | MEDIUM | Called per-test-result callback; single INSERT OR REPLACE per call |
| ProfileConfigRepository | `updateProfileExItem()` | MEDIUM | Single INSERT OR REPLACE per call |
| ProfileitemDAO | `deleteBySubId()` | LOW-MEDIUM | Potentially hundreds of rows deleted per call, no transaction |

**Constraint:** SQLite does not support cross-connection transactions. `syncDatabases()` opens srcDb and dstDb as separate connections — we can only wrap dstDb writes in a transaction.

---

## Files to Modify

1. **Modify:** `src/SubitemUpdaterV2.cpp` — Add transaction wrapping to `syncDatabases()` proxy migration loop
2. **Modify:** `src/ProfileExItemDAO.cpp` — Add new `updateTestResultBatch()` method for batch writes
3. **Modify:** `include/Profileexitem.h` — Add `updateTestResultBatch()` declaration
4. **Modify:** `src/ProfileitemDAO.cpp` — Add transaction wrapping to `deleteBySubId()`
5. **Modify:** `docs/INDEX.md` — Register this fix

---

### Task 1: Add transaction wrapping to `SubitemUpdaterV2::syncDatabases()` proxy migration loop

**Files:**
- Modify: `src/SubitemUpdaterV2.cpp:1133-1164` (the proxy migration loop in `syncDatabases`)

**Problem:** Lines 1134-1164 iterate over valid proxies and call `migrateProxy()` for each without any transaction on `dstDb`. If the process crashes mid-sync, dstDb will have partial data.

**Fix:** Wrap the entire migration loop in a single `BEGIN TRANSACTION` / `COMMIT` block on `dstDb`.

**Implementation:**

Around line 1133, BEFORE the for loop, add:
```cpp
char* errMsg = nullptr;
if (sqlite3_exec(dstDb, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
    Logger::write("syncDatabases: BEGIN TRANSACTION failed: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
    sqlite3_free(errMsg);
    sqlite3_close(srcDb);
    sqlite3_close(dstDb);
    return false;
}
```

AFTER the for loop (around line 1165), BEFORE the statistics output, add:
```cpp
if (failCount > 0) {
    Logger::write("WARNING: " + std::to_string(failCount) + " proxies failed to migrate during sync", LogLevel::WARN);
}

if (sqlite3_exec(dstDb, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
    Logger::write("syncDatabases: COMMIT failed, attempting ROLLBACK: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
    sqlite3_free(errMsg);
    sqlite3_exec(dstDb, "ROLLBACK;", nullptr, nullptr, nullptr);
    sqlite3_close(srcDb);
    sqlite3_close(dstDb);
    return false;
}
```

**Success Criteria:**
- The entire proxy migration loop runs within a single transaction
- On COMMIT failure, ROLLBACK is attempted
- Partial sync (some successes + some failures) still commits

---

### Task 2: Add batch transaction method to `ProfileExItemDAO`

**Files:**
- Modify: `include/Profileexitem.h:81` — Add new method declaration
- Modify: `src/ProfileExItemDAO.cpp:41-90` — Add new implementation

**Problem:** `updateTestResult()` is called individually per test callback. Each call is a separate statement with implicit auto-commit. Under heavy concurrent testing (100+ proxies), this causes excessive fsync operations.

**Fix:** Add a new `updateTestResultBatch()` method that accepts a vector of test results and wraps them in a single transaction. The existing single-item method remains unchanged for backward compatibility.

**Header addition (after line 81 in `include/Profileexitem.h`):**
```cpp
  bool updateTestResultBatch(const std::vector<std::tuple<std::string, long, bool, std::string>>& results, sqlite3* db = nullptr);
```

**Source implementation (add after line 90 in `src/ProfileExItemDAO.cpp`):**
```cpp
bool ProfileExItemDAO::updateTestResultBatch(
    const std::vector<std::tuple<std::string, long, bool, std::string>>& results,
    sqlite3* db) {
    
    if (results.empty()) {
        return true;
    }
    
    sqlite3* execDb = db ? db : db_;
    char* errMsg = nullptr;
    
    if (sqlite3_exec(execDb, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("updateTestResultBatch: BEGIN failed: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
        sqlite3_free(errMsg);
        return false;
    }
    
    bool allOk = true;
    const char* insertSql = "INSERT OR REPLACE INTO ProfileExItem (indexid, delay, speed, sort, message, consecutive_failures) VALUES (?, ?, '0', '0', ?, ?);";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(execDb, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::write("updateTestResultBatch: prepare failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
        sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    
    // Pre-fetch consecutive_failures for all items in one shot
    const char* selectSql = "SELECT IndexId, consecutive_failures FROM ProfileExItem";
    sqlite3_stmt* selStmt = nullptr;
    if (sqlite3_prepare_v2(execDb, selectSql, -1, &selStmt, nullptr) != SQLITE_OK) {
        Logger::write("updateTestResultBatch: select prepare failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
        sqlite3_finalize(stmt);
        sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    
    std::unordered_map<std::string, int> failureMap;
    while (sqlite3_step(selStmt) == SQLITE_ROW) {
        const char* idx = reinterpret_cast<const char*>(sqlite3_column_text(selStmt, 0));
        int failures = sqlite3_column_int(selStmt, 1);
        if (idx) {
            failureMap[std::string(idx)] = failures;
        }
    }
    sqlite3_finalize(selStmt);
    
    // Execute batch inserts
    for (const auto& result : results) {
        const std::string& indexid = std::get<0>(result);
        long latencyMs = std::get<1>(result);
        bool success = std::get<2>(result);
        const std::string& curlMsg = std::get<3>(result);
        
        std::string message = success ? "OK" : (curlMsg.empty() ? "FAILED" : curlMsg);
        std::string delayStr = (success && latencyMs >= 0) ? std::to_string(latencyMs / 10) : "-1";
        int currentFailures = 0;
        auto it = failureMap.find(indexid);
        if (it != failureMap.end()) {
            currentFailures = it->second;
        }
        int newFailures = success ? 0 : currentFailures + 1;
        
        sqlite3_bind_text(stmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, delayStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, message.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, newFailures);
        
        int rc = sqlite3_step(stmt);
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        
        if (rc != SQLITE_DONE) {
            Logger::write("updateTestResultBatch: insert failed for " + indexid + ": " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
            allOk = false;
        }
    }
    
    sqlite3_finalize(stmt);
    
    if (!allOk) {
        sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    
    if (sqlite3_exec(execDb, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("updateTestResultBatch: COMMIT failed: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
        sqlite3_free(errMsg);
        sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    
    return true;
}
```

**Success Criteria:**
- New `updateTestResultBatch()` method compiles and works
- Existing `updateTestResult()` single-item method unchanged
- Batch method uses prepared statement reuse (prepare once, step N times)
- Proper BEGIN/COMMIT/ROLLBACK with error handling

---

### Task 3: Add transaction wrapping to `ProfileitemDAO::deleteBySubId()`

**Files:**
- Modify: `src/ProfileitemDAO.cpp:183-198`

**Problem:** `deleteBySubId()` deletes potentially hundreds of ProfileItem rows matching a SubId, but each row deletion is an implicit auto-commit. No transaction wrapping.

**Fix:** Wrap the DELETE in an explicit transaction.

**Implementation — Replace lines 183-198:**
```cpp
bool ProfileitemDAO::deleteBySubId(const std::string& subId) {
    char* errMsg = nullptr;
    
    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("deleteBySubId: BEGIN failed: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
        sqlite3_free(errMsg);
        return false;
    }
    
    bool ok = true;
    const char* sql = "DELETE FROM ProfileItem WHERE Subid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::write("deleteBySubId: prepare error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        ok = false;
    } else {
        sqlite3_bind_text(stmt, 1, subId.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            Logger::write("deleteBySubId: step error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
            ok = false;
        }
    }
    
    if (!ok) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    
    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("deleteBySubId: COMMIT failed: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
        sqlite3_free(errMsg);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    
    return sqlite3_changes(db_) > 0;
}
```

**Success Criteria:**
- `deleteBySubId()` wraps the multi-row DELETE in a transaction
- On failure, ROLLBACK is executed
- Behavior identical to before for single-subId deletion

---

### Task 4: Update `docs/INDEX.md` to register this fix

**Files:**
- Modify: `docs/INDEX.md`

Add a new entry under the appropriate section documenting the transaction safety fix.

---

## Implementation Order

1. **Task 1** (`syncDatabases` transaction) — Highest impact, most dangerous gap
2. **Task 3** (`deleteBySubId` transaction) — Simple, low-risk
3. **Task 2** (`updateTestResultBatch`) — Moderate complexity, requires new method
4. **Task 4** — Documentation update

## Verification

After all tasks:
```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
ctest -V
```

All tests must pass. Additionally:
- Manual sync test with test database to verify no partial writes
- Verify `lsp_diagnostics` clean on all modified files
