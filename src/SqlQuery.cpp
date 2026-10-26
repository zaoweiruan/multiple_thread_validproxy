#include "SqlQuery.h"
#include "Logger.h"
#include <sqlite3.h>
#include <ctime>
#include <sstream>

namespace db {

SqlQueryDAO::SqlQueryDAO(sqlite3* db) : db_(db) {}

bool SqlQueryDAO::prepareStatement(sqlite3* db, sqlite3_stmt** stmt, const std::string& sql) {
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::write("[SqlQueryDAO] Failed to prepare statement: " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);
        return false;
    }
    return true;
}

void SqlQueryDAO::finalizeStatement(sqlite3_stmt* stmt) {
    if (stmt) sqlite3_finalize(stmt);
}

static std::string nowTimestamp() {
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    return std::string(buf);
}

// ---- instance wrappers ----
bool SqlQueryDAO::createTableIfNotExists() const {
    return createTable(db_);
}

int SqlQueryDAO::seedDefaults(const std::string& sqlAll, const std::string& sqlBySubid) const {
    return seedDefaults(db_, sqlAll, sqlBySubid);
}

std::vector<SqlQueryItem> SqlQueryDAO::getAll(const std::string& scope) const {
    return getAll(db_, scope);
}

SqlQueryItem SqlQueryDAO::getById(int id) const {
    return getById(db_, id);
}

int SqlQueryDAO::insert(const SqlQueryItem& item) const {
    return insert(db_, item);
}

bool SqlQueryDAO::update(const SqlQueryItem& item) const {
    return update(db_, item);
}

bool SqlQueryDAO::remove(int id) const {
    return remove(db_, id);
}

bool SqlQueryDAO::setDefault(int id) const {
    return setDefault(db_, id);
}

// ---- static implementations ----
bool SqlQueryDAO::createTable(sqlite3* db) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS SqlQuery ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL UNIQUE,"
        "sql_text TEXT NOT NULL,"
        "scope TEXT NOT NULL DEFAULT 'all',"
        "description TEXT,"
        "is_default INTEGER NOT NULL DEFAULT 0,"
        "created_at TEXT NOT NULL,"
        "updated_at TEXT NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_sql_scope ON SqlQuery(scope);";
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::write("[SqlQueryDAO] Failed to create table: " + std::string(errMsg ? errMsg : sqlite3_errmsg(db)), LogLevel::ERR);
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    return true;
}

int SqlQueryDAO::seedDefaults(sqlite3* db, const std::string& sqlAll, const std::string& sqlBySubid) {
    int inserted = 0;
    std::string ts = nowTimestamp();

    if (!sqlAll.empty()) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT OR IGNORE INTO SqlQuery (name, sql_text, scope, is_default, created_at, updated_at) VALUES (?,?,?,?,?,?)";
        if (prepareStatement(db, &stmt, sql)) {
            sqlite3_bind_text(stmt, 1, "default_all", -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, sqlAll.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, "all", -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 4, 1);
            sqlite3_bind_text(stmt, 5, ts.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, ts.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                if (sqlite3_changes(db) > 0) inserted++;
            }
            finalizeStatement(stmt);
        }
    }

    if (!sqlBySubid.empty()) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT OR IGNORE INTO SqlQuery (name, sql_text, scope, is_default, created_at, updated_at) VALUES (?,?,?,?,?,?)";
        if (prepareStatement(db, &stmt, sql)) {
            sqlite3_bind_text(stmt, 1, "default_by_subid", -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, sqlBySubid.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, "by_subid", -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 4, 1);
            sqlite3_bind_text(stmt, 5, ts.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, ts.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_DONE) {
                if (sqlite3_changes(db) > 0) inserted++;
            }
            finalizeStatement(stmt);
        }
    }
    return inserted;
}

std::vector<SqlQueryItem> SqlQueryDAO::getAll(sqlite3* db, const std::string& scope) {
    std::vector<SqlQueryItem> result;
    sqlite3_stmt* stmt = nullptr;
    std::string sql = "SELECT id, name, sql_text, scope, description, is_default, created_at, updated_at FROM SqlQuery";
    if (!scope.empty()) {
        sql += " WHERE scope = ?";
    }
    sql += " ORDER BY id ASC";
    if (!prepareStatement(db, &stmt, sql)) return result;
    if (!scope.empty()) {
        sqlite3_bind_text(stmt, 1, scope.c_str(), -1, SQLITE_TRANSIENT);
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SqlQueryItem item;
        item.id = sqlite3_column_int(stmt, 0);
        const char* n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item.name = n ? n : "";
        const char* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        item.sql_text = s ? s : "";
        const char* sc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        item.scope = sc ? sc : "all";
        const char* d = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        item.description = d ? d : "";
        item.is_default = sqlite3_column_int(stmt, 5) != 0;
        const char* c = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        item.created_at = c ? c : "";
        const char* u = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        item.updated_at = u ? u : "";
        result.push_back(item);
    }
    finalizeStatement(stmt);
    return result;
}

