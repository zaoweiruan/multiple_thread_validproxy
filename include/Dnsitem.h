#ifndef DB_DNSITEM_H
#define DB_DNSITEM_H

#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>
#include <iostream>
#include <sstream>

namespace db {
namespace models {

struct Dnsitem {
  std::string id;  // Id
  std::string remarks;  // Remarks
  std::string enabled;  // Enabled
  std::string coretype;  // CoreType
  std::string usesystemhosts;  // UseSystemHosts
  std::string normaldns;  // NormalDNS
  std::string tundns;  // TunDNS
  std::string domainstrategy4freedom;  // DomainStrategy4Freedom
  std::string domaindnsaddress;  // DomainDNSAddress

  Dnsitem() = default;

  static Dnsitem fromStmt(sqlite3_stmt* stmt) {
    Dnsitem obj;
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
    // UseSystemHosts
    text = (const char*)sqlite3_column_text(stmt, 4);
    obj.usesystemhosts = text ? text : "";
    // NormalDNS
    text = (const char*)sqlite3_column_text(stmt, 5);
    obj.normaldns = text ? text : "";
    // TunDNS
    text = (const char*)sqlite3_column_text(stmt, 6);
    obj.tundns = text ? text : "";
    // DomainStrategy4Freedom
    text = (const char*)sqlite3_column_text(stmt, 7);
    obj.domainstrategy4freedom = text ? text : "";
    // DomainDNSAddress
    text = (const char*)sqlite3_column_text(stmt, 8);
    obj.domaindnsaddress = text ? text : "";
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
    oss << "\"UseSystemHosts\": ";
    oss << usesystemhosts;  // 简化输出
    oss << ", ";
    oss << "\"NormalDNS\": ";
    oss << normaldns;  // 简化输出
    oss << ", ";
    oss << "\"TunDNS\": ";
    oss << tundns;  // 简化输出
    oss << ", ";
    oss << "\"DomainStrategy4Freedom\": ";
    oss << domainstrategy4freedom;  // 简化输出
    oss << ", ";
    oss << "\"DomainDNSAddress\": ";
    oss << domaindnsaddress;  // 简化输出
    oss << "}";
    return oss.str();
  }
};

class DnsitemDAO {
private:
  sqlite3* db_;

public:
  explicit DnsitemDAO(sqlite3* db) : db_(db) {}

  std::vector<Dnsitem> getAll() {
    std::vector<Dnsitem> result;
    const char* sql = "SELECT * FROM DNSItem;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      std::cerr << "SQL错误: " << sqlite3_errmsg(db_) << std::endl;
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      result.push_back(Dnsitem::fromStmt(stmt));
    }

    sqlite3_finalize(stmt);
    return result;
  }
};

} // namespace models
} // namespace db

#endif // DB_DNSITEM_H