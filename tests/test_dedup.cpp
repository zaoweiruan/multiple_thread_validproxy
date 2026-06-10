#include <gtest/gtest.h>
#include <sqlite3.h>
#include <string>
#include <sstream>
#include <vector>

class DedupTest : public ::testing::Test {
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
            "Cert TEXT, CertSha TEXT, EchConfigList TEXT, EchForceQuery TEXT"
        ")");
        exec("CREATE TABLE ProfileExItem ("
            "IndexId TEXT PRIMARY KEY,"
            "Delay TEXT, Speed TEXT, Sort TEXT, Message TEXT,"
            "consecutive_failures INTEGER DEFAULT 0"
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

    int countProfiles() {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM ProfileItem", -1, &stmt, nullptr);
        sqlite3_step(stmt);
        int count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return count;
    }

    std::vector<std::string> getRemainingIndexIds() {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db_, "SELECT IndexId FROM ProfileItem ORDER BY IndexId", -1, &stmt, nullptr);
        std::vector<std::string> ids;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ids.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        sqlite3_finalize(stmt);
        return ids;
    }

    // Replicates the core SQL from deduplicateMergedPhase()
    int runDedup(const std::vector<std::string>& dedupSubids) {
        if (dedupSubids.empty()) return 0;

        std::string subidsList;
        for (size_t i = 0; i < dedupSubids.size(); ++i) {
            if (i > 0) subidsList += ", ";
            subidsList += "'" + dedupSubids[i] + "'";
        }

        std::string sql =
            "DELETE FROM ProfileItem WHERE IndexId IN ("
            "SELECT IndexId FROM ("
            "SELECT pi.IndexId, ROW_NUMBER() OVER ("
            "PARTITION BY lower(pi.Address), pi.Port, pi.ConfigType, lower(pi.Id), lower(pi.Network) "
            "ORDER BY CASE WHEN pi.SubId IN (" + subidsList + ") THEN 0 ELSE 1 END, "
            "CAST(COALESCE(pe.Delay, 0) AS INTEGER) DESC"
            ") as rn FROM ProfileItem pi "
            "LEFT JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId"
            ") WHERE rn > 1"
            ")";

        exec(sql);
        return sqlite3_changes(db_);
    }
};

TEST_F(DedupTest, BasicDuplicate) {
    insertProfile("1", "5", "1.1.1.1", "443", "uuid-a", "tcp", "sub1");
    insertProfile("2", "5", "1.1.1.1", "443", "uuid-a", "tcp", "sub1");
    insertProfile("3", "5", "1.1.1.1", "443", "uuid-a", "tcp", "sub1");
    insertDelay("1", "100");
    insertDelay("2", "200");
    insertDelay("3", "300");

    int deleted = runDedup({"sub1"});

    EXPECT_EQ(deleted, 2);
    EXPECT_EQ(countProfiles(), 1);
    EXPECT_EQ(getRemainingIndexIds()[0], "3");
}

TEST_F(DedupTest, ProtectedSubIdPriority) {
    insertProfile("1", "5", "1.1.1.1", "443", "uuid-a", "tcp", "subA");
    insertProfile("2", "5", "1.1.1.1", "443", "uuid-a", "tcp", "subB");
    insertProfile("3", "5", "1.1.1.1", "443", "uuid-a", "tcp", "subC");
    insertDelay("1", "0");
    insertDelay("2", "0");
    insertDelay("3", "0");

    int deleted = runDedup({"subA"});

    EXPECT_EQ(deleted, 2);
    EXPECT_EQ(countProfiles(), 1);
    EXPECT_EQ(getRemainingIndexIds()[0], "1");
}

TEST_F(DedupTest, ProtectedSubIdWithDelay) {
    insertProfile("1", "5", "1.1.1.1", "443", "uuid-a", "tcp", "subA");
    insertProfile("2", "5", "1.1.1.1", "443", "uuid-a", "tcp", "subB");
    insertProfile("3", "5", "1.1.1.1", "443", "uuid-a", "tcp", "subB");
    insertDelay("1", "0");
    insertDelay("2", "50");
    insertDelay("3", "300");

    int deleted = runDedup({"subB"});

    EXPECT_EQ(deleted, 2);
    EXPECT_EQ(countProfiles(), 1);
    EXPECT_EQ(getRemainingIndexIds()[0], "3");
}

