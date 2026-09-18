// Tests for ProxyRuntimeHistoryDAO (proxy_runtime_history detail table +
// ProfileExItem aggregation columns start_count / total_runtime_ms / crash_count).
#include "ProxyRuntimeHistory.h"
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <string>

namespace {

class RuntimeHistoryDAOTest : public ::testing::Test {
protected:
  sqlite3* db_ = nullptr;

  void SetUp() override {
    ASSERT_EQ(sqlite3_open(":memory:", &db_), SQLITE_OK);
    // Base ProfileExItem WITHOUT the 3 aggregation columns: the DAO migration
    // must add them (idempotent ALTERs).
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

  void insertProxy(const std::string& indexId) {
    sqlite3_stmt* stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO ProfileExItem (IndexId, Delay, Speed, Sort, Message) VALUES (?, '0', '0', '0', '')", -1, &stmt, nullptr), SQLITE_OK);
    sqlite3_bind_text(stmt, 1, indexId.c_str(), -1, SQLITE_TRANSIENT);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_DONE);
    sqlite3_finalize(stmt);
  }

  int columnInt(const std::string& sql, int64_t bindId = -1) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) { sqlite3_finalize(stmt); return 0; }
    if (bindId >= 0) sqlite3_bind_int64(stmt, 1, bindId);
    int value = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) value = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
  }

  int64_t columnInt64(const std::string& sql, int64_t bindId = -1) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) { sqlite3_finalize(stmt); return 0; }
    if (bindId >= 0) sqlite3_bind_int64(stmt, 1, bindId);
    int64_t value = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
  }

  std::string columnText(const std::string& sql, int64_t bindId = -1) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) { sqlite3_finalize(stmt); return ""; }
    if (bindId >= 0) sqlite3_bind_int64(stmt, 1, bindId);
    std::string value;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      const char* text = (const char*)sqlite3_column_text(stmt, 0);
      value = text ? text : "";
    }
    sqlite3_finalize(stmt);
    return value;
  }

  // Text-bound scalar reader (for IndexId-style TEXT placeholders).
  int columnIntText(const std::string& sql, const std::string& text) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) { sqlite3_finalize(stmt); return 0; }
    sqlite3_bind_text(stmt, 1, text.c_str(), -1, SQLITE_TRANSIENT);
    int value = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) value = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
  }

  // Aggregate column readers on ProfileExItem.
  int startCountOf(const std::string& indexId) {
    return columnIntText("SELECT start_count FROM ProfileExItem WHERE IndexId = ?", indexId);
  }
  int crashCountOf(const std::string& indexId) {
    return columnIntText("SELECT crash_count FROM ProfileExItem WHERE IndexId = ?", indexId);
  }
};

const std::string A1 = "index-a-1";
const std::string B1 = "index-b-1";
const std::string T0 = "2026-08-14 08:00:00";
const std::string T1 = "2026-08-14 08:30:00";

// ---- migration ----

TEST_F(RuntimeHistoryDAOTest, MigrateTable_CreatesDetailTable) {
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  // Table must exist after construction.
  EXPECT_EQ(columnInt("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='proxy_runtime_history'"), 1);
  // Composite index must exist.
  EXPECT_EQ(columnInt("SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='idx_runtime_history_index'"), 1);
}

TEST_F(RuntimeHistoryDAOTest, MigrateTable_AddsAggregateColumns) {
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  // Columns now exist with proper defaults.
  insertProxy(A1);
  EXPECT_EQ(startCountOf(A1), 0);
  EXPECT_EQ(crashCountOf(A1), 0);
}

TEST_F(RuntimeHistoryDAOTest, MigrateTable_IsIdempotent) {
  db::models::ProxyRuntimeHistoryDAO first(db_);
  db::models::ProxyRuntimeHistoryDAO second(db_);  // second migrate must not fail
  insertProxy(A1);
  EXPECT_EQ(startCountOf(A1), 0);
}

// ---- insertStart ----

TEST_F(RuntimeHistoryDAOTest, InsertStart_CreatesDetailRowAndReturnsId) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t id = dao.insertStart(A1, T0, 1001);
  EXPECT_GT(id, 0);

  std::string started = columnText("SELECT started_at FROM proxy_runtime_history WHERE id = ?", id);
  EXPECT_EQ(started, T0);
  // ended_at must be NULL while session is in progress.
  std::string ended = columnText("SELECT IFNULL(ended_at, '') FROM proxy_runtime_history WHERE id = ?", id);
  EXPECT_TRUE(ended.empty());
  EXPECT_EQ(columnInt("SELECT source FROM proxy_runtime_history WHERE id = ?", id) == 0, true);
}

