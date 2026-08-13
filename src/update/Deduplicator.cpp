#include "update/Deduplicator.h"
#include "Profileitem.h"
#include "Subitem.h"
#include "Logger.h"
#include "Utils.h"

namespace update {

Deduplicator::Deduplicator(sqlite3* db, const config::AppConfig& config)
    : db_(db), config_(config) {
}

bool Deduplicator::deduplicate() {
    Logger::write("========================================", LogLevel::INFO);
    Logger::write("INFO: Starting Deduplication", LogLevel::INFO);
    Logger::write("========================================", LogLevel::INFO);
    
    beforeCount_ = countAllProxies();
    Logger::write("Total proxies before: " + std::to_string(beforeCount_), LogLevel::REPORT);
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("ERROR: Failed to begin transaction - " + std::string(errMsg), LogLevel::ERR);
        sqlite3_free(errMsg);
        return false;
    }
    
    Logger::write("Phase 1/6 - Marking working proxies with protected subid", LogLevel::REPORT);
    protectedCount_ = deduplicatePhase0();
    Logger::write("Phase 1 completed: " + std::to_string(protectedCount_) + " proxies marked", LogLevel::REPORT);
    
    Logger::write("Phase 2/6 - Moving blacklisted proxies to blacklist subid", LogLevel::REPORT);
    blacklistedCount_ = deduplicateBlacklistPhase();
    Logger::write("Phase 2 completed: moved " + std::to_string(blacklistedCount_) + " proxies to blacklist", LogLevel::REPORT);
    
    Logger::write("Phase 3/6 - Removing invalid addresses (private IPs)", LogLevel::REPORT);
    invalidCount_ = deduplicatePhase1();
    Logger::write("Phase 3 completed: removed " + std::to_string(invalidCount_) + " proxies", LogLevel::REPORT);
    
    Logger::write("Phase 4/6 - Removing config-invalid proxies (checkRequired + non-printable Security/Id)", LogLevel::REPORT);
    configErrorCount_ = deduplicateConfigErrorPhase();
    Logger::write("Phase 4 completed: removed " + std::to_string(configErrorCount_) + " proxies", LogLevel::REPORT);
    
    Logger::write("Phase 5/6 - Removing duplicates (merged CTE)", LogLevel::REPORT);
    mergedCount_ = deduplicateMergedPhase();
    Logger::write("Phase 5 completed: removed " + std::to_string(mergedCount_) + " proxies", LogLevel::REPORT);
    
    Logger::write("Cleaning up ProfileExItem...", LogLevel::REPORT);
    cleanupProfileExItem();
    
    if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("ERROR: Failed to commit transaction - " + std::string(errMsg), LogLevel::ERR);
        sqlite3_free(errMsg);
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }
    
    afterCount_ = countAllProxies();
    
    int totalDeleted = beforeCount_ - afterCount_;
    
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Deduplication Summary", LogLevel::REPORT);
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Total deleted: " + std::to_string(totalDeleted), LogLevel::REPORT);
    Logger::write("Total remaining: " + std::to_string(afterCount_), LogLevel::REPORT);
    Logger::write("Dedup completed successfully", LogLevel::REPORT);
    
    return true;
}

