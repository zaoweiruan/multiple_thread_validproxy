#include "Profileexitem.h"
#include "Logger.h"
#include <sqlite3.h>
#include <string>
#include <vector>

namespace db {
namespace models {

ProfileExItemDAO::ProfileExItemDAO(sqlite3* db) : db_(db) {
    migrateTable(db);
  }

void ProfileExItemDAO::migrateTable(sqlite3* db) {
    const char* addCols[] = {
        "ALTER TABLE ProfileExItem ADD COLUMN consecutive_failures INTEGER DEFAULT 0"
    };
    for (const char* sql : addCols) {
        sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    }
  }

std::vector<ProfileExItem> ProfileExItemDAO::getAll() {
    std::vector<ProfileExItem> result;
    const char* sql = "SELECT * FROM ProfileExItem;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      Logger::write("SQL错误: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      result.push_back(ProfileExItem::fromStmt(stmt));
    }

    sqlite3_finalize(stmt);
    return result;
  }

bool ProfileExItemDAO::updateTestResult(const std::string& indexid, long latencyMs, bool success, const std::string& curlMsg, sqlite3* db) {
    std::string message;
    if (success) {
      message = "OK";
    } else {
      if (!curlMsg.empty()) {
        message = curlMsg;
      } else {
        message = "FAILED";
      }
    }

    int currentFailures = 0;
    sqlite3_stmt* selectStmt = nullptr;
    const char* selectSql = "SELECT consecutive_failures FROM ProfileExItem WHERE IndexId = ?";
    sqlite3* execDb = db ? db : db_;

    if (sqlite3_prepare_v2(execDb, selectSql, -1, &selectStmt, nullptr) == SQLITE_OK) {
      sqlite3_bind_text(selectStmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(selectStmt) == SQLITE_ROW) {
        currentFailures = sqlite3_column_int(selectStmt, 0);
      }
      sqlite3_finalize(selectStmt);
    } else {
      Logger::write("SQL prepare failed for select: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
    }

    int newFailures = success ? 0 : currentFailures + 1;

    std::string delayStr = (success && latencyMs >= 0) ? std::to_string(latencyMs / 10) : "-1";

    const char* insertSql = "INSERT OR REPLACE INTO ProfileExItem (indexid, delay, speed, sort, message, consecutive_failures) VALUES (?, ?, '0', '0', ?, ?);";
    sqlite3_stmt* insertStmt = nullptr;
    if (sqlite3_prepare_v2(execDb, insertSql, -1, &insertStmt, nullptr) != SQLITE_OK) {
        Logger::write("SQL insert prepare error: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
        return false;
    }
    sqlite3_bind_text(insertStmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insertStmt, 2, delayStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insertStmt, 3, message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(insertStmt, 4, newFailures);

    int rc = sqlite3_step(insertStmt);
    sqlite3_finalize(insertStmt);
    if (rc != SQLITE_DONE) {
        Logger::write("SQL insert error: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
        return false;
    }
    return true;
  }

} // namespace models
} // namespace db
