// Tests for ProfileExItemDAO message field semantics:
// message carries ONLY two kinds of info: test time + startup time,
// separated by a '+' (always present). Changing one side preserves the other.
#include "Profileexitem.h"
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <string>

namespace {

class ProfileExItemDAOTest : public ::testing::Test {
protected:
  sqlite3* db_ = nullptr;

  void SetUp() override {
    ASSERT_EQ(sqlite3_open(":memory:", &db_), SQLITE_OK);
    exec("CREATE TABLE ProfileExItem ("
         "IndexId TEXT PRIMARY KEY, Delay TEXT, Speed TEXT, Sort TEXT, "
         "Message TEXT, consecutive_failures INTEGER DEFAULT 0)");
  }

  void TearDown() override {
    if (db_) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
  }

  void exec(const std::string& sql) {
    char* errMsg = nullptr;
    ASSERT_EQ(sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg), SQLITE_OK)
        << (errMsg ? errMsg : "unknown sqlite error");
    sqlite3_free(errMsg);
  }

  std::string getMessage(const std::string& indexid) {
    std::string result;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT Message FROM ProfileExItem WHERE IndexId = ?", -1, &stmt, nullptr) != SQLITE_OK) {
      ADD_FAILURE() << "prepare failed: " << sqlite3_errmsg(db_);
      return std::string();
    }
    sqlite3_bind_text(stmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      const char* text = (const char*)sqlite3_column_text(stmt, 0);
      result = text ? text : "";
    }
    sqlite3_finalize(stmt);
    return result;
  }

  void insertRow(const std::string& indexid, const std::string& message, int failures = 0) {
    sqlite3_stmt* stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO ProfileExItem (IndexId, Delay, Speed, Sort, Message, consecutive_failures) VALUES (?, '0', '0', '0', ?, ?)", -1, &stmt, nullptr), SQLITE_OK);
    sqlite3_bind_text(stmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, failures);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE);
    sqlite3_finalize(stmt);
  }
};

const std::string T1 = "2026-08-11 08:00:00";  // existing test time
const std::string S1 = "2026-08-11 09:00:00";  // existing startup time
const std::string NT = "2026-08-11 10:00:00";  // new test time
const std::string NS = "2026-08-11 11:00:00";  // new startup time

// ---- formatTestMessage (test-result write) ----

TEST_F(ProfileExItemDAOTest, FormatTestMessage_LegacyReplaced) {
  // "OK" contains no '+' -> both sides invalid -> whole value replaced
  EXPECT_EQ(db::models::ProfileExItemDAO::formatTestMessage("OK", NT), NT + "+");
  EXPECT_EQ(db::models::ProfileExItemDAO::formatTestMessage("FAILED", NT), NT + "+");
  EXPECT_EQ(db::models::ProfileExItemDAO::formatTestMessage("curl error 7", NT), NT + "+");
  EXPECT_EQ(db::models::ProfileExItemDAO::formatTestMessage("", NT), NT + "+");
  EXPECT_EQ(db::models::ProfileExItemDAO::formatTestMessage("NOT_TESTED", NT), NT + "+");
}

TEST_F(ProfileExItemDAOTest, FormatTestMessage_KeepsValidStartupSide) {
  EXPECT_EQ(db::models::ProfileExItemDAO::formatTestMessage(T1 + "+" + S1, NT), NT + "+" + S1);
  EXPECT_EQ(db::models::ProfileExItemDAO::formatTestMessage("+" + S1, NT), NT + "+" + S1);
}

TEST_F(ProfileExItemDAOTest, FormatTestMessage_InvalidStartupSideDropped) {
  // startup side is not a valid timestamp -> replaced entirely
  EXPECT_EQ(db::models::ProfileExItemDAO::formatTestMessage(T1 + "+garbage", NT), NT + "+");
  EXPECT_EQ(db::models::ProfileExItemDAO::formatTestMessage(T1 + "+", NT), NT + "+");
  EXPECT_EQ(db::models::ProfileExItemDAO::formatTestMessage("bad+2026/08/11 09:00:00", NT), NT + "+");
}

// ---- formatStartupMessage (startup write) ----

