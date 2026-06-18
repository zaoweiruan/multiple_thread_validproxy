#include "update/Importer.h"
#include "Subitem.h"
#include "Utils.h"
#include "Logger.h"

#include <fstream>
#include <algorithm>

namespace update {

namespace {

void bindTextOrNull(sqlite3_stmt* stmt, int idx, const std::string& val) {
    if (val.empty()) {
        sqlite3_bind_null(stmt, idx);
    } else {
        sqlite3_bind_text(stmt, idx, val.c_str(), -1, SQLITE_TRANSIENT);
    }
}

bool insertSubItem(sqlite3* db, const db::models::Subitem& subitem) {
    std::string sql = "INSERT INTO SubItem (Id, Remarks, Url, MoreUrl, Enabled, "
                     "UserAgent, Sort, Filter, AutoUpdateInterval, UpdateTime, "
                     "ConvertTarget, PrevProfile, NextProfile, PreSocksPort, Memo) "
                     "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        Logger::write("ERROR: insertSubItem prepare failed: " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, subitem.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, subitem.remarks.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, subitem.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, subitem.enabled.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, subitem.sort.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, subitem.autoupdateinterval.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, subitem.updatetime.c_str(), -1, SQLITE_TRANSIENT);
    
    bindTextOrNull(stmt, 4, subitem.moreurl);
    bindTextOrNull(stmt, 6, subitem.useragent);
    bindTextOrNull(stmt, 8, subitem.filter);
    bindTextOrNull(stmt, 11, subitem.converttarget);
    bindTextOrNull(stmt, 12, subitem.prevprofile);
    bindTextOrNull(stmt, 13, subitem.nextprofile);
    bindTextOrNull(stmt, 14, subitem.presocksport);
    bindTextOrNull(stmt, 15, subitem.memo);
    
    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!success) {
        Logger::write("ERROR: insertSubItem failed for id=" + subitem.id + ": " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);
    }
    sqlite3_finalize(stmt);
    return success;
}

} // anonymous namespace

Importer::Importer(sqlite3* db, const config::AppConfig& config)
    : db_(db), config_(config) {
}

bool Importer::importFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        Logger::write("ERROR: Cannot open file: " + filePath, LogLevel::ERR);
        return false;
    }
    
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Subitem Import Starting...", LogLevel::REPORT);
    Logger::write("File: " + filePath, LogLevel::REPORT);
    Logger::write("========================================", LogLevel::REPORT);
    
    int totalLines = 0;
    int successCount = 0;
    int skippedCount = 0;
    int failedCount = 0;
    std::vector<std::string> importedList;
    std::vector<std::string> skippedList;
    std::vector<std::string> failedList;
    
    int nextSort = getNextSortValue();
    
    std::string line;
    while (std::getline(file, line)) {
        totalLines++;
        
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        if (line.empty()) continue;
        
        std::string url = line;
        std::string remarks;
        
        size_t spacePos = line.find(' ');
        if (spacePos != std::string::npos) {
            url = line.substr(0, spacePos);
            remarks = line.substr(spacePos + 1);
        }
        
        if (!utils::isValidUrlFormat(url)) {
            failedCount++;
            failedList.push_back(url + " (invalid format - no valid domain)");
            Logger::write("ERROR: Invalid URL format: " + url, LogLevel::ERR);
            continue;
        }
        
        if (!hasValidPath(url)) {
            Logger::write("WARN: URL has only domain, no path: " + url, LogLevel::WARN);
        }
        
        if (isUrlExists(url)) {
            skippedCount++;
            skippedList.push_back(url + " (already exists)");
            Logger::write("SKIPPED: URL already exists: " + url, LogLevel::WARN);
            continue;
        }
        
        db::models::Subitem subitem;
        subitem.id = utils::generateUniqueId();
        subitem.remarks = remarks.empty() ? extractRemarksFromUrl(url) : remarks;
        subitem.url = url;
        subitem.enabled = "0";
        subitem.autoupdateinterval = "1440";
        subitem.updatetime = "0";
        subitem.sort = std::to_string(nextSort);
        
        if (insertSubItem(db_, subitem)) {
            successCount++;
            importedList.push_back("[" + subitem.remarks + "] " + url);
            Logger::write("Imported: [" + subitem.remarks + "] " + url, LogLevel::INFO);
            nextSort += 10;
        } else {
            failedCount++;
            failedList.push_back(url + " (database insert failed)");
            Logger::write("ERROR: Failed to insert: " + url, LogLevel::ERR);
        }
    }
    
    file.close();
    
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Subitem Import Summary", LogLevel::REPORT);
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Total lines: " + std::to_string(totalLines), LogLevel::REPORT);
    Logger::write("Success: " + std::to_string(successCount), LogLevel::REPORT);
    Logger::write("Skipped (duplicates): " + std::to_string(skippedCount), LogLevel::REPORT);
    Logger::write("Failed (invalid format): " + std::to_string(failedCount), LogLevel::REPORT);
    Logger::write("========================================", LogLevel::REPORT);
    
    if (!importedList.empty()) {
        Logger::write("Imported URLs:", LogLevel::INFO);
        for (size_t i = 0; i < importedList.size(); i++) {
            Logger::write("  " + std::to_string(i + 1) + ". " + importedList[i], LogLevel::INFO);
        }
    }

    if (!skippedList.empty()) {
        Logger::write("========================================", LogLevel::REPORT);
        Logger::write("Skipped URLs (duplicates):", LogLevel::WARN);
        for (size_t i = 0; i < skippedList.size(); i++) {
            Logger::write("  " + std::to_string(i + 1) + ". " + skippedList[i], LogLevel::WARN);
        }
    }

    if (!failedList.empty()) {
        Logger::write("========================================", LogLevel::REPORT);
        Logger::write("Failed URLs (invalid format):", LogLevel::ERR);
        for (size_t i = 0; i < failedList.size(); i++) {
            Logger::write("  " + std::to_string(i + 1) + ". " + failedList[i], LogLevel::ERR);
        }
    }

    Logger::write("========================================", LogLevel::REPORT);
    
    return failedCount == 0;
}

