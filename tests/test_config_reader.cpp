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
    original.xray_executable = "myxray.exe";
    original.xray_workers = 8;
    original.xray_start_port = 2080;
    original.xray_api_port = 2081;
    original.test_url = "https://test.com";
    original.test_timeout_ms = 3000;
    original.log_enabled = false;
    original.log_network_failures = true;
    original.log_console_level = "WARN";
    original.log_file_level = "INFO";
    original.priority_mode = "proxy_first";
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
    EXPECT_TRUE(content.find("proxy_first") != std::string::npos);
}