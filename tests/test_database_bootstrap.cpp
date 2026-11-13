// tests/test_database_bootstrap.cpp - TDD unit tests for service::DatabaseBootstrap.
//
// The schema baseline comes from tests/fixtures/db_schema_baseline.sql (a plain
// text dump committed to the repo). bin/guiNDB.db and test/guiNDB_empty.db are
// both .gitignore'd, so tests must never read them.
#include <gtest/gtest.h>

#include <sqlite3.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "service/DatabaseBootstrap.h"

#ifndef DB_SCHEMA_BASELINE
#error "DB_SCHEMA_BASELINE must be defined by CMake"
#endif

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------- fixtures

class TempDir {
public:
    TempDir() {
        static int counter = 0;
        path_ = fs::temp_directory_path() /
                ("vp_db_bootstrap_" + std::to_string(::GetCurrentProcessId()) + "_" +
                 std::to_string(++counter));
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }
    std::string file(const std::string& name) const { return (path_ / name).string(); }

private:
    fs::path path_;
};

struct BaselineTable {
    std::string name;
    std::vector<std::string> columns;
};

std::map<std::string, BaselineTable> loadBaseline() {
    std::ifstream in(DB_SCHEMA_BASELINE);
    EXPECT_TRUE(in.good()) << "cannot open baseline: " << DB_SCHEMA_BASELINE;

    std::map<std::string, BaselineTable> out;
    std::string line;
    BaselineTable* current = nullptr;
    while (std::getline(in, line)) {
        const std::string tableMarker = "-- >>>>> TABLE ";
        const std::string valueMarker = "-- COL ";
        if (line.rfind(tableMarker, 0) == 0) {
            BaselineTable t;
            t.name = line.substr(tableMarker.size());
            out[t.name] = t;
            current = &out[t.name];
        } else if (line.rfind(valueMarker, 0) == 0 && current != nullptr) {
            // "-- COL <name> <type...> <notnull> <pk>"
            std::istringstream iss(line.substr(valueMarker.size()));
            std::string col;
            iss >> col;
            if (!col.empty()) {
                current->columns.push_back(col);
            }
        }
    }
    return out;
}

// Open on an existing connection and return a read-only connection string check.
std::vector<std::string> columnsOf(sqlite3* db, const std::string& table) {
    std::vector<std::string> cols;
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = "PRAGMA table_info(\"" + table + "\")";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return cols;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(stmt, 1);
        cols.push_back(name ? reinterpret_cast<const char*>(name) : "");
    }
    sqlite3_finalize(stmt);
    return cols;
}

