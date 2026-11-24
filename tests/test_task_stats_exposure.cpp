// Task 4 (task-report-hover): 聚合任务操作暴露 TaskStats* 出参统计。
//
// 代表性测试用 Deduplicator（最易在内存库构造）验证：
//   1. deduplicate(&st) 在汇总点把既有计数写入 TaskStats，且
//      不变式 success + failed + skipped == total 恒成立；
//   2. 传 nullptr 与不传（默认 nullptr）行为一致，编译期可调用、运行无异常。
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <string>
#include <vector>

#include "update/Deduplicator.h"
#include "ConfigReader.h"
#include "task/TaskType.h"

class TaskStatsExposureTest : public ::testing::Test {
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
        exec("CREATE TABLE ProfileItem ("
            "IndexId TEXT PRIMARY KEY,"
            "ConfigType TEXT, ConfigVersion TEXT, Address TEXT, Port TEXT,"
            "Ports TEXT, Id TEXT, AlterId TEXT, Security TEXT, Network TEXT,"
            "Remarks TEXT, HeaderType TEXT, RequestHost TEXT, Path TEXT,"
            "StreamSecurity TEXT, AllowInsecure TEXT, Subid TEXT, IsSub TEXT,"
            "Flow TEXT, Sni TEXT, Alpn TEXT, CoreType TEXT, PreSocksPort TEXT,"
            "Fingerprint TEXT, DisplayLog TEXT, PublicKey TEXT, ShortId TEXT,"
            "SpiderX TEXT, Mldsa65Verify TEXT, Extra TEXT, MuxEnabled TEXT,"
            "Cert TEXT, CertSha TEXT, EchConfigList TEXT, EchForceQuery TEXT,"
            "Region TEXT"
        ")");
        exec("CREATE TABLE ProfileExItem ("
            "IndexId TEXT PRIMARY KEY,"
            "Delay TEXT, Speed TEXT, Sort TEXT, Message TEXT,"
            "consecutive_failures INTEGER DEFAULT 0"
        ")");
        exec("CREATE TABLE SubItem ("
            "Id TEXT PRIMARY KEY,"
            "Url TEXT, Enabled TEXT, Remarks TEXT, UpdateTime TEXT"
        ")");
    }

    void exec(const std::string& sql) {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::string msg = "SQL error: " + std::string(err) + "\nSQL: " + sql;
            sqlite3_free(err);
            FAIL() << msg;
        }
    }

    void insertProfile(const std::string& indexId, const std::string& configType,
                       const std::string& address, const std::string& port,
                       const std::string& id, const std::string& network,
                       const std::string& subId) {
        std::string sql = "INSERT INTO ProfileItem (IndexId, ConfigType, Address, Port, Id, Network, Subid) VALUES ('"
            + indexId + "','" + configType + "','" + address + "','" + port + "','" + id + "','" + network + "','" + subId + "')";
        exec(sql);
    }

    void insertDelay(const std::string& indexId, const std::string& delay) {
        std::string sql = "INSERT INTO ProfileExItem (IndexId, Delay) VALUES ('" + indexId + "','" + delay + "')";
        exec(sql);
    }

    // 代表性场景：两个重复代理 + 一个独立代理，合并阶段会删掉重复项。
    void seedDuplicateScenario() {
        insertProfile("1", "5", "1.1.1.1", "443", "12345678-1234-1234-1234-123456789abc", "tcp", "sub1");
        insertProfile("2", "5", "1.1.1.1", "443", "12345678-1234-1234-1234-123456789abc", "tcp", "sub1");
        insertProfile("3", "5", "2.2.2.2", "443", "12345678-1234-1234-1234-123456789abd", "tcp", "sub1");
        insertDelay("1", "100");
        insertDelay("2", "200");
        insertDelay("3", "300");
        exec("INSERT INTO SubItem (Id) VALUES ('sub1')");
    }
};

// 1. 出参填充 + 不变式 success + failed + skipped == total
TEST_F(TaskStatsExposureTest, DeduplicatorFillsStatsOutParam) {
    seedDuplicateScenario();

    config::AppConfig cfg;
    cfg.dedup_subids.push_back("sub1");

    update::Deduplicator dedup(db_, cfg);
    task::TaskStats st;
    bool ok = dedup.deduplicate(&st);

    ASSERT_TRUE(ok) << "deduplicate() failed with stats out-param";
    EXPECT_EQ(st.total, dedup.getBeforeCount());
    EXPECT_EQ(st.success + st.failed + st.skipped, st.total);
}

// 2. 传 nullptr 与不传（默认 nullptr）行为一致：编译期可调用、运行无异常
TEST_F(TaskStatsExposureTest, NullStatsKeepsBehaviorUnchanged) {
    seedDuplicateScenario();

    config::AppConfig cfg;
    cfg.dedup_subids.push_back("sub1");

    update::Deduplicator dedup(db_, cfg);
    bool ok = dedup.deduplicate(nullptr);   // 显式 nullptr
    ASSERT_TRUE(ok) << "deduplicate(nullptr) failed";

    bool ok2 = dedup.deduplicate();         // 默认 nullptr
    ASSERT_TRUE(ok2) << "deduplicate() (default nullptr) failed";
}
