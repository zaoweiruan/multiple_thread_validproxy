#include <gtest/gtest.h>
#include <sqlite3.h>

#include <filesystem>
#include <string>

#include "ConfigReader.h"
#include "SubitemUpdaterV2.h"
#include "test_utils.h"

namespace {

bool execSql(sqlite3* db, const std::string& sql) {
    char* errMsg = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (errMsg) {
        sqlite3_free(errMsg);
    }
    return rc == SQLITE_OK;
}

std::string queryText(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::string();
    }
    std::string value;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text) {
            value = reinterpret_cast<const char*>(text);
        }
    }
    sqlite3_finalize(stmt);
    return value;
}

int queryInt(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    int value = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        value = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return value;
}

long long queryInt64(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    long long value = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        value = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return value;
}

} // namespace

TEST(SyncRegionTest, migratesProfileItemRegion) {
    TempDir tmp;
    const std::string srcPath = tmp.path() + "/src.db";
    const std::string dstPath = tmp.path() + "/dst.db";

    std::filesystem::copy_file(TEST_DB_EMPTY_PATH, srcPath,
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(TEST_DB_EMPTY_PATH, dstPath,
                               std::filesystem::copy_options::overwrite_existing);

    sqlite3* srcDb = nullptr;
    ASSERT_EQ(sqlite3_open(srcPath.c_str(), &srcDb), SQLITE_OK);
    ASSERT_TRUE(execSql(srcDb,
        "INSERT INTO ProfileItem "
        "(IndexId, ConfigType, Address, Port, Subid, Remarks, Region) "
        "VALUES ('region-test-id', 1, '1.2.3.4', 443, "
        "'region-test-sub', 'region-test', 'US-TestRegion');"));
    ASSERT_TRUE(execSql(srcDb,
        "INSERT INTO ProfileExItem "
        "(IndexId, Delay, start_count, total_runtime_ms, crash_count) "
        "VALUES ('region-test-id', 123, 7, 123456, 2);"));
    ASSERT_TRUE(execSql(srcDb,
        "INSERT INTO ProfileItem "
        "(IndexId, ConfigType, Address, Port, Subid, Remarks, Region) "
        "VALUES ('region-update-id', 1, '5.6.7.8', 8443, "
        "'region-update-sub', 'region-update', 'DE-TestRegion');"));
    ASSERT_TRUE(execSql(srcDb,
        "INSERT INTO ProfileExItem "
        "(IndexId, Delay, start_count, total_runtime_ms, crash_count) "
        "VALUES ('region-update-id', 456, 9, 654321, 3);"));
    sqlite3_close(srcDb);

    sqlite3* preDb = nullptr;
    ASSERT_EQ(sqlite3_open(dstPath.c_str(), &preDb), SQLITE_OK);
    ASSERT_TRUE(execSql(preDb,
        "INSERT INTO ProfileExItem "
        "(IndexId, Delay, start_count, total_runtime_ms, crash_count) "
        "VALUES ('region-update-id', 1, 1, 1, 1);"));
    sqlite3_close(preDb);

    config::AppConfig cfg;
    cfg.sync.sync_skip_subids = false;

    update::SubitemUpdaterV2 updater(nullptr, "", cfg, nullptr, "");
    ASSERT_TRUE(updater.syncDatabases(srcPath, dstPath));

    sqlite3* dstDb = nullptr;
    ASSERT_EQ(sqlite3_open(dstPath.c_str(), &dstDb), SQLITE_OK);
    const std::string region = queryText(dstDb,
        "SELECT Region FROM ProfileItem WHERE IndexId = 'region-test-id';");
    const int insertStartCount = queryInt(dstDb,
        "SELECT start_count FROM ProfileExItem WHERE IndexId = 'region-test-id';");
    const long long insertRuntime = queryInt64(dstDb,
        "SELECT total_runtime_ms FROM ProfileExItem WHERE IndexId = 'region-test-id';");
    const int insertCrashCount = queryInt(dstDb,
        "SELECT crash_count FROM ProfileExItem WHERE IndexId = 'region-test-id';");
    const std::string updatedRegion = queryText(dstDb,
        "SELECT Region FROM ProfileItem WHERE IndexId = 'region-update-id';");
    const int updateStartCount = queryInt(dstDb,
        "SELECT start_count FROM ProfileExItem WHERE IndexId = 'region-update-id';");
    const long long updateRuntime = queryInt64(dstDb,
        "SELECT total_runtime_ms FROM ProfileExItem WHERE IndexId = 'region-update-id';");
    const int updateCrashCount = queryInt(dstDb,
        "SELECT crash_count FROM ProfileExItem WHERE IndexId = 'region-update-id';");
    sqlite3_close(dstDb);

    EXPECT_EQ(region, "US-TestRegion");
    EXPECT_EQ(insertStartCount, 7);
    EXPECT_EQ(insertRuntime, 123456LL);
    EXPECT_EQ(insertCrashCount, 2);
    EXPECT_EQ(updatedRegion, "DE-TestRegion");
    EXPECT_EQ(updateStartCount, 9);
    EXPECT_EQ(updateRuntime, 654321LL);
    EXPECT_EQ(updateCrashCount, 3);
}
