#ifndef DB_PROFILEEXITEM_H
#define DB_PROFILEEXITEM_H

#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>
#include <iostream>
#include <sstream>
#include "Logger.h"

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
  explicit ProfileExItemDAO(sqlite3* db);
  static void migrateTable(sqlite3* db);
  std::vector<ProfileExItem> getAll();
  bool updateTestResult(const std::string& indexid, long latencyMs, bool success, const std::string& curlMsg, sqlite3* db = nullptr);
};

} // namespace models
} // namespace db

#endif // DB_PROFILEEXITEM_H