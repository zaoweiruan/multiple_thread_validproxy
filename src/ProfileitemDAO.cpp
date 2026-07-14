#include "Profileitem.h"
#include "Logger.h"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace db {
namespace models {

std::vector<Profileitem> ProfileitemDAO::getAll(const std::string& sql) {
    std::vector<Profileitem> result;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
      Logger::write("SQL错误: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      Profileitem item = Profileitem::fromStmt(stmt);
      result.push_back(std::move(item));
    }

    sqlite3_finalize(stmt);
    return result;
  }

std::unordered_map<std::string, int> ProfileitemDAO::countBySubId() {
    std::unordered_map<std::string, int> result;
    const char* sql = "SELECT SubId, COUNT(*) FROM ProfileItem GROUP BY SubId;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      Logger::write("SQL错误: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const char* subId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      int count = sqlite3_column_int(stmt, 1);
      if (subId) {
        result[subId] = count;
      }
    }
    sqlite3_finalize(stmt);
    return result;
  }

std::unordered_map<std::string, int> ProfileitemDAO::countValidBySubId() {
    std::unordered_map<std::string, int> result;
    const char* sql = "SELECT p.SubId, COUNT(DISTINCT p.IndexId) "
                      "FROM ProfileItem p "
                      "INNER JOIN ProfileExItem e ON p.IndexId = e.IndexId "
                      "WHERE CAST(e.delay AS INTEGER) > 0 "
                      "GROUP BY p.SubId;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      Logger::write("SQL错误: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
      return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const char* subId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      int count = sqlite3_column_int(stmt, 1);
      if (subId) {
        result[subId] = count;
      }
    }
    sqlite3_finalize(stmt);
    return result;
  }

std::optional<Profileitem> ProfileitemDAO::getByIndexId(const std::string& indexId) {
    const char* sql = "SELECT * FROM ProfileItem WHERE IndexId = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      return std::nullopt;
    }
    sqlite3_bind_text(stmt, 1, indexId.c_str(), -1, SQLITE_TRANSIENT);
    Profileitem item;
    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      item = Profileitem::fromStmt(stmt);
      found = true;
    }
    sqlite3_finalize(stmt);
    if (found) {
      return item;
    }
    return std::nullopt;
  }

bool ProfileitemDAO::deleteByIndexId(const std::string& indexId) {
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, "BEGIN", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("deleteByIndexId begin error: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
        sqlite3_free(errMsg);
        return false;
    }

    bool ok = true;
    {
        const char* sql = "DELETE FROM ProfileExItem WHERE IndexId = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            Logger::write("deleteByIndexId ex prepare error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }
        sqlite3_bind_text(stmt, 1, indexId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            Logger::write("deleteByIndexId ex error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
            ok = false;
        }
        sqlite3_finalize(stmt);
    }

    if (ok) {
        const char* sql = "DELETE FROM ProfileItem WHERE IndexId = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            Logger::write("deleteByIndexId prepare error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            return false;
        }
        sqlite3_bind_text(stmt, 1, indexId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            Logger::write("deleteByIndexId error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
            ok = false;
        }
        sqlite3_finalize(stmt);
    }

    if (!ok) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }

    if (sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("deleteByIndexId commit error: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
        sqlite3_free(errMsg);
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

bool ProfileitemDAO::deleteByIndexIdNoTx(const std::string& indexId) {
    bool ok = true;
    {
        const char* sql = "DELETE FROM ProfileExItem WHERE IndexId = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            Logger::write("deleteByIndexIdNoTx ex prepare error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
            return false;
        }
        sqlite3_bind_text(stmt, 1, indexId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            Logger::write("deleteByIndexIdNoTx ex error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
            ok = false;
        }
        sqlite3_finalize(stmt);
    }

    if (ok) {
        const char* sql = "DELETE FROM ProfileItem WHERE IndexId = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            Logger::write("deleteByIndexIdNoTx prepare error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
            return false;
        }
        sqlite3_bind_text(stmt, 1, indexId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            Logger::write("deleteByIndexIdNoTx error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
            ok = false;
        }
        sqlite3_finalize(stmt);
    }
    return ok;
}

bool ProfileitemDAO::deleteBySubId(const std::string& subId) {
    char* errMsg = nullptr;
    
    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("deleteBySubId: BEGIN failed: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
        sqlite3_free(errMsg);
        return false;
    }
    
    bool ok = true;
    const char* sql = "DELETE FROM ProfileItem WHERE Subid = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::write("deleteBySubId: prepare error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        ok = false;
    } else {
        sqlite3_bind_text(stmt, 1, subId.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) {
            Logger::write("deleteBySubId: step error: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
            ok = false;
        }
    }
    
    if (!ok) {
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    
    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("deleteBySubId: COMMIT failed: " + std::string(errMsg ? errMsg : "unknown"), LogLevel::ERR);
        sqlite3_free(errMsg);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    
    return sqlite3_changes(db_) > 0;
  }

} // namespace models
} // namespace db
