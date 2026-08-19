#include "ProxyRuntimeHistory.h"
#include "Logger.h"
#include <sqlite3.h>
#include <string>

namespace db {
namespace models {

ProxyRuntimeHistoryDAO::ProxyRuntimeHistoryDAO(sqlite3* db) : db_(db) {
    migrateTable(db);
}

void ProxyRuntimeHistoryDAO::migrateTable(sqlite3* db) {
    // Detail table (idempotent - CREATE TABLE IF NOT EXISTS).
    const char* createTableSql =
        "CREATE TABLE IF NOT EXISTS proxy_runtime_history ("
        "id           INTEGER PRIMARY KEY AUTOINCREMENT, "
        "index_id     TEXT NOT NULL, "
        "started_at   TEXT NOT NULL, "
        "ended_at     TEXT NULL, "
        "exit_code    INTEGER, "
        "duration_ms  INTEGER, "
        "source       TEXT DEFAULT 'standalone')";
    sqlite3_exec(db, createTableSql, nullptr, nullptr, nullptr);

    // Composite index on (index_id, started_at).
    const char* createIndexSql =
        "CREATE INDEX IF NOT EXISTS idx_runtime_history_index "
        "ON proxy_runtime_history(index_id, started_at)";
    sqlite3_exec(db, createIndexSql, nullptr, nullptr, nullptr);

    // Aggregate columns on ProfileExItem (idempotent - column already exists is OK).
    const char* addCols[] = {
        "ALTER TABLE ProfileExItem ADD COLUMN start_count INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE ProfileExItem ADD COLUMN total_runtime_ms INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE ProfileExItem ADD COLUMN crash_count INTEGER NOT NULL DEFAULT 0"
    };
    for (const char* sql : addCols) {
        sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    }
}

int64_t ProxyRuntimeHistoryDAO::insertStart(const std::string& indexId,
                                            const std::string& startedAt,
                                            sqlite3* db) {
    sqlite3* execDb = db ? db : db_;

    // Nested-transaction safety: only begin when the connection is in
    // autocommit mode. If the caller already owns a transaction we
    // participate in it and leave commit/rollback to the caller.
    bool beganTransaction = false;
    if (sqlite3_get_autocommit(execDb)) {
        char* errMsg = nullptr;
        int beginRc = sqlite3_exec(execDb, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg);
        if (beginRc != SQLITE_OK) {
            Logger::write("ProxyRuntimeHistoryDAO::insertStart: BEGIN failed rc=" + std::to_string(beginRc) + " msg=" + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
            sqlite3_free(errMsg);
            return -1;
        }
        beganTransaction = true;
    }

    int64_t historyId = -1;
    bool ok = true;

    // 1. Insert the detail row (ended_at = NULL, session in progress).
    {
        const char* sql = "INSERT INTO proxy_runtime_history (index_id, started_at, source) VALUES (?, ?, 'standalone')";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(execDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            Logger::write("ProxyRuntimeHistoryDAO::insertStart: insert prepare failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
            ok = false;
        } else {
            sqlite3_bind_text(stmt, 1, indexId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, startedAt.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                historyId = sqlite3_last_insert_rowid(execDb);
            } else {
                Logger::write("ProxyRuntimeHistoryDAO::insertStart: insert step failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
                ok = false;
            }
            sqlite3_finalize(stmt);
        }
    }

    // 2. Bump the aggregate start_count in the same transaction.
    if (ok) {
        const char* sql = "UPDATE ProfileExItem SET start_count = start_count + 1 WHERE IndexId = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(execDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            Logger::write("ProxyRuntimeHistoryDAO::insertStart: aggregate prepare failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
            ok = false;
        } else {
            sqlite3_bind_text(stmt, 1, indexId.c_str(), -1, SQLITE_TRANSIENT);
            int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (rc != SQLITE_DONE) {
                Logger::write("ProxyRuntimeHistoryDAO::insertStart: aggregate step failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
                ok = false;
            }
        }
    }

    if (ok) {
        if (beganTransaction) {
            char* errMsg = nullptr;
            if (sqlite3_exec(execDb, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                Logger::write("ProxyRuntimeHistoryDAO::insertStart: COMMIT failed: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
                sqlite3_free(errMsg);
                sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
                return -1;
            }
        }
        return historyId;
    }

    if (beganTransaction) {
        sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    return -1;
}

bool ProxyRuntimeHistoryDAO::finalizeStop(int64_t historyId,
                                          const std::string& endedAt,
                                          int exitCode,
                                          int64_t durationMs,
                                          sqlite3* db) {
    sqlite3* execDb = db ? db : db_;

    // Nested-transaction safety (same rule as insertStart).
    bool beganTransaction = false;
    if (sqlite3_get_autocommit(execDb)) {
        char* errMsg = nullptr;
        int beginRc2 = sqlite3_exec(execDb, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg);
        if (beginRc2 != SQLITE_OK) {
            Logger::write("ProxyRuntimeHistoryDAO::finalizeStop: BEGIN failed rc=" + std::to_string(beginRc2) + " msg=" + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
            sqlite3_free(errMsg);
            return false;
        }
        beganTransaction = true;
    }

    bool ok = true;

    // 1. Resolve the index_id of the session row (aggregate update needs it).
    std::string indexId;
    {
        const char* sql = "SELECT index_id FROM proxy_runtime_history WHERE id = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(execDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            Logger::write("ProxyRuntimeHistoryDAO::finalizeStop: select prepare failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
            ok = false;
        } else {
            sqlite3_bind_int64(stmt, 1, historyId);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* text = (const char*)sqlite3_column_text(stmt, 0);
                indexId = text ? text : "";
            } else {
                Logger::write("ProxyRuntimeHistoryDAO::finalizeStop: history row not found, id=" + std::to_string(historyId), LogLevel::ERR);
                ok = false;
            }
            sqlite3_finalize(stmt);
        }
    }

    // 2. Idempotent backfill: only when ended_at IS NULL, so a second
    //    finalizeStop for the same session is a no-op (no double accounting).
    int changed = 0;
    if (ok) {
        const char* sql = "UPDATE proxy_runtime_history SET ended_at = ?, exit_code = ?, duration_ms = ? "
                          "WHERE id = ? AND ended_at IS NULL";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(execDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            Logger::write("ProxyRuntimeHistoryDAO::finalizeStop: update prepare failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
            ok = false;
        } else {
            sqlite3_bind_text(stmt, 1, endedAt.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 2, exitCode);
            sqlite3_bind_int64(stmt, 3, durationMs);
            sqlite3_bind_int64(stmt, 4, historyId);
            int rc = sqlite3_step(stmt);
            changed = sqlite3_changes(execDb);
            sqlite3_finalize(stmt);
            if (rc != SQLITE_DONE) {
                Logger::write("ProxyRuntimeHistoryDAO::finalizeStop: update step failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
                ok = false;
            }
        }
    }

    // 3. Accumulate aggregates only when this call actually backfilled the row
    //    (changed > 0), keeping the second call idempotent.
    if (ok && changed > 0) {
        int crashIncrement = (exitCode == 259) ? 1 : 0;
        const char* sql = "UPDATE ProfileExItem SET total_runtime_ms = total_runtime_ms + ?, "
                          "crash_count = crash_count + ? WHERE IndexId = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(execDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            Logger::write("ProxyRuntimeHistoryDAO::finalizeStop: aggregate prepare failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
            ok = false;
        } else {
            sqlite3_bind_int64(stmt, 1, durationMs);
            sqlite3_bind_int(stmt, 2, crashIncrement);
            sqlite3_bind_text(stmt, 3, indexId.c_str(), -1, SQLITE_TRANSIENT);
            int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (rc != SQLITE_DONE) {
                Logger::write("ProxyRuntimeHistoryDAO::finalizeStop: aggregate step failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
                ok = false;
            }
        }
    }

    if (ok) {
        if (beganTransaction) {
            char* errMsg = nullptr;
            if (sqlite3_exec(execDb, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                Logger::write("ProxyRuntimeHistoryDAO::finalizeStop: COMMIT failed: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
                sqlite3_free(errMsg);
                sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
                return false;
            }
        }
        return true;
    }

    if (beganTransaction) {
        sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
    }
    return false;
}

bool ProxyRuntimeHistoryDAO::touchHeartbeat(int64_t historyId,
                                            int64_t durationMs,
                                            sqlite3* db) {
    sqlite3* execDb = db ? db : db_;
    // Single atomic statement - no transaction needed. Only touches rows that
    // are still in progress (ended_at IS NULL); a finalized row is a no-op.
    const char* sql = "UPDATE proxy_runtime_history SET duration_ms = ? "
                      "WHERE id = ? AND ended_at IS NULL";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(execDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::write("ProxyRuntimeHistoryDAO::touchHeartbeat: prepare failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
        return false;
    }
    sqlite3_bind_int64(stmt, 1, durationMs);
    sqlite3_bind_int64(stmt, 2, historyId);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        Logger::write("ProxyRuntimeHistoryDAO::touchHeartbeat: step failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
        return false;
    }
    return true;
}

int64_t ProxyRuntimeHistoryDAO::findInProgressHistory(const std::string& indexId,
                                                      sqlite3* db) {
    sqlite3* execDb = db ? db : db_;
    const char* sql = "SELECT id FROM proxy_runtime_history "
                      "WHERE index_id = ? AND ended_at IS NULL "
                      "ORDER BY id DESC LIMIT 1";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(execDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::write("ProxyRuntimeHistoryDAO::findInProgressHistory: prepare failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, indexId.c_str(), -1, SQLITE_TRANSIENT);
    int64_t historyId = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        historyId = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return historyId;
}

bool ProxyRuntimeHistoryDAO::getRuntimeStats(const std::string& indexId,
                                             int* startCount,
                                             int64_t* totalRuntimeMs,
                                             int* crashCount,
                                             sqlite3* db) {
    if (!startCount || !totalRuntimeMs || !crashCount) {
        return false;
    }
    *startCount = 0;
    *totalRuntimeMs = 0;
    *crashCount = 0;

    sqlite3* execDb = db ? db : db_;
    const char* sql = "SELECT start_count, total_runtime_ms, crash_count FROM ProfileExItem WHERE IndexId = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(execDb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::write("ProxyRuntimeHistoryDAO::getRuntimeStats: prepare failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
        return false;
    }
    sqlite3_bind_text(stmt, 1, indexId.c_str(), -1, SQLITE_TRANSIENT);
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *startCount = sqlite3_column_int(stmt, 0);
        *totalRuntimeMs = sqlite3_column_int64(stmt, 1);
        *crashCount = sqlite3_column_int(stmt, 2);
        found = true;
    }
    sqlite3_finalize(stmt);
    return found;
}

} // namespace models
} // namespace db