#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include "test_utils.h"
#include "ConfigReader.h"

using namespace config;

// ============================================================
// ConfigReader Test Fixture - uses TempDir for file I/O
// ============================================================
class ConfigReaderTest : public ::testing::Test {
protected:
    TempDir tempDir_;

    void writeConfig(const std::string& filename, const std::string& content) {
        std::ofstream file(tempDir_.path() + "/" + filename);
        file << content;
        file.close();
    }

    std::string configPath(const std::string& filename) {
        return tempDir_.path() + "/" + filename;
    }
};

// ============================================================
// Save and Reload Round Trip - tests save() only
// ============================================================
TEST_F(ConfigReaderTest, SaveRoundTrip) {
    AppConfig original;
    original.database_path = "mydb.db";
    original.proxy.xray_executable = "myxray.exe";
    original.xray_workers = 8;
    original.xray_start_port = 2080;
    original.xray_api_port = 2081;
    original.test_url = "https://test.com";
    original.test_timeout_ms = 3000;
    original.log_enabled = false;
    original.log_network_failures = true;
    original.log_console_level = "WARN";
    original.log_file_level = "INFO";
    original.accelerator_url = "https://cdn.acc.com/";
    original.update_methods = {"proxy"};
    original.check_auto_update_interval = true;
    original.dedup_enabled = false;
    original.dedup_after_update = true;
    original.blacklist_threshold = 20;
    original.dedup_subids = {"subA", "subB"};
    original.notification_enabled = true;
    original.notification_on_update = true;
    original.notification_on_test = true;
    original.sync.source_db = "src.db";
    original.sync.target_db = "dst.db";
    original.sync.sync_skip_subids = true;

    EXPECT_TRUE(ConfigReader::save(configPath("output.json"), original));

    // Verify file was created and contains content
    std::ifstream in(configPath("output.json"));
    ASSERT_TRUE(in.is_open());
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    EXPECT_TRUE(content.find("mydb.db") != std::string::npos);
    EXPECT_TRUE(content.find("myxray.exe") != std::string::npos);
    EXPECT_TRUE(content.find("cdn.acc.com") != std::string::npos);
    EXPECT_TRUE(content.find("update_methods") != std::string::npos);
    EXPECT_TRUE(content.find("proxy") != std::string::npos);
}

// ============================================================
// Full-field save/load round-trip — all struct members
// ============================================================

