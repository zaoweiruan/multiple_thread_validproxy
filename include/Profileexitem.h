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

struct Profileexitem {
  std::string indexid;  // IndexId
  std::string delay;  // Delay
  std::string speed;  // Speed
  std::string sort;  // Sort
  std::string message;  // Message

  Profileexitem() = default;

  static Profileexitem fromStmt(sqlite3_stmt* stmt) {
    Profileexitem obj;
    const char* text;
    // IndexId
    text = (const char*)sqlite3_column_text(stmt, 0);
    obj.indexid = text ? text : "";
    // Delay
    text = (const char*)sqlite3_column_text(stmt, 1);
    obj.delay = text ? text : "";
    // Speed
    text = (const char*)sqlite3_column_text(stmt, 2);
    obj.speed = text ? text : "";
    // Sort
    text = (const char*)sqlite3_column_text(stmt, 3);
    obj.sort = text ? text : "";
    // Message
    text = (const char*)sqlite3_column_text(stmt, 4);
    obj.message = text ? text : "";
    return obj;
  }

  std::string toString() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"IndexId\": ";
    oss << indexid;  // 简化输出
    oss << ", ";
    oss << "\"Delay\": ";
    oss << delay;  // 简化输出
    oss << ", ";
    oss << "\"Speed\": ";
    oss << speed;  // 简化输出
    oss << ", ";
    oss << "\"Sort\": ";
    oss << sort;  // 简化输出
    oss << ", ";
    oss << "\"Message\": ";
    oss << message;  // 简化输出
    oss << "}";
    return oss.str();
  }
};

class ProfileexitemDAO {
private:
  sqlite3* db_;

public:
  explicit ProfileexitemDAO(sqlite3* db) : db_(db) {}

  std::vector<Profileexitem> getAll() {
    std::vector<Profileexitem> result;
    const char* sql = "SELECT * FROM ProfileExItem;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      std::cerr << "SQL错误: " << sqlite3_errmsg(db_) << std::endl;
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      result.push_back(Profileexitem::fromStmt(stmt));
    }

    sqlite3_finalize(stmt);
    return result;
  }

  bool updateTestResult(const std::string& indexid, long latencyMs, bool success, const std::string& curlMsg) {
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
    
    std::ostringstream oss;
    oss << "INSERT OR REPLACE INTO ProfileExItem (indexid, delay, speed, sort, message) VALUES (";
    oss << "'" << indexid << "', ";
    oss << "'" << (success && latencyMs >= 0 ? std::to_string(latencyMs / 10) : "-1") << "', ";
    oss << "'0', '0', ";
    oss << "'" << message << "')";
    
    std::string sql = oss.str();
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
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