TEST_F(RuntimeHistoryDAOTest, InsertStart_BumpsStartCount) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  dao.insertStart(A1, T0, 1001);
  dao.insertStart(A1, T1, 1001);
  EXPECT_EQ(startCountOf(A1), 2);
}

TEST_F(RuntimeHistoryDAOTest, InsertStart_UnknownIndex_StillWritesDetail) {
  // Unknown IndexId: detail row is still recorded (audit), aggregate UPDATE
  // affects 0 rows but must not fail the transaction.
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t id = dao.insertStart("ghost-index", T0, 1001);
  EXPECT_GT(id, 0);
  EXPECT_EQ(columnInt("SELECT COUNT(*) FROM proxy_runtime_history WHERE index_id='ghost-index'"), 1);
}

// ---- finalizeStop ----

TEST_F(RuntimeHistoryDAOTest, FinalizeStop_BackfillsDetailRow) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t id = dao.insertStart(A1, T0, 1001);
  ASSERT_GT(id, 0);

  EXPECT_TRUE(dao.finalizeStop(id, T1, 0, 5000));
  EXPECT_EQ(columnText("SELECT ended_at FROM proxy_runtime_history WHERE id = ?", id), T1);
  EXPECT_EQ(columnInt("SELECT exit_code FROM proxy_runtime_history WHERE id = ?", id), 0);
  EXPECT_EQ(columnInt64("SELECT duration_ms FROM proxy_runtime_history WHERE id = ?", id), 5000);
}

TEST_F(RuntimeHistoryDAOTest, FinalizeStop_AccumulatesTotalRuntime) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t id1 = dao.insertStart(A1, T0, 1001);
  int64_t id2 = dao.insertStart(A1, T1, 1001);
  ASSERT_GT(id1, 0);
  ASSERT_GT(id2, 0);

  dao.finalizeStop(id1, T1, 0, 3000);
  dao.finalizeStop(id2, "2026-08-14 09:00:00", 0, 7000);

  int startCount = 0, crashCount = 0;
  int64_t totalRuntime = 0;
  EXPECT_TRUE(dao.getRuntimeStats(A1, &startCount, &totalRuntime, &crashCount));
  EXPECT_EQ(startCount, 2);
  EXPECT_EQ(totalRuntime, 10000);
  EXPECT_EQ(crashCount, 0);
}

TEST_F(RuntimeHistoryDAOTest, FinalizeStop_SecondCall_NoDoubleAccumulate) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t id = dao.insertStart(A1, T0, 1001);
  ASSERT_GT(id, 0);

  EXPECT_TRUE(dao.finalizeStop(id, T1, 0, 5000));
  // Second call must be a no-op (ended_at already set) -> no double accounting.
  EXPECT_TRUE(dao.finalizeStop(id, T1, 0, 5000));

  int startCount = 0, crashCount = 0;
  int64_t totalRuntime = 0;
  EXPECT_TRUE(dao.getRuntimeStats(A1, &startCount, &totalRuntime, &crashCount));
  EXPECT_EQ(startCount, 1);
  EXPECT_EQ(totalRuntime, 5000);
}

TEST_F(RuntimeHistoryDAOTest, FinalizeStop_CrashExitCode259_IncrementsCrashCount) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t id = dao.insertStart(A1, T0, 1001);
  ASSERT_GT(id, 0);

  EXPECT_TRUE(dao.finalizeStop(id, T1, 259, 4000));
  EXPECT_EQ(crashCountOf(A1), 1);

  // Normal exit on a second session must not increment crash count.
  int64_t id2 = dao.insertStart(A1, T1, 1001);
  ASSERT_GT(id2, 0);
  EXPECT_TRUE(dao.finalizeStop(id2, "2026-08-14 09:00:00", 0, 2000));
  EXPECT_EQ(crashCountOf(A1), 1);
}

TEST_F(RuntimeHistoryDAOTest, FinalizeStop_NonCrashExit_NoCrashIncrement) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t id = dao.insertStart(A1, T0, 1001);
  ASSERT_GT(id, 0);

  EXPECT_TRUE(dao.finalizeStop(id, T1, 1, 1000));
  EXPECT_EQ(crashCountOf(A1), 0);
}

TEST_F(RuntimeHistoryDAOTest, FinalizeStop_UnknownId_Fails) {
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  EXPECT_FALSE(dao.finalizeStop(999999, T1, 0, 1000));
}

// ---- getRuntimeStats ----

