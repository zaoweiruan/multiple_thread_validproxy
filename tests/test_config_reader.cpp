#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include "test_utils.h"
#include "ConfigReader.h"

using namespace config;

// ============================================================
// Modal-dialog interception.
//
// ConfigReader's default error reporter (src/ConfigReader.cpp:35-37) calls
// MessageBoxA(). Under a headless test runner that modal dialog blocks the
// process until something dismisses it: loading a missing config file stalled
// the whole ConfigReaderTest suite for ~82 s.
//
// ErrorReporter is a plain function POINTER (include/ConfigReader.h:139,
// `void(*)(const std::string&, const std::string&)`), so it cannot capture --
// the captured state has to live in namespace-scope storage that the free
// function below writes into. TearDown() restores the original pointer, so
// every other test binary keeps the real MessageBoxA behaviour.
// ============================================================
namespace {
struct CapturedError {
    std::string title;
    std::string message;
};

std::vector<CapturedError> g_capturedErrors;

void captureErrorReport(const std::string& title, const std::string& message) {
    g_capturedErrors.push_back(CapturedError{title, message});
    std::fprintf(stderr, "[ConfigReader error] %s: %s\n", title.c_str(), message.c_str());
}
}  // namespace

// ============================================================
// ConfigReader Test Fixture - uses TempDir for file I/O
// ============================================================
class ConfigReaderTest : public ::testing::Test {
protected:
    // Each TEST_F instance builds its own TempDir, and TempDir() derives a RANDOM
    // directory name ("test_tmp_<8 hex>", see tests/test_utils.h:19-31). A file
    // written by one TEST_F is therefore NOT visible to any other TEST_F -- every
    // test has to create the files it reads.
    TempDir tempDir_;

    // ConfigReader's error reporter is replaced for the duration of each test so
    // the modal MessageBoxA is never shown; what it WOULD have displayed is kept
    // in the file-scope g_capturedErrors vector (see captureErrorReport above).
    void SetUp() override {
        savedErrorReporter_ = ConfigReader::errorReporter_;
        ConfigReader::errorReporter_ = &captureErrorReport;
        g_capturedErrors.clear();
    }

    void TearDown() override {
        ConfigReader::errorReporter_ = savedErrorReporter_;
        g_capturedErrors.clear();
    }

    bool noConfigErrors() const { return g_capturedErrors.empty(); }