int Deduplicator::countAllProxies() {
    std::string countSql = "SELECT COUNT(*) FROM ProfileItem";
    sqlite3_stmt* stmt = nullptr;
    int count = 0;
    if (sqlite3_prepare_v2(db_, countSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

int Deduplicator::deduplicatePhase0() {
    if (config_.dedup_subids.empty()) {
        Logger::write("INFO: Phase 0 skipped - no dedup_subids configured", LogLevel::INFO);
        return 0;
    }
    
    int updated = 0;
    
    std::string protectedSubId = config_.dedup_subids[0];
    
    std::string sql = "UPDATE ProfileItem SET SubId = '" + protectedSubId + "' WHERE IndexId IN (SELECT pi.IndexId FROM ProfileItem pi JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId WHERE pe.Delay > 0 AND pe.Delay != '-1')";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("ERROR: Phase0 update failed - " + std::string(errMsg), LogLevel::ERR);
        sqlite3_free(errMsg);
        return 0;
    }
    
    updated += sqlite3_changes(db_);
    
    if (config_.dedup_subids.size() > 1) {
        std::string fallbackSubId = config_.dedup_subids[1];
        
        sql = "UPDATE ProfileItem SET SubId = '" + fallbackSubId + "' WHERE SubId = '" + protectedSubId + "' AND IndexId IN (SELECT pi.IndexId FROM ProfileItem pi JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId WHERE pe.Delay <= 0 OR pe.Delay = '-1')";
        
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            Logger::write("ERROR: Phase0 fallback update failed - " + std::string(errMsg), LogLevel::ERR);
            sqlite3_free(errMsg);
            return updated;
        }
        
        updated += sqlite3_changes(db_);
        Logger::write("INFO: Phase 0 updated: " + std::to_string(updated) + " proxies (protected: " + protectedSubId + ", fallback: " + fallbackSubId + ")", LogLevel::INFO);
    } else {
        Logger::write("INFO: Phase 0 updated: " + std::to_string(updated) + " proxies to subid: " + protectedSubId, LogLevel::INFO);
    }
    
    return updated;
}

int Deduplicator::deduplicatePhase1() {
    std::string subidsList;
    for (size_t i = 0; i < config_.dedup_subids.size(); ++i) {
        if (i > 0) subidsList += ", ";
        subidsList += "'" + config_.dedup_subids[i] + "'";
    }
    
    std::string sql = "DELETE FROM ProfileItem WHERE (";
    sql += "Address LIKE '10.%' OR ";
    sql += "Address LIKE '172.16.%' OR Address LIKE '172.17.%' OR Address LIKE '172.18.%' OR ";
    sql += "Address LIKE '172.19.%' OR Address LIKE '172.20.%' OR Address LIKE '172.21.%' OR ";
    sql += "Address LIKE '172.22.%' OR Address LIKE '172.23.%' OR Address LIKE '172.24.%' OR ";
    sql += "Address LIKE '172.25.%' OR Address LIKE '172.26.%' OR Address LIKE '172.27.%' OR ";
    sql += "Address LIKE '172.28.%' OR Address LIKE '172.29.%' OR Address LIKE '172.30.%' OR ";
    sql += "Address LIKE '172.31.%' OR Address LIKE '192.168.%' OR ";
    sql += "LENGTH(Address) < 5 OR Address NOT LIKE '%.%' OR Address LIKE '127.%' OR ";
    sql += "Address = '0.0.0.0' OR Address LIKE '% %' OR Address LIKE '[%' OR ";
    sql += "Address LIKE '%:%' OR Address LIKE '%[%]%' OR Address LIKE '%@%' OR ";
    sql += "Address LIKE 'http://%' OR Address LIKE 'https://%' OR Address LIKE '%.'";
    sql += " OR SubId = '' OR SubId IS NULL";
    sql += " OR (SubId NOT IN (SELECT Id FROM SubItem))";
    
    if (!config_.dedup_subids.empty()) {
        sql += " OR (StreamSecurity = '' AND SubId NOT IN (" + subidsList + "))";
    }
    sql += ")";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("ERROR: Phase1 dedup failed - " + std::string(errMsg), LogLevel::ERR);
        Logger::write("ERROR: SQL: " + sql, LogLevel::ERR);
        sqlite3_free(errMsg);
        return 0;
    }
    
    int deleted = sqlite3_changes(db_);
    Logger::write("INFO: Phase 1 deleted: " + std::to_string(deleted) + " (invalid addresses)", LogLevel::INFO);
    
    if (!config_.dedup_subids.empty()) {
        std::string sql2 = "DELETE FROM ProfileItem WHERE (";
        sql2 += "Port <= 0 OR Port > 65535 OR Port IS NULL";
        sql2 += ")";
        
        char* errMsg2 = nullptr;
        if (sqlite3_exec(db_, sql2.c_str(), nullptr, nullptr, &errMsg2) != SQLITE_OK) {
            Logger::write("ERROR: Phase1 secondary dedup failed - " + std::string(errMsg2), LogLevel::ERR);
            sqlite3_free(errMsg2);
        } else {
            deleted += sqlite3_changes(db_);
        }
    }
    
    return deleted;
}

int Deduplicator::deduplicateMergedPhase() {
    std::string subidsList;
    for (size_t i = 0; i < config_.dedup_subids.size(); ++i) {
        if (i > 0) subidsList += ", ";
        subidsList += "'" + config_.dedup_subids[i] + "'";
    }

    std::string sql = "WITH ranked AS ("
        "SELECT pi.IndexId, ROW_NUMBER() OVER ("
        "PARTITION BY LOWER(pi.Address), pi.Port, pi.ConfigType, LOWER(pi.Id), LOWER(pi.Network) "
        "ORDER BY CASE WHEN pi.SubId IN (" + subidsList + ") THEN 0 ELSE 1 END, "
        "CAST(COALESCE(pe.Delay, 0) AS INTEGER) DESC"
        ") AS rn FROM ProfileItem pi "
        "LEFT JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId"
        ") DELETE FROM ProfileItem WHERE IndexId IN ("
        "SELECT IndexId FROM ranked WHERE rn > 1"
        ")";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("ERROR: Merged dedup failed - " + std::string(errMsg), LogLevel::ERR);
        sqlite3_free(errMsg);
        return 0;
    }
    
    int deleted = sqlite3_changes(db_);
    Logger::write("INFO: Merged dedup deleted: " + std::to_string(deleted) + " (CTE, keep best per combo)", LogLevel::INFO);
    return deleted;
}

