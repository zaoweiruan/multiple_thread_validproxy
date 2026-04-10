#ifndef DB_FULLCONFIGTEMPLATEITEM_H
#define DB_FULLCONFIGTEMPLATEITEM_H

#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>
#include <iostream>
#include <sstream>

namespace db {
namespace models {

struct Fullconfigtemplateitem {
  std::string id;  // Id
  std::string remarks;  // Remarks
  std::string enabled;  // Enabled
  std::string coretype;  // CoreType
  std::string config;  // Config
  std::string tunconfig;  // TunConfig
  std::string addproxyonly;  // AddProxyOnly
  std::string proxydetour;  // ProxyDetour

  Fullconfigtemplateitem() = default;

  static Fullconfigtemplateitem fromStmt(sqlite3_stmt* stmt) {
    Fullconfigtemplateitem obj;
    const char* text;
    // Id
    text = (const char*)sqlite3_column_text(stmt, 0);
    obj.id = text ? text : "";
    // Remarks
    text = (const char*)sqlite3_column_text(stmt, 1);
    obj.remarks = text ? text : "";
    // Enabled
    text = (const char*)sqlite3_column_text(stmt, 2);
    obj.enabled = text ? text : "";
    // CoreType
    text = (const char*)sqlite3_column_text(stmt, 3);
    obj.coretype = text ? text : "";
    // Config
    text = (const char*)sqlite3_column_text(stmt, 4);
    obj.config = text ? text : "";
    // TunConfig
    text = (const char*)sqlite3_column_text(stmt, 5);
    obj.tunconfig = text ? text : "";
    // AddProxyOnly
    text = (const char*)sqlite3_column_text(stmt, 6);
    obj.addproxyonly = text ? text : "";
    // ProxyDetour
    text = (const char*)sqlite3_column_text(stmt, 7);
    obj.proxydetour = text ? text : "";
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
    oss << "\"Enabled\": ";
    oss << enabled;  // 简化输出
    oss << ", ";
    oss << "\"CoreType\": ";
    oss << coretype;  // 简化输出
    oss << ", ";
    oss << "\"Config\": ";
    oss << config;  // 简化输出
    oss << ", ";
    oss << "\"TunConfig\": ";
    oss << tunconfig;  // 简化输出
    oss << ", ";
    oss << "\"AddProxyOnly\": ";
    oss << addproxyonly;  // 简化输出
    oss << ", ";
    oss << "\"ProxyDetour\": ";
    oss << proxydetour;  // 简化输出
    oss << "}";
    return oss.str();
  }
};

class FullconfigtemplateitemDAO {
private:
  sqlite3* db_;

public:
  explicit FullconfigtemplateitemDAO(sqlite3* db) : db_(db) {}

  std::vector<Fullconfigtemplateitem> getAll() {
    std::vector<Fullconfigtemplateitem> result;
    const char* sql = "SELECT * FROM FullConfigTemplateItem;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      std::cerr << "SQL错误: " << sqlite3_errmsg(db_) << std::endl;
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      result.push_back(Fullconfigtemplateitem::fromStmt(stmt));
    }

    sqlite3_finalize(stmt);
    return result;
  }
};

} // namespace models
} // namespace db

#endif // DB_FULLCONFIGTEMPLATEITEM_H