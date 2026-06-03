# 订阅右键删除功能实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现订阅面板右键删除订阅功能，支持级联删除相关代理数据

**Architecture:** 
- 在 `SubitemDAO` 和 `ProfileitemDAO` 中添加删除方法
- 在 `AppController` 中封装删除逻辑（先删代理，再删订阅）
- 在 `SubscriptionPanel::onDeleteSubscription` 中调用 controller 方法

**Tech Stack:** C++17, SQLite3, Google Test

---

## Task 1: SubitemDAO::deleteById 方法

**Files:**
- Modify: `include/Subitem.h:189-218` (after updateSubitem method)

- [ ] **Step 1: Add escape helper and deleteById method to SubitemDAO**

```cpp
// Inside class SubitemDAO, add after updateSubitem():
static std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) { if (c == '\'') out += "''"; else out += c; }
    return out;
}

bool deleteById(const std::string& id) {
    std::string sql = "DELETE FROM SubItem WHERE Id = '" + escape(id) + "';";
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::write("Delete subscription error: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}
```

- [ ] **Step 2: Build to verify compilation**

Run: `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel 8`
Expected: Build succeeds without errors

## Task 2: ProfileitemDAO::deleteBySubId 方法

**Files:**
- Modify: `include/Profileitem.h:374-375` (after getByIndexId method)

- [ ] **Step 1: Add escape helper and deleteBySubId method to ProfileitemDAO**

```cpp
// Inside class ProfileitemDAO, add after getByIndexId():
static std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) { if (c == '\'') out += "''"; else out += c; }
    return out;
}

bool deleteBySubId(const std::string& subId) {
    std::string sql = "DELETE FROM ProfileItem WHERE Subid = '" + escape(subId) + "';";
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::write("Delete proxies by subId error: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}
```

- [ ] **Step 2: Build to verify compilation**

Run: `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel 8`
Expected: Build succeeds without errors

## Task 3: AppController::deleteSubscription 方法

**Files:**
- Modify: `src/ui/AppController.h:38-39` (add declaration)
- Modify: `src/ui/AppController.cpp:153-156` (add implementation after updateSubitem)

- [ ] **Step 1: Add declaration to AppController.h**

```cpp
// In the Subscriptions section:
bool deleteSubscription(const std::string& subId);
```

- [ ] **Step 2: Add implementation to AppController.cpp**

```cpp
bool AppController::deleteSubscription(const std::string& subId) {
    // Delete associated proxies first
    db::models::ProfileitemDAO proxyDao(db_);
    proxyDao.deleteBySubId(subId);
    
    // Then delete the subscription itself
    db::models::SubitemDAO subDao(db_);
    return subDao.deleteById(subId);
}
```

- [ ] **Step 3: Build to verify compilation**

Run: `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel 8`
Expected: Build succeeds without errors

## Task 4: SubscriptionPanel::onDeleteSubscription 实现

**Files:**
- Modify: `src/ui/SubscriptionPanel.cpp:201-213`

- [ ] **Step 1: Implement onDeleteSubscription handler**

```cpp
void SubscriptionPanel::onDeleteSubscription(wxCommandEvent&) {
    std::string subId = getSelectedSubId();
    if (subId.empty()) return;
    std::string remarks;
    for (const auto& sub : subs_) {
        if (sub.id == subId) { remarks = sub.remarks; break; }
    }
    if (confirmDelete(subId, remarks)) {
        if (controller_) {
            controller_->deleteSubscription(subId);
        }
        loadSubscriptions();
    }
}
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --parallel 8`
Expected: Build succeeds

## Task 5: 单元测试

**Files:**
- Create: `tests/test_delete_subscription.cpp`

- [ ] **Step 1: Write test file**

