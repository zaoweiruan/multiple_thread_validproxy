#ifndef DB_SUBITEM_H
#define DB_SUBITEM_H

#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>
#include <iostream>
#include <sstream>

namespace db {
namespace models {

struct Subitem {
  std::string id;  // Id
  std::string remarks;  // Remarks
  std::string url;  // Url
  std::string moreurl;  // MoreUrl
  std::string enabled;  // Enabled
  std::string useragent;  // UserAgent
  std::string sort;  // Sort
  std::string filter;  // Filter
  std::string autoupdateinterval;  // AutoUpdateInterval
  std::string updatetime;  // UpdateTime
  std::string converttarget;  // ConvertTarget
  std::string prevprofile;  // PrevProfile
  std::string nextprofile;  // NextProfile
  std::string presocksport;  // PreSocksPort
  std::string memo;  // Memo

  Subitem() = default;

  static Subitem fromStmt(sqlite3_stmt* stmt) {
    Subitem obj;
    const char* text;
    // Id
    text = (const char*)sqlite3_column_text(stmt, 0);
    obj.id = text ? text : "";
    // Remarks
    text = (const char*)sqlite3_column_text(stmt, 1);
    obj.remarks = text ? text : "";
    // Url
    text = (const char*)sqlite3_column_text(stmt, 2);
    obj.url = text ? text : "";
    // MoreUrl
    text = (const char*)sqlite3_column_text(stmt, 3);
    obj.moreurl = text ? text : "";
    // Enabled
    text = (const char*)sqlite3_column_text(stmt, 4);
    obj.enabled = text ? text : "";
    // UserAgent
    text = (const char*)sqlite3_column_text(stmt, 5);
    obj.useragent = text ? text : "";
    // Sort
    text = (const char*)sqlite3_column_text(stmt, 6);
    obj.sort = text ? text : "";
    // Filter
    text = (const char*)sqlite3_column_text(stmt, 7);
    obj.filter = text ? text : "";
    // AutoUpdateInterval
    text = (const char*)sqlite3_column_text(stmt, 8);
    obj.autoupdateinterval = text ? text : "";
    // UpdateTime
    text = (const char*)sqlite3_column_text(stmt, 9);
    obj.updatetime = text ? text : "";
    // ConvertTarget
    text = (const char*)sqlite3_column_text(stmt, 10);
    obj.converttarget = text ? text : "";
    // PrevProfile
    text = (const char*)sqlite3_column_text(stmt, 11);
    obj.prevprofile = text ? text : "";
    // NextProfile
    text = (const char*)sqlite3_column_text(stmt, 12);
    obj.nextprofile = text ? text : "";
    // PreSocksPort
    text = (const char*)sqlite3_column_text(stmt, 13);
    obj.presocksport = text ? text : "";
    // Memo
    text = (const char*)sqlite3_column_text(stmt, 14);
    obj.memo = text ? text : "";
    return obj;
  }

  std::string toString() const {
    std::ostringstream oss;
    oss << "{";
    oss << "\"Id\": ";
    oss << id;  // 简化输出
    oss << ", ";
    oss << "\"Remarks\": ";
    oss << remarks;  // 简化输出
    oss << ", ";
    oss << "\"Url\": ";
    oss << url;  // 简化输出
    oss << ", ";
    oss << "\"MoreUrl\": ";
    oss << moreurl;  // 简化输出
    oss << ", ";
    oss << "\"Enabled\": ";
    oss << enabled;  // 简化输出
    oss << ", ";
    oss << "\"UserAgent\": ";
    oss << useragent;  // 简化输出
    oss << ", ";
    oss << "\"Sort\": ";
    oss << sort;  // 简化输出
    oss << ", ";
    oss << "\"Filter\": ";
    oss << filter;  // 简化输出
    oss << ", ";
    oss << "\"AutoUpdateInterval\": ";
    oss << autoupdateinterval;  // 简化输出
    oss << ", ";
    oss << "\"UpdateTime\": ";
    oss << updatetime;  // 简化输出
    oss << ", ";
    oss << "\"ConvertTarget\": ";
    oss << converttarget;  // 简化输出
    oss << ", ";
    oss << "\"PrevProfile\": ";
    oss << prevprofile;  // 简化输出
    oss << ", ";
    oss << "\"NextProfile\": ";
    oss << nextprofile;  // 简化输出
    oss << ", ";
    oss << "\"PreSocksPort\": ";
    oss << presocksport;  // 简化输出
    oss << ", ";
    oss << "\"Memo\": ";
    oss << memo;  // 简化输出
    oss << "}";
    return oss.str();
  }
};

class SubitemDAO {
private:
  sqlite3* db_;

public:
  explicit SubitemDAO(sqlite3* db) : db_(db) {}

  std::vector<Subitem> getAll() {
    std::vector<Subitem> result;
    const char* sql = "SELECT * FROM SubItem;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      std::cerr << "SQL错误: " << sqlite3_errmsg(db_) << std::endl;
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      result.push_back(Subitem::fromStmt(stmt));
    }

    sqlite3_finalize(stmt);
    return result;
  }

  std::vector<Subitem> getEnabledSubscriptions() {
    std::vector<Subitem> result;
    const char* sql = "SELECT * FROM SubItem WHERE Enabled = 'true' AND Url != '' AND Url IS NOT NULL;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      std::cerr << "SQL错误: " << sqlite3_errmsg(db_) << std::endl;
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      result.push_back(Subitem::fromStmt(stmt));
    }

    sqlite3_finalize(stmt);
    return result;
  }
};

} // namespace models
} // namespace db

#endif // DB_SUBITEM_H