bool Importer::importSingleUrl(const std::string& url) {
    Logger::write("========================================", LogLevel::REPORT);
    Logger::write("Subitem Import Starting (Single URL)...", LogLevel::REPORT);
    Logger::write("URL: " + url, LogLevel::REPORT);
    Logger::write("========================================", LogLevel::REPORT);
    
    if (!utils::isValidUrlFormat(url)) {
        Logger::write("ERROR: Invalid URL format: " + url, LogLevel::ERR);
        Logger::write("========================================", LogLevel::REPORT);
        Logger::write("Import Summary: Success=0, Skipped=0, Failed=1", LogLevel::REPORT);
        Logger::write("========================================", LogLevel::REPORT);
        return false;
    }
    
    if (!hasValidPath(url)) {
        Logger::write("WARN: URL has only domain, no path: " + url, LogLevel::WARN);
    }
    
    if (isUrlExists(url)) {
        Logger::write("SKIPPED: URL already exists: " + url, LogLevel::REPORT);
        Logger::write("========================================", LogLevel::REPORT);
        Logger::write("Import Summary: Success=0, Skipped=1, Failed=0", LogLevel::REPORT);
        Logger::write("========================================", LogLevel::REPORT);
        return true;
    }
    
    int nextSort = getNextSortValue();
    
    db::models::Subitem subitem;
    subitem.id = utils::generateUniqueId();
    subitem.remarks = extractRemarksFromUrl(url);
    subitem.url = url;
    subitem.enabled = "0";
    subitem.autoupdateinterval = "1440";
    subitem.updatetime = "0";
    subitem.sort = std::to_string(nextSort);
    
    if (insertSubItem(db_, subitem)) {
        Logger::write("Imported: [" + subitem.remarks + "] " + url, LogLevel::REPORT);
        Logger::write("========================================", LogLevel::REPORT);
        Logger::write("Import Summary: Success=1, Skipped=0, Failed=0", LogLevel::REPORT);
        Logger::write("========================================", LogLevel::REPORT);
        return true;
    } else {
        Logger::write("ERROR: Failed to insert: " + url, LogLevel::ERR);
        Logger::write("========================================", LogLevel::REPORT);
        Logger::write("Import Summary: Success=0, Skipped=0, Failed=1", LogLevel::REPORT);
        Logger::write("========================================", LogLevel::REPORT);
        return false;
    }
}

std::string Importer::extractRemarksFromUrl(const std::string& url) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return "imported";
    
    std::string pathPart = url.substr(schemeEnd + 3);
    size_t pathStart = pathPart.find('/');
    if (pathStart == std::string::npos || pathStart == 0) {
        return "imported";
    }
    
    std::string path = pathPart.substr(pathStart + 1);
    std::string domain = pathPart.substr(0, pathStart);
    
    size_t segEnd = path.find('/');
    std::string firstSeg = (segEnd != std::string::npos) ? path.substr(0, segEnd) : path;
    
    std::string filename = firstSeg;
    size_t lastSlash = firstSeg.find_last_of('/');
    if (lastSlash != std::string::npos) {
        filename = firstSeg.substr(lastSlash + 1);
    }
    
    size_t dotPos = filename.find_last_of('.');
    if (dotPos != std::string::npos) {
        filename = filename.substr(0, dotPos);
    }
    
    dotPos = firstSeg.find_last_of('.');
    if (dotPos != std::string::npos) {
        firstSeg = firstSeg.substr(0, dotPos);
    }
    
    std::string lastSeg = path;
    size_t lastSlash2 = path.find_last_of('/');
    if (lastSlash2 != std::string::npos) {
        lastSeg = path.substr(lastSlash2 + 1);
    }
    dotPos = lastSeg.find_last_of('.');
    if (dotPos != std::string::npos) {
        lastSeg = lastSeg.substr(0, dotPos);
    }
    
    std::string remarks = firstSeg;
    if (!lastSeg.empty() && lastSeg != firstSeg) {
        remarks = firstSeg + "-" + lastSeg;
    }
    
    if (remarks.empty()) {
        remarks = domain;
    }
    
    return remarks;
}

int Importer::getNextSortValue() {
    std::string sql = "SELECT MAX(CAST(Sort AS INTEGER)) FROM SubItem";
    sqlite3_stmt* stmt = nullptr;
    int maxSort = 0;
    
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            maxSort = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    
    return maxSort + 10;
}

bool Importer::isUrlExists(const std::string& url) {
    std::string sql = "SELECT COUNT(*) FROM SubItem WHERE Url = ?";
    sqlite3_stmt* stmt = nullptr;
    bool exists = false;
    
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            exists = sqlite3_column_int(stmt, 0) > 0;
        }
        sqlite3_finalize(stmt);
    }
    
    return exists;
}

bool Importer::hasValidPath(const std::string& url) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;
    
    std::string hostPart = url.substr(schemeEnd + 3);
    size_t pathStart = hostPart.find('/');
    
    return (pathStart != std::string::npos && pathStart > 0);
}

} // namespace update