```cpp
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <string>
#include "Subitem.h"
#include "Profileitem.h"

class DeleteSubscriptionTest : public ::testing::Test {
protected:
    sqlite3* db_ = nullptr;

    void SetUp() override {
        sqlite3_open(":memory:", &db_);
        createTables();
    }

    void TearDown() override {
        if (db_) sqlite3_close(db_);
    }

    void createTables() {
        exec("CREATE TABLE SubItem ("
             "Id TEXT PRIMARY KEY,"
             "Remarks TEXT,"
             "Url TEXT,"
             "MoreUrl TEXT,"
             "Enabled INTEGER,"
             "UserAgent TEXT,"
             "Sort TEXT,"
             "Filter TEXT,"
             "AutoUpdateInterval TEXT,"
             "UpdateTime TEXT,"
             "ConvertTarget TEXT,"
             "PrevProfile TEXT,"
             "NextProfile TEXT,"
             "PreSocksPort TEXT,"
             "Memo TEXT"
        ")");
        exec("CREATE TABLE ProfileItem ("
             "IndexId TEXT PRIMARY KEY,"
             "ConfigType TEXT, Address TEXT, Port TEXT, Id TEXT, Network TEXT, Subid TEXT"
        ")");
    }

    void exec(const std::string& sql) {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            FAIL() << "SQL error: " << (err ? err : "unknown");
            sqlite3_free(err);
        }
    }

    void insertSubscription(const std::string& id, const std::string& remarks) {
        exec("INSERT INTO SubItem (Id, Remarks, Url, Enabled) VALUES ('" + id + "', '" + remarks + "', 'http://test.com', 1)");
    }

    void insertProxy(const std::string& indexId, const std::string& address, const std::string& subId) {
        exec("INSERT INTO ProfileItem (IndexId, ConfigType, Address, Subid) VALUES ('" + indexId + "', '5', '" + address + "', '" + subId + "')");
    }

    int countSubscriptions() {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM SubItem", -1, &stmt, nullptr);
        sqlite3_step(stmt);
        int count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return count;
    }

    int countProxies() {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM ProfileItem", -1, &stmt, nullptr);
        sqlite3_step(stmt);
        int count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return count;
    }
};

TEST_F(DeleteSubscriptionTest, DeleteSubscriptionRemovesRecord) {
    insertSubscription("sub1", "Test Subscription");
    
    db::models::SubitemDAO dao(db_);
    EXPECT_TRUE(dao.deleteById("sub1"));
    
    EXPECT_EQ(countSubscriptions(), 0);
}

TEST_F(DeleteSubscriptionTest, DeleteSubscriptionNonExistentReturnsFalse) {
    db::models::SubitemDAO dao(db_);
    EXPECT_FALSE(dao.deleteById("nonexistent"));
}

TEST_F(DeleteSubscriptionTest, DeleteProxiesBySubId) {
    insertProxy("proxy1", "1.1.1.1", "sub1");
    insertProxy("proxy2", "2.2.2.2", "sub1");
    insertProxy("proxy3", "3.3.3.3", "sub2");
    
    db::models::ProfileitemDAO dao(db_);
    EXPECT_TRUE(dao.deleteBySubId("sub1"));
    
    EXPECT_EQ(countProxies(), 1);
}

TEST_F(DeleteSubscriptionTest, EscapePreventsInjection) {
    insertSubscription("sub1", "Test");
    
    db::models::SubitemDAO dao(db_);
    // This should not delete anything (malformed ID)
    EXPECT_FALSE(dao.deleteById("' OR '1'='1"));
    
    EXPECT_EQ(countSubscriptions(), 1);
}
```

- [ ] **Step 2: Add test to CMakeLists.txt**

In `tests/CMakeLists.txt`, add `test_delete_subscription.cpp` to the test sources list.

- [ ] **Step 3: Build and run tests**

Run: `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel 8 && ctest -R DeleteSubscription -V`
Expected: All tests pass
</think>
- [ ] **Step 4: Commit test file**

```bash
git add tests/test_delete_subscription.cpp src/ui/AppController.h src/ui/AppController.cpp include/Subitem.h include/Profileitem.h src/ui/SubscriptionPanel.cpp
git commit -m "feat: implement subscription right-click delete with cascade proxy deletion"
```

## Task 6: 全量验证

- [ ] **Step 1: Run all tests**

Run: `ctest -V`
Expected: All existing tests pass (706 tests reported)

- [ ] **Step 2: Manual GUI test**

Run: `./build/validproxy.exe -ui`
- Add test subscription
- Right-click → Delete → Confirm
- Verify subscription disappears from list

- [ ] **Step 3: Final commit**

```bash
git commit --allow-empty -m "feat: subscription delete feature - manual verification passed"
```