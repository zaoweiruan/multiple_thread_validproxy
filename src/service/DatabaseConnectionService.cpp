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

void DatabaseConnectionService::applyPragmas(sqlite3* db) {
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA cache_size=-8000", nullptr, nullptr, nullptr);      // 8MB page cache
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);     // WAL + NORMAL = fast + safe
    sqlite3_exec(db, "PRAGMA temp_store=MEMORY", nullptr, nullptr, nullptr);      // temp tables in RAM
    sqlite3_exec(db, "PRAGMA mmap_size=268435456", nullptr, nullptr, nullptr);    // 256MB memory-mapped I/O
    sqlite3_exec(db, "PRAGMA journal_size_limit=67108864", nullptr, nullptr, nullptr); // 64MB journal cap

    // Composite index for dedup phases: covers dedup key (Address, Port, ConfigType, Id, Network)
    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_profile_dedup ON ProfileItem(LOWER(Address), Port, ConfigType, LOWER(Id), LOWER(Network))", nullptr, nullptr, nullptr);
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