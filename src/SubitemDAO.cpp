#include <cstdlib>
#include "i18n/I18n.h"
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
      Logger::write(i18n::u8(i18n::StrId::Db_SqlError) + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
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
      Logger::write(i18n::u8(i18n::StrId::Db_SqlError) + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
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
    const char* sql = "UPDATE SubItem SET Remarks = ?, Url = ?, Enabled = ?, "
                      "UserAgent = ?, AutoUpdateInterval = ? WHERE Id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::write("updateSubitem prepare error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return false;
    }
    sqlite3_bind_text(stmt, 1, sub.remarks.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sub.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, std::stoi(sub.enabled));
    sqlite3_bind_text(stmt, 4, sub.useragent.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, sub.autoupdateinterval.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, sub.id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        Logger::write("updateSubitem step error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return false;
    }
    return true;
  }

bool SubitemDAO::upsertSubitem(const Subitem& sub) {
    const char* sql =
        "INSERT INTO SubItem (Id, Remarks, Url, MoreUrl, Enabled, UserAgent, Sort, "
        "Filter, AutoUpdateInterval, UpdateTime, ConvertTarget, PrevProfile, "
        "NextProfile, PreSocksPort, Memo) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(Id) DO UPDATE SET "
        "Remarks=excluded.Remarks, Url=excluded.Url, MoreUrl=excluded.MoreUrl, "
        "Enabled=excluded.Enabled, UserAgent=excluded.UserAgent, Sort=excluded.Sort, "
        "Filter=excluded.Filter, AutoUpdateInterval=excluded.AutoUpdateInterval, "
        "UpdateTime=excluded.UpdateTime, ConvertTarget=excluded.ConvertTarget, "
        "PrevProfile=excluded.PrevProfile, NextProfile=excluded.NextProfile, "
        "PreSocksPort=excluded.PreSocksPort, Memo=excluded.Memo;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::write("upsertSubitem prepare error: " + std::string(sqlite3_errmsg(db_)),
                      LogLevel::ERR);
        return false;
    }
    sqlite3_bind_text(stmt, 1, sub.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, sub.remarks.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sub.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, sub.moreurl.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, sub.enabled.empty() ? 0 : std::atoi(sub.enabled.c_str()));
    sqlite3_bind_text(stmt, 6, sub.useragent.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, sub.sort.empty() ? 0 : std::atoi(sub.sort.c_str()));
    sqlite3_bind_text(stmt, 8, sub.filter.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9, sub.autoupdateinterval.empty()
                                      ? 0
                                      : std::atoi(sub.autoupdateinterval.c_str()));
    sqlite3_bind_int64(stmt, 10, sub.updatetime.empty()
                                         ? 0
                                         : static_cast<sqlite3_int64>(
                                                   std::atoll(sub.updatetime.c_str())));
    sqlite3_bind_text(stmt, 11, sub.converttarget.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, sub.prevprofile.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, sub.nextprofile.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 14, sub.presocksport.empty()
                                       ? 0
                                       : std::atoi(sub.presocksport.c_str()));
    sqlite3_bind_text(stmt, 15, sub.memo.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        Logger::write("upsertSubitem step error: " + std::string(sqlite3_errmsg(db_)),
                      LogLevel::ERR);
        return false;
    }
    return true;
}

bool SubitemDAO::deleteById(const std::string& id) {
    const char* sql = "DELETE FROM SubItem WHERE Id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::write("deleteById prepare error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return false;
    }
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        Logger::write("deleteById step error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return false;
    }
    return sqlite3_changes(db_) > 0;
  }

} // namespace models
} // namespace db