TEST_F(DedupTest, MultipleDuplicateGroups) {
    insertProfile("1", "5", "1.1.1.1", "443", "uuid-a", "tcp", "sub1");
    insertProfile("2", "5", "1.1.1.1", "443", "uuid-a", "tcp", "sub1");
    insertProfile("3", "5", "2.2.2.2", "443", "uuid-b", "tcp", "sub1");
    insertProfile("4", "5", "2.2.2.2", "443", "uuid-b", "tcp", "sub1");
    insertDelay("1", "100");
    insertDelay("2", "200");
    insertDelay("3", "300");
    insertDelay("4", "400");

    int deleted = runDedup({"sub1"});

    EXPECT_EQ(deleted, 2);
    EXPECT_EQ(countProfiles(), 2);
}

TEST_F(DedupTest, DifferentConfigTypesNotDeduped) {
    insertProfile("1", "5", "1.1.1.1", "443", "uuid-a", "tcp", "sub1");
    insertProfile("2", "3", "1.1.1.1", "443", "uuid-a", "tcp", "sub1");

    int deleted = runDedup({"sub1"});

    EXPECT_EQ(deleted, 0);
    EXPECT_EQ(countProfiles(), 2);
}

TEST_F(DedupTest, DifferentPortsNotDeduped) {
    insertProfile("1", "5", "1.1.1.1", "443", "uuid-a", "tcp", "sub1");
    insertProfile("2", "5", "1.1.1.1", "8443", "uuid-a", "tcp", "sub1");

    int deleted = runDedup({"sub1"});

    EXPECT_EQ(deleted, 0);
    EXPECT_EQ(countProfiles(), 2);
}

TEST_F(DedupTest, NullDelayTreatedAsZero) {
    insertProfile("1", "5", "1.1.1.1", "443", "uuid-a", "tcp", "sub1");
    insertProfile("2", "5", "1.1.1.1", "443", "uuid-a", "tcp", "sub1");
    insertDelay("1", "100");

    int deleted = runDedup({"sub1"});

    EXPECT_EQ(deleted, 1);
    EXPECT_EQ(countProfiles(), 1);
    EXPECT_EQ(getRemainingIndexIds()[0], "1");
}

TEST_F(DedupTest, NoDuplicatesPreservesAll) {
    insertProfile("1", "5", "1.1.1.1", "443", "uuid-a", "tcp", "sub1");
    insertProfile("2", "3", "2.2.2.2", "443", "uuid-b", "tcp", "sub1");
    insertProfile("3", "5", "3.3.3.3", "80", "uuid-c", "tcp", "sub1");

    int deleted = runDedup({"sub1"});

    EXPECT_EQ(deleted, 0);
    EXPECT_EQ(countProfiles(), 3);
}

TEST_F(DedupTest, EmptyDedupSubidsReturnsZero) {
    insertProfile("1", "5", "1.1.1.1", "443", "uuid-a", "tcp", "sub1");
    insertProfile("2", "5", "1.1.1.1", "443", "uuid-a", "tcp", "sub1");

    int deleted = runDedup({});

    EXPECT_EQ(deleted, 0);
    EXPECT_EQ(countProfiles(), 2);
}

TEST_F(DedupTest, CaseInsensitiveAddressDedup) {
    insertProfile("1", "5", "Example.COM", "443", "uuid-a", "tcp", "sub1");
    insertProfile("2", "5", "example.com", "443", "uuid-a", "tcp", "sub1");
    insertDelay("1", "100");
    insertDelay("2", "200");

    int deleted = runDedup({"sub1"});

    EXPECT_EQ(deleted, 1);
    EXPECT_EQ(countProfiles(), 1);
    EXPECT_EQ(getRemainingIndexIds()[0], "2");
}

