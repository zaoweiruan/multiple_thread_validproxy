#ifndef DB_PROXY_RUNTIME_HISTORY_H
#define DB_PROXY_RUNTIME_HISTORY_H

#include <cstdint>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace db {
namespace models {

// One standalone-proxy runtime session (detail row of proxy_runtime_history).
// startedAt/endedAt use "yyyy-MM-dd HH:mm:ss"; endedAt empty = session in progress.
struct ProxyRuntimeHistoryItem {
  int64_t id = -1;
  std::string indexId;
  std::string startedAt;
  std::string endedAt;
  int exitCode = 0;        // 0 = normal exit; 259 (STILL_ACTIVE) = killed; other = crash
  int64_t durationMs = 0;
  std::string source = "standalone";
  int64_t pid = -1;        // proxy process pid (populated by getInProgressSessions)
};

// DAO for proxy_runtime_history + the 3 aggregation columns on ProfileExItem
// (start_count / total_runtime_ms / crash_count).
//
// Transaction contract (same as ProfileExItemDAO::updateTestResultBatch):
// detail-row write and the aggregate update always happen inside ONE
// transaction. Nested-transaction safety uses sqlite3_get_autocommit(): a
// BEGIN/COMMIT/ROLLBACK is only issued when the connection is in autocommit
// mode, so this DAO can safely participate in an outer transaction owned by
// the caller.
class ProxyRuntimeHistoryDAO {
private:
  sqlite3* db_;

public:
  explicit ProxyRuntimeHistoryDAO(sqlite3* db);

  // Update the internal database handle (used after database switching).
  void setDb(sqlite3* db) { db_ = db; }

  // Idempotent migration: creates proxy_runtime_history + composite index and
  // adds the 3 aggregate columns to ProfileExItem (ALTER errors ignored).
  static void migrateTable(sqlite3* db);

  // Startup: insert detail row (ended_at = NULL) + bump start_count in the
  // same transaction. pid identifies the process instance (proxy process pid);
  // it is stored on the row so in-progress sessions can be matched per
  // instance later. Returns the new row id, or -1 on failure (rolled back).
  int64_t insertStart(const std::string& indexId, const std::string& startedAt,
                      int64_t pid, sqlite3* db = nullptr);

  // Exit: backfill ended_at/exit_code/duration_ms (only when ended_at IS NULL,
  // so a second call is a no-op) + accumulate total_runtime_ms / crash_count
  // (crash counted when exitCode == 259) in the same transaction.
  bool finalizeStop(int64_t historyId, const std::string& endedAt,
                    int exitCode, int64_t durationMs, sqlite3* db = nullptr);

  // Periodic heartbeat: refresh duration_ms of an in-progress session
  // (ended_at stays NULL). Called by the process watcher while the proxy is
  // still running, so an abnormal exit or an externally-stopped process still
  // leaves the latest running duration behind for evaluation. No-op when the
  // session was already finalized; aggregates are only touched by finalizeStop.
  bool touchHeartbeat(int64_t historyId, int64_t durationMs,
                      sqlite3* db = nullptr);

  // Finds the most recent in-progress (ended_at IS NULL) session id for an
  // indexId that belongs to the SAME process instance, identified by the
  // (pid, startedAt) triplet — or -1 when there is none. Used when adopting a
  // dangling proxy process so the existing session is reused instead of
  // opening a new one; a new process instance (different pid or startedAt)
  // intentionally does NOT match, so it starts a fresh session.
  int64_t findInProgressHistory(const std::string& indexId, int64_t pid,
                                const std::string& startedAt,
                                sqlite3* db = nullptr);

  // Reads the 3 aggregation columns for a proxy. Returns false when the row
  // does not exist (outputs are zeroed).
  bool getRuntimeStats(const std::string& indexId, int* startCount,
                       int64_t* totalRuntimeMs, int* crashCount,
                       sqlite3* db = nullptr);

  // Returns all in-progress (ended_at IS NULL) sessions ordered by started_at.
  // Populates id/indexId/startedAt/pid on each item; used by the standalone
  // monitor dialog to join live watched processes with their history rows.
  std::vector<ProxyRuntimeHistoryItem> getInProgressSessions(
      sqlite3* db = nullptr);
};

} // namespace models
} // namespace db

#endif // DB_PROXY_RUNTIME_HISTORY_H
