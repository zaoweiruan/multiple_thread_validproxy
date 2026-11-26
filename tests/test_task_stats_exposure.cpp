// Task 4 (task-report-hover): 聚合任务操作暴露 TaskStats* 出参统计。
//
// 代表性测试用 Deduplicator（最易在内存库构造）验证：
//   1. deduplicate(&st) 在汇总点把既有计数写入 TaskStats，且
//      不变式 success + failed + skipped == total 恒成立；
//   2. 传 nullptr 与不传（默认 nullptr）行为一致，编译期可调用、运行无异常。
//
// task-report-format Task 2: SubitemUpdaterV2::run/runSingle 新增末位出参
// `std::string* updateMethods`（默认 nullptr，向后兼容）——在方法字符串构建后、
// 任何早返回前填充，格式 "accelerator, proxy"（逗号+空格分隔）。
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>

#include "update/Deduplicator.h"
#include "SubitemUpdaterV2.h"
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
        // SubitemUpdaterV2::run/runSingle 需要与真实库一致的列顺序（DAO 用
        // SELECT * + 下标读取）；空表与间隔内跳过/失败用例共用此 schema。
        exec("CREATE TABLE SubItem ("
            "Id TEXT PRIMARY KEY,"
            "Remarks TEXT, Url TEXT, MoreUrl TEXT, Enabled INTEGER,"
            "UserAgent TEXT, Sort TEXT, Filter TEXT, AutoUpdateInterval TEXT,"
            "UpdateTime TEXT, ConvertTarget TEXT, PrevProfile TEXT,"
            "NextProfile TEXT, PreSocksPort TEXT, Memo TEXT"
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

    void insertSub(const std::string& id, const std::string& remarks,
                   const std::string& url, const std::string& intervalMinutes,
                   const std::string& updateTimeEpochSec) {
        const std::string sql =
            "INSERT INTO SubItem (Id, Remarks, Url, Enabled, AutoUpdateInterval, UpdateTime)"
            " VALUES ('" + id + "','" + remarks + "','" + url + "',1,'"
            + intervalMinutes + "','" + updateTimeEpochSec + "')";
        exec(sql);
    }

    static std::string nowEpochSec() {
        const long long sec = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return std::to_string(sec);
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

    // SubitemUpdaterV2 只需内存库 + 空表；无网络、无 Xray（路径在早返回前）。
    update::SubitemUpdaterV2 makeUpdater(const config::AppConfig& cfg) {
        return update::SubitemUpdaterV2(db_, "", cfg, nullptr, "", nullptr, nullptr);
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

// 3. run() 无启用订阅 → 成功早返回，updateMethods 出参已按
//    "accelerator, proxy"（逗号+空格）填充。
TEST_F(TaskStatsExposureTest, RunFillsUpdateMethodsOutParam) {
    config::AppConfig cfg;
    cfg.update_methods = std::vector<std::string>{"accelerator", "proxy"};

    update::SubitemUpdaterV2 updater = makeUpdater(cfg);
    std::string methods;
    bool ok = updater.run(nullptr, &methods);

    ASSERT_TRUE(ok) << "run() should succeed with no enabled subscriptions";
    EXPECT_EQ(methods, "accelerator, proxy");
}

// 4. run() 不传 updateMethods（默认 nullptr）→ 行为不变、无异常。
TEST_F(TaskStatsExposureTest, RunAcceptsNullUpdateMethods) {
    config::AppConfig cfg;
    cfg.update_methods = std::vector<std::string>{"direct"};

    update::SubitemUpdaterV2 updater = makeUpdater(cfg);
    bool ok = updater.run(nullptr);   // updateMethods 默认 nullptr
    ASSERT_TRUE(ok);
}

// 5. runSingle() 订阅不存在 → 早返回 false，但 updateMethods 仍已填充
//    （方法字符串在早返回前构建/填充）。
TEST_F(TaskStatsExposureTest, RunSingleFillsUpdateMethodsEvenWhenSubMissing) {
    config::AppConfig cfg;
    cfg.update_methods = std::vector<std::string>{"accelerator", "proxy"};

    update::SubitemUpdaterV2 updater = makeUpdater(cfg);
    std::string methods;
    bool ok = updater.runSingle("no-such-sub", nullptr, &methods);

    ASSERT_FALSE(ok) << "runSingle of a missing subscription must fail";
    EXPECT_EQ(methods, "accelerator, proxy");
}

// 6. runSingle() 不传 updateMethods（默认 nullptr）→ 行为不变、无异常。
TEST_F(TaskStatsExposureTest, RunSingleAcceptsNullUpdateMethods) {
    config::AppConfig cfg;
    cfg.update_methods = std::vector<std::string>{"direct"};

    update::SubitemUpdaterV2 updater = makeUpdater(cfg);
    bool ok = updater.runSingle("no-such-sub", nullptr);   // updateMethods 默认 nullptr
    ASSERT_FALSE(ok);
}

// 7. 间隔内跳过 → skipped 计数，绝不计入 failed（bug：更新订阅日志把跳过
//    当成失败）。run() 在全部跳过时返回 true 且不触网。
TEST_F(TaskStatsExposureTest, RunCountsIntervalSkippedAsSkippedNotFailed) {
    insertSub("sub-skip", "间隔内订阅", "http://127.0.0.1:1/skip", "60", nowEpochSec());

    config::AppConfig cfg;
    cfg.update_methods = std::vector<std::string>{"direct"};
    cfg.check_auto_update_interval = true;

    update::SubitemUpdaterV2 updater = makeUpdater(cfg);
    task::TaskStats st;
    bool ok = updater.run(&st, nullptr);

    ASSERT_TRUE(ok) << "all-skipped run() must succeed";
    EXPECT_EQ(st.total, 1);
    EXPECT_EQ(st.success, 0);
    EXPECT_EQ(st.failed, 0);
    EXPECT_EQ(st.skipped, 1);
    EXPECT_TRUE(st.failures.empty());
}

// 8. runSingle() 间隔内跳过同样计 skipped，不计 failed。
TEST_F(TaskStatsExposureTest, RunSingleCountsIntervalSkippedAsSkippedNotFailed) {
    insertSub("sub-skip", "间隔内订阅", "http://127.0.0.1:1/skip", "60", nowEpochSec());

    config::AppConfig cfg;
    cfg.update_methods = std::vector<std::string>{"direct"};
    cfg.check_auto_update_interval = true;

    update::SubitemUpdaterV2 updater = makeUpdater(cfg);
    task::TaskStats st;
    bool ok = updater.runSingle("sub-skip", &st, nullptr);

    ASSERT_TRUE(ok);
    EXPECT_EQ(st.total, 1);
    EXPECT_EQ(st.success, 0);
    EXPECT_EQ(st.failed, 0);
    EXPECT_EQ(st.skipped, 1);
    EXPECT_TRUE(st.failures.empty());
}

// 9. 更新失败 → stats.failures 带订阅名称（Remarks）与 ID，供报告展示。
TEST_F(TaskStatsExposureTest, RunReportsFailedSubscriptionNameAndId) {
    insertSub("sub-fail", "失败订阅", "http://127.0.0.1:1/fail", "", "0");

    config::AppConfig cfg;
    cfg.update_methods = std::vector<std::string>{"direct"};
    cfg.subscription_connect_timeout_ms = 200;

    update::SubitemUpdaterV2 updater = makeUpdater(cfg);
    task::TaskStats st;
    bool ok = updater.run(&st, nullptr);

    ASSERT_FALSE(ok) << "the only subscription failed to fetch";
    EXPECT_EQ(st.total, 1);
    EXPECT_EQ(st.failed, 1);
    EXPECT_EQ(st.skipped, 0);
    ASSERT_EQ(st.failures.size(), 1u);
    EXPECT_EQ(st.failures[0].id, "sub-fail");
    EXPECT_EQ(st.failures[0].name, "失败订阅");
}

// 10. runSingle() 失败同样记录名称与 ID。
TEST_F(TaskStatsExposureTest, RunSingleReportsFailedSubscriptionNameAndId) {
    insertSub("sub-fail", "失败订阅", "http://127.0.0.1:1/fail", "", "0");

    config::AppConfig cfg;
    cfg.update_methods = std::vector<std::string>{"direct"};
    cfg.subscription_connect_timeout_ms = 200;

    update::SubitemUpdaterV2 updater = makeUpdater(cfg);
    task::TaskStats st;
    bool ok = updater.runSingle("sub-fail", &st, nullptr);

    ASSERT_FALSE(ok);
    EXPECT_EQ(st.total, 1);
    EXPECT_EQ(st.failed, 1);
    ASSERT_EQ(st.failures.size(), 1u);
    EXPECT_EQ(st.failures[0].id, "sub-fail");
    EXPECT_EQ(st.failures[0].name, "失败订阅");
}
