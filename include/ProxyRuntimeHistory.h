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
  int socksPort = 0;       // SOCKS inbound port; 0 = not recorded (NULL column)
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

  // Append variant of insertStart: same row shape plus the SOCKS port that
  // was resolved for this process instance (socks_port column). The history
  // table is the single source of truth for adopted standalone proxies, so a
  // session whose live (in-progress) row is missing gets a NEW row appended
  // carrying the port instead of an UPDATE of an unrelated existing row.
  // socksPort <= 0 is stored as NULL (the column stays "unknown").
  // Returns the new row id, or -1 on failure (rolled back).
  int64_t insertStartWithPort(const std::string& indexId,
                              const std::string& startedAt, int64_t pid,
                              int socksPort, sqlite3* db = nullptr);

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

  // Same lookup without started_at, for callers that cannot reconstruct the
  // triplet (process creation time unreadable). Used when discarding an
  // unresolvable dangling process so its session does not stay "in progress"
  // forever. Returns -1 when there is none.
  int64_t findInProgressHistoryLoose(const std::string& indexId, int64_t pid,
                                     sqlite3* db = nullptr);

  // Reads the 3 aggregation columns for a proxy. Returns false when the row
  // does not exist (outputs are zeroed).
  bool getRuntimeStats(const std::string& indexId, int* startCount,
                       int64_t* totalRuntimeMs, int* crashCount,
                       sqlite3* db = nullptr);

  // Records the SOCKS inbound port of an in-progress session so a later
  // adoption can recover it when the on-disk config file is gone.
  // socksPort <= 0 is a no-op returning true. Does NOT open a transaction.
  // Returns false when no in-progress row matched historyId (0 rows changed).
  bool updateSocksPort(int64_t historyId, int socksPort,
                       sqlite3* db = nullptr);

  // Returns the recorded SOCKS port of the matching in-progress session, or 0
  // when there is no match / the column is NULL / the value is invalid.
  // When startedAt is empty (process creation time unreadable), matches on
  // (indexId, pid) only -- the triplet is not reconstructible in that case.
  int lookupSocksPort(const std::string& indexId, int64_t pid,
                      const std::string& startedAt,
                      sqlite3* db = nullptr);

  // Reads back the started_at value stored on a history row. Used by the
  // adoption path when it reuses an existing session row: the duration must be
  // measured against the timestamp that row was created with, not against the
  // timestamp the current scan happened to compute (which may be a "now"
  // fallback). Returns an empty string when the row does not exist.
  // Does NOT open a transaction.
  std::string getStartedAt(int64_t historyId, sqlite3* db = nullptr);

  // Returns all in-progress (ended_at IS NULL) sessions ordered by started_at.
  // Populates id/indexId/startedAt/pid/socksPort on each item; used by the
  // standalone monitor dialog to join live watched processes with their
  // history rows.
  std::vector<ProxyRuntimeHistoryItem> getInProgressSessions(
      sqlite3* db = nullptr);
};

} // namespace models
} // namespace db

#endif // DB_PROXY_RUNTIME_HISTORY_H
