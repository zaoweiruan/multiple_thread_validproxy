#ifndef DB_SERVERSTATITEM_H
#define DB_SERVERSTATITEM_H

#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>
#include <iostream>
#include <sstream>

namespace db {
namespace models {

struct Serverstatitem {
  std::string indexid;  // IndexId
  std::string totalup;  // TotalUp
  std::string totaldown;  // TotalDown
  std::string todayup;  // TodayUp
  std::string todaydown;  // TodayDown
  std::string datenow;  // DateNow

  Serverstatitem() = default;

  static Serverstatitem fromStmt(sqlite3_stmt* stmt) {
    Serverstatitem obj;
    const char* text;
    // IndexId
    text = (const char*)sqlite3_column_text(stmt, 0);
    obj.indexid = text ? text : "";
    // TotalUp
    text = (const char*)sqlite3_column_text(stmt, 1);
    obj.totalup = text ? text : "";
    // TotalDown
    text = (const char*)sqlite3_column_text(stmt, 2);
    obj.totaldown = text ? text : "";
    // TodayUp
    text = (const char*)sqlite3_column_text(stmt, 3);
    obj.todayup = text ? text : "";
    // TodayDown
    text = (const char*)sqlite3_column_text(stmt, 4);
    obj.todaydown = text ? text : "";
    // DateNow
    text = (const char*)sqlite3_column_text(stmt, 5);
    obj.datenow = text ? text : "";
    return obj;
  }

  std::string toString() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"IndexId\": ";
    oss << indexid;  // 简化输出
    oss << ", ";
    oss << "\"TotalUp\": ";
    oss << totalup;  // 简化输出
    oss << ", ";
    oss << "\"TotalDown\": ";
    oss << totaldown;  // 简化输出
    oss << ", ";
    oss << "\"TodayUp\": ";
    oss << todayup;  // 简化输出
    oss << ", ";
    oss << "\"TodayDown\": ";
    oss << todaydown;  // 简化输出
    oss << ", ";
    oss << "\"DateNow\": ";
    oss << datenow;  // 简化输出
    oss << "}";
    return oss.str();
  }
};

class ServerstatitemDAO {
private:
  sqlite3* db_;

public:
  explicit ServerstatitemDAO(sqlite3* db) : db_(db) {}

  std::vector<Serverstatitem> getAll() {
    std::vector<Serverstatitem> result;
    const char* sql = "SELECT * FROM ServerStatItem;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      std::cerr << "SQL错误: " << sqlite3_errmsg(db_) << std::endl;
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      result.push_back(Serverstatitem::fromStmt(stmt));
    }

    sqlite3_finalize(stmt);
    return result;
  }
};

} // namespace models
} // namespace db

#endif // DB_SERVERSTATITEM_H