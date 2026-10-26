#ifndef SQL_QUERY_DAO_H
#define SQL_QUERY_DAO_H

#include <string>
#include <vector>
#include <sqlite3.h>

namespace db {

/// SqlQuery data model — a saved SQL query statement
struct SqlQueryItem {
    int id{0};
    std::string name;
    std::string sql_text;
    std::string scope{"all"};       // "all" | "by_subid"
    std::string description;
    bool is_default{false};
    std::string created_at;
    std::string updated_at;
};

/// DAO for SqlQuery table
class SqlQueryDAO {
public:
    explicit SqlQueryDAO(sqlite3* db);
    ~SqlQueryDAO() = default;

    // ---- instance wrappers (delegate to static) ----
    bool createTableIfNotExists() const;
    bool createTable() const { return createTable(db_); }

    int seedDefaults(const std::string& sqlAll, const std::string& sqlBySubid) const;

    std::vector<SqlQueryItem> getAll(const std::string& scope = "") const;
    SqlQueryItem getById(int id) const;

    int insert(const SqlQueryItem& item) const;
    bool update(const SqlQueryItem& item) const;
    bool remove(int id) const;

    bool setDefault(int id) const;

    // ---- static API (spec-compatible) ----
    static bool createTable(sqlite3* db);
    static bool createTableIfNotExists(sqlite3* db) { return createTable(db); }

    static int seedDefaults(sqlite3* db, const std::string& sqlAll, const std::string& sqlBySubid);

    static std::vector<SqlQueryItem> getAll(sqlite3* db, const std::string& scope = "");
    static SqlQueryItem getById(sqlite3* db, int id);

    static int insert(sqlite3* db, const SqlQueryItem& item);
    static bool update(sqlite3* db, const SqlQueryItem& item);
    static bool remove(sqlite3* db, int id);

    static bool setDefault(sqlite3* db, int id);
    static bool setDefault(sqlite3* db, int id, const std::string& scope);

private:
    sqlite3* db_;
    static bool prepareStatement(sqlite3* db, sqlite3_stmt** stmt, const std::string& sql);
    static void finalizeStatement(sqlite3_stmt* stmt);
};

} // namespace db

#endif // SQL_QUERY_DAO_H