TEST_F(ConfigReaderTest, SaveRoundTrip_FieldCompleteness) {
    std::string tmpDirGeneric = std::filesystem::path(tempDir_.path()).generic_string();

    AppConfig original;
    original.database_path = tmpDirGeneric + "/fc_db.db";
    original.sql_query = "SELECT * FROM profiles";
    original.sql_by_subid = "SELECT * FROM profiles WHERE subid = '{subid}'";
    original.proxy.xray_executable = tmpDirGeneric + "/fc_xray.exe";
    original.xray_workers = 4;
    original.xray_start_port = 2080;
    original.xray_api_port = 2081;
    original.test_url = "https://fc.example.com";
    original.test_timeout_ms = 3000;
    original.log_enabled = false;
    original.log_network_failures = true;
    original.log_console_level = "WARN";
    original.log_file_level = "ERROR";
    original.accelerator_url = "https://fc.acc.com/";
    original.update_methods = {"accelerator", "proxy", "direct"};
    original.check_auto_update_interval = true;
    original.subscription_connect_timeout_ms = 5000;
    original.subscription_timeout_ms = 20000;
    original.dedup_enabled = false;
    original.dedup_after_update = true;
    original.blacklist_threshold = 10;
    original.blacklist_enabled = false;
    original.blacklist_subid = "fc_bl";
    original.dedup_subids = {"subA", "subB", "subC"};
    original.notification_enabled = true;
    original.notification_on_update = true;
    original.notification_on_test = false;
    original.sync.source_db = tmpDirGeneric + "/fc_src.db";
    original.sync.target_db = tmpDirGeneric + "/fc_dst.db";
    original.sync.sync_skip_subids = true;
    original.auto_task.steps = {"step1", "step2", "step3"};
    original.auto_task.notify_on_complete = false;
    original.auto_task.state_file = tmpDirGeneric + "/fc_state.json";
    original.network_monitor.enabled = false;
    original.network_monitor.checkUrls = {"https://fc.example.com"};
    original.network_monitor.checkIntervalMs = 15000;
    original.network_monitor.checkTimeoutMs = 8000;
    original.proxy_process_monitor.enabled = true;
    original.proxy_process_monitor.checkIntervalMs = 5000;

    // Create files needed by load validation
    touchFile(original.database_path);
    touchFile(original.proxy.xray_executable);

    ASSERT_TRUE(ConfigReader::save(configPath("fc_roundtrip.json"), original));

    std::optional<AppConfig> loaded = ConfigReader::load(configPath("fc_roundtrip.json"));
    ASSERT_TRUE(loaded.has_value());

    EXPECT_EQ(loaded->database_path, original.database_path);
    EXPECT_EQ(loaded->sql_query, original.sql_query);
    EXPECT_EQ(loaded->sql_by_subid, original.sql_by_subid);
    EXPECT_EQ(loaded->proxy.xray_executable, original.proxy.xray_executable);
    EXPECT_EQ(loaded->xray_workers, original.xray_workers);
    EXPECT_EQ(loaded->xray_start_port, original.xray_start_port);
    EXPECT_EQ(loaded->xray_api_port, original.xray_api_port);
    EXPECT_EQ(loaded->test_url, original.test_url);
    EXPECT_EQ(loaded->test_timeout_ms, original.test_timeout_ms);
    EXPECT_EQ(loaded->log_enabled, original.log_enabled);
    EXPECT_EQ(loaded->log_network_failures, original.log_network_failures);
    EXPECT_EQ(loaded->log_console_level, original.log_console_level);
    EXPECT_EQ(loaded->log_file_level, original.log_file_level);
    EXPECT_EQ(loaded->accelerator_url, original.accelerator_url);
    EXPECT_EQ(loaded->update_methods, original.update_methods);
    EXPECT_EQ(loaded->check_auto_update_interval, original.check_auto_update_interval);
    EXPECT_EQ(loaded->subscription_connect_timeout_ms, original.subscription_connect_timeout_ms);
    EXPECT_EQ(loaded->subscription_timeout_ms, original.subscription_timeout_ms);
    EXPECT_EQ(loaded->dedup_enabled, original.dedup_enabled);
    EXPECT_EQ(loaded->dedup_after_update, original.dedup_after_update);
    EXPECT_EQ(loaded->blacklist_threshold, original.blacklist_threshold);
    EXPECT_EQ(loaded->blacklist_enabled, original.blacklist_enabled);
    EXPECT_EQ(loaded->blacklist_subid, original.blacklist_subid);
    EXPECT_EQ(loaded->dedup_subids, original.dedup_subids);
    EXPECT_EQ(loaded->notification_enabled, original.notification_enabled);
    EXPECT_EQ(loaded->notification_on_update, original.notification_on_update);
    EXPECT_EQ(loaded->notification_on_test, original.notification_on_test);
    EXPECT_EQ(loaded->sync.source_db, original.sync.source_db);
    EXPECT_EQ(loaded->sync.target_db, original.sync.target_db);
    EXPECT_EQ(loaded->sync.sync_skip_subids, original.sync.sync_skip_subids);
    EXPECT_EQ(loaded->auto_task.steps, original.auto_task.steps);
    EXPECT_EQ(loaded->auto_task.notify_on_complete, original.auto_task.notify_on_complete);
    EXPECT_EQ(loaded->auto_task.state_file, original.auto_task.state_file);
    EXPECT_EQ(loaded->network_monitor.enabled, original.network_monitor.enabled);
    EXPECT_EQ(loaded->network_monitor.checkUrls, original.network_monitor.checkUrls);
    EXPECT_EQ(loaded->network_monitor.checkIntervalMs, original.network_monitor.checkIntervalMs);
    EXPECT_EQ(loaded->network_monitor.checkTimeoutMs, original.network_monitor.checkTimeoutMs);
    EXPECT_EQ(loaded->proxy_process_monitor.enabled, original.proxy_process_monitor.enabled);
    EXPECT_EQ(loaded->proxy_process_monitor.checkIntervalMs, original.proxy_process_monitor.checkIntervalMs);
}

// ============================================================
// Save overwrite — second save replaces first
// ============================================================

TEST_F(ConfigReaderTest, SaveOverwrite) {
    AppConfig first;
    first.xray_workers = 2;
    first.test_timeout_ms = 3000;

    ASSERT_TRUE(ConfigReader::save(configPath("overwrite.json"), first));

    AppConfig second;
    second.xray_workers = 8;
    second.test_timeout_ms = 10000;

    ASSERT_TRUE(ConfigReader::save(configPath("overwrite.json"), second));

    // Load and verify the saved config has the SECOND values
    std::optional<AppConfig> loaded = ConfigReader::load(configPath("overwrite.json"));
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->xray_workers, 8);
    EXPECT_EQ(loaded->test_timeout_ms, 10000);
}

// ============================================================
// Save to custom nested path
// ============================================================

TEST_F(ConfigReaderTest, SaveToCustomPath) {
    AppConfig cfg;
    cfg.xray_workers = 5;

    // Verify save() when parent directory exists
    std::string nestedPath = tempDir_.path() + "/custom_nested/output.json";
    std::filesystem::create_directories(tempDir_.path() + "/custom_nested");

    EXPECT_TRUE(ConfigReader::save(nestedPath, cfg));

    // Verify file was created with content
    std::ifstream in(nestedPath);
    ASSERT_TRUE(in.is_open());
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    EXPECT_GT(content.size(), 0u);
    EXPECT_TRUE(content.find("workers") != std::string::npos);
    EXPECT_TRUE(content.find("5") != std::string::npos);
}