TEST_F(DedupTest, MultipleProtectedSubIds) {
    insertProfile("1", "5", "1.1.1.1", "443", "uuid-a", "tcp", "subA");
    insertProfile("2", "5", "1.1.1.1", "443", "uuid-a", "tcp", "subB");
    insertProfile("3", "5", "1.1.1.1", "443", "uuid-a", "tcp", "subC");
    insertDelay("1", "0");
    insertDelay("2", "0");
    insertDelay("3", "300");

    int deleted = runDedup({"subA", "subB"});

    EXPECT_EQ(deleted, 2);
    EXPECT_EQ(countProfiles(), 1);
    // subA and subB are both protected, tie broken by delay DESC -> sub3 has delay 300 but subC is not protected
    // subA and subB both have delay 0, subC has 300 but is not protected
    // Protected ones (subA, subB) come first (CASE 0), then by delay DESC
    // Both have delay 0, so subA (first) wins
    std::string remaining = getRemainingIndexIds()[0];
    EXPECT_TRUE(remaining == "1" || remaining == "2");
}

// Blacklist phase tests
TEST_F(DedupTest, BlacklistPhaseMovesProxiesAboveThreshold) {
    // Insert proxies with different consecutive_failures counts
    insertProfile("1", "5", "1.1.1.1", "443", "uuid-a", "tcp", "sub-normal");
    insertProfile("2", "5", "2.2.2.2", "443", "uuid-b", "tcp", "sub-normal");
    insertProfile("3", "5", "3.3.3.3", "443", "uuid-c", "tcp", "sub-normal");
    
    // Insert ProfileExItem with consecutive_failures
    exec("INSERT INTO ProfileExItem (IndexId, consecutive_failures) VALUES ('1', 2)");
    exec("INSERT INTO ProfileExItem (IndexId, consecutive_failures) VALUES ('2', 5)");
    exec("INSERT INTO ProfileExItem (IndexId, consecutive_failures) VALUES ('3', 10)");

    // Simulate blacklist phase: move proxies with consecutive_failures >= 5 to blacklist subid
    int threshold = 5;
    std::string blacklistSubid = "blacklist-subid";
    std::string sql = "UPDATE ProfileItem SET Subid = '" + blacklistSubid + "' "
                      "WHERE IndexId IN (SELECT IndexId FROM ProfileExItem WHERE consecutive_failures >= " + std::to_string(threshold) + ")";
    exec(sql);
    
    // Verify proxy "2" and "3" were moved to blacklist
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT Subid FROM ProfileItem WHERE IndexId = '2'", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    std::string subid2 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    EXPECT_EQ(subid2, blacklistSubid);
    
    sqlite3_prepare_v2(db_, "SELECT Subid FROM ProfileItem WHERE IndexId = '3'", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    std::string subid3 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    EXPECT_EQ(subid3, blacklistSubid);
    
    // Verify proxy "1" kept original subid
    sqlite3_prepare_v2(db_, "SELECT Subid FROM ProfileItem WHERE IndexId = '1'", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    std::string subid1 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    EXPECT_EQ(subid1, "sub-normal");
}

TEST_F(DedupTest, BlacklistPhaseSkipsWhenThresholdNotMet) {
    insertProfile("1", "5", "1.1.1.1", "443", "uuid-a", "tcp", "sub-normal");
    exec("INSERT INTO ProfileExItem (IndexId, consecutive_failures) VALUES ('1', 3)");

    int threshold = 5;
    std::string blacklistSubid = "blacklist-subid";
    std::string sql = "UPDATE ProfileItem SET Subid = '" + blacklistSubid + "' "
                      "WHERE IndexId IN (SELECT IndexId FROM ProfileExItem WHERE consecutive_failures >= " + std::to_string(threshold) + ")";
    exec(sql);

    // Verify proxy was NOT moved
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT Subid FROM ProfileItem WHERE IndexId = '1'", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    std::string subid = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    EXPECT_EQ(subid, "sub-normal");
}
