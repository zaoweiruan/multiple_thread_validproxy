#include "Subitem.h"
#include "Logger.h"
#include <sqlite3.h>
#include <string>
#include <vector>

namespace db {
namespace models {

std::vector<Subitem> SubitemDAO::getAll() {
    std::vector<Subitem> result;
    const char* sql = "SELECT * FROM SubItem;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      Logger::write("SQL错误: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      result.push_back(Subitem::fromStmt(stmt));
    }

    sqlite3_finalize(stmt);
    return result;
  }

std::vector<Subitem> SubitemDAO::getEnabledSubscriptions() {
    std::vector<Subitem> result;
    const char* sql = "SELECT * FROM SubItem WHERE Enabled = 1 AND Url != '' AND Url IS NOT NULL;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      Logger::write("SQL错误: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      result.push_back(Subitem::fromStmt(stmt));
    }

    sqlite3_finalize(stmt);
    return result;
  }

bool SubitemDAO::updateEnabled(const std::string& id, bool enabled) {
    const char* sql = "UPDATE SubItem SET Enabled = ? WHERE Id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      Logger::write("SQL error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
      return false;
    }
    sqlite3_bind_int(stmt, 1, enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
      Logger::write("SQL error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
      return false;
    }
    return true;
  }

bool SubitemDAO::updateSubitem(const Subitem& sub) {
    auto esc = [](const std::string& s) -> std::string {
      std::string out;
      out.reserve(s.size());
      for (char c : s) { if (c == '\'') out += "''"; else out += c; }
      return out;
    };

    std::string sql =
        "UPDATE SubItem SET "
        "Remarks = '" + esc(sub.remarks) + "', "
        "Url = '" + esc(sub.url) + "', "
        "Enabled = " + sub.enabled + ", "
        "UserAgent = '" + esc(sub.useragent) + "', "
        "AutoUpdateInterval = '" + esc(sub.autoupdateinterval) + "' "
        "WHERE Id = '" + esc(sub.id) + "';";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
      Logger::write("SQL error: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
      sqlite3_free(errMsg);
      return false;
    }
    return true;
  }

std::string SubitemDAO::escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) { if (c == '\'') out += "''"; else out += c; }
    return out;
  }

bool SubitemDAO::deleteById(const std::string& id) {
    std::string sql = "DELETE FROM SubItem WHERE Id = '" + escape(id) + "';";
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
      Logger::write("Delete subscription error: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
      sqlite3_free(errMsg);
      return false;
    }
    return sqlite3_changes(db_) > 0;
  }

} // namespace models
} // namespace db
