#include <iostream>
#include <sqlite3.h>
#include "../../include/Subitem.h"

int main() {
    sqlite3* db;
    if (sqlite3_open("E:/eclipse_workspace/multiple_thread_validproxy/test/guiNDB.db", &db) != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return 1;
    }
    
    // Create SubItem table
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS SubItem (
            Id TEXT PRIMARY KEY,
            Remarks TEXT,
            Url TEXT,
            MoreUrl TEXT,
            Enabled INTEGER DEFAULT 1,
            UserAgent TEXT,
            Sort TEXT,
            Filter TEXT,
            AutoUpdateInterval TEXT,
            UpdateTime TEXT,
            ConvertTarget TEXT,
            PrevProfile TEXT,
            NextProfile TEXT,
            PreSocksPort TEXT,
            Memo TEXT
        );
    )";
    
    char* errMsg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "SQL error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        sqlite3_close(db);
        return 1;
    }
    
    // Insert test subscriptions
    const char* insertSql = R"(
        INSERT OR REPLACE INTO SubItem (Id, Remarks, Url, Enabled, UpdateTime) VALUES
            ('sub001', 'Test Subscription 1', 'https://example.com/sub1', 1, '1715000000'),
            ('sub002', 'Test Subscription 2', 'https://example.com/sub2', 0, '1715001000'),
            ('sub003', 'Test Subscription 3', 'https://example.com/sub3', 1, '1715002000');
    )";
    
    if (sqlite3_exec(db, insertSql, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "Insert error: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        sqlite3_close(db);
        return 1;
    }
    
    // Verify
    db::models::SubitemDAO dao(db);
    auto subs = dao.getAll();
    std::cout << "Found " << subs.size() << " subscriptions:" << std::endl;
    for (const auto& sub : subs) {
        std::cout << "  ID: " << sub.id << ", Remarks: " << sub.remarks 
                  << ", Enabled: " << sub.enabled << std::endl;
    }
    
    sqlite3_close(db);
    return 0;
}