SqlQueryItem SqlQueryDAO::getById(sqlite3* db, int id) {
    SqlQueryItem item;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, name, sql_text, scope, description, is_default, created_at, updated_at FROM SqlQuery WHERE id = ?";
    if (!prepareStatement(db, &stmt, sql)) return item;
    sqlite3_bind_int(stmt, 1, id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        item.id = sqlite3_column_int(stmt, 0);
        const char* n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        item.name = n ? n : "";
        const char* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        item.sql_text = s ? s : "";
        const char* sc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        item.scope = sc ? sc : "all";
        const char* d = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        item.description = d ? d : "";
        item.is_default = sqlite3_column_int(stmt, 5) != 0;
        const char* c = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        item.created_at = c ? c : "";
        const char* u = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        item.updated_at = u ? u : "";
    }
    finalizeStatement(stmt);
    return item;
}

int SqlQueryDAO::insert(sqlite3* db, const SqlQueryItem& item) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO SqlQuery (name, sql_text, scope, description, is_default, created_at, updated_at) VALUES (?,?,?,?,?,?,?)";
    if (!prepareStatement(db, &stmt, sql)) return -1;
    std::string ts = nowTimestamp();
    sqlite3_bind_text(stmt, 1, item.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, item.sql_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, item.scope.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, item.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, item.is_default ? 1 : 0);
    sqlite3_bind_text(stmt, 6, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, ts.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    finalizeStatement(stmt);
    if (rc != SQLITE_DONE) {
        Logger::write("[SqlQueryDAO] Insert failed: " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);
        return -1;
    }
    return static_cast<int>(sqlite3_last_insert_rowid(db));
}

bool SqlQueryDAO::update(sqlite3* db, const SqlQueryItem& item) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE SqlQuery SET name=?, sql_text=?, scope=?, description=?, is_default=?, updated_at=? WHERE id=?";
    if (!prepareStatement(db, &stmt, sql)) return false;
    std::string ts = nowTimestamp();
    sqlite3_bind_text(stmt, 1, item.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, item.sql_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, item.scope.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, item.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, item.is_default ? 1 : 0);
    sqlite3_bind_text(stmt, 6, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, item.id);
    int rc = sqlite3_step(stmt);
    finalizeStatement(stmt);
    if (rc != SQLITE_DONE) {
        Logger::write("[SqlQueryDAO] Update failed: " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);
        return false;
    }
    return true;
}

bool SqlQueryDAO::remove(sqlite3* db, int id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM SqlQuery WHERE id = ?";
    if (!prepareStatement(db, &stmt, sql)) return false;
    sqlite3_bind_int(stmt, 1, id);
    int rc = sqlite3_step(stmt);
    finalizeStatement(stmt);
    if (rc != SQLITE_DONE) {
        Logger::write("[SqlQueryDAO] Remove failed: " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);
        return false;
    }
    return true;
}

bool SqlQueryDAO::setDefault(sqlite3* db, int id) {
    SqlQueryItem item = getById(db, id);
    if (item.id == 0) return false;
    return setDefault(db, id, item.scope);
}

bool SqlQueryDAO::setDefault(sqlite3* db, int id, const std::string& scope) {
    SqlQueryItem item = getById(db, id);
    if (item.id == 0) return false;

    std::string sc = scope.empty() ? item.scope : scope;

    // Clear existing defaults in same scope
    sqlite3_stmt* stmt = nullptr;
    const char* clearSql = "UPDATE SqlQuery SET is_default = 0 WHERE scope = ? AND id != ?";
    if (prepareStatement(db, &stmt, clearSql)) {
        sqlite3_bind_text(stmt, 1, sc.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, id);
        sqlite3_step(stmt);
        finalizeStatement(stmt);
    }

    // Set new default
    sqlite3_stmt* stmt2 = nullptr;
    const char* setSql = "UPDATE SqlQuery SET is_default = 1 WHERE id = ?";
    if (!prepareStatement(db, &stmt2, setSql)) return false;
    sqlite3_bind_int(stmt2, 1, id);
    int rc = sqlite3_step(stmt2);
    finalizeStatement(stmt2);
    if (rc != SQLITE_DONE) {
        Logger::write("[SqlQueryDAO] setDefault failed: " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);
        return false;
    }
    return true;
}

} // namespace db