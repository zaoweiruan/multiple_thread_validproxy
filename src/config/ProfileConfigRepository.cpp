#include "config/ProfileConfigRepository.h"
#include "Profileitem.h"
#include "ProfileExItem.h"
#include "Logger.h"
#include "Utils.h"
#include <sstream>
#include <iomanip>

namespace config {

void bindTextOrNull(sqlite3_stmt* stmt, int idx, const std::string& val) {
    if (val.empty()) {
        sqlite3_bind_null(stmt, idx);
    } else {
        sqlite3_bind_text(stmt, idx, val.c_str(), -1, SQLITE_TRANSIENT);
    }
}

ProfileConfigRepository::ProfileConfigRepository(sqlite3* db) : db_(db) {}

std::vector<db::models::Profileitem> ProfileConfigRepository::loadProfiles(const std::string& sqlQuery) {
    db::models::ProfileitemDAO dao(db_);
    std::vector<db::models::Profileitem> profiles = dao.getAll(sqlQuery.empty() ? "SELECT * FROM ProfileItem;" : sqlQuery);
    
    Logger::write("[ConfigGenerator] SQL returned " + std::to_string(profiles.size()) + " profiles", LogLevel::INFO);
    
    std::vector<db::models::Profileitem> validProfiles;
    for (db::models::Profileitem& p : profiles) {
        if (p.network.empty()) {
            Logger::write("[ConfigGenerator] Using default network 'tcp' for " + p.address + ":" + p.port, LogLevel::DEBUG);
            p.network = "tcp";
        }

        if (p.network == "splithttp") {
            Logger::write("[ConfigGenerator] Mapping splithttp to xhttp for " + p.indexid, LogLevel::DEBUG);
            p.network = "xhttp";
        }

        if (!utils::isValidNetwork(p.network)) {
            Logger::write("[ConfigGenerator] Using default network 'tcp' for " + p.address + ":" + p.port +
                          " (invalid: '" + p.network + "')", LogLevel::DEBUG);
            p.network = "tcp";
        }

        validProfiles.push_back(p);
    }
    
    Logger::write("[ConfigGenerator] Valid profiles: " + std::to_string(validProfiles.size()), LogLevel::INFO);
    return validProfiles;
}

std::vector<db::models::ProfileExItem> ProfileConfigRepository::loadProfileExItems() {
    db::models::ProfileExItemDAO dao(db_);
    return dao.getAll();
}

bool ProfileConfigRepository::updateProfileExItem(const db::models::ProfileExItem& exitem) {
    std::ostringstream sql;
    sql << "INSERT OR REPLACE INTO ProfileExItem (IndexId, Delay, Speed, Sort, Message, consecutive_failures) VALUES (?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, exitem.indexid.c_str(), -1, SQLITE_TRANSIENT);
    bindTextOrNull(stmt, 2, exitem.delay);
    bindTextOrNull(stmt, 3, exitem.speed);
    bindTextOrNull(stmt, 4, exitem.sort);
    bindTextOrNull(stmt, 5, exitem.message);
    sqlite3_bind_int(stmt, 6, exitem.consecutive_failures);

    bool success = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return success;
}

} // namespace config
