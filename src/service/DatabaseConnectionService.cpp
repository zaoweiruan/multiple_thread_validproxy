#include "service/DatabaseConnectionService.h"
#include "Logger.h"

namespace service {

DatabaseConnectionService::DatabaseConnectionService() {
}

sqlite3* DatabaseConnectionService::open(const std::string& path) {
    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        Logger::write("Failed to open database: " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);
        if (db) sqlite3_close(db);
        return nullptr;
    }
    applyPragmas(db);
    return db;
}

void DatabaseConnectionService::applyPragmas(sqlite3* db) const {
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
}

bool DatabaseConnectionService::close(sqlite3* db) {
    if (!db) return false;
    int rc = sqlite3_close(db);
    return rc == SQLITE_OK;
}

bool DatabaseConnectionService::isValid(sqlite3* db) const {
    return db != nullptr;
}

} // namespace service