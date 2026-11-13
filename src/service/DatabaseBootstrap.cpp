// src/service/DatabaseBootstrap.cpp
#include "service/DatabaseBootstrap.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <vector>

#include "Logger.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace service {

// Provided by the generated translation unit (external linkage).
extern const char* const kDatabaseSchemaInitSql;

namespace {

const char kSqliteMagic[16] = {'S', 'Q', 'L', 'i', 't', 'e', ' ', 'f',
                               'o', 'r', 'm', 'a', 't', ' ', '3', '\0'};

bool hasSqliteHeader(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    char buf[16] = {0};
    const size_t got = std::fread(buf, 1, sizeof(buf), f);
    std::fclose(f);
    return got == sizeof(buf) && std::memcmp(buf, kSqliteMagic, sizeof(buf)) == 0;
}

bool isNonEmptyFile(const std::string& path) {
    std::error_code ec;
    const bool exists = fs::exists(path, ec);
    if (!exists || ec) {
        return false;
    }
    const uintmax_t size = fs::file_size(path, ec);
    return !ec && size > 0;
}

std::string lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Windows reserved device names (case-insensitive, extension ignored).
bool isReservedDeviceName(const std::string& stem) {
    static const char* kReserved[] = {
            "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5",
            "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4",
            "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    const std::string upper = [&] {
        std::string t = stem;
        std::transform(t.begin(), t.end(), t.begin(), ::toupper);
        return t;
    }();
    for (const char* r : kReserved) {
        if (upper == r) {
            return true;
        }
    }
    return false;
}

bool isReparsePoint(const fs::path& p) {
#ifdef _WIN32
    const DWORD attrs = GetFileAttributesW(p.wstring().c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code ec;
    return fs::is_symlink(fs::symlink_status(p, ec)) && !ec;
#endif
}

struct Stmt {
    sqlite3_stmt* raw = nullptr;
    ~Stmt() {
        if (raw != nullptr) {
            sqlite3_finalize(raw);
        }
    }
};

struct Db {
    sqlite3* raw = nullptr;
    ~Db() {
        if (raw != nullptr) {
            sqlite3_close(raw);
        }
    }
};

bool execSql(sqlite3* db, const char* sql, std::string* error) {
    char* msg = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &msg);
    if (rc != SQLITE_OK) {
        if (error != nullptr) {
            *error = msg != nullptr ? msg : sqlite3_errmsg(db);
        }
        if (msg != nullptr) {
            sqlite3_free(msg);
        }
        return false;
    }
    if (msg != nullptr) {
        sqlite3_free(msg);
    }
    return true;
}

// Reads sqlite_master; returns the sqlite return code so callers can tell
// NOTADB/CORRUPT from BUSY/LOCKED from success.
int readSchemaVersion(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    const int prep = sqlite3_prepare_v2(db, "PRAGMA schema_version", -1, &stmt, nullptr);
    if (prep != SQLITE_OK) {
        return prep;
    }
    const int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return step == SQLITE_ROW ? SQLITE_OK : step;
}

bool tableExists(sqlite3* db, const char* table) {
    sqlite3_stmt* stmt = nullptr;
    const std::string sql =
            std::string("SELECT 1 FROM sqlite_master WHERE type='table' AND name='") + table +
            "'";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

bool columnExists(sqlite3* db, const char* table, const char* column) {
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = std::string("PRAGMA table_info(\"") + table + "\")";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(stmt, 1);
        if (name != nullptr && lower(reinterpret_cast<const char*>(name)) == lower(column)) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

struct RequiredColumn {
    const char* table;
    const char* column;
};

const RequiredColumn kRequiredColumns[] = {
        {"ProfileItem", "Region"},
        {"ProfileExItem", "consecutive_failures"},
        {"ProfileExItem", "start_count"},
        {"ProfileExItem", "total_runtime_ms"},
        {"ProfileExItem", "crash_count"},
        {"FullConfigTemplateItem", "ParentId"},
        {"FullConfigTemplateItem", "BundleChain"},
        {"FullConfigTemplateItem", "TemplateType"},
        {"FullConfigTemplateItem", "BundleVersion"},
};

const char* const kCoreTables[] = {"SubItem", "ProfileItem", "ProfileExItem",
                                   "FullConfigTemplateItem"};

// Finds <path>.bak.<timestamp>, newest mtime first. Ignores .partial.
std::string findRecoverableBackup(const std::string& path) {
    const fs::path p(path);
    const fs::path dir = p.has_parent_path() ? p.parent_path() : fs::path(".");
    const std::string prefix = p.filename().string() + ".bak.";
    std::error_code ec;

    std::string best;
    fs::file_time_type bestTime{};
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) != 0) {
            continue;
        }
        if (name.size() > 8 && name.compare(name.size() - 8, 8, ".partial") == 0) {
            continue;
        }
        const fs::file_time_type t = fs::last_write_time(entry.path(), ec);
        if (ec) {
            continue;
        }
        if (best.empty() || t > bestTime) {
            best = entry.path().string();
            bestTime = t;
        }
    }
    return best;
}

} // namespace