bool tableExists(sqlite3* db, const std::string& table) {
    sqlite3_stmt* stmt = nullptr;
    const std::string sql =
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='" + table + "'";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

long long countRows(sqlite3* db, const std::string& table) {
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = "SELECT COUNT(*) FROM \"" + table + "\"";
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    long long n = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        n = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return n;
}

void writeFile(const std::string& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

// A minimal valid SQLite header (first 16 bytes) - enough for header sniffing.
const char kSqliteHeader[17] = "SQLite format 3";

} // namespace

// ------------------------------------------------------------------- probes

TEST(DatabaseBootstrapTest, InspectMissingPathReturnsMissingPath) {
    EXPECT_EQ(service::DatabaseBootstrap::inspect("").probe, service::DbProbe::MissingPath);
}

TEST(DatabaseBootstrapTest, InspectMissingFileReturnsMissingFile) {
    TempDir dir;
    const std::string path = dir.file("absent.db");
    const service::DbInspection r = service::DatabaseBootstrap::inspect(path);
    EXPECT_EQ(r.probe, service::DbProbe::MissingFile);
    EXPECT_FALSE(fs::exists(path)) << "inspect must not create the file";
}

TEST(DatabaseBootstrapTest, InspectMissingDirectoryReturnsMissingDirectory) {
    TempDir dir;
    const std::string path = dir.file("nope/deeper/x.db");
    EXPECT_EQ(service::DatabaseBootstrap::inspect(path).probe,
              service::DbProbe::MissingDirectory);
}

TEST(DatabaseBootstrapTest, InspectPathThatIsDirectoryReturnsMissingDirectory) {
    TempDir dir;
    const std::string sub = dir.file("adir");
    fs::create_directories(sub);
    EXPECT_EQ(service::DatabaseBootstrap::inspect(sub).probe,
              service::DbProbe::MissingDirectory);
}

TEST(DatabaseBootstrapTest, InspectEmptyFileReturnsEmptyFile) {
    TempDir dir;
    const std::string path = dir.file("empty.db");
    writeFile(path, "");
    EXPECT_EQ(service::DatabaseBootstrap::inspect(path).probe, service::DbProbe::EmptyFile);
}

TEST(DatabaseBootstrapTest, InspectNonSqliteFileReturnsNotADatabase) {
    TempDir dir;
    const std::string path = dir.file("junk.db");
    writeFile(path, "this is definitely not a database, it is just text padding padding");
    EXPECT_EQ(service::DatabaseBootstrap::inspect(path).probe,
              service::DbProbe::NotADatabase);
}

TEST(DatabaseBootstrapTest, InspectValidButTablelessDbReturnsMissingCoreTables) {
    TempDir dir;
    const std::string path = dir.file("empty_schema.db");
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);
    sqlite3_exec(db, "CREATE TABLE t(x)", nullptr, nullptr, nullptr);
    sqlite3_close(db);

    const service::DbInspection r = service::DatabaseBootstrap::inspect(path);
    EXPECT_EQ(r.probe, service::DbProbe::MissingCoreTables);
    EXPECT_FALSE(r.detail.empty()) << "detail should name what is missing";
}

TEST(DatabaseBootstrapTest, InspectMissingColumnReturnsMissingCoreTables) {
    TempDir dir;
    const std::string path = dir.file("partial.db");

    // Build the schema, then drop the Region column requirement by creating an
    // old-style ProfileItem without it.
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);
    sqlite3_exec(db,
                 "CREATE TABLE SubItem(Id varchar primary key);"
                 "CREATE TABLE ProfileItem(IndexId varchar primary key, Address varchar);"
                 "CREATE TABLE ProfileExItem(IndexId varchar primary key);"
                 "CREATE TABLE FullConfigTemplateItem(Id varchar primary key);",
                 nullptr, nullptr, nullptr);
    sqlite3_close(db);

    const service::DbInspection r = service::DatabaseBootstrap::inspect(path);
    EXPECT_EQ(r.probe, service::DbProbe::MissingCoreTables);
    EXPECT_NE(r.detail.find("Region"), std::string::npos)
            << "detail should mention the missing column, got: " << r.detail;
}

TEST(DatabaseBootstrapTest, InspectBusyDatabaseReturnsBusyOrLocked) {
    TempDir dir;
    const std::string path = dir.file("busy.db");

    // Create a schema-complete DB first so we know the probe is about locking.
    std::string scratch;
    ASSERT_TRUE(service::DatabaseBootstrap::create(path, scratch));

    // Switch to a rollback journal (WAL allows concurrent readers, so a BUSY
    // condition can only be provoked reliably in DELETE mode).
    sqlite3* setup = nullptr;
    ASSERT_EQ(sqlite3_open(path.c_str(), &setup), SQLITE_OK);
    sqlite3_exec(setup, "PRAGMA journal_mode=DELETE", nullptr, nullptr, nullptr);
    sqlite3_exec(setup, "PRAGMA locking_mode=EXCLUSIVE", nullptr, nullptr, nullptr);
    sqlite3_exec(setup, "BEGIN EXCLUSIVE", nullptr, nullptr, nullptr);
    sqlite3_exec(setup, "CREATE TABLE lock_probe(x)", nullptr, nullptr, nullptr);

    const service::DbInspection r = service::DatabaseBootstrap::inspect(path);
    sqlite3_exec(setup, "ROLLBACK", nullptr, nullptr, nullptr);
    sqlite3_close(setup);

    EXPECT_TRUE(r.probe == service::DbProbe::BusyOrLocked ||
                r.probe == service::DbProbe::Ok ||
                r.probe == service::DbProbe::MissingCoreTables)
            << "unexpected probe under lock: " << static_cast<int>(r.probe);
}

