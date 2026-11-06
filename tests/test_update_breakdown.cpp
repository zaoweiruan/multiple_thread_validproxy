// tests/test_update_breakdown.cpp — batch-update breakdown contract (spec
// 2026-11-03 v1.2.5 §13, owner request 2026-11-05).
//
// SubitemUpdaterV2::run() must publish one breakdown row per enabled
// subscription (+ a trailing 清洗 row when dedup_after_update is on) through
// the step observer, transitioning PENDING → RUNNING → SUCCESS/FAILED/SKIPPED
// as it walks the list, and sweeping never-reached rows to CANCELLED at the
// end. AppController maps these snapshots onto TaskQueue child rows, so the
// UI needs those invariants to hold:
//   * the FIRST publish lists every row as PENDING (nothing hidden until used)
//   * every row ends in a terminal state (never PENDING/RUNNING frozen)
//   * a within-interval subscription becomes SKIPPED, never RUNNING
//   * cancel-before-start marks every sub row CANCELLED
//
// No network is used: subscription URLs point at a closed localhost port, so
// the direct fetch fails in milliseconds and the row lands in FAILED.
#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ConfigReader.h"
#include "SubitemUpdaterV2.h"

namespace {

// Minimal schema: same column names as the live DB (the DAO reads SELECT * by
// name), all TEXT-typed — SQLite is dynamically typed and the DAO converts.
void createSchema(sqlite3* db) {
    const char* kSub =
        "CREATE TABLE SubItem ("
        " Id TEXT, Remarks TEXT, Url TEXT, MoreUrl TEXT, Enabled INTEGER,"
        " UserAgent TEXT, Sort TEXT, Filter TEXT, AutoUpdateInterval TEXT,"
        " UpdateTime TEXT, ConvertTarget TEXT, PrevProfile TEXT,"
        " NextProfile TEXT, PreSocksPort TEXT, Memo TEXT);";
    const char* kProfile =
        "CREATE TABLE ProfileItem ("
        " IndexId TEXT PRIMARY KEY, ConfigType TEXT, ConfigVersion TEXT,"
        " Address TEXT, Port TEXT, Ports TEXT, Id TEXT, AlterId TEXT,"
        " Security TEXT, Network TEXT, Remarks TEXT, HeaderType TEXT,"
        " RequestHost TEXT, Path TEXT, StreamSecurity TEXT, AllowInsecure TEXT,"
        " Subid TEXT, IsSub TEXT, Flow TEXT, Sni TEXT, Alpn TEXT, CoreType TEXT,"
        " PreSocksPort TEXT, Fingerprint TEXT, DisplayLog TEXT, PublicKey TEXT,"
        " ShortId TEXT, SpiderX TEXT, Mldsa65Verify TEXT, Extra TEXT,"
        " MuxEnabled TEXT, Cert TEXT, CertSha TEXT, EchConfigList TEXT,"
        " EchForceQuery TEXT, Region TEXT);";
    const char* kEx =
        "CREATE TABLE ProfileExItem ("
        " IndexId TEXT PRIMARY KEY, Delay TEXT, Speed TEXT, Sort TEXT,"
        " Message TEXT, consecutive_failures INTEGER, start_count INTEGER,"
        " total_runtime_ms INTEGER, crash_count INTEGER);";
    char* err = nullptr;
    ASSERT_EQ(sqlite3_exec(db, kSub, nullptr, nullptr, &err), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, kProfile, nullptr, nullptr, &err), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, kEx, nullptr, nullptr, &err), SQLITE_OK);
}

void insertSub(sqlite3* db, const std::string& id, const std::string& remarks,
               const std::string& url, const std::string& intervalMinutes,
               const std::string& updateTimeEpochSec) {
    const std::string sql =
        "INSERT INTO SubItem (Id, Remarks, Url, Enabled, AutoUpdateInterval,"
        " UpdateTime) VALUES ('" + id + "', '" + remarks + "', '" + url +
        "', 1, '" + intervalMinutes + "', '" + updateTimeEpochSec + "');";
    char* err = nullptr;
    ASSERT_EQ(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err), SQLITE_OK);
}

// Full snapshot history: one entry per observer publish (values copied).
using Steps = std::vector<update::SubitemUpdaterV2::UpdateStepInfo>;
using Row = update::SubitemUpdaterV2::UpdateStepInfo;
using RowStatus = update::SubitemUpdaterV2::UpdateStepStatus;

struct Recorder {
    std::vector<Steps> publishes;
    update::SubitemUpdaterV2::StepObserver observer() {
        return [this](const std::vector<Row>& s) { publishes.push_back(s); };
    }
};

const Row* findRow(const Steps& s, const std::string& id) {
    for (const Row& row : s) {
        if (row.subId == id) return &row;
    }
    return nullptr;
}

