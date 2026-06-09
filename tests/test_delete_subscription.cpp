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
             "Memo TEXT)");
        exec("CREATE TABLE ProfileItem ("
             "IndexId TEXT PRIMARY KEY,"
             "ConfigType TEXT, Address TEXT, Port TEXT, Id TEXT, Network TEXT, Subid TEXT)");
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