TEST_F(RuntimeHistoryDAOTest, GetRuntimeStats_MissingIndex_ReturnsFalseWithZeros) {
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int startCount = 42, crashCount = 42;
  int64_t totalRuntime = 42;
  EXPECT_FALSE(dao.getRuntimeStats("no-such-index", &startCount, &totalRuntime, &crashCount));
  EXPECT_EQ(startCount, 0);
  EXPECT_EQ(totalRuntime, 0);
  EXPECT_EQ(crashCount, 0);
}

TEST_F(RuntimeHistoryDAOTest, GetRuntimeStats_ExistingIndex_ZeroBaseline) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int startCount = 42, crashCount = 42;
  int64_t totalRuntime = 42;
  EXPECT_TRUE(dao.getRuntimeStats(A1, &startCount, &totalRuntime, &crashCount));
  EXPECT_EQ(startCount, 0);
  EXPECT_EQ(totalRuntime, 0);
  EXPECT_EQ(crashCount, 0);
}

TEST_F(RuntimeHistoryDAOTest, GetRuntimeStats_NullOutputs_ReturnsFalse) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  EXPECT_FALSE(dao.getRuntimeStats(A1, nullptr, nullptr, nullptr));
}

// ---- nested-transaction safety (sqlite3_get_autocommit contract) ----

TEST_F(RuntimeHistoryDAOTest, InsertStart_InsideOuterTransaction_Participates) {
  insertProxy(A1);
  char* errMsg = nullptr;
  ASSERT_EQ(sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg), SQLITE_OK)
      << (errMsg ? errMsg : "unknown");
  sqlite3_free(errMsg);

  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t id = dao.insertStart(A1, T0, 1001);
  EXPECT_GT(id, 0);

  // ROLLBACK should also discard the detail row (DAO joined the outer txn).
  ASSERT_EQ(sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &errMsg), SQLITE_OK)
      << (errMsg ? errMsg : "unknown");
  sqlite3_free(errMsg);

  EXPECT_EQ(columnInt("SELECT COUNT(*) FROM proxy_runtime_history"), 0);
  EXPECT_EQ(startCountOf(A1), 0);
}

TEST_F(RuntimeHistoryDAOTest, FinalizeStop_InsideOuterTransaction_Participates) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t id = dao.insertStart(A1, T0, 1001);
  ASSERT_GT(id, 0);

  char* errMsg = nullptr;
  ASSERT_EQ(sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg), SQLITE_OK)
      << (errMsg ? errMsg : "unknown");
  sqlite3_free(errMsg);

  EXPECT_TRUE(dao.finalizeStop(id, T1, 0, 5000));

  ASSERT_EQ(sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &errMsg), SQLITE_OK)
      << (errMsg ? errMsg : "unknown");
  sqlite3_free(errMsg);

  // Aggregate rollback: total_runtime_ms back to 0.
  int startCount = 42, crashCount = 42;
  int64_t totalRuntime = 42;
  EXPECT_TRUE(dao.getRuntimeStats(A1, &startCount, &totalRuntime, &crashCount));
  EXPECT_EQ(totalRuntime, 0);
}

// ---- lifecycle end-to-end ----

TEST_F(RuntimeHistoryDAOTest, FullLifecycle_SingleProxy) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);

  int64_t s1 = dao.insertStart(A1, T0, 1001);
  int64_t s2 = dao.insertStart(A1, T1, 1001);
  ASSERT_GT(s1, 0);
  ASSERT_GT(s2, 0);
  EXPECT_TRUE(dao.finalizeStop(s1, T1, 259, 15000));
  EXPECT_TRUE(dao.finalizeStop(s2, "2026-08-14 09:30:00", 0, 25000));

  int startCount = 0, crashCount = 0;
  int64_t totalRuntime = 0;
  ASSERT_TRUE(dao.getRuntimeStats(A1, &startCount, &totalRuntime, &crashCount));
  EXPECT_EQ(startCount, 2);
  EXPECT_EQ(crashCount, 1);
  EXPECT_EQ(totalRuntime, 40000);

  EXPECT_EQ(columnIntText("SELECT COUNT(*) FROM proxy_runtime_history WHERE index_id = ? AND ended_at IS NOT NULL", A1), 2);
}

TEST_F(RuntimeHistoryDAOTest, FullLifecycle_IndependentProxies) {
  insertProxy(A1);
  insertProxy(B1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);

  int64_t id = dao.insertStart(A1, T0, 1001);
  ASSERT_GT(id, 0);
  EXPECT_TRUE(dao.finalizeStop(id, T1, 259, 9000));

  int startCount = 0, crashCount = 0;
  int64_t totalRuntime = 0;
  ASSERT_TRUE(dao.getRuntimeStats(A1, &startCount, &totalRuntime, &crashCount));
  EXPECT_EQ(startCount, 1);
  EXPECT_EQ(crashCount, 1);
  EXPECT_EQ(totalRuntime, 9000);

  // B untouched.
  ASSERT_TRUE(dao.getRuntimeStats(B1, &startCount, &totalRuntime, &crashCount));
  EXPECT_EQ(startCount, 0);
  EXPECT_EQ(crashCount, 0);
  EXPECT_EQ(totalRuntime, 0);
}