int Deduplicator::deduplicateBlacklistPhase() {
    if (!config_.blacklist_enabled) {
        Logger::write("INFO: Blacklist phase skipped - blacklist_enabled=false", LogLevel::INFO);
        return 0;
    }
    
    if (config_.blacklist_subid.empty()) {
        Logger::write("INFO: Blacklist phase skipped - blacklist_subid not configured", LogLevel::INFO);
        return 0;
    }
    
    int threshold = config_.blacklist_threshold;
    
    std::string sql = "UPDATE ProfileItem SET SubId = '" + config_.blacklist_subid + "' "
                       "WHERE IndexId IN (SELECT IndexId FROM ProfileExItem "
                       "WHERE consecutive_failures >= " + std::to_string(threshold) + ")";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("ERROR: Blacklist phase failed - " + std::string(errMsg), LogLevel::ERR);
        sqlite3_free(errMsg);
        return 0;
    }
    
    int updated = sqlite3_changes(db_);
    Logger::write("INFO: Blacklist phase moved " + std::to_string(updated) + " proxies to blacklist subid: " + config_.blacklist_subid, LogLevel::INFO);
    return updated;
}

int Deduplicator::deduplicateConfigErrorPhase() {
    db::models::ProfileitemDAO dao(db_);
    std::vector<db::models::Profileitem> all = dao.getAll("SELECT * FROM ProfileItem;");
    std::vector<std::string> failedIds;
    failedIds.reserve(all.size() / 10);
    size_t garbageCount = 0;

    for (const db::models::Profileitem& p : all) {
        bool bad = false;
        try {
            p.checkRequired();
        } catch (const std::exception& e) {
            bad = true;
            Logger::write("CONFIG_ERROR: " + p.indexid + " - " + p.address + ":" + p.port + " - " + e.what(), LogLevel::WARN);
        }
        if (!bad && (!utils::isPrintableAscii(p.security) || !utils::isPrintableAscii(p.id))) {
            // Non-printable Security/Id = binary garbage that xray can never
            // accept (e.g. base64 of a URL-encoded username). Counted in bulk,
            // no per-row log — there can be tens of thousands of such rows.
            garbageCount++;
            bad = true;
        }
        if (!bad && !utils::isPublicAddress(p.address)) {
            Logger::write("CONFIG_ERROR: " + p.indexid + " - " + p.address + ":" + p.port + " - private/invalid address", LogLevel::WARN);
            bad = true;
        }
        if (!bad && (p.configtype == "1" || p.configtype == "5") && !utils::isValidUuid(p.id)) {
            Logger::write("CONFIG_ERROR: " + p.indexid + " - " + p.address + ":" + p.port + " - invalid UUID format", LogLevel::WARN);
            bad = true;
        }
        if (!bad && p.configtype == "3" && !utils::isSupportedSsCipher(p.security)) {
            Logger::write("CONFIG_ERROR: " + p.indexid + " - " + p.address + ":" + p.port + " - unsupported SS cipher: '" + p.security + "'", LogLevel::WARN);
            bad = true;
        }
        if (bad) {
            failedIds.push_back(p.indexid);
        }
    }

    if (!failedIds.empty()) {
        dao.deleteByIndexIdsNoTx(failedIds);
    }

    Logger::write("INFO: Phase ConfigError deleted: " + std::to_string(failedIds.size()) +
                      " (checkRequired failed + non-printable Security/Id: " +
                      std::to_string(garbageCount) + ")",
                  LogLevel::INFO);
    return static_cast<int>(failedIds.size());
}

void Deduplicator::cleanupProfileExItem() {
    std::string sql = "DELETE FROM ProfileExItem WHERE IndexId NOT IN (SELECT IndexId FROM ProfileItem)";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        Logger::write("ERROR: ProfileExItem cleanup failed - " + std::string(errMsg), LogLevel::ERR);
        sqlite3_free(errMsg);
        return;
    }
    
    int deleted = sqlite3_changes(db_);
    Logger::write("INFO: ProfileExItem cleaned: " + std::to_string(deleted) + " orphaned records", LogLevel::INFO);
}

} // namespace update