#ifndef DB_ROUTINGITEM_H
#define DB_ROUTINGITEM_H

#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>
#include <iostream>
#include <sstream>

namespace db {
namespace models {

struct Routingitem {
  std::string id;  // Id
  std::string remarks;  // Remarks
  std::string url;  // Url
  std::string ruleset;  // RuleSet
  std::string rulenum;  // RuleNum
  std::string enabled;  // Enabled
  std::string locked;  // Locked
  std::string customicon;  // CustomIcon
  std::string customrulesetpath4singbox;  // CustomRulesetPath4Singbox
  std::string domainstrategy;  // DomainStrategy
  std::string domainstrategy4singbox;  // DomainStrategy4Singbox
  std::string sort;  // Sort
  std::string isactive;  // IsActive

  Routingitem() = default;

  static Routingitem fromStmt(sqlite3_stmt* stmt) {
    Routingitem obj;
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
    // RuleSet
    text = (const char*)sqlite3_column_text(stmt, 3);
    obj.ruleset = text ? text : "";
    // RuleNum
    text = (const char*)sqlite3_column_text(stmt, 4);
    obj.rulenum = text ? text : "";
    // Enabled
    text = (const char*)sqlite3_column_text(stmt, 5);
    obj.enabled = text ? text : "";
    // Locked
    text = (const char*)sqlite3_column_text(stmt, 6);
    obj.locked = text ? text : "";
    // CustomIcon
    text = (const char*)sqlite3_column_text(stmt, 7);
    obj.customicon = text ? text : "";
    // CustomRulesetPath4Singbox
    text = (const char*)sqlite3_column_text(stmt, 8);
    obj.customrulesetpath4singbox = text ? text : "";
    // DomainStrategy
    text = (const char*)sqlite3_column_text(stmt, 9);
    obj.domainstrategy = text ? text : "";
    // DomainStrategy4Singbox
    text = (const char*)sqlite3_column_text(stmt, 10);
    obj.domainstrategy4singbox = text ? text : "";
    // Sort
    text = (const char*)sqlite3_column_text(stmt, 11);
    obj.sort = text ? text : "";
    // IsActive
    text = (const char*)sqlite3_column_text(stmt, 12);
    obj.isactive = text ? text : "";
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
    oss << "\"RuleSet\": ";
    oss << ruleset;  // 简化输出
    oss << ", ";
    oss << "\"RuleNum\": ";
    oss << rulenum;  // 简化输出
    oss << ", ";
    oss << "\"Enabled\": ";
    oss << enabled;  // 简化输出
    oss << ", ";
    oss << "\"Locked\": ";
    oss << locked;  // 简化输出
    oss << ", ";
    oss << "\"CustomIcon\": ";
    oss << customicon;  // 简化输出
    oss << ", ";
    oss << "\"CustomRulesetPath4Singbox\": ";
    oss << customrulesetpath4singbox;  // 简化输出
    oss << ", ";
    oss << "\"DomainStrategy\": ";
    oss << domainstrategy;  // 简化输出
    oss << ", ";
    oss << "\"DomainStrategy4Singbox\": ";
    oss << domainstrategy4singbox;  // 简化输出
    oss << ", ";
    oss << "\"Sort\": ";
    oss << sort;  // 简化输出
    oss << ", ";
    oss << "\"IsActive\": ";
    oss << isactive;  // 简化输出
    oss << "}";
    return oss.str();
  }
};

class RoutingitemDAO {
private:
  sqlite3* db_;

public:
  explicit RoutingitemDAO(sqlite3* db) : db_(db) {}

  std::vector<Routingitem> getAll() {
    std::vector<Routingitem> result;
    const char* sql = "SELECT * FROM RoutingItem;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      std::cerr << "SQL错误: " << sqlite3_errmsg(db_) << std::endl;
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      result.push_back(Routingitem::fromStmt(stmt));
    }

    sqlite3_finalize(stmt);
    return result;
  }
};

} // namespace models
} // namespace db

#endif // DB_ROUTINGITEM_H