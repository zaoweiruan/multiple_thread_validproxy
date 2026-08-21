#include "Profileexitem.h"
#include "Logger.h"
#include "Utils.h"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <ctime>

namespace db {
namespace models {

ProfileExItemDAO::ProfileExItemDAO(sqlite3* db) : db_(db) {
    migrateTable(db);
  }

void ProfileExItemDAO::migrateTable(sqlite3* db) {
    // Add columns to ProfileExItem (existing migrations)
    const char* addCols[] = {
        "ALTER TABLE ProfileExItem ADD COLUMN consecutive_failures INTEGER DEFAULT 0",
        // ProxyScoring history aggregation columns (idempotent - column already exists is OK)
        "ALTER TABLE ProfileExItem ADD COLUMN start_count INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE ProfileExItem ADD COLUMN total_runtime_ms INTEGER NOT NULL DEFAULT 0",
        "ALTER TABLE ProfileExItem ADD COLUMN crash_count INTEGER NOT NULL DEFAULT 0"
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

std::string ProfileExItemDAO::currentTimeString() {
    std::time_t now = std::time(nullptr);
    std::tm local = {};
    localtime_s(&local, &now);
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
    return std::string(buf);
  }

bool ProfileExItemDAO::isMessageTimestamp(const std::string& text) {
    // Strict pattern: yyyy-MM-dd HH:mm:ss (19 chars)
    if (text.size() != 19) {
      return false;
    }
    static const int digitIdx[] = {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18};
    for (int idx : digitIdx) {
      if (text[idx] < '0' || text[idx] > '9') {
        return false;
      }
    }
    if (text[4] != '-' || text[7] != '-' || text[10] != ' ' || text[13] != ':' || text[16] != ':') {
      return false;
    }
    return true;
  }

std::string ProfileExItemDAO::formatTestMessage(const std::string& existingMessage, const std::string& newTestTime) {
    // Keep the startup side (after first '+') only when it is a valid timestamp.
    std::string startupPart;
    std::size_t plusPos = existingMessage.find('+');
    if (plusPos != std::string::npos && plusPos + 1 < existingMessage.size()) {
      std::string candidate = existingMessage.substr(plusPos + 1);
      if (isMessageTimestamp(candidate)) {
        startupPart = candidate;
      }
    }
    if (startupPart.empty()) {
      return newTestTime + "+";
    }
    return newTestTime + "+" + startupPart;
  }

std::string ProfileExItemDAO::formatStartupMessage(const std::string& existingMessage, const std::string& newStartupTime) {
    // Keep the test side (before first '+') only when it is a valid timestamp.
    // When the test side is empty/invalid, FILL it with the new startup time
    // (bugfix 2026-08-21): "<NS>+<NS>" instead of a blank test side, so the
    // message always carries two valid timestamps after a monitored start.
    std::string testPart;
    std::size_t plusPos = existingMessage.find('+');
    if (plusPos != std::string::npos && plusPos > 0) {
      std::string candidate = existingMessage.substr(0, plusPos);
      if (isMessageTimestamp(candidate)) {
        testPart = candidate;
      }
    }
    if (testPart.empty()) {
      return newStartupTime + "+" + newStartupTime;
    }
    return testPart + "+" + newStartupTime;
  }

std::string ProfileExItemDAO::messageActiveTime(const std::string& message) {
    // Split at the first '+' into the test side (before) and the startup side
    // (after). Without a '+', the whole string is treated as a single side.
    // ISO timestamps compare lexicographically == chronologically.
    std::string testPart;
    std::string startupPart;
    std::size_t plusPos = message.find('+');
    if (plusPos == std::string::npos) {
      if (isMessageTimestamp(message)) {
        testPart = message;
      }
    } else {
      if (plusPos > 0) {
        std::string candidate = message.substr(0, plusPos);
        if (isMessageTimestamp(candidate)) {
          testPart = candidate;
        }
      }
      if (plusPos + 1 < message.size()) {
        std::string candidate = message.substr(plusPos + 1);
        if (isMessageTimestamp(candidate)) {
          startupPart = candidate;
        }
      }
    }
    if (testPart.empty()) {
      return startupPart;
    }
    if (startupPart.empty()) {
      return testPart;
    }
    return (testPart >= startupPart) ? testPart : startupPart;
  }

int ProfileExItemDAO::compareMessage(const std::string& lhs, const std::string& rhs) {
    std::string activeA = messageActiveTime(lhs);
    std::string activeB = messageActiveTime(rhs);
    if (activeA.empty() && activeB.empty()) {
      return 0;
    }
    if (activeA.empty()) {
      return 1;  // empty / legacy sorts after any valid message
    }
    if (activeB.empty()) {
      return -1;
    }
    int raw = activeA.compare(activeB);
    return (raw > 0) - (raw < 0);
  }

bool ProfileExItemDAO::updateStartupTime(const std::string& indexid, sqlite3* db) {
    sqlite3* execDb = db ? db : db_;
    std::string now = currentTimeString();

    std::string existingMessage;
    bool rowExists = false;
    sqlite3_stmt* selectStmt = nullptr;
    const char* selectSql = "SELECT Message FROM ProfileExItem WHERE IndexId = ?";
    if (sqlite3_prepare_v2(execDb, selectSql, -1, &selectStmt, nullptr) == SQLITE_OK) {
      sqlite3_bind_text(selectStmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(selectStmt) == SQLITE_ROW) {
        const char* text = (const char*)sqlite3_column_text(selectStmt, 0);
        existingMessage = text ? text : "";
        rowExists = true;
      }
      sqlite3_finalize(selectStmt);
    } else {
      Logger::write("updateStartupTime: select prepare failed for " + indexid + ": " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
      return false;
    }

    std::string newMessage = formatStartupMessage(existingMessage, now);

    int rc = SQLITE_OK;
    if (rowExists) {
      const char* updateSql = "UPDATE ProfileExItem SET message = ? WHERE IndexId = ?";
      sqlite3_stmt* updateStmt = nullptr;
      if (sqlite3_prepare_v2(execDb, updateSql, -1, &updateStmt, nullptr) != SQLITE_OK) {
        Logger::write("updateStartupTime: update prepare failed for " + indexid + ": " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
        return false;
      }
      sqlite3_bind_text(updateStmt, 1, newMessage.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(updateStmt, 2, indexid.c_str(), -1, SQLITE_TRANSIENT);
      rc = sqlite3_step(updateStmt);
      sqlite3_finalize(updateStmt);
    } else {
      const char* insertSql = "INSERT OR REPLACE INTO ProfileExItem (indexid, delay, speed, sort, message, consecutive_failures) VALUES (?, '-1', '0', '0', ?, 0)";
      sqlite3_stmt* insertStmt = nullptr;
      if (sqlite3_prepare_v2(execDb, insertSql, -1, &insertStmt, nullptr) != SQLITE_OK) {
        Logger::write("updateStartupTime: insert prepare failed for " + indexid + ": " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
        return false;
      }
      sqlite3_bind_text(insertStmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(insertStmt, 2, newMessage.c_str(), -1, SQLITE_TRANSIENT);
      rc = sqlite3_step(insertStmt);
      sqlite3_finalize(insertStmt);
    }

    if (rc != SQLITE_DONE) {
      Logger::write("updateStartupTime: step failed for " + indexid + ": " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
      return false;
    }
    return true;
  }

bool ProfileExItemDAO::updateTestResult(const std::string& indexid, long latencyMs, bool success, const std::string& curlMsg, sqlite3* db) {
    // Only two kinds of info are written to message: test time (successful test)
    // and startup time. A failed test never overwrites the existing message.
    std::string message;
    std::string existingMessage;

    int currentFailures = 0;
    // Evaluation history columns — preserved on success, reset on failure.
    int histStartCount = 0;
    int64_t histRuntimeMs = 0;
    int histCrashCount = 0;

    sqlite3_stmt* selectStmt = nullptr;
    const char* selectSql = "SELECT consecutive_failures, Message, start_count, total_runtime_ms, crash_count FROM ProfileExItem WHERE IndexId = ?";
    sqlite3* execDb = db ? db : db_;

    if (sqlite3_prepare_v2(execDb, selectSql, -1, &selectStmt, nullptr) == SQLITE_OK) {
      sqlite3_bind_text(selectStmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
      if (sqlite3_step(selectStmt) == SQLITE_ROW) {
        currentFailures = sqlite3_column_int(selectStmt, 0);
        const char* text = (const char*)sqlite3_column_text(selectStmt, 1);
        existingMessage = text ? text : "";
        histStartCount = sqlite3_column_int(selectStmt, 2);
        histRuntimeMs = sqlite3_column_int64(selectStmt, 3);
        histCrashCount = sqlite3_column_int(selectStmt, 4);
      }
      sqlite3_finalize(selectStmt);
    } else {
      Logger::write("SQL prepare failed for select: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
    }

    if (success) {
      message = formatTestMessage(existingMessage, currentTimeString());
    } else {
      // Keep the existing message untouched (may be empty for a brand-new row).
      message = existingMessage;
      // Reset evaluation history on test failure.
      histStartCount = 0;
      histRuntimeMs = 0;
      histCrashCount = 0;
      if (!curlMsg.empty()) {
        Logger::write("[ProfileExItem] test failed for " + indexid + ": " + curlMsg, LogLevel::DEBUG);
      }
    }

    int newFailures = success ? 0 : currentFailures + 1;

    std::string delayStr = utils::isTestResultValid(success, latencyMs) ? std::to_string(latencyMs / 10) : "-1";

    const char* insertSql = "INSERT OR REPLACE INTO ProfileExItem (indexid, delay, speed, sort, message, consecutive_failures, start_count, total_runtime_ms, crash_count) VALUES (?, ?, '0', '0', ?, ?, ?, ?, ?);";
    sqlite3_stmt* insertStmt = nullptr;
    if (sqlite3_prepare_v2(execDb, insertSql, -1, &insertStmt, nullptr) != SQLITE_OK) {
        Logger::write("SQL insert prepare error: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
        return false;
    }
    sqlite3_bind_text(insertStmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insertStmt, 2, delayStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(insertStmt, 3, message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(insertStmt, 4, newFailures);
    sqlite3_bind_int(insertStmt, 5, histStartCount);
    sqlite3_bind_int64(insertStmt, 6, histRuntimeMs);
    sqlite3_bind_int(insertStmt, 7, histCrashCount);

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
      
      const char* insertSql = "INSERT OR REPLACE INTO ProfileExItem (indexid, delay, speed, sort, message, consecutive_failures, start_count, total_runtime_ms, crash_count) VALUES (?, ?, '0', '0', ?, ?, ?, ?, ?);";
      const char* selectSql = "SELECT consecutive_failures, Message, start_count, total_runtime_ms, crash_count FROM ProfileExItem WHERE IndexId = ?";
      
      int totalSize = static_cast<int>(results.size());
      
      for (int batchStart = 0; batchStart < totalSize; batchStart += SUB_BATCH_SIZE) {
          int batchEnd = batchStart + SUB_BATCH_SIZE;
          if (batchEnd > totalSize) {
              batchEnd = totalSize;
          }
          
          // Only begin a new transaction if the connection is currently in
          // autocommit mode. If the caller already has an active transaction,
          // we participate in it and leave commit/rollback to the caller.
          bool beganTransaction = false;
          if (sqlite3_get_autocommit(execDb)) {
              char* errMsg = nullptr;
              if (sqlite3_exec(execDb, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                  Logger::write("updateTestResultBatch: BEGIN failed at batch " + std::to_string(batchStart) + ": " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
                  sqlite3_free(errMsg);
                  allOk = false;
                  break;
              }
              beganTransaction = true;
          }
          
          sqlite3_stmt* stmt = nullptr;
          if (sqlite3_prepare_v2(execDb, insertSql, -1, &stmt, nullptr) != SQLITE_OK) {
              Logger::write("updateTestResultBatch: prepare failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
              if (beganTransaction) {
                  sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
              }
              allOk = false;
              break;
          }
          
          sqlite3_stmt* selStmt = nullptr;
          if (sqlite3_prepare_v2(execDb, selectSql, -1, &selStmt, nullptr) != SQLITE_OK) {
              Logger::write("updateTestResultBatch: select prepare failed: " + std::string(sqlite3_errmsg(execDb)), LogLevel::ERR);
              sqlite3_finalize(stmt);
              if (beganTransaction) {
                  sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
              }
              allOk = false;
              break;
          }
          
          for (int i = batchStart; i < batchEnd; ++i) {
              const std::string& indexid = std::get<0>(results[i]);
              long latencyMs = std::get<1>(results[i]);
              bool success = std::get<2>(results[i]);
              const std::string& curlMsg = std::get<3>(results[i]);
              
              // Fetch current failure count, existing message, and evaluation history.
              sqlite3_bind_text(selStmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
              int failures = 0;
              std::string existingMessage;
              int histStartCount = 0;
              int64_t histRuntimeMs = 0;
              int histCrashCount = 0;
              if (sqlite3_step(selStmt) == SQLITE_ROW) {
                  failures = sqlite3_column_int(selStmt, 0);
                  const char* text = (const char*)sqlite3_column_text(selStmt, 1);
                  existingMessage = text ? text : "";
                  histStartCount = sqlite3_column_int(selStmt, 2);
                  histRuntimeMs = sqlite3_column_int64(selStmt, 3);
                  histCrashCount = sqlite3_column_int(selStmt, 4);
              }
              sqlite3_reset(selStmt);
              sqlite3_clear_bindings(selStmt);
              
              // Only two kinds of info: test time (on success) + startup time.
              // A failed test never overwrites the existing message.
              std::string message;
              if (success) {
                  message = formatTestMessage(existingMessage, currentTimeString());
              } else {
                  message = existingMessage;
                  // Reset evaluation history on test failure.
                  histStartCount = 0;
                  histRuntimeMs = 0;
                  histCrashCount = 0;
                  if (!curlMsg.empty()) {
                      Logger::write("[ProfileExItem] test failed for " + indexid + ": " + curlMsg, LogLevel::DEBUG);
                  }
              }
              
              std::string delayStr = utils::isTestResultValid(success, latencyMs) ? std::to_string(latencyMs / 10) : "-1";
              int newFailures = success ? 0 : failures + 1;
              
              sqlite3_bind_text(stmt, 1, indexid.c_str(), -1, SQLITE_TRANSIENT);
              sqlite3_bind_text(stmt, 2, delayStr.c_str(), -1, SQLITE_TRANSIENT);
              sqlite3_bind_text(stmt, 3, message.c_str(), -1, SQLITE_TRANSIENT);
              sqlite3_bind_int(stmt, 4, newFailures);
              sqlite3_bind_int(stmt, 5, histStartCount);
              sqlite3_bind_int64(stmt, 6, histRuntimeMs);
              sqlite3_bind_int(stmt, 7, histCrashCount);
              
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
              if (beganTransaction) {
                  sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
              }
              break;
          }
          
          if (beganTransaction) {
              char* errMsg = nullptr;
              if (sqlite3_exec(execDb, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                  Logger::write("updateTestResultBatch: COMMIT failed at batch " + std::to_string(batchStart) + ": " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
                  sqlite3_free(errMsg);
                  sqlite3_exec(execDb, "ROLLBACK;", nullptr, nullptr, nullptr);
                  allOk = false;
                  break;
              }
          }
      }
      
      return allOk;
  }

} // namespace models
} // namespace db