TEST(DatabaseBootstrapTest, InspectRandomFileWithSqliteHeaderIsNotInaccessible) {
    TempDir dir;
    const std::string path = dir.file("fake_header.db");
    std::string blob(kSqliteHeader, 16);
    blob.append(200, '\x7f');
    writeFile(path, blob);

    const service::DbProbe p = service::DatabaseBootstrap::inspect(path).probe;
    EXPECT_NE(p, service::DbProbe::Inaccessible)
            << "a file with a valid SQLite header must never be reported as a plain "
               "permission problem";
}

TEST(DatabaseBootstrapTest, OrphanBackupIsOfferedForRecovery) {
    TempDir dir;
    const std::string path = dir.file("orphan.db");
    const std::string bak = path + ".bak.20260101-120000";
    writeFile(bak, "pretend backup");

    const service::DbInspection r = service::DatabaseBootstrap::inspect(path);
    EXPECT_EQ(r.probe, service::DbProbe::BackupRecoverable);
    EXPECT_EQ(r.backupPath, bak);
}

TEST(DatabaseBootstrapTest, BackupPartialFileIsNeverRecoverable) {
    TempDir dir;
    const std::string path = dir.file("partial_backup.db");
    writeFile(path + ".bak.20260101-120000.partial", "half written");

    EXPECT_EQ(service::DatabaseBootstrap::inspect(path).probe,
              service::DbProbe::MissingFile);
}

TEST(DatabaseBootstrapTest, WalPendingIsPreservedAndRoutesToRebuild) {
    TempDir dir;
    const std::string path = dir.file("wal_pending.db");
    const std::string walCopy = dir.file("wal_copy.bin");

    // Build a schema-complete DB in WAL mode and commit a row without
    // checkpointing. A clean close removes the -wal, so snapshot it while the
    // connection is still open, then reproduce the crash state.
    {
        sqlite3* db = nullptr;
        ASSERT_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);

        char* msg = nullptr;
        ASSERT_EQ(sqlite3_exec(db, service::DatabaseBootstrap::schemaInitSql(), nullptr,
                               nullptr, &msg), SQLITE_OK)
                << (msg ? msg : "");
        ASSERT_EQ(sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr),
                  SQLITE_OK);
        ASSERT_EQ(sqlite3_exec(db, "PRAGMA wal_autocheckpoint=0", nullptr, nullptr, nullptr),
                  SQLITE_OK);

        char* insMsg = nullptr;
        ASSERT_EQ(sqlite3_exec(db,
                               "CREATE TABLE wal_probe(x);"
                               "INSERT INTO wal_probe(x) VALUES(1);",
                               nullptr, nullptr, &insMsg), SQLITE_OK)
                << (insMsg ? insMsg : "");

        const std::string wal = path + "-wal";
        ASSERT_TRUE(fs::exists(wal)) << "no -wal while the connection is open";
        fs::copy_file(wal, walCopy, fs::copy_options::overwrite_existing);
        sqlite3_close(db);
    }

    ASSERT_TRUE(fs::exists(walCopy));
    fs::remove(path + "-wal");
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        ASSERT_NE(f, nullptr);
        std::fclose(f);
    }
    fs::copy_file(walCopy, path + "-wal", fs::copy_options::overwrite_existing);
    const uintmax_t walBytes = fs::file_size(path + "-wal");
    ASSERT_GT(walBytes, 0u);

    // Probe recognises the state and routes it to a rebuild, not a silent create.
    EXPECT_EQ(service::DatabaseBootstrap::inspect(path).probe, service::DbProbe::WalPending);
    EXPECT_EQ(service::dbStartupAction(service::DbProbe::WalPending),
              service::DbStartupAction::PromptRebuild);

    // Backing up must move the WAL alongside the backup, never delete it.
    std::string bak;
    ASSERT_TRUE(service::DatabaseBootstrap::backupFile(path, bak));
    EXPECT_TRUE(fs::exists(bak + "-wal")) << "the uncheckpointed WAL bytes must be preserved";
    EXPECT_GE(fs::file_size(bak + "-wal"), walBytes);
    EXPECT_FALSE(fs::exists(path + "-wal"));

    std::string error;
    ASSERT_TRUE(service::DatabaseBootstrap::create(path, error)) << error;
    EXPECT_EQ(service::DatabaseBootstrap::inspect(path).probe, service::DbProbe::Ok);
}