const char* DatabaseBootstrap::schemaInitSql() {
    return kDatabaseSchemaInitSql;
}

std::string DatabaseBootstrap::backupTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32] = {0};
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::string(buf);
}

DbInspection DatabaseBootstrap::inspect(const std::string& path) {
    DbInspection result;

    if (path.empty()) {
        result.probe = DbProbe::MissingPath;
        result.detail = "database.path is empty";
        return result;
    }

    std::error_code ec;
    const fs::path p(path);

    if (fs::exists(p, ec) && !ec && fs::is_directory(p, ec)) {
        result.probe = DbProbe::MissingDirectory;
        result.detail = "path is a directory";
        return result;
    }

    if (!fs::exists(p, ec) || ec) {
        const fs::path parent = p.has_parent_path() ? p.parent_path() : fs::path(".");
        if (!fs::exists(parent, ec) || ec) {
            result.probe = DbProbe::MissingDirectory;
            result.detail = "parent directory does not exist";
            return result;
        }
        const std::string bak = findRecoverableBackup(path);
        if (!bak.empty()) {
            result.probe = DbProbe::BackupRecoverable;
            result.backupPath = bak;
            result.detail = "leftover backup from an interrupted rebuild";
            return result;
        }
        result.probe = DbProbe::MissingFile;
        result.detail = "file does not exist";
        return result;
    }

    // Size checks.
    const uintmax_t size = fs::file_size(p, ec);
    if (!ec && size == 0) {
        if (isNonEmptyFile(path + "-wal")) {
            result.probe = DbProbe::WalPending;
            result.detail = "main file is empty but a non-empty -wal holds data";
            return result;
        }
        result.probe = DbProbe::EmptyFile;
        result.detail = "file is 0 bytes";
        return result;
    }

    Db db;
    const int rc = sqlite3_open_v2(path.c_str(), &db.raw, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
            result.probe = DbProbe::BusyOrLocked;
            result.detail = "another instance holds a lock";
            return result;
        }
        if (hasSqliteHeader(path)) {
            // Valid header, cannot open: almost always a read-only directory that
            // blocks -shm creation for a WAL database.
            result.probe = DbProbe::WALReadOnlyBlocked;
            result.detail = "valid SQLite file but the directory is not writable (WAL needs -shm)";
            return result;
        }
        result.probe = DbProbe::Inaccessible;
        result.detail = "cannot open the file for reading";
        return result;
    }

    const int schemaRc = readSchemaVersion(db.raw);
    if (schemaRc == SQLITE_BUSY || schemaRc == SQLITE_LOCKED) {
        result.probe = DbProbe::BusyOrLocked;
        result.detail = "another instance holds a write lock";
        return result;
    }
    if (schemaRc == SQLITE_NOTADB || schemaRc == SQLITE_CORRUPT) {
        result.probe = DbProbe::NotADatabase;
        result.detail = "file is not a valid SQLite database";
        return result;
    }
    if (schemaRc != SQLITE_OK) {
        result.probe = DbProbe::NotADatabase;
        result.detail = std::string("cannot read schema: ") + sqlite3_errmsg(db.raw);
        return result;
    }

    std::string missing;
    for (const char* table : kCoreTables) {
        if (!tableExists(db.raw, table)) {
            missing += std::string(missing.empty() ? "" : ", ") + table;
        }
    }
    if (!missing.empty()) {
        result.probe = DbProbe::MissingCoreTables;
        result.detail = "missing tables: " + missing;
        return result;
    }

    std::string missingColumns;
    for (const RequiredColumn& rc2 : kRequiredColumns) {
        if (!columnExists(db.raw, rc2.table, rc2.column)) {
            missingColumns += std::string(missingColumns.empty() ? "" : ", ") + rc2.table +
                              "." + rc2.column;
        }
    }
    if (!missingColumns.empty()) {
        result.probe = DbProbe::MissingCoreTables;
        result.detail = "missing columns: " + missingColumns;
        return result;
    }

    result.probe = DbProbe::Ok;
    return result;
}