    // Flattens every captured (title, message) pair into one string so an ASSERT
    // can print what the dialog WOULD have said instead of just "false".
    std::string errorSummary() const {
        std::string out;
        for (std::size_t i = 0; i < g_capturedErrors.size(); ++i) {
            if (i != 0) out += "\n---\n";
            out += g_capturedErrors[i].title + ": " + g_capturedErrors[i].message;
        }
        return out;
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
    ConfigReader::ErrorReporter savedErrorReporter_;
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
// independent_probe + scoring weights 往返 — 方案 B 新字段
// ============================================================
TEST_F(ConfigReaderTest, SaveRoundTrip_IndependentProbeAndScoring) {
    std::string tmpDirGeneric = std::filesystem::path(tempDir_.path()).generic_string();

    AppConfig original;
    original.database_path = tmpDirGeneric + "/fc_db.db";
    original.proxy.xray_executable = tmpDirGeneric + "/fc_xray.exe";
    original.independent_probe.enabled = false;
    original.proxy.scoring_delay_weight = 0.4;
    original.proxy.scoring_stability_weight = 0.35;
    original.proxy.scoring_history_weight = 0.25;

    touchFile(original.database_path);
    touchFile(original.proxy.xray_executable);

    ASSERT_TRUE(ConfigReader::save(configPath("fc_indprobe.json"), original));

    std::optional<AppConfig> loaded = ConfigReader::load(configPath("fc_indprobe.json"));
    ASSERT_TRUE(loaded.has_value());

    EXPECT_EQ(loaded->independent_probe.enabled, false);
    EXPECT_DOUBLE_EQ(loaded->proxy.scoring_delay_weight, 0.4);
    EXPECT_DOUBLE_EQ(loaded->proxy.scoring_stability_weight, 0.35);
    EXPECT_DOUBLE_EQ(loaded->proxy.scoring_history_weight, 0.25);
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
// ============================================================
// port.* section (2026-10-12 Spec StandalonePortImmediateRebind)
// ============================================================

TEST_F(ConfigReaderTest, PortTimeWaitBlocking_DefaultsToFalseWhenSectionMissing) {
    writeConfig("no_port.json", R"({"database_path":"x.db"})");
    const auto cfg = ConfigReader::load(configPath("no_port.json"));
    ASSERT_TRUE(cfg.has_value());
    EXPECT_FALSE(cfg->port.time_wait_blocking)
        << "port.time_wait_blocking must default to false when the section is absent";
}

TEST_F(ConfigReaderTest, PortTimeWaitBlocking_ReadsFalseExplicitly) {
    writeConfig("port_off.json", R"({"port":{"time_wait_blocking":false}})");
    const auto cfg = ConfigReader::load(configPath("port_off.json"));
    ASSERT_TRUE(cfg.has_value());
    EXPECT_FALSE(cfg->port.time_wait_blocking);
}

TEST_F(ConfigReaderTest, PortTimeWaitBlocking_ReadsTrue) {
    writeConfig("port_on.json", R"({"port":{"time_wait_blocking":true}})");
    const auto cfg = ConfigReader::load(configPath("port_on.json"));
    ASSERT_TRUE(cfg.has_value());
    EXPECT_TRUE(cfg->port.time_wait_blocking);
}

TEST_F(ConfigReaderTest, PortTimeWaitBlocking_WrongTypeKeepsDefaultAndWarns) {
    writeConfig("port_bad.json", R"({"port":{"time_wait_blocking":"yes"}})");
    const auto cfg = ConfigReader::load(configPath("port_bad.json"));
    ASSERT_TRUE(cfg.has_value());
    EXPECT_FALSE(cfg->port.time_wait_blocking)
        << "a wrong-typed value must not flip the switch";
}

TEST_F(ConfigReaderTest, PortTimeWaitBlocking_SurvivesSaveRoundTrip) {
    // This TEST_F has its own TempDir, so it must write the file it reads.
    // Reading a file written by another TEST_F hits ConfigFileStore::read's
    // failure path, which used to raise a modal MessageBox and stall the suite.
    writeConfig("no_port.json", R"({"database_path":"x.db"})");
    auto cfg = ConfigReader::load(configPath("no_port.json"));
    ASSERT_TRUE(cfg.has_value()) << errorSummary();
    cfg->port.time_wait_blocking = true;
    ASSERT_TRUE(ConfigReader::save(configPath("rt_port.json"), *cfg)) << errorSummary();
    const auto reloaded = ConfigReader::load(configPath("rt_port.json"));
    ASSERT_TRUE(reloaded.has_value()) << errorSummary();
    EXPECT_TRUE(reloaded->port.time_wait_blocking)
        << "port.time_wait_blocking must survive save/reload";
    EXPECT_TRUE(noConfigErrors()) << "a clean round-trip must not report any config error";
}

// ============================================================
// proxy.api_base_port (2026-10-12 Spec StandalonePortImmediateRebind, Task 3 / P0-3)
// The standalone proxy API port used to be the hard-coded literal 10085 at
// src/ui/AppController.cpp:880 with no availability check, so a second proxy
// (or the pool) collided on it and fell into the 5s failure branches.
// ============================================================

TEST_F(ConfigReaderTest, ProxyApiBasePort_DefaultsTo10085WhenMissing) {
    writeConfig("no_api_base.json", R"({"database_path":"x.db"})");
    const auto cfg = ConfigReader::load(configPath("no_api_base.json"));
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->proxy.api_base_port, 10085);
}

TEST_F(ConfigReaderTest, ProxyApiBasePort_ReadsExplicitValue) {
    writeConfig("api_base.json", R"({"database_path":"x.db","proxy":{"api_base_port":10150}})");
    const auto cfg = ConfigReader::load(configPath("api_base.json"));
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->proxy.api_base_port, 10150);
}

TEST_F(ConfigReaderTest, ProxyApiBasePort_SurvivesSaveRoundTrip) {
    // This TEST_F has its own TempDir, so it must write the file it reads.
    writeConfig("no_api_base.json", R"({"database_path":"x.db"})");
    auto cfg = ConfigReader::load(configPath("no_api_base.json"));
    ASSERT_TRUE(cfg.has_value()) << errorSummary();
    cfg->proxy.api_base_port = 10200;
    ASSERT_TRUE(ConfigReader::save(configPath("rt_api_base.json"), *cfg)) << errorSummary();
    const auto reloaded = ConfigReader::load(configPath("rt_api_base.json"));
    ASSERT_TRUE(reloaded.has_value()) << errorSummary();
    EXPECT_EQ(reloaded->proxy.api_base_port, 10200)
        << "proxy.api_base_port must survive save/reload";
    EXPECT_TRUE(noConfigErrors()) << "a clean round-trip must not report any config error";
}