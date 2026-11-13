// include/service/DatabaseBootstrap.h - read-only database probing plus
// idempotent schema creation, used by the GUI startup path so that an invalid
// database.path no longer silently produces a 0-byte database.
//
// Dependencies: sqlite3 + std::filesystem + Logger only. No wxWidgets, so the
// CLI targets and the unit-test target can compile it directly.
#ifndef SERVICE_DATABASE_BOOTSTRAP_H
#define SERVICE_DATABASE_BOOTSTRAP_H

#include <string>

namespace service {

// Order here defines nothing at runtime; keep it stable for log readability.
enum class DbProbe {
    Ok,
    MissingPath,        // configured path is the empty string
    MissingDirectory,   // parent directory absent, or the path itself is a directory
    MissingFile,        // file absent and no sibling backup
    EmptyFile,          // size 0 and no non-empty -wal sibling
    WalPending,         // size ~0 but a non-empty -wal holds committed data
    Inaccessible,       // CANTOPEN and the header is not a SQLite header
    WALReadOnlyBlocked, // valid header but the directory is not writable (needs -shm)
    BusyOrLocked,       // another instance holds a write/exclusive lock
    NotADatabase,       // readable but sqlite_master reporting NOTADB/CORRUPT
    MissingCoreTables,  // core tables or required columns absent
    BackupRecoverable   // file absent but a leftover .bak.<ts> can be restored
};

enum class DbStartupAction { Continue, PromptCreate, PromptRebuild, PromptRecover, Abort };

struct DbInspection {
    DbProbe probe = DbProbe::Ok;
    std::string detail;      // human-readable reason / missing table-or-column list
    std::string backupPath;  // only for BackupRecoverable: the candidate .bak.<ts>
};

// Pure policy: probe -> what the UI should do. Unit-tested exhaustively.
inline DbStartupAction dbStartupAction(DbProbe probe) {
    switch (probe) {
        case DbProbe::Ok:
            return DbStartupAction::Continue;
        case DbProbe::MissingPath:
        case DbProbe::MissingDirectory:
        case DbProbe::MissingFile:
        case DbProbe::EmptyFile:
            return DbStartupAction::PromptCreate;
        case DbProbe::MissingCoreTables:
            // Structurally unusable == treat like a damaged file: back it up and
            // rebuild. This version never mutates an existing schema in place.
            return DbStartupAction::PromptRebuild;
        case DbProbe::NotADatabase:
            return DbStartupAction::PromptRebuild;
        case DbProbe::BackupRecoverable:
            return DbStartupAction::PromptRecover;
        case DbProbe::WalPending:
            // Verified: when the main file is 0 bytes SQLite discards the -wal
            // (wal_checkpoint returns 0 pages, schema is gone). The bytes are not
            // replayable, so preserve them via a backup and rebuild.
            return DbStartupAction::PromptRebuild;
        case DbProbe::Inaccessible:
        case DbProbe::WALReadOnlyBlocked:
        case DbProbe::BusyOrLocked:
            return DbStartupAction::Abort;
    }
    return DbStartupAction::Abort;
}

class DatabaseBootstrap {
public:
    // Read-only probe. Never creates, modifies or deletes anything.
    static DbInspection inspect(const std::string& path);

    // Idempotent create. Deletes the target file only when this call created it;
    // an existing file is never removed (only rolled back).
    static bool create(const std::string& path, std::string& error);

    // <exeDir>/<basename of configuredPath>; empty basename -> <exeDir>/guiNDB.db
    static std::string defaultTargetPath(const std::string& exeDir,
                                         const std::string& configuredPath);

    // This version always delegates to defaultTargetPath: no in-place repair.
    // Kept as a named seam so v2 can restore the "configured path" branch
    // without touching call sites.
    static std::string createTargetPath(DbProbe probe, const std::string& exeDir,
                                        const std::string& configuredPath);

    // Safety gate before any creation: returns false when the derived target is
    // already a usable database, already a non-empty unrelated file, a reparse
    // point, or otherwise unsafe. On success `target` is filled in.
    static bool mayCreateAt(const std::string& exeDir, const std::string& configuredPath,
                            std::string& target, std::string& error);

    // Rename to <path>.bak.<yyyyMMdd-HHmmss>, and move any -wal/-shm siblings to
    // <backup>-wal / <backup>-shm so a not-yet-checkpointed WAL is preserved.
    // Across volumes: copy to <path>.bak.<ts>.partial, verify, then rename.
    // Returns false (and leaves the original untouched) when the name exists.
    static bool backupFile(const std::string& path, std::string& backupPath);

    // Rename a .bak.<ts> back to `path`. Fails when the destination already exists.
    static bool restoreFromBackup(const std::string& backupPath, const std::string& path,
                                  std::string& error);

    // Delete a backup after a successful rebuild. Missing file counts as success.
    static bool removeBackup(const std::string& backupPath);

    // The embedded, idempotent bootstrap DDL (generated from
    // resources/schema_init.sql at configure time).
    static const char* schemaInitSql();

    // Timestamp used in backup names; exposed so tests can reason about it.
    static std::string backupTimestamp();
};

} // namespace service

#endif // SERVICE_DATABASE_BOOTSTRAP_H