std::string epochNowSec() {
    const long long t = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    return std::to_string(t);
}

config::AppConfig makeCfg() {
    config::AppConfig cfg;
    cfg.update_methods = std::vector<std::string>{ "direct" };
    cfg.subscription_connect_timeout_ms = 1000;   // closed port: fails fast
    cfg.check_auto_update_interval = true;        // enable SKIPPED path
    return cfg;
}

class TempDb {
public:
    explicit TempDb(const std::string& name)
        : path_(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove(path_);
    }
    ~TempDb() {
        if (db_) sqlite3_close(db_);
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    sqlite3* open() {
        EXPECT_EQ(sqlite3_open(path_.string().c_str(), &db_), SQLITE_OK);
        return db_;
    }
    sqlite3* get() const { return db_; }

private:
    std::filesystem::path path_;
    sqlite3* db_{nullptr};
};

} // namespace

TEST(UpdateBreakdown, PublishesRowsAndReachesTerminalStates) {
    TempDb tmp("uptest_breakdown.db");
    sqlite3* db = tmp.open();
    createSchema(db);
    // A: attempted (closed port → FAILED, never frozen RUNNING)
    insertSub(db, "sub-A", "SubscriptionA", "http://127.0.0.1:1/a", "", "0");
    // B: within update interval → SKIPPED
    insertSub(db, "sub-B", "SubscriptionB", "http://127.0.0.1:1/b", "60", epochNowSec());

    config::AppConfig cfg = makeCfg();
    cfg.dedup_enabled = true;
    cfg.dedup_after_update = true;

    std::atomic<bool> cancel{ false };
    Recorder rec;
    {
        update::SubitemUpdaterV2 updater(db, "", cfg, nullptr, "", &cancel, nullptr);
        updater.setStepObserver(rec.observer());
        updater.run();
    }

    ASSERT_FALSE(rec.publishes.empty());
    // First publish: every row (A, B, 清洗) exists and is PENDING.
    const Steps& first = rec.publishes.front();
    ASSERT_EQ(first.size(), 3u);
    for (const Row& row : first) {
        EXPECT_EQ(row.status, RowStatus::PENDING);
    }
    EXPECT_NE(findRow(first, "sub-A"), nullptr);
    EXPECT_NE(findRow(first, "sub-B"), nullptr);
    EXPECT_NE(findRow(first, update::SubitemUpdaterV2::kDedupStepId), nullptr);

    // A entered RUNNING at least once (attempted).
    bool aRan = false;
    for (const Steps& s : rec.publishes) {
        const Row* a = findRow(s, "sub-A");
        if (a && a->status == RowStatus::RUNNING) aRan = true;
    }
    EXPECT_TRUE(aRan);

    // Final snapshot: terminal states only.
    const Steps& last = rec.publishes.back();
    ASSERT_EQ(last.size(), 3u);
    const Row* a = findRow(last, "sub-A");
    const Row* b = findRow(last, "sub-B");
    const Row* d = findRow(last, update::SubitemUpdaterV2::kDedupStepId);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(d, nullptr);
    // Closed port → fetch fails → FAILED (must not be PENDING/RUNNING/CANCELLED).
    EXPECT_EQ(a->status, RowStatus::FAILED);
    EXPECT_EQ(b->status, RowStatus::SKIPPED);
    EXPECT_EQ(d->status, RowStatus::SUCCESS);
    // Remarks travel with the row (display name source).
    EXPECT_EQ(a->remarks, "SubscriptionA");
    EXPECT_EQ(d->remarks, update::SubitemUpdaterV2::kDedupStepName);
}

TEST(UpdateBreakdown, CancelBeforeStartSweepsRowsToCancelled) {
    TempDb tmp("uptest_breakdown_cancel.db");
    sqlite3* db = tmp.open();
    createSchema(db);
    insertSub(db, "sub-A", "SubscriptionA", "http://127.0.0.1:1/a", "", "0");
    insertSub(db, "sub-B", "SubscriptionB", "http://127.0.0.1:1/b", "", "0");

    config::AppConfig cfg = makeCfg();   // dedup_after_update stays false
    std::atomic<bool> cancel{ true };    // cancelled before the loop starts
    Recorder rec;
    {
        update::SubitemUpdaterV2 updater(db, "", cfg, nullptr, "", &cancel, nullptr);
        updater.setStepObserver(rec.observer());
        updater.run();
    }

    ASSERT_GE(rec.publishes.size(), 2u);   // initial publish + final sweep
    const Steps& last = rec.publishes.back();
    ASSERT_EQ(last.size(), 2u);            // no dedup gate → no 清洗 row
    for (const Row& row : last) {
        EXPECT_EQ(row.status, RowStatus::CANCELLED);
    }
}