TEST_F(ProfileExItemDAOTest, FormatStartupMessage_LegacyReplaced) {
  EXPECT_EQ(db::models::ProfileExItemDAO::formatStartupMessage("OK", NS), "+" + NS);
  EXPECT_EQ(db::models::ProfileExItemDAO::formatStartupMessage("", NS), "+" + NS);
  EXPECT_EQ(db::models::ProfileExItemDAO::formatStartupMessage("NOT_TESTED", NS), "+" + NS);
}

TEST_F(ProfileExItemDAOTest, FormatStartupMessage_KeepsValidTestSide) {
  EXPECT_EQ(db::models::ProfileExItemDAO::formatStartupMessage(T1 + "+" + S1, NS), T1 + "+" + NS);
  EXPECT_EQ(db::models::ProfileExItemDAO::formatStartupMessage(T1 + "+", NS), T1 + "+" + NS);
}

TEST_F(ProfileExItemDAOTest, FormatStartupMessage_InvalidTestSideDropped) {
  EXPECT_EQ(db::models::ProfileExItemDAO::formatStartupMessage("garbage+" + S1, NS), "+" + NS);
  EXPECT_EQ(db::models::ProfileExItemDAO::formatStartupMessage("+" + S1, NS), "+" + NS);
}

// ---- currentTimeString ----

TEST_F(ProfileExItemDAOTest, CurrentTimeStringFormat) {
  std::string now = db::models::ProfileExItemDAO::currentTimeString();
  ASSERT_EQ(now.size(), 19u);
  EXPECT_EQ(now[4], '-');
  EXPECT_EQ(now[7], '-');
  EXPECT_EQ(now[10], ' ');
  EXPECT_EQ(now[13], ':');
  EXPECT_EQ(now[16], ':');
  EXPECT_TRUE(now[0] == '2');
}

// ---- updateTestResult ----

TEST_F(ProfileExItemDAOTest, UpdateTestResult_Success_WritesTestTime) {
  db::models::ProfileExItemDAO dao(db_);
  ASSERT_TRUE(dao.updateTestResult("id1", 1234, true, ""));

  std::string msg = getMessage("id1");
  // "yyyy-MM-dd HH:mm:ss+" exactly 20 chars, trailing '+'
  EXPECT_EQ(msg.size(), 20u);
  EXPECT_EQ(msg[19], '+');
  EXPECT_EQ(msg[10], ' ');
}

TEST_F(ProfileExItemDAOTest, UpdateTestResult_Success_PreservesStartupSide) {
  insertRow("id2", T1 + "+" + S1);
  db::models::ProfileExItemDAO dao(db_);
  ASSERT_TRUE(dao.updateTestResult("id2", 567, true, ""));

  std::string msg = getMessage("id2");
  // new test time + '+' + preserved startup time
  EXPECT_EQ(msg.size(), 39u);
  EXPECT_EQ(msg[19], '+');
  EXPECT_EQ(msg.substr(20), S1);
}

TEST_F(ProfileExItemDAOTest, UpdateTestResult_Failure_PreservesMessage) {
  insertRow("id3", T1 + "+" + S1);
  db::models::ProfileExItemDAO dao(db_);
  ASSERT_TRUE(dao.updateTestResult("id3", -1, false, "connection refused"));

  EXPECT_EQ(getMessage("id3"), T1 + "+" + S1);
}

TEST_F(ProfileExItemDAOTest, UpdateTestResult_Failure_NewRow_EmptyMessage) {
  db::models::ProfileExItemDAO dao(db_);
  ASSERT_TRUE(dao.updateTestResult("id4", -1, false, "timeout"));

  EXPECT_EQ(getMessage("id4"), "");
}

// ---- updateTestResultBatch ----

TEST_F(ProfileExItemDAOTest, UpdateTestResultBatch_Success_WritesTestTime) {
  db::models::ProfileExItemDAO dao(db_);
  std::vector<std::tuple<std::string, long, bool, std::string>> results;
  results.push_back(std::make_tuple("b1", 1000, true, std::string("")));
  ASSERT_TRUE(dao.updateTestResultBatch(results));

  std::string msg = getMessage("b1");
  EXPECT_EQ(msg.size(), 20u);
  EXPECT_EQ(msg[19], '+');
}