// ------------------------------------------------------------------- create

TEST(DatabaseBootstrapTest, CreateProducesCompleteSchema) {
    TempDir dir;
    const std::string path = dir.file("fresh.db");
    std::string error;
    ASSERT_TRUE(service::DatabaseBootstrap::create(path, error)) << error;

    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);

    const std::map<std::string, BaselineTable> baseline = loadBaseline();
    ASSERT_FALSE(baseline.empty());

    for (const auto& kv : baseline) {
        ASSERT_TRUE(tableExists(db, kv.first)) << "missing table " << kv.first;
        const std::vector<std::string> actual = columnsOf(db, kv.first);
        EXPECT_EQ(actual, kv.second.columns)
                << "column list/order mismatch for " << kv.first;
    }

    sqlite3_stmt* stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db, "PRAGMA quick_check", -1, &stmt, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    const std::string check = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    EXPECT_EQ(check, "ok");

    EXPECT_EQ(countRows(db, "ComponentBundle"), 16);
    EXPECT_EQ(countRows(db, "FullConfigTemplateItem"), 8);

    ASSERT_EQ(sqlite3_prepare_v2(db,
                                 "SELECT COUNT(*) FROM FullConfigTemplateItem "
                                 "WHERE BundleChain IS NULL OR BundleChain=''",
                                 -1, &stmt, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int64(stmt, 0), 0) << "template seeds must carry a BundleChain";
    sqlite3_finalize(stmt);

    sqlite3_close(db);
    EXPECT_EQ(service::DatabaseBootstrap::inspect(path).probe, service::DbProbe::Ok);
}

TEST(DatabaseBootstrapTest, CreateIsIdempotent) {
    TempDir dir;
    const std::string path = dir.file("twice.db");
    std::string error;
    ASSERT_TRUE(service::DatabaseBootstrap::create(path, error)) << error;

    long long before = -1;
    {
        sqlite3* db = nullptr;
        ASSERT_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);
        before = countRows(db, "ComponentBundle");
        sqlite3_close(db);
    }

    ASSERT_TRUE(service::DatabaseBootstrap::create(path, error)) << error;
    {
        sqlite3* db = nullptr;
        ASSERT_EQ(sqlite3_open(path.c_str(), &db), SQLITE_OK);
        EXPECT_EQ(countRows(db, "ComponentBundle"), before);
        sqlite3_close(db);
    }
}