bool DatabaseBootstrap::create(const std::string& path, std::string& error) {
    error.clear();
    if (path.empty()) {
        error = "database path is empty";
        return false;
    }

    const fs::path p(path);
    const fs::path parent = p.has_parent_path() ? p.parent_path() : fs::path(".");
    std::error_code ec;
    if (!fs::exists(parent, ec) || ec) {
        error = "parent directory does not exist: " + parent.string();
        return false;
    }

    const bool existedBefore = fs::exists(p, ec) && !ec;

    Db db;
    const int rc = sqlite3_open_v2(path.c_str(), &db.raw,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                           SQLITE_OPEN_FULLMUTEX,
                                   nullptr);
    if (rc != SQLITE_OK) {
        error = db.raw != nullptr ? sqlite3_errmsg(db.raw) : "cannot open database file";
        if (!existedBefore) {
            std::error_code rmEc;
            fs::remove(p, rmEc);
        }
        return false;
    }

    sqlite3_busy_timeout(db.raw, 10000);
    if (!execSql(db.raw, "PRAGMA journal_mode=WAL", &error)) {
        if (!existedBefore) {
            std::error_code rmEc;
            fs::remove(p, rmEc);
        }
        return false;
    }

    if (!execSql(db.raw, "BEGIN IMMEDIATE", &error)) {
        if (!existedBefore) {
            std::error_code rmEc;
            fs::remove(p, rmEc);
        }
        return false;
    }

    if (!execSql(db.raw, kDatabaseSchemaInitSql, &error)) {
        execSql(db.raw, "ROLLBACK", nullptr);
        if (!existedBefore) {
            std::error_code rmEc;
            fs::remove(p, rmEc);
        }
        if (error.empty()) {
            error = "schema creation failed";
        }
        return false;
    }

    if (!execSql(db.raw, "COMMIT", &error)) {
        execSql(db.raw, "ROLLBACK", nullptr);
        if (!existedBefore) {
            std::error_code rmEc;
            fs::remove(p, rmEc);
        }
        if (error.empty()) {
            error = "commit failed";
        }
        return false;
    }

    return true;
}

namespace {

// Returns a safe file name derived from `configuredPath`, or "guiNDB.db" when the
// candidate is unusable (empty, relative-dir, reserved device, ADS, trailing
// dot/space, or anything that would resolve outside exeDir).
std::string safeTargetName(const std::string& exeDir, const std::string& configuredPath) {
    const std::string fallback = "guiNDB.db";
    if (configuredPath.empty()) {
        return fallback;
    }

    const fs::path configured(configuredPath);
    const std::string name = configured.filename().string();
    if (name.empty()) {
        return fallback;
    }
    if (name == "." || name == "..") {
        return fallback;
    }
    if (name.back() == '.' || name.back() == ' ') {
        return fallback;
    }
    if (name.find(':') != std::string::npos) {
        return fallback;
    }
    const std::string ext = fs::path(name).extension().string();
    std::string stem = name.substr(0, name.size() - ext.size());
    if (stem.empty()) {
        stem = name;
    }
    if (isReservedDeviceName(stem)) {
        return fallback;
    }

    const fs::path exe(exeDir);
    if (!exeDir.empty()) {
        const fs::path resolved = (exe / name).lexically_normal();
        if (resolved.parent_path() != exe.lexically_normal()) {
            return fallback;
        }
        return name;
    }
    return name;
}

} // namespace

std::string DatabaseBootstrap::defaultTargetPath(const std::string& exeDir,
                                                 const std::string& configuredPath) {
    return (fs::path(exeDir) / safeTargetName(exeDir, configuredPath)).string();
}

std::string DatabaseBootstrap::createTargetPath(DbProbe /*probe*/,
                                                const std::string& exeDir,
                                                const std::string& configuredPath) {
    return defaultTargetPath(exeDir, configuredPath);
}