TEST_F(ProfileExItemDAOTest, UpdateTestResultBatch_Success_PreservesStartupSide) {
  insertRow("b2", T1 + "+" + S1);
  db::models::ProfileExItemDAO dao(db_);
  std::vector<std::tuple<std::string, long, bool, std::string>> results;
  results.push_back(std::make_tuple("b2", 900, true, std::string("")));
  ASSERT_TRUE(dao.updateTestResultBatch(results));

  std::string msg = getMessage("b2");
  EXPECT_EQ(msg.size(), 39u);
  EXPECT_EQ(msg[19], '+');
  EXPECT_EQ(msg.substr(20), S1);
}

TEST_F(ProfileExItemDAOTest, UpdateTestResultBatch_Failure_PreservesMessage) {
  insertRow("b3", T1 + "+" + S1);
  db::models::ProfileExItemDAO dao(db_);
  std::vector<std::tuple<std::string, long, bool, std::string>> results;
  results.push_back(std::make_tuple("b3", -1, false, std::string("refused")));
  ASSERT_TRUE(dao.updateTestResultBatch(results));

  EXPECT_EQ(getMessage("b3"), T1 + "+" + S1);
}

// ---- updateStartupTime ----

TEST_F(ProfileExItemDAOTest, UpdateStartupTime_NewRow_WritesStartupTime) {
  db::models::ProfileExItemDAO dao(db_);
  ASSERT_TRUE(dao.updateStartupTime("s1"));

  std::string msg = getMessage("s1");
  // "+yyyy-MM-dd HH:mm:ss" exactly 20 chars, leading '+'
  EXPECT_EQ(msg.size(), 20u);
  EXPECT_EQ(msg[0], '+');
  EXPECT_EQ(msg[11], ' ');
}

TEST_F(ProfileExItemDAOTest, UpdateStartupTime_PreservesTestSide) {
  insertRow("s2", T1 + "+");
  db::models::ProfileExItemDAO dao(db_);
  ASSERT_TRUE(dao.updateStartupTime("s2"));

  std::string msg = getMessage("s2");
  EXPECT_EQ(msg.size(), 39u);
  EXPECT_EQ(msg[19], '+');
  EXPECT_EQ(msg.substr(0, 19), T1);
}

TEST_F(ProfileExItemDAOTest, UpdateStartupTime_LegacyReplaced) {
  insertRow("s3", "OK");
  db::models::ProfileExItemDAO dao(db_);
  ASSERT_TRUE(dao.updateStartupTime("s3"));

  std::string msg = getMessage("s3");
  EXPECT_EQ(msg.size(), 20u);
  EXPECT_EQ(msg[0], '+');
}

// ---- messageActiveTime / compareMessage (Message column sort key) ----

TEST_F(ProfileExItemDAOTest, MessageActiveTime_EmptyAndLegacy_ReturnsEmpty) {
  EXPECT_EQ(db::models::ProfileExItemDAO::messageActiveTime(""), "");
  EXPECT_EQ(db::models::ProfileExItemDAO::messageActiveTime("OK"), "");
  EXPECT_EQ(db::models::ProfileExItemDAO::messageActiveTime("FAILED"), "");
  EXPECT_EQ(db::models::ProfileExItemDAO::messageActiveTime("curl error 7"), "");
  EXPECT_EQ(db::models::ProfileExItemDAO::messageActiveTime("NOT_TESTED"), "");
  EXPECT_EQ(db::models::ProfileExItemDAO::messageActiveTime("garbage+garbage"), "");
}

TEST_F(ProfileExItemDAOTest, MessageActiveTime_OnlyOneSide) {
  EXPECT_EQ(db::models::ProfileExItemDAO::messageActiveTime(T1 + "+"), T1);
  EXPECT_EQ(db::models::ProfileExItemDAO::messageActiveTime("+" + S1), S1);
}

