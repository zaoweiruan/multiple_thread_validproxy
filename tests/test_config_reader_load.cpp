#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include "test_utils.h"
#include "ConfigReader.h"

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

static void touchFile(const std::string& path) {
    std::ofstream f(path);
    f.close();
}

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
    EXPECT_EQ(result->priority_mode, "direct_first");
    EXPECT_TRUE(result->dedup_enabled);
    EXPECT_TRUE(result->blacklist_enabled);
}

TEST_F(ConfigReaderLoadTest, MissingDbFile) {
    writeConfig("nodbfile.json", R"({
        "database": "/nonexistent/path/db.db"
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("nodbfile.json"));
    EXPECT_FALSE(result.has_value());
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
            "executable": ")" + xrayExe + R"(",
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
        }
    })");
    std::optional<AppConfig> result = ConfigReader::load(configPath("full.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->database_path, dbPath);
    EXPECT_EQ(result->xray_executable, xrayExe);
    EXPECT_EQ(result->xray_workers, 4);
    EXPECT_EQ(result->xray_start_port, 2080);
    EXPECT_EQ(result->xray_api_port, 2081);
    EXPECT_EQ(result->test_url, "https://example.com");
    EXPECT_EQ(result->test_timeout_ms, 3000);
    EXPECT_TRUE(result->log_enabled);
    EXPECT_EQ(result->log_console_level, "INFO");
    EXPECT_EQ(result->priority_mode, "proxy_first");
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
