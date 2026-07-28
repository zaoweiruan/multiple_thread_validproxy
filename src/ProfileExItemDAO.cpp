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
    // Add columns to ProfileExItem (existing migrations)
    const char* addCols[] = {
        "ALTER TABLE ProfileExItem ADD COLUMN consecutive_failures INTEGER DEFAULT 0"
    };
    for (const char* sql : addCols) {
        sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    }

    // Add Region column to ProfileItem (idempotent - column already exists is OK)
    {
        const char* sql = "ALTER TABLE ProfileItem ADD COLUMN Region TEXT;";
        char* errMsg = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
            // Column already exists - this is normal
            sqlite3_free(errMsg);
        }
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

  bool ProfileExItemDAO::updateTestResultBatch(
      const std::vector<std::tuple<std::string, long, bool, std::string>>& results,
      sqlite3* db) {
      
      if (results.empty()) {
          return true;
      }
      
      sqlite3* execDb = db ? db : db_;
      
      // Sub-batch commit: commit every SUB_BATCH_SIZE items to avoid
      // one massive transaction that blocks other WAL readers.
      static constexpr int SUB_BATCH_SIZE = 50;
      bool allOk = true;
      
      const char* insertSql = "INSERT OR REPLACE INTO ProfileExItem (indexid, delay, speed, sort, message, consecutive_failures) VALUES (?, ?, '0', '0', ?, ?);";
      const char* selectSql = "SELECT consecutive_failures FROM ProfileExItem WHERE IndexId = ?";
      
      int totalSize = static_cast<int>(results.size());
      
      for (int batchStart = 0; batchStart < totalSize; batchStart += SUB_BATCH_SIZE) {
          int batchEnd = batchStart + SUB_BATCH_SIZE;
          if (batchEnd > totalSize) {
              batchEnd = totalSize;
          }
          
          char* errMsg = nullptr;
          if (sqlite3_exec(execDb, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
              Logger::write("updateTestResultBatch: BEGIN failed at batch " + std::to_string(batchStart) + ": " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
              sqlite3_free(errMsg);
              allOk = false;
              break;
          }
          
          sqlite3_stmt* stmt = nullptr;
          if (sqlite3_prepare_v2(execDb, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
              Logger::write("updateTestResultBatch: prepare failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
              sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
              allOk = false;
              break;
          }
          
          sqlite3_stmt* selStmt = nullptr;
          if (sqlite3_prepare_v2(execDb, selectSql, -1, &selStmt, nullptr) != SQLITE_OK) {
              Logger::write("updateTestResultBatch: select prepare failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
              sqlite3_finalize(stmt);
              sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
              allOk = false;
              break;
          }
          
          for (int i = batchStart; i < batchEnd; ++i) {
              const std::string& indexid = std::get<0>(results[i]);
              long latencyMs = std::get<1>(results[i]);
              bool success = std::get<2>(results[i]);
              const std::string& curlMsg = std::get<3>(results[i]);
              
              // Fetch current failure count
              sqlite3_bind_text(selStmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
              int failures = 0;
              if (sqlite3_step(selStmt) == SQLITE_ROW) {
                  failures = sqlite3_column_int(selStmt, 0);
              }
              sqlite3_reset(selStmt);
              sqlite3_clear_bindings(selStmt);
              
              std::string message;
              if (success) {
                  message = "OK";
              } else {
                  message = curlMsg.empty() ? "FAILED" : curlMsg;
              }
              
              std::string delayStr = (success && latencyMs >= 0) ? std::to_string(latencyMs / 10) : "-1";
              int newFailures = success ? 0 : failures + 1;
              
              sqlite3_bind_text(stmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
              sqlite3_bind_text(stmt, 2, delayStr.c_str(), -1, SQLITE_TRANSIENT);
              sqlite3_bind_text(stmt, 3, message.c_str(), -1, SQLITE_TRANSIENT);
              sqlite3_bind_int(stmt, 4, newFailures);
              
              int rc = sqlite3_step(stmt);
              sqlite3_reset(stmt);
              sqlite3_clear_bindings(stmt);
              
              if (rc != SQLITE_DONE) {
                  Logger::write("updateTestResultBatch: insert failed for " + indexid + ": " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
                  allOk = false;
              }
          }
          
          sqlite3_finalize(stmt);
          sqlite3_finalize(selStmt);
          
          if (!allOk) {
              sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
              break;
          }
          
          if (sqlite3_exec(execDb, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
              Logger::write("updateTestResultBatch: COMMIT failed at batch " + std::to_string(batchStart) + ": " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
              sqlite3_free(errMsg);
              sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
              allOk = false;
              break;
          }
      }
      
      return allOk;
  }

} // namespace models
} // namespace db
