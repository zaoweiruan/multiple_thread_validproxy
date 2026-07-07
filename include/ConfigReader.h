#ifndef CONFIG_READER_H
#define CONFIG_READER_H

#include <string>
#include <optional>
#include <vector>

namespace config {

struct AppConfig {
    std::string database_path;
    std::string sql_query;
    std::string sql_by_subid;
    int xray_workers = 1;
    int xray_start_port = 1083;
    int xray_api_port = 0;
    std::string test_url;
    int test_timeout_ms = 5000;
    bool log_enabled = true;
    bool log_network_failures = false;
    std::string log_console_level;
    std::string log_file_level;
    std::string accelerator_url;
    std::vector<std::string> update_methods;
    bool check_auto_update_interval = false;
    int subscription_connect_timeout_ms = 10000;  // Default: 10s connect timeout
    int subscription_timeout_ms = 30000;          // Default: 30s total timeout
    bool dedup_enabled = true;
    bool dedup_after_update = false;
    std::vector<std::string> dedup_subids;
    int blacklist_threshold = 5;
    bool blacklist_enabled = true;
    std::string blacklist_subid;
    bool notification_enabled = false;
    bool notification_on_update = false;
    bool notification_on_test = false;

    // Sync configuration
    struct {
        std::string source_db;
        std::string target_db;
        bool sync_skip_subids = false;
    } sync;

    // AutoTask configuration
    struct {
        std::vector<std::string> steps;
        bool notify_on_complete = true;
        std::string state_file;
    } auto_task;

    // NetworkMonitor configuration
    struct {
        bool enabled{true};
        std::vector<std::string> checkUrls{
            "https://www.baidu.com",
            "https://www.qq.com",
            "https://www.taobao.com"
        };
        int checkIntervalMs{10000};
        int checkTimeoutMs{5000};

        // Probe-on-disconnect: pause testing on disconnect, probe N times before cancel.
        // If maxProbes > 0: pause and probe before cancel (configurable grace window).
        // If maxProbes = 0: immediate cancel on first disconnect (legacy behavior).
        int maxProbes{3};
    } network_monitor;

    // Proxy configuration (standalone proxy)
    struct {
        int socks_base_port = 10808;
        std::string xray_executable;         // Xray executable path (moved from top-level)
        std::string xray_asset_dir;
        std::string template_config_path;
        bool use_singbox = false;            // Proxy backend selector: true=sing-box, false=Xray
        std::string singbox_executable;      // sing-box executable path (only relevant when use_singbox=true)
        std::string singbox_asset_dir;       // sing-box geoip/geosite resource directory
        std::string singbox_template_config_path;  // sing-box config template path
    } proxy;
};

class ConfigReader {
public:
    // Error reporting hook — defaults to MessageBoxA. Tests can replace to suppress popups.
    using ErrorReporter = void(*)(const std::string& title, const std::string& message);
    static ErrorReporter errorReporter_;

    static std::optional<AppConfig> load(const std::string& configPath);
    static bool save(const std::string& configPath, const AppConfig& config);
    static std::string getDefaultConfigPath();

private:
    ConfigReader() = default;
};

} // namespace config

#endif // CONFIG_READER_H