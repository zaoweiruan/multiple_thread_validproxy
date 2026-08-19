#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include "test_utils.h"
#include "ConfigReader.h"
#include "Logger.h"

using namespace config;

class ConfigReaderLoadTest : public ::testing::Test {
protected:
    TempDir tempDir_;

    void SetUp() override {
        // Suppress MessageBoxA during tests
        originalReporter_ = ConfigReader::errorReporter_;
        ConfigReader::errorReporter_ = [](const std::string&, const std::string&) {
            // no-op: suppress popup
        };
    }

    void TearDown() override {
        ConfigReader::errorReporter_ = originalReporter_;
    }

    void writeConfig(const std::string& filename, const std::string& content) {
        std::ofstream file(tempDir_.path() + "/" + filename);
        file << content;
        file.close();
    }

    std::string configPath(const std::string& filename) {
        return tempDir_.path() + "/" + filename;
    }

private:
    ConfigReader::ErrorReporter originalReporter_;
};

TEST_F(ConfigReaderLoadTest, FileNotFound) {
    std::optional<AppConfig> result = ConfigReader::load(configPath("nonexistent.json"));
    EXPECT_FALSE(result.has_value());
}

TEST_F(ConfigReaderLoadTest, InvalidJsonSyntax) {
    writeConfig("bad.json", "{invalid json here}");
    std::optional<AppConfig> result = ConfigReader::load(configPath("bad.json"));
    EXPECT_FALSE(result.has_value());
}

TEST_F(ConfigReaderLoadTest, NotAJsonObject) {
    writeConfig("arr.json", "[1, 2, 3]");
    std::optional<AppConfig> result = ConfigReader::load(configPath("arr.json"));
    EXPECT_FALSE(result.has_value());
}

