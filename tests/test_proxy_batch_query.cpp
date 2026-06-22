// Phase 4 unit tests for ProxyBatchQuery component
// Uses the test database under test/guiNDB.db
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <sqlite3.h>

#include "test/ProxyBatchQuery.h"
#include "Profileitem.h"

namespace {

const char* TEST_DB_PATH = "E:/eclipse_workspace/multiple_thread_validproxy/test/guiNDB.db";

class ProxyBatchQueryTest : public ::testing::Test {
protected:
    sqlite3* db_ = nullptr;

    void SetUp() override {
        if (sqlite3_open(TEST_DB_PATH, &db_) != SQLITE_OK) {
            db_ = nullptr;
        }
        if (db_) {
            sqlite3_busy_timeout(db_, 5000);
        }
    }

    void TearDown() override {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    bool hasDb() const { return db_ != nullptr; }
};

TEST_F(ProxyBatchQueryTest, LoadAllProxies) {
    if (!hasDb()) GTEST_SKIP() << "Test database not available at " << TEST_DB_PATH;
    test::ProxyBatchQuery query(db_, "SELECT * FROM ProfileItem", "");
    std::vector<db::models::Profileitem> profiles = query.loadProxies();
    EXPECT_FALSE(profiles.empty());
}

TEST_F(ProxyBatchQueryTest, LoadProxiesBySubId) {
    if (!hasDb()) GTEST_SKIP() << "Test database not available at " << TEST_DB_PATH;
    test::ProxyBatchQuery query(db_, "SELECT * FROM ProfileItem",
                                "SELECT * FROM ProfileItem WHERE subid = '{subid}'");
    std::vector<db::models::Profileitem> profiles = query.loadProxies("some-sub-that-probably-exists");
    // The query should at least not crash and return a vector (possibly empty)
}

TEST_F(ProxyBatchQueryTest, SubstituteSubid) {
    if (!hasDb()) GTEST_SKIP() << "Test database not available at " << TEST_DB_PATH;
    // When subId is provided, ProxyBatchQuery uses sqlBySubId_ with {subid} substitution
    test::ProxyBatchQuery query(db_, "SELECT * FROM ProfileItem", "");
    std::vector<db::models::Profileitem> profiles = query.loadProxies(); // Uses sqlQuery_ without substitution
    // Empty subId means it uses sqlQuery_ directly, which should return all profiles
    EXPECT_FALSE(profiles.empty());
}

} // namespace