TEST_F(ProfileExItemDAOTest, MessageActiveTime_BothSides_PicksNewer) {
  // test side (08:00) older than startup side (09:00) -> startup side
  EXPECT_EQ(db::models::ProfileExItemDAO::messageActiveTime(T1 + "+" + S1), S1);
  // test side (11:00) newer than startup side (09:00) -> test side
  EXPECT_EQ(db::models::ProfileExItemDAO::messageActiveTime(NS + "+" + S1), NS);
}

TEST_F(ProfileExItemDAOTest, MessageActiveTime_InvalidSideIgnored) {
  EXPECT_EQ(db::models::ProfileExItemDAO::messageActiveTime("bad+" + S1), S1);
  EXPECT_EQ(db::models::ProfileExItemDAO::messageActiveTime(T1 + "+bad"), T1);
  EXPECT_EQ(db::models::ProfileExItemDAO::messageActiveTime("+"), "");
}

TEST_F(ProfileExItemDAOTest, CompareMessage_SortsByNewerSide) {
  // T1+S1 active=09:00 vs NT+ active=10:00: the second one is newer
  EXPECT_EQ(db::models::ProfileExItemDAO::compareMessage(T1 + "+" + S1, NT + "+"), -1);
  EXPECT_EQ(db::models::ProfileExItemDAO::compareMessage(NT + "+", T1 + "+" + S1), 1);
  // startup-only vs test-only, whichever side is newer wins
  EXPECT_EQ(db::models::ProfileExItemDAO::compareMessage("+" + S1, NT + "+"), -1);
  EXPECT_EQ(db::models::ProfileExItemDAO::compareMessage(NT + "+", "+" + S1), 1);
}

TEST_F(ProfileExItemDAOTest, CompareMessage_EmptySortsLast) {
  EXPECT_EQ(db::models::ProfileExItemDAO::compareMessage("", T1 + "+"), 1);
  EXPECT_EQ(db::models::ProfileExItemDAO::compareMessage(T1 + "+", ""), -1);
  EXPECT_EQ(db::models::ProfileExItemDAO::compareMessage("OK", "+" + S1), 1);
  EXPECT_EQ(db::models::ProfileExItemDAO::compareMessage("", ""), 0);
  EXPECT_EQ(db::models::ProfileExItemDAO::compareMessage("OK", "FAILED"), 0);
}

TEST_F(ProfileExItemDAOTest, CompareMessage_EqualActiveTime_ReturnsZero) {
  EXPECT_EQ(db::models::ProfileExItemDAO::compareMessage(T1 + "+", T1 + "+"), 0);
  EXPECT_EQ(db::models::ProfileExItemDAO::compareMessage(T1 + "+" + S1, S1 + "+"), 0);
}

// ---- updateTestResultBatch nested-transaction safety ----

TEST_F(ProfileExItemDAOTest, UpdateTestResultBatch_SucceedsInsideOuterTransaction) {
  // Reproduce the runtime failure: caller already has an active transaction
  // on the same sqlite3* when updateTestResultBatch is invoked.
  char* errMsg = nullptr;
  ASSERT_EQ(sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg), SQLITE_OK)
      << (errMsg ? errMsg : "unknown");
  sqlite3_free(errMsg);

  db::models::ProfileExItemDAO dao(db_);
  std::vector<std::tuple<std::string, long, bool, std::string>> results;
  results.push_back(std::make_tuple("nested1", 1000, true, std::string("")));
  results.push_back(std::make_tuple("nested2", -1, false, std::string("timeout")));

  // Should NOT fail with "cannot start a transaction within a transaction".
  EXPECT_TRUE(dao.updateTestResultBatch(results));

  EXPECT_EQ(getMessage("nested1").size(), 20u);
  EXPECT_EQ(getMessage("nested1")[19], '+');
  EXPECT_EQ(getMessage("nested2"), "");

  ASSERT_EQ(sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &errMsg), SQLITE_OK)
      << (errMsg ? errMsg : "unknown");
  sqlite3_free(errMsg);

  // After outer rollback the rows should not persist.
  EXPECT_EQ(getMessage("nested1"), "");
  EXPECT_EQ(getMessage("nested2"), "");
}

}  // namespace