TEST_F(ConfigReaderLoadTest, EmptyConfigWithDefaultDb) {
    writeConfig("empty.json", "{}");
    std::optional<AppConfig> result = ConfigReader::load(configPath("empty.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->xray_workers, 1);
    EXPECT_EQ(result->xray_start_port, 1083);
    EXPECT_EQ(result->test_timeout_ms, 5000);
    EXPECT_EQ(result->log_console_level, "INFO");
    EXPECT_EQ(result->log_file_level, "DEBUG");
    ASSERT_EQ(result->update_methods.size(), 1);
    EXPECT_EQ(result->update_methods[0], "accelerator");
    EXPECT_TRUE(result->dedup_enabled);
    EXPECT_TRUE(result->blacklist_enabled);
}

TEST_F(ConfigReaderLoadTest, MissingDbFile) {
    writeConfig("nodbfile.json", R"({
        "database": "/nonexistent/path/db.db"
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("nodbfile.json"));
    // Original behavior: missing DB logs a warning but does not block load.
    // Path is resolved relative to exeDir (drive letter prepended on Windows).
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->database_path.empty());
}

TEST_F(ConfigReaderLoadTest, FullConfigRoundTrip) {
    // Create a DB file that actually exists
    std::filesystem::path dbFilePath = std::filesystem::path(tempDir_.path()) / "test.db";
    std::string dbPath = dbFilePath.generic_string();
    touchFile(dbPath);

    // Use absolute paths to avoid resolvePath mangling relative paths with exeDir
    // Use generic_string() to ensure forward slashes in JSON
    std::string tmpDirGeneric = std::filesystem::path(tempDir_.path()).generic_string();
    std::string xrayExe = tmpDirGeneric + "/xray.exe";
    std::string srcDb = tmpDirGeneric + "/src.db";
    std::string dstDb = tmpDirGeneric + "/dst.db";

    writeConfig("full.json", R"({
        "database": ")" + dbPath + R"(",
        "xray": {
            "workers": 4,
            "start_port": 2080,
            "api_port": 2081
        },
        "test": {
            "url": "https://example.com",
            "timeout_ms": 3000
        },
        "log": {
            "enabled": true,
            "network_failures": false,
            "console_level": "INFO",
            "file_level": "DEBUG"
        },
        "subscription": {
            "priority_mode": "proxy_first",
            "check_auto_update_interval": true,
            "connect_timeout_ms": 5000,
            "timeout_ms": 20000
        },
        "dedup": {
            "enabled": true,
            "dedup_after_update": true,
            "blacklist_threshold": 10,
            "blacklist_enabled": true,
            "blacklist_subid": "bl_sub",
            "subids": ["sub1", "sub2"]
        },
        "notification": {
            "enabled": true,
            "on_update": true,
            "on_test": false
        },
        "sync": {
            "source_db": ")" + srcDb + R"(",
            "target_db": ")" + dstDb + R"(",
            "sync_skip_subids": true
        },
        "proxy": {
            "xray_executable": ")" + xrayExe + R"("
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("full.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->database_path, dbPath);
    EXPECT_EQ(result->proxy.xray_executable, xrayExe);
    EXPECT_EQ(result->xray_workers, 4);
    EXPECT_EQ(result->xray_start_port, 2080);
    EXPECT_EQ(result->xray_api_port, 2081);
    EXPECT_EQ(result->test_url, "https://example.com");
    EXPECT_EQ(result->test_timeout_ms, 3000);
    EXPECT_TRUE(result->log_enabled);
    EXPECT_EQ(result->log_console_level, "INFO");
    ASSERT_EQ(result->update_methods.size(), 1);
    EXPECT_EQ(result->update_methods[0], "proxy");
    EXPECT_TRUE(result->check_auto_update_interval);
    EXPECT_EQ(result->subscription_connect_timeout_ms, 5000);
    EXPECT_EQ(result->subscription_timeout_ms, 20000);
    EXPECT_TRUE(result->dedup_enabled);
    EXPECT_TRUE(result->dedup_after_update);
    EXPECT_EQ(result->blacklist_threshold, 10);
    EXPECT_EQ(result->blacklist_subid, "bl_sub");
    EXPECT_EQ(result->dedup_subids.size(), 2);
    EXPECT_EQ(result->dedup_subids[0], "sub1");
    EXPECT_EQ(result->dedup_subids[1], "sub2");
    EXPECT_TRUE(result->notification_enabled);
    EXPECT_TRUE(result->notification_on_update);
    EXPECT_FALSE(result->notification_on_test);
    EXPECT_EQ(result->sync.source_db, srcDb);
    EXPECT_EQ(result->sync.target_db, dstDb);
    EXPECT_TRUE(result->sync.sync_skip_subids);
}

TEST_F(ConfigReaderLoadTest, DatabaseAsObject) {
    std::filesystem::path dbFilePath = std::filesystem::path(tempDir_.path()) / "test.db";
    std::string dbPath = dbFilePath.generic_string();
    touchFile(dbPath);

    writeConfig("dbobj.json", R"({
        "database": {
            "path": ")" + dbPath + R"(",
            "sql": "SELECT * FROM profiles",
            "sql_by_subid": "SELECT * FROM profiles WHERE subid = '{subid}'"
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("dbobj.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->database_path, dbPath);
    EXPECT_EQ(result->sql_query, "SELECT * FROM profiles");
    EXPECT_EQ(result->sql_by_subid, "SELECT * FROM profiles WHERE subid = '{subid}'");
}

TEST_F(ConfigReaderLoadTest, WrongTypeDefaults) {
    writeConfig("wrong.json", R"({
        "xray": {
            "workers": "not-an-int",
            "start_port": true,
            "api_port": "bad"
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("wrong.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->xray_workers, 1);
    EXPECT_EQ(result->xray_start_port, 1083);
    EXPECT_EQ(result->xray_api_port, 0);
}

TEST_F(ConfigReaderLoadTest, ClampingOutOfRange) {
    writeConfig("clamp.json", R"({
        "xray": {
            "workers": -5,
            "start_port": 0
        },
        "test": {
            "timeout_ms": -100
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("clamp.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->xray_workers, 1);
    EXPECT_EQ(result->xray_start_port, 1083);
    EXPECT_EQ(result->test_timeout_ms, 5000);
}

TEST_F(ConfigReaderLoadTest, UnknownSqlPlaceholderWarning) {
    writeConfig("placeholder.json", R"({
        "database": {
            "sql": "SELECT * FROM {unknown_placeholder}"
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("placeholder.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->sql_query, "SELECT * FROM {unknown_placeholder}");
}

TEST_F(ConfigReaderLoadTest, EmptyDatabasePath) {
    writeConfig("empty.json", R"({
        "database": ""
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("empty.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->database_path.empty());
}

TEST_F(ConfigReaderLoadTest, PriorityModeBackwardCompat_DirectFirst) {
    writeConfig("pm_df.json", R"({
        "subscription": {
            "priority_mode": "direct_first"
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("pm_df.json"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->update_methods.size(), 2);
    EXPECT_EQ(result->update_methods[0], "direct");
    EXPECT_EQ(result->update_methods[1], "proxy");
}

TEST_F(ConfigReaderLoadTest, PriorityModeBackwardCompat_ProxyFirst) {
    writeConfig("pm_pf.json", R"({
        "subscription": {
            "priority_mode": "proxy_first"
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("pm_pf.json"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->update_methods.size(), 1);
    EXPECT_EQ(result->update_methods[0], "proxy");
}

TEST_F(ConfigReaderLoadTest, PriorityModeBackwardCompat_DirectOnly) {
    writeConfig("pm_do.json", R"({
        "subscription": {
            "priority_mode": "direct_only"
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("pm_do.json"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->update_methods.size(), 1);
    EXPECT_EQ(result->update_methods[0], "direct");
}

TEST_F(ConfigReaderLoadTest, UpdateMethodsTakesPriority) {
    writeConfig("um_priority.json", R"({
        "subscription": {
            "priority_mode": "proxy_first",
            "update_methods": ["accelerator", "direct"]
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("um_priority.json"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->update_methods.size(), 2);
    EXPECT_EQ(result->update_methods[0], "accelerator");
    EXPECT_EQ(result->update_methods[1], "direct");
}

TEST_F(ConfigReaderLoadTest, AcceleratorUrlParsed) {
    writeConfig("acc_url.json", R"({
        "subscription": {
            "accelerator_url": "https://cdn.acc.com/"
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("acc_url.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->accelerator_url, "https://cdn.acc.com/");
}

TEST_F(ConfigReaderLoadTest, UpdateMethodsDefaultWhenEmpty) {
    writeConfig("um_empty.json", R"({
        "subscription": {}
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("um_empty.json"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->update_methods.size(), 1);
    EXPECT_EQ(result->update_methods[0], "accelerator");
}

TEST_F(ConfigReaderLoadTest, UpdateMethodsFiltersInvalid) {
    writeConfig("um_invalid.json", R"({
        "subscription": {
            "update_methods": ["accelerator", "invalid", "proxy"]
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("um_invalid.json"));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->update_methods.size(), 2);
    EXPECT_EQ(result->update_methods[0], "accelerator");
    EXPECT_EQ(result->update_methods[1], "proxy");
}

// ============================================================
// Section-level defaults: each section parsed independently
// ============================================================

TEST_F(ConfigReaderLoadTest, SectionDefaults_Database) {
    std::string dbPath =
        (std::filesystem::path(tempDir_.path()) / "section_test.db").generic_string();
    touchFile(dbPath);

    writeConfig("sec_db.json", R"({
        "database": {
            "path": ")" + dbPath + R"(",
            "sql": "SELECT 1",
            "sql_by_subid": "SELECT 1 WHERE subid='{subid}'"
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("sec_db.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->database_path, dbPath);
    EXPECT_EQ(result->sql_query, "SELECT 1");
    EXPECT_EQ(result->sql_by_subid, "SELECT 1 WHERE subid='{subid}'");
}

TEST_F(ConfigReaderLoadTest, SectionDefaults_Xray) {
    std::string xrayPath =
        (std::filesystem::path(tempDir_.path()) / "section_xray.exe").generic_string();
    touchFile(xrayPath);

    writeConfig("sec_xray.json", R"({
        "xray": {
            "workers": 4,
            "start_port": 2080,
            "api_port": 2081
        },
        "proxy": {
            "xray_executable": ")" + xrayPath + R"("
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("sec_xray.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->proxy.xray_executable, xrayPath);
    EXPECT_EQ(result->xray_workers, 4);
    EXPECT_EQ(result->xray_start_port, 2080);
    EXPECT_EQ(result->xray_api_port, 2081);
}

TEST_F(ConfigReaderLoadTest, SectionDefaults_Test) {
    writeConfig("sec_test.json", R"({
        "test": {
            "url": "https://test.com",
            "timeout_ms": 3000
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("sec_test.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->test_url, "https://test.com");
    EXPECT_EQ(result->test_timeout_ms, 3000);
}

TEST_F(ConfigReaderLoadTest, SectionDefaults_Log) {
    writeConfig("sec_log.json", R"({
        "log": {
            "enabled": false,
            "network_failures": true,
            "console_level": "WARN",
            "file_level": "INFO"
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("sec_log.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->log_enabled);
    EXPECT_TRUE(result->log_network_failures);
    EXPECT_EQ(result->log_console_level, "WARN");
    EXPECT_EQ(result->log_file_level, "INFO");
}

TEST_F(ConfigReaderLoadTest, SectionDefaults_Subscription) {
    writeConfig("sec_sub.json", R"({
        "subscription": {
            "accelerator_url": "https://cdn.acc.com/",
            "update_methods": ["proxy", "direct"],
            "check_auto_update_interval": true,
            "connect_timeout_ms": 5000,
            "timeout_ms": 20000
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("sec_sub.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->accelerator_url, "https://cdn.acc.com/");
    ASSERT_EQ(result->update_methods.size(), 2);
    EXPECT_EQ(result->update_methods[0], "proxy");
    EXPECT_EQ(result->update_methods[1], "direct");
    EXPECT_TRUE(result->check_auto_update_interval);
    EXPECT_EQ(result->subscription_connect_timeout_ms, 5000);
    EXPECT_EQ(result->subscription_timeout_ms, 20000);
}

TEST_F(ConfigReaderLoadTest, SectionDefaults_Dedup) {
    writeConfig("sec_dedup.json", R"({
        "dedup": {
            "enabled": false,
            "dedup_after_update": true,
            "blacklist_threshold": 10,
            "blacklist_enabled": false,
            "blacklist_subid": "bl",
            "subids": ["a", "b"]
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("sec_dedup.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->dedup_enabled);
    EXPECT_TRUE(result->dedup_after_update);
    EXPECT_EQ(result->blacklist_threshold, 10);
    EXPECT_FALSE(result->blacklist_enabled);
    EXPECT_EQ(result->blacklist_subid, "bl");
    ASSERT_EQ(result->dedup_subids.size(), 2);
    EXPECT_EQ(result->dedup_subids[0], "a");
    EXPECT_EQ(result->dedup_subids[1], "b");
}

TEST_F(ConfigReaderLoadTest, SectionDefaults_Notification) {
    writeConfig("sec_notif.json", R"({
        "notification": {
            "enabled": true,
            "on_update": true,
            "on_test": true
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("sec_notif.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->notification_enabled);
    EXPECT_TRUE(result->notification_on_update);
    EXPECT_TRUE(result->notification_on_test);
}

TEST_F(ConfigReaderLoadTest, SectionDefaults_Sync) {
    // Use absolute paths (drive-letter qualified on MinGW/Windows)
    std::string srcDb =
        (std::filesystem::path(tempDir_.path()) / "sec_src.db").generic_string();
    std::string dstDb =
        (std::filesystem::path(tempDir_.path()) / "sec_dst.db").generic_string();

    writeConfig("sec_sync.json", R"({
        "sync": {
            "source_db": ")" + srcDb + R"(",
            "target_db": ")" + dstDb + R"(",
            "sync_skip_subids": true
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("sec_sync.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->sync.source_db, srcDb);
    EXPECT_EQ(result->sync.target_db, dstDb);
    EXPECT_TRUE(result->sync.sync_skip_subids);
}

// ============================================================
// Type coercion: wrong JSON types fall back to defaults
// ============================================================

TEST_F(ConfigReaderLoadTest, TypeCoercion_BoolFromString) {
    writeConfig("boolstr.json", R"({
        "log": {
            "enabled": "true"
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("boolstr.json"));
    ASSERT_TRUE(result.has_value());
    // String "true" is not a JSON bool, so the code falls back to the default (true)
    EXPECT_TRUE(result->log_enabled);
}

TEST_F(ConfigReaderLoadTest, TypeCoercion_IntFromBool) {
    writeConfig("intbool.json", R"({
        "xray": {
            "workers": true
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("intbool.json"));
    ASSERT_TRUE(result.has_value());
    // Bool true is not int64, so the code falls back to the default (1)
    EXPECT_EQ(result->xray_workers, 1);
}

// ============================================================
// Path resolution: subdirectory config files
// ============================================================

TEST_F(ConfigReaderLoadTest, PathResolution_Relative) {
    // Config in a subdirectory with absolute paths — verifies that
    // loading from a nested path does not break absolute path resolution.
    std::string subdir = tempDir_.path() + "/subdir";
    std::filesystem::create_directories(subdir);

    std::string dbPath =
        (std::filesystem::path(subdir) / "sub_test.db").generic_string();
    std::string xrayPath =
        (std::filesystem::path(subdir) / "sub_xray.exe").generic_string();
    touchFile(dbPath);
    touchFile(xrayPath);

    writeConfig("subdir/config.json", R"({
        "database": ")" + dbPath + R"(",
        "xray": {
            "workers": 2,
            "start_port": 2085
        },
        "proxy": {
            "xray_executable": ")" + xrayPath + R"("
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("subdir/config.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->database_path, dbPath);
    EXPECT_EQ(result->proxy.xray_executable, xrayPath);
}

// ============================================================
// Save → load round-trip via ConfigReader::save()
// ============================================================

TEST_F(ConfigReaderLoadTest, SaveRoundTripInLoad) {
    std::string tmpDirGeneric = std::filesystem::path(tempDir_.path()).generic_string();
    std::string dbPath = tmpDirGeneric + "/sriltest.db";
    std::string xrayPath = tmpDirGeneric + "/srilxray.exe";
    std::string syncSrcPath = tmpDirGeneric + "/sril_src.db";
    std::string syncDstPath = tmpDirGeneric + "/sril_dst.db";
    touchFile(dbPath);
    touchFile(xrayPath);

    writeConfig("sril_load.json", R"({
        "database": ")" + dbPath + R"(",
        "xray": {
            "executable": ")" + xrayPath + R"(",
            "workers": 3,
            "start_port": 2000,
            "api_port": 2001
        },
        "test": {
            "url": "https://sril.example.com",
            "timeout_ms": 4000
        },
        "log": {
            "enabled": false,
            "network_failures": true,
            "console_level": "WARN",
            "file_level": "ERROR"
        },
        "subscription": {
            "accelerator_url": "https://sril.acc.com/",
            "update_methods": ["accelerator", "proxy"],
            "check_auto_update_interval": true,
            "connect_timeout_ms": 7000,
            "timeout_ms": 25000
        },
        "dedup": {
            "enabled": false,
            "dedup_after_update": true,
            "blacklist_threshold": 3,
            "blacklist_enabled": false,
            "blacklist_subid": "sril_bl",
            "subids": ["x", "y"]
        },
        "notification": {
            "enabled": true,
            "on_update": false,
            "on_test": true
        },
        "sync": {
            "source_db": ")" + syncSrcPath + R"(",
            "target_db": ")" + syncDstPath + R"(",
            "sync_skip_subids": false
        }
    })");
    std::optional<AppConfig> original = ConfigReader::load(configPath("sril_load.json"));
    ASSERT_TRUE(original.has_value());

    std::string savedPath = configPath("sril_roundtrip.json");
    ASSERT_TRUE(ConfigReader::save(savedPath, *original));

    std::optional<AppConfig> reloaded = ConfigReader::load(savedPath);
    ASSERT_TRUE(reloaded.has_value());

    EXPECT_EQ(reloaded->database_path, original->database_path);
    EXPECT_EQ(reloaded->sql_query, original->sql_query);
    EXPECT_EQ(reloaded->sql_by_subid, original->sql_by_subid);
    EXPECT_EQ(reloaded->proxy.xray_executable, original->proxy.xray_executable);
    EXPECT_EQ(reloaded->xray_workers, original->xray_workers);
    EXPECT_EQ(reloaded->xray_start_port, original->xray_start_port);
    EXPECT_EQ(reloaded->xray_api_port, original->xray_api_port);
    EXPECT_EQ(reloaded->test_url, original->test_url);
    EXPECT_EQ(reloaded->test_timeout_ms, original->test_timeout_ms);
    EXPECT_EQ(reloaded->log_enabled, original->log_enabled);
    EXPECT_EQ(reloaded->log_network_failures, original->log_network_failures);
    EXPECT_EQ(reloaded->log_console_level, original->log_console_level);
    EXPECT_EQ(reloaded->log_file_level, original->log_file_level);
    EXPECT_EQ(reloaded->accelerator_url, original->accelerator_url);
    EXPECT_EQ(reloaded->update_methods, original->update_methods);
    EXPECT_EQ(reloaded->check_auto_update_interval, original->check_auto_update_interval);
    EXPECT_EQ(reloaded->subscription_connect_timeout_ms, original->subscription_connect_timeout_ms);
    EXPECT_EQ(reloaded->subscription_timeout_ms, original->subscription_timeout_ms);
    EXPECT_EQ(reloaded->dedup_enabled, original->dedup_enabled);
    EXPECT_EQ(reloaded->dedup_after_update, original->dedup_after_update);
    EXPECT_EQ(reloaded->blacklist_threshold, original->blacklist_threshold);
    EXPECT_EQ(reloaded->blacklist_enabled, original->blacklist_enabled);
    EXPECT_EQ(reloaded->blacklist_subid, original->blacklist_subid);
    EXPECT_EQ(reloaded->dedup_subids, original->dedup_subids);
    EXPECT_EQ(reloaded->notification_enabled, original->notification_enabled);
    EXPECT_EQ(reloaded->notification_on_update, original->notification_on_update);
    EXPECT_EQ(reloaded->notification_on_test, original->notification_on_test);
    EXPECT_EQ(reloaded->sync.source_db, original->sync.source_db);
    EXPECT_EQ(reloaded->sync.target_db, original->sync.target_db);
    EXPECT_EQ(reloaded->sync.sync_skip_subids, original->sync.sync_skip_subids);
}

// --- Proxy section type validation ---
// Captures Logger output via callback to assert no spurious "wrong type" warning
// when proxy is a valid object, and that a correct warning is emitted when the
// proxy value has the wrong type.

namespace {

std::vector<std::string> g_capturedLogs;

void captureLogger(const std::string& msg, LogLevel) {
    g_capturedLogs.push_back(msg);
}

} // namespace

TEST_F(ConfigReaderLoadTest, ProxySectionValidObject_NoWrongTypeWarning) {
    writeConfig("proxy_valid.json", R"({
        "proxy": {
            "socks_base_port": 10808,
            "xray_executable": "C:/xray/xray.exe",
            "use_singbox": false
        }
    })");
    g_capturedLogs.clear();
    Logger::pushCallback(captureLogger);

    std::optional<AppConfig> result = ConfigReader::load(configPath("proxy_valid.json"));

    Logger::popCallback();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->proxy.socks_base_port, 10808);
    EXPECT_EQ(result->proxy.xray_executable, "C:/xray/xray.exe");
    // Regression: valid proxy object must NOT emit the wrong-type warning.
    for (const std::string& log : g_capturedLogs) {
        EXPECT_EQ(log.find("config.proxy has wrong type"), std::string::npos)
            << "unexpected spurious warning: " << log;
    }
}

TEST_F(ConfigReaderLoadTest, ProxySectionWrongType_UsesDefaultAndWarns) {
    writeConfig("proxy_wrong.json", R"({
        "proxy": "not-an-object"
    })");
    g_capturedLogs.clear();
    Logger::pushCallback(captureLogger);

    std::optional<AppConfig> result = ConfigReader::load(configPath("proxy_wrong.json"));

    Logger::popCallback();

    ASSERT_TRUE(result.has_value());
    // Defaults from AppConfig::proxy struct must be retained.
    EXPECT_EQ(result->proxy.socks_base_port, 10808);
    EXPECT_FALSE(result->proxy.use_singbox);

    bool found = false;
    for (const std::string& log : g_capturedLogs) {
        if (log.find("config.proxy has wrong type (expected object), using default") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "expected wrong-type warning with (expected object), using default";
}

TEST_F(ConfigReaderLoadTest, ProxyProcessMonitor_Defaults) {
    writeConfig("ppm_defaults.json", R"({
        "database": {"path": "test/guiNDB.db"},
        "xray": {"workers": 2}
    })");

    std::optional<AppConfig> result = ConfigReader::load(configPath("ppm_defaults.json"));

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->proxy_process_monitor.enabled);
    EXPECT_EQ(result->proxy_process_monitor.checkIntervalMs, 30000);
}

TEST_F(ConfigReaderLoadTest, ProxyProcessMonitor_CustomValues) {
    writeConfig("ppm_custom.json", R"({
        "database": {"path": "test/guiNDB.db"},
        "xray": {"workers": 2},
        "proxy_process_monitor": {
            "enabled": true,
            "check_interval_ms": 15000
        }
    })");

    std::optional<AppConfig> result = ConfigReader::load(configPath("ppm_custom.json"));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->proxy_process_monitor.enabled);
    EXPECT_EQ(result->proxy_process_monitor.checkIntervalMs, 15000);
}

TEST_F(ConfigReaderLoadTest, ProxyProcessMonitor_WrongType_UsesDefaultAndWarns) {
    writeConfig("ppm_wrong.json", R"({
        "database": {"path": "test/guiNDB.db"},
        "xray": {"workers": 2},
        "proxy_process_monitor": "not-an-object"
    })");
    g_capturedLogs.clear();
    Logger::pushCallback(captureLogger);

    std::optional<AppConfig> result = ConfigReader::load(configPath("ppm_wrong.json"));

    Logger::popCallback();

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->proxy_process_monitor.enabled);
    EXPECT_EQ(result->proxy_process_monitor.checkIntervalMs, 30000);

    bool found = false;
    for (const std::string& log : g_capturedLogs) {
        if (log.find("config.proxy_process_monitor has wrong type (expected object), using default") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "expected wrong-type warning";
}
// ============================================================
// ProxyProcessMonitor section tests
// ============================================================

TEST_F(ConfigReaderLoadTest, ProxyProcessMonitorDefaults) {
    writeConfig("empty.json", "{}");
    std::optional<AppConfig> result = ConfigReader::load(configPath("empty.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->proxy_process_monitor.enabled);
    EXPECT_EQ(result->proxy_process_monitor.checkIntervalMs, 30000);
}

TEST_F(ConfigReaderLoadTest, ProxyProcessMonitorCustomValues) {
    writeConfig("ppm.json", R"({
        "proxy_process_monitor": {
            "enabled": true,
            "check_interval_ms": 15000
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("ppm.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->proxy_process_monitor.enabled);
    EXPECT_EQ(result->proxy_process_monitor.checkIntervalMs, 15000);
}

TEST_F(ConfigReaderLoadTest, ProxyProcessMonitorClampsLowInterval) {
    writeConfig("ppm_low.json", R"({
        "proxy_process_monitor": {
            "enabled": true,
            "check_interval_ms": 1000
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("ppm_low.json"));
    ASSERT_TRUE(result.has_value());
    // Parser clamps to minimum 5000
    EXPECT_EQ(result->proxy_process_monitor.checkIntervalMs, 5000);
}

TEST_F(ConfigReaderLoadTest, ProxyProcessMonitorClampsHighInterval) {
    writeConfig("ppm_high.json", R"({
        "proxy_process_monitor": {
            "enabled": true,
            "check_interval_ms": 500000
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("ppm_high.json"));
    ASSERT_TRUE(result.has_value());
    // Parser clamps to maximum 300000
    EXPECT_EQ(result->proxy_process_monitor.checkIntervalMs, 300000);
}
