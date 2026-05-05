#ifndef DB_PROFILEEXITEM_H
#define DB_PROFILEEXITEM_H

#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>
#include <iostream>
#include <sstream>

namespace db {
namespace models {

struct ProfileExItem {
  std::string indexid;  // IndexId
  std::string delay;  // Delay
  std::string speed;  // Speed
  std::string sort;  // Sort
  std::string message;  // Message
  int consecutive_failures = 0;  // 连续失败次数（>=阈值则视为黑名单）

  ProfileExItem() = default;

  static ProfileExItem fromStmt(sqlite3_stmt* stmt) {
    ProfileExItem obj;
    const char* text;
    // IndexId (column 0)
    text = (const char*)sqlite3_column_text(stmt, 0);
    obj.indexid = text ? text : "";
    // Delay (column 1)
    text = (const char*)sqlite3_column_text(stmt, 1);
    obj.delay = text ? text : "";
    // Speed (column 2)
    text = (const char*)sqlite3_column_text(stmt, 2);
    obj.speed = text ? text : "";
    // Sort (column 3)
    text = (const char*)sqlite3_column_text(stmt, 3);
    obj.sort = text ? text : "";
    // Message (column 4)
    text = (const char*)sqlite3_column_text(stmt, 4);
    obj.message = text ? text : "";
    // consecutive_failures (column 5)
    obj.consecutive_failures = sqlite3_column_int(stmt, 5);
    return obj;
  }

  std::string toString() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"IndexId\": ";
    oss << indexid;
    oss << ", ";
    oss << "\"Delay\": ";
    oss << delay;
    oss << ", ";
    oss << "\"Speed\": ";
    oss << speed;
    oss << ", ";
    oss << "\"Sort\": ";
    oss << sort;
    oss << ", ";
    oss << "\"Message\": ";
    oss << message;
    oss << ", ";
    oss << "\"consecutive_failures\": ";
    oss << consecutive_failures;
    oss << "}";
    return oss.str();
  }
};

class ProfileExItemDAO {
private:
  sqlite3* db_;

public:
  explicit ProfileExItemDAO(sqlite3* db) : db_(db) {
        // Auto-migrate on construction
        migrateTable(db);
    }

  // Migrate table: add missing columns if not exist
  static void migrateTable(sqlite3* db) {
      const char* addCols[] = {
          "ALTER TABLE ProfileExItem ADD COLUMN consecutive_failures INTEGER DEFAULT 0"
      };
      for (const auto& sql : addCols) {
          sqlite3_exec(db, sql, nullptr, nullptr, nullptr); // ignore error if column exists
      }
  }

  std::vector<ProfileExItem> getAll() {
    std::vector<ProfileExItem> result;
    const char* sql = "SELECT * FROM ProfileExItem;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      std::cerr << "SQL错误: " << sqlite3_errmsg(db_) << std::endl;
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      result.push_back(ProfileExItem::fromStmt(stmt));
    }

    sqlite3_finalize(stmt);
    return result;
  }

  bool updateTestResult(const std::string& indexid, long latencyMs, bool success, const std::string& curlMsg, sqlite3* db = nullptr) {
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
    
    // 先查询当前连续失败次数
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
      // Prepare failed, log error and use default
      std::cerr << "SQL prepare failed for select: " << sqlite3_errmsg(execDb) << std::endl;
    }
    
    int newFailures = success ? 0 : currentFailures + 1;
    
    std::ostringstream oss;
    oss << "INSERT OR REPLACE INTO ProfileExItem (indexid, delay, speed, sort, message, consecutive_failures) VALUES (";
    oss << "'" << indexid << "', ";
    oss << "'" << (success && latencyMs >= 0 ? std::to_string(latencyMs / 10) : "-1") << "', ";
    oss << "'0', '0', ";
    oss << "'" << message << "', ";
    oss << newFailures << ")";
    
    std::string sql = oss.str();
    char* errMsg = nullptr;
    if (sqlite3_exec(execDb, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
      std::cerr << "SQL insert error: " << errMsg << std::endl;
      sqlite3_free(errMsg);
      return false;
    }
    return true;
  }
};

} // namespace models
} // namespace db

#endif // DB_PROFILEEXITEM_H