// ---- pid factor + triplet matching ----

TEST_F(RuntimeHistoryDAOTest, MigrateTable_AddsPidColumn) {
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  insertProxy(A1);
  int64_t id = dao.insertStart(A1, T0, 1001);
  EXPECT_GT(id, 0);
  EXPECT_EQ(columnInt64("SELECT pid FROM proxy_runtime_history WHERE id = ?", id), 1001);
}

TEST_F(RuntimeHistoryDAOTest, FindInProgressHistory_ExactMatch) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t id = dao.insertStart(A1, T0, 1001);
  int64_t found = dao.findInProgressHistory(A1, 1001, T0);
  EXPECT_EQ(found, id);
}

TEST_F(RuntimeHistoryDAOTest, FindInProgressHistory_DifferentPid_ReturnsMinusOne) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  dao.insertStart(A1, T0, 1001);
  EXPECT_EQ(dao.findInProgressHistory(A1, 2002, T0), -1);
}

TEST_F(RuntimeHistoryDAOTest, FindInProgressHistory_DifferentStartedAt_ReturnsMinusOne) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  dao.insertStart(A1, T0, 1001);
  EXPECT_EQ(dao.findInProgressHistory(A1, 1001, T1), -1);
}

TEST_F(RuntimeHistoryDAOTest, FindInProgressHistory_Finalized_ReturnsMinusOne) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t id = dao.insertStart(A1, T0, 1001);
  dao.finalizeStop(id, T1, 0, 5000);
  EXPECT_EQ(dao.findInProgressHistory(A1, 1001, T0), -1);
}

TEST_F(RuntimeHistoryDAOTest, FindInProgressHistory_LatestWins) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t id1 = dao.insertStart(A1, T0, 1001);
  int64_t id2 = dao.insertStart(A1, T1, 1001);
  EXPECT_NE(id1, id2);
  EXPECT_EQ(dao.findInProgressHistory(A1, 1001, T1), id2);
}

// ---- getInProgressSessions (standalone monitor dialog data source) ----

TEST_F(RuntimeHistoryDAOTest, GetInProgressSessions_ReturnsPidAndStartedAt) {
  insertProxy(A1);
  insertProxy(B1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t idA = dao.insertStart(A1, T0, 1001);
  int64_t idB = dao.insertStart(B1, T1, 2002);
  ASSERT_GT(idA, 0);
  ASSERT_GT(idB, 0);

  std::vector<db::models::ProxyRuntimeHistoryItem> rows =
      dao.getInProgressSessions();
  ASSERT_EQ(rows.size(), static_cast<std::size_t>(2));
  // Ordered by started_at: T0 row first.
  EXPECT_EQ(rows[0].id, idA);
  EXPECT_EQ(rows[0].indexId, A1);
  EXPECT_EQ(rows[0].startedAt, T0);
  EXPECT_EQ(rows[0].pid, 1001);
  EXPECT_EQ(rows[1].id, idB);
  EXPECT_EQ(rows[1].indexId, B1);
  EXPECT_EQ(rows[1].startedAt, T1);
  EXPECT_EQ(rows[1].pid, 2002);
}

TEST_F(RuntimeHistoryDAOTest, GetInProgressSessions_ExcludesFinalizedRows) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t idA = dao.insertStart(A1, T0, 1001);
  int64_t idB = dao.insertStart(B1, T1, 2002);
  ASSERT_GT(idA, 0);
  ASSERT_GT(idB, 0);

  EXPECT_TRUE(dao.finalizeStop(idA, T1, 0, 5000));

  std::vector<db::models::ProxyRuntimeHistoryItem> rows =
      dao.getInProgressSessions();
  ASSERT_EQ(rows.size(), static_cast<std::size_t>(1));
  EXPECT_EQ(rows[0].id, idB);
  EXPECT_EQ(rows[0].pid, 2002);
}

TEST_F(RuntimeHistoryDAOTest, GetInProgressSessions_EmptyTable_ReturnsEmpty) {
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  std::vector<db::models::ProxyRuntimeHistoryItem> rows =
      dao.getInProgressSessions();
  EXPECT_TRUE(rows.empty());
}

}  // namespace