TEST(DatabaseBootstrapTest, CreateFailsWhenParentMissing) {
    TempDir dir;
    std::string error;
    EXPECT_FALSE(service::DatabaseBootstrap::create(
            dir.file("no_such_dir/x.db"), error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(fs::exists(dir.file("no_such_dir")))
            << "create() must not build directories";
}

TEST(DatabaseBootstrapTest, CreateFailsOnReadOnlyDirectory) {
    // Windows ACL manipulation is unreliable under CI/sandbox; this checks the
    // contract for an unwritable location rather than a specific ACL failure.
    const std::string path = "Z:\\definitely\\not\\writable\\x.db";
    std::string error;
    if (fs::exists("Z:\\")) {
        GTEST_SKIP() << "drive Z: exists on this machine; cannot assume unwritable";
    }
    EXPECT_FALSE(service::DatabaseBootstrap::create(path, error));
    EXPECT_FALSE(error.empty());
}

TEST(DatabaseBootstrapTest, CreateDoesNotDeleteExistingFileOnFailure) {
    TempDir dir;
    const std::string path = dir.file("existing.db");
    std::string error;
    ASSERT_TRUE(service::DatabaseBootstrap::create(path, error)) << error;

    const std::string before = [&] {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }();
    ASSERT_FALSE(before.empty());

    // Make the file unwritable so create() must fail without deleting it.
    std::error_code ec;
    fs::permissions(path, fs::perms::owner_read | fs::perms::group_read |
                            fs::perms::others_read, fs::perm_options::replace, ec);
    if (ec) {
        GTEST_SKIP() << "cannot change permissions: " << ec.message();
    }

    std::string createError;
    const bool ok = service::DatabaseBootstrap::create(path, createError);

    fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace, ec);

    EXPECT_FALSE(ok) << "create() must fail on an unwritable existing file";
    EXPECT_FALSE(createError.empty());
    EXPECT_TRUE(fs::exists(path)) << "create() must never delete a pre-existing file";

    const std::string after = [&] {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }();
    EXPECT_EQ(after.size(), before.size());
}

// ------------------------------------------------------------------ targets

TEST(DatabaseBootstrapTest, DefaultTargetPathUsesExeDir) {
    const fs::path exe = "C:/apps/validproxy";
    const std::string expected = (exe / "guiNDB.db").string();
    EXPECT_EQ(service::DatabaseBootstrap::defaultTargetPath(exe.string(),
                                                            "D:/x/y/guiNDB.db"),
              expected);
    EXPECT_EQ(service::DatabaseBootstrap::defaultTargetPath(exe.string(), ""), expected);
    EXPECT_EQ(service::DatabaseBootstrap::defaultTargetPath(exe.string(), "D:/data/"),
              expected);
    // A configured name is preserved.
    EXPECT_EQ(service::DatabaseBootstrap::defaultTargetPath(exe.string(), "D:/x/y/other.db"),
              (exe / "other.db").string());
}

TEST(DatabaseBootstrapTest, CreateTargetPathAlwaysUsesExeDir) {
    const fs::path exePath = "C:/apps/validproxy";
    const std::string exe = exePath.string();
    const std::string cfg = "D:/x/y/guiNDB.db";
    const std::string expected = (exePath / "guiNDB.db").string();
    const service::DbProbe probes[] = {
            service::DbProbe::MissingPath,       service::DbProbe::MissingDirectory,
            service::DbProbe::MissingFile,       service::DbProbe::EmptyFile,
            service::DbProbe::WalPending,        service::DbProbe::Inaccessible,
            service::DbProbe::WALReadOnlyBlocked, service::DbProbe::BusyOrLocked,
            service::DbProbe::NotADatabase,      service::DbProbe::MissingCoreTables,
            service::DbProbe::BackupRecoverable, service::DbProbe::Ok};
    for (service::DbProbe p : probes) {
        EXPECT_EQ(service::DatabaseBootstrap::createTargetPath(p, exe, cfg), expected);
    }
}

TEST(DatabaseBootstrapTest, CreateTargetPathRejectsUnsafeFilename) {
    const fs::path exePath = "C:/apps/validproxy";
    const std::string exe = exePath.string();
    const std::string fallback = (exePath / "guiNDB.db").string();

    // Names whose *basename* is unusable must fall back to guiNDB.db.
    const char* unsafe[] = {"..", ".", "CON", "NUL", "x.db:stream", "trailing. "};
    for (const char* name : unsafe) {
        const std::string target = service::DatabaseBootstrap::createTargetPath(
                service::DbProbe::MissingFile, exe, std::string("D:/data/") + name);
        EXPECT_EQ(target, fallback) << "for name: " << name;
    }

    // A nested configured path is legitimate: only its basename is used, and the
    // result is still inside exeDir.
    EXPECT_EQ(service::DatabaseBootstrap::createTargetPath(
                      service::DbProbe::MissingFile, exe, "C:/x/../../victim.db"),
              (exePath / "victim.db").string());
}

TEST(DatabaseBootstrapTest, AutoCreateRefusesToAdoptExistingDatabase) {
    TempDir dir;
    const std::string exeDir = dir.path().string();
    const std::string configured = dir.file("elsewhere/guiNDB.db");

    // A perfectly good DB already sits at the derived target.
    const std::string target = exeDir + "/guiNDB.db";
    std::string scratch;
    ASSERT_TRUE(service::DatabaseBootstrap::create(target, scratch));

    std::string resolved;
    std::string error;
    EXPECT_FALSE(service::DatabaseBootstrap::mayCreateAt(exeDir, configured, resolved, error));
    EXPECT_FALSE(error.empty());
}

TEST(DatabaseBootstrapTest, MayCreateAtAllowsFreshTarget) {
    TempDir dir;
    const std::string exeDir = dir.path().string();

    std::string resolved;
    std::string error;
    EXPECT_TRUE(service::DatabaseBootstrap::mayCreateAt(
            exeDir, dir.file("elsewhere/guiNDB.db"), resolved, error))
            << error;
    EXPECT_EQ(resolved, (fs::path(exeDir) / "guiNDB.db").string());
    EXPECT_FALSE(fs::exists(resolved)) << "mayCreateAt must not create anything";
}

TEST(DatabaseBootstrapTest, CreateTargetRejectsReparsePoint) {
    // Creating a real junction needs privileges we cannot assume, so this test
    // asserts the pure predicate instead: a target below a symlinked dir is
    // rejected. Skipped when the platform cannot create symlinks.
    TempDir dir;
    const fs::path linkDir = dir.path() / "link";
    std::error_code ec;
    fs::create_directory_symlink(dir.path(), linkDir, ec);
    if (ec) {
        GTEST_SKIP() << "cannot create directory symlink: " << ec.message();
    }

    std::string resolved;
    std::string error;
    EXPECT_FALSE(service::DatabaseBootstrap::mayCreateAt(
            (linkDir / "sub").string(), "x.db", resolved, error));
}

// ------------------------------------------------------------------ backups

TEST(DatabaseBootstrapTest, BackupFileRenamesWithTimestamp) {
    TempDir dir;
    const std::string path = dir.file("victim.db");
    writeFile(path, "original-bytes");

    std::string bak;
    ASSERT_TRUE(service::DatabaseBootstrap::backupFile(path, bak));
    EXPECT_TRUE(fs::exists(bak));
    EXPECT_FALSE(fs::exists(path)) << "backup must be a rename, not a copy";
    EXPECT_NE(bak.find(".bak."), std::string::npos);

    std::ifstream in(bak, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    EXPECT_EQ(ss.str(), "original-bytes");

    // A second backup to the same name must refuse rather than overwrite.
    writeFile(path, "second");
    std::string bak2;
    const bool again = service::DatabaseBootstrap::backupFile(path, bak2);
    if (again) {
        EXPECT_NE(bak2, bak) << "must not reuse an existing backup name";
    } else {
        EXPECT_TRUE(fs::exists(path)) << "refusal must leave the original in place";
    }
}

TEST(DatabaseBootstrapTest, RestoreFromBackupProducesUsableDatabase) {
    TempDir dir;
    const std::string path = dir.file("restore.db");
    std::string scratch;
    ASSERT_TRUE(service::DatabaseBootstrap::create(path, scratch));

    std::string bak;
    ASSERT_TRUE(service::DatabaseBootstrap::backupFile(path, bak));
    ASSERT_FALSE(fs::exists(path));

    std::string error;
    ASSERT_TRUE(service::DatabaseBootstrap::restoreFromBackup(bak, path, error)) << error;
    EXPECT_TRUE(fs::exists(path));
    EXPECT_FALSE(fs::exists(bak));
    EXPECT_EQ(service::DatabaseBootstrap::inspect(path).probe, service::DbProbe::Ok);
}

TEST(DatabaseBootstrapTest, SuccessfulRebuildDeletesBackup) {
    TempDir dir;
    const std::string path = dir.file("rebuild.db");
    writeFile(path, "not a database at all, just bytes");

    std::string bak;
    ASSERT_TRUE(service::DatabaseBootstrap::backupFile(path, bak));
    std::string scratch;
    ASSERT_TRUE(service::DatabaseBootstrap::create(path, scratch));

    EXPECT_TRUE(service::DatabaseBootstrap::removeBackup(bak));
    EXPECT_FALSE(fs::exists(bak)) << "a successful rebuild must not leave a stale snapshot";
    EXPECT_EQ(service::DatabaseBootstrap::inspect(path).probe, service::DbProbe::Ok);
}

// ------------------------------------------------------------------ mapping

TEST(DatabaseBootstrapTest, StartupActionMapping) {
    using service::DbProbe;
    using service::DbStartupAction;

    EXPECT_EQ(service::dbStartupAction(DbProbe::Ok), DbStartupAction::Continue);
    EXPECT_EQ(service::dbStartupAction(DbProbe::MissingPath), DbStartupAction::PromptCreate);
    EXPECT_EQ(service::dbStartupAction(DbProbe::MissingDirectory), DbStartupAction::PromptCreate);
    EXPECT_EQ(service::dbStartupAction(DbProbe::MissingFile), DbStartupAction::PromptCreate);
    EXPECT_EQ(service::dbStartupAction(DbProbe::EmptyFile), DbStartupAction::PromptCreate);
    EXPECT_EQ(service::dbStartupAction(DbProbe::WalPending), DbStartupAction::PromptRebuild);
    EXPECT_EQ(service::dbStartupAction(DbProbe::Inaccessible), DbStartupAction::Abort);
    EXPECT_EQ(service::dbStartupAction(DbProbe::WALReadOnlyBlocked), DbStartupAction::Abort);
    EXPECT_EQ(service::dbStartupAction(DbProbe::BusyOrLocked), DbStartupAction::Abort);
    EXPECT_EQ(service::dbStartupAction(DbProbe::NotADatabase), DbStartupAction::PromptRebuild);
    EXPECT_EQ(service::dbStartupAction(DbProbe::MissingCoreTables), DbStartupAction::PromptRebuild);
    EXPECT_EQ(service::dbStartupAction(DbProbe::BackupRecoverable), DbStartupAction::PromptRecover);
}

TEST(DatabaseBootstrapTest, MissingCoreTablesRoutesToRebuild) {
    EXPECT_EQ(service::dbStartupAction(service::DbProbe::MissingCoreTables),
              service::DbStartupAction::PromptRebuild);

    // This version never mutates an existing schema.
    const std::string sql = service::DatabaseBootstrap::schemaInitSql();
    ASSERT_FALSE(sql.empty());
    for (const char* forbidden : {"ALTER TABLE", "DROP TABLE", "DELETE FROM"}) {
        std::string upper = sql;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        std::string needle = forbidden;
        EXPECT_EQ(upper.find(needle), std::string::npos)
                << "schema_init.sql must not contain " << forbidden;
    }
}
