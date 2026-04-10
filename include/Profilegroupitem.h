#ifndef DB_PROFILEGROUPITEM_H
#define DB_PROFILEGROUPITEM_H

#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>
#include <iostream>
#include <sstream>

namespace db {
namespace models {

struct Profilegroupitem {
  std::string indexid;  // IndexId
  std::string childitems;  // ChildItems
  std::string subchilditems;  // SubChildItems
  std::string filter;  // Filter
  std::string multipleload;  // MultipleLoad

  Profilegroupitem() = default;

  static Profilegroupitem fromStmt(sqlite3_stmt* stmt) {
    Profilegroupitem obj;
    const char* text;
    // IndexId
    text = (const char*)sqlite3_column_text(stmt, 0);
    obj.indexid = text ? text : "";
    // ChildItems
    text = (const char*)sqlite3_column_text(stmt, 1);
    obj.childitems = text ? text : "";
    // SubChildItems
    text = (const char*)sqlite3_column_text(stmt, 2);
    obj.subchilditems = text ? text : "";
    // Filter
    text = (const char*)sqlite3_column_text(stmt, 3);
    obj.filter = text ? text : "";
    // MultipleLoad
    text = (const char*)sqlite3_column_text(stmt, 4);
    obj.multipleload = text ? text : "";
    return obj;
  }

  std::string toString() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"IndexId\": ";
    oss << indexid;  // 简化输出
    oss << ", ";
    oss << "\"ChildItems\": ";
    oss << childitems;  // 简化输出
    oss << ", ";
    oss << "\"SubChildItems\": ";
    oss << subchilditems;  // 简化输出
    oss << ", ";
    oss << "\"Filter\": ";
    oss << filter;  // 简化输出
    oss << ", ";
    oss << "\"MultipleLoad\": ";
    oss << multipleload;  // 简化输出
    oss << "}";
    return oss.str();
  }
};

class ProfilegroupitemDAO {
private:
  sqlite3* db_;

public:
  explicit ProfilegroupitemDAO(sqlite3* db) : db_(db) {}

  std::vector<Profilegroupitem> getAll() {
    std::vector<Profilegroupitem> result;
    const char* sql = "SELECT * FROM ProfileGroupItem;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      std::cerr << "SQL错误: " << sqlite3_errmsg(db_) << std::endl;
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      result.push_back(Profilegroupitem::fromStmt(stmt));
    }

    sqlite3_finalize(stmt);
    return result;
  }
};

} // namespace models
} // namespace db

#endif // DB_PROFILEGROUPITEM_H