bool DatabaseBootstrap::mayCreateAt(const std::string& exeDir,
                                    const std::string& configuredPath,
                                    std::string& target, std::string& error) {
    error.clear();
    target.clear();

    if (exeDir.empty()) {
        error = "application directory is unknown";
        return false;
    }

    const fs::path exe(exeDir);
    if (isReparsePoint(exe)) {
        error = "application directory is a reparse point";
        return false;
    }

    // Unsafe names fall back to guiNDB.db rather than failing: the user still gets
    // a usable database, and nothing can escape exeDir.
    const std::string name = safeTargetName(exeDir, configuredPath);
    const fs::path resolved = (exe / name).lexically_normal();
    if (resolved.parent_path() != exe.lexically_normal()) {
        error = "derived target escapes the application directory";
        return false;
    }

    if (isReparsePoint(resolved)) {
        error = "derived target is a reparse point";
        return false;
    }

    target = resolved.string();

    // Never take over an existing usable database, and never overwrite a
    // non-empty unrelated file.
    const DbInspection existing = inspect(target);
    switch (existing.probe) {
        case DbProbe::MissingFile:
        case DbProbe::EmptyFile:
        case DbProbe::MissingPath:
            return true;
        case DbProbe::Ok:
            error = "a usable database already exists at " + target;
            return false;
        default:
            error = "refusing to create over existing target (" + target + "): " +
                    existing.detail;
            return false;
    }
}

namespace {

// Move <path>-wal / <path>-shm next to the backup. These bytes are the user's
// last copy of an uncheckpointed WAL and must never be deleted outright.
void moveSidecars(const std::string& path, const std::string& backupBase) {
    std::error_code ec;
    for (const char* suffix : {"-wal", "-shm"}) {
        const std::string sidecar = path + suffix;
        if (fs::exists(sidecar, ec) && !ec) {
            std::error_code mvEc;
            fs::rename(sidecar, backupBase + suffix, mvEc);
        }
    }
}

} // namespace

bool DatabaseBootstrap::backupFile(const std::string& path, std::string& backupPath) {
    backupPath.clear();
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) {
        return false;
    }

    const std::string base = path + ".bak." + backupTimestamp();

    // Try the plain name first, then disambiguate with a counter.
    for (int attempt = 0; attempt < 100; ++attempt) {
        const std::string candidate =
                attempt == 0 ? base : base + "-" + std::to_string(attempt);
        if (fs::exists(candidate, ec) || ec) {
            continue;
        }

        // Same-volume fast path.
        fs::rename(path, candidate, ec);
        if (!ec) {
            moveSidecars(path, candidate);
            backupPath = candidate;
            return true;
        }
        ec.clear();

        // Cross-volume: copy to .partial, verify, then rename into place.
        const std::string partial = candidate + ".partial";
        fs::copy_file(path, partial, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        const uintmax_t srcSize = fs::file_size(path, ec);
        const uintmax_t dstSize = fs::file_size(partial, ec);
        if (ec || srcSize != dstSize) {
            std::error_code rmEc;
            fs::remove(partial, rmEc);
            ec.clear();
            continue;
        }
        fs::rename(partial, candidate, ec);
        if (ec) {
            ec.clear();
            std::error_code rmEc;
            fs::remove(partial, rmEc);
            continue;
        }
        std::error_code rmEc;
        fs::remove(path, rmEc);
        moveSidecars(path, candidate);
        backupPath = candidate;
        return true;
    }

    return false;
}

bool DatabaseBootstrap::restoreFromBackup(const std::string& backupPath,
                                          const std::string& path, std::string& error) {
    error.clear();
    std::error_code ec;
    if (!fs::exists(backupPath, ec) || ec) {
        error = "backup file does not exist";
        return false;
    }
    if (fs::exists(path, ec) && !ec) {
        error = "destination already exists";
        return false;
    }
    fs::rename(backupPath, path, ec);
    if (ec) {
        error = "cannot restore backup: " + ec.message();
        return false;
    }
    return true;
}

bool DatabaseBootstrap::removeBackup(const std::string& backupPath) {
    if (backupPath.empty()) {
        return true;
    }
    std::error_code ec;
    if (!fs::exists(backupPath, ec) || ec) {
        return true;
    }
    fs::remove(backupPath, ec);
    return !ec;
}

} // namespace service
