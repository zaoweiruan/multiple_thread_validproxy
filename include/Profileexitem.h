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

  // Validates "yyyy-MM-dd HH:mm:ss" (19 chars, strict pattern).
  // Returns false for legacy values ("OK"/"FAILED"/curlMsg/"NOT_TESTED"/empty).
  static bool isMessageTimestamp(const std::string& text);

public:
  explicit ProfileExItemDAO(sqlite3* db);
  static void migrateTable(sqlite3* db);
  std::vector<ProfileExItem> getAll();

  // ---- Message field: only two kinds of info (test time + startup time) ----

  // Returns current local time as "yyyy-MM-dd HH:mm:ss".
  static std::string currentTimeString();

  // Test-result write: replace the test side (before first '+'), keep the
  // startup side (after first '+') when it is a valid timestamp.
  // Invariant: result always contains exactly one '+'.
  static std::string formatTestMessage(const std::string& existingMessage, const std::string& newTestTime);

  // Startup write: replace the startup side (after first '+'), keep the test
  // side (before first '+') when it is a valid timestamp.
  // Invariant: result always contains exactly one '+'.
  static std::string formatStartupMessage(const std::string& existingMessage, const std::string& newStartupTime);

  // Sort key for the Message column: the newer of the two timestamp sides
  // (test side before '+', startup side after). Returns "" when neither side
  // is a valid timestamp, so empty / legacy values sort last.
  static std::string messageActiveTime(const std::string& message);

  // Comparator for the Message column, ordered by messageActiveTime().
  // Returns -1/0/1; empty / legacy messages always sort after valid ones.
  static int compareMessage(const std::string& lhs, const std::string& rhs);

  // Writes "+<now>" as the startup time into ProfileExItem.message,
  // preserving the existing test time. Inserts a new row when absent.
  bool updateStartupTime(const std::string& indexid, sqlite3* db = nullptr);

  bool updateTestResult(const std::string& indexid, long latencyMs, bool success, const std::string& curlMsg, sqlite3* db = nullptr);
  bool updateTestResultBatch(const std::vector<std::tuple<std::string, long, bool, std::string>>& results, sqlite3* db = nullptr);
};

} // namespace models
} // namespace db

#endif // DB_PROFILEEXITEM_H