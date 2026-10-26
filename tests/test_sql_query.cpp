#include "SqlQuery.h"
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace {

class SqlQueryDAOTest : public ::testing::Test {
protected:
    sqlite3* db_ = nullptr;

    void SetUp() override {
        ASSERT_EQ(sqlite3_open(":memory:", &db_), SQLITE_OK);
        ASSERT_TRUE(db::SqlQueryDAO::createTable(db_));
    }

    void TearDown() override {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }
};

TEST_F(SqlQueryDAOTest, CreateAndSeed) {
    int n = db::SqlQueryDAO::seedDefaults(db_, "SELECT * FROM ProfileItem;", "SELECT * FROM ProfileItem WHERE SubId='{subid}';");
    EXPECT_GE(n, 0);
    std::vector<db::SqlQueryItem> all = db::SqlQueryDAO::getAll(db_);
    EXPECT_GE(all.size(), 2u);
}

TEST_F(SqlQueryDAOTest, InsertGetUpdateRemove) {
    db::SqlQueryItem item;
    item.name = "q1";
    item.sql_text = "SELECT 1;";
    item.scope = "all";
    item.description = "desc";
    int id = db::SqlQueryDAO::insert(db_, item);
    ASSERT_GT(id, 0);

    db::SqlQueryItem got = db::SqlQueryDAO::getById(db_, id);
    EXPECT_EQ(got.id, id);
    EXPECT_EQ(got.name, "q1");
    EXPECT_EQ(got.sql_text, "SELECT 1;");

    got.sql_text = "SELECT 2;";
    EXPECT_TRUE(db::SqlQueryDAO::update(db_, got));
    db::SqlQueryItem got2 = db::SqlQueryDAO::getById(db_, id);
    EXPECT_EQ(got2.sql_text, "SELECT 2;");

    EXPECT_TRUE(db::SqlQueryDAO::remove(db_, id));
    db::SqlQueryItem got3 = db::SqlQueryDAO::getById(db_, id);
    EXPECT_EQ(got3.id, 0);
}

TEST_F(SqlQueryDAOTest, SetDefault) {
    db::SqlQueryItem a;
    a.name = "a"; a.sql_text = "SELECT a;"; a.scope = "all";
    db::SqlQueryItem b;
    b.name = "b"; b.sql_text = "SELECT b;"; b.scope = "all";
    int ida = db::SqlQueryDAO::insert(db_, a);
    int idb = db::SqlQueryDAO::insert(db_, b);
    ASSERT_GT(ida, 0);
    ASSERT_GT(idb, 0);

    EXPECT_TRUE(db::SqlQueryDAO::setDefault(db_, ida));
    db::SqlQueryItem da = db::SqlQueryDAO::getById(db_, ida);
    EXPECT_TRUE(da.is_default);

    EXPECT_TRUE(db::SqlQueryDAO::setDefault(db_, idb));
    db::SqlQueryItem da2 = db::SqlQueryDAO::getById(db_, ida);
    db::SqlQueryItem db2 = db::SqlQueryDAO::getById(db_, idb);
    EXPECT_FALSE(da2.is_default);
    EXPECT_TRUE(db2.is_default);
}

TEST_F(SqlQueryDAOTest, InstanceWrapper) {
    db::SqlQueryDAO dao(db_);
    EXPECT_TRUE(dao.createTableIfNotExists());
    db::SqlQueryItem item;
    item.name = "w"; item.sql_text = "SELECT w;";
    int id = dao.insert(item);
    ASSERT_GT(id, 0);
    db::SqlQueryItem got = dao.getById(id);
    EXPECT_EQ(got.name, "w");
    EXPECT_TRUE(dao.remove(id));
}

} // namespace