#ifndef CONFIG_READER_H
#define CONFIG_READER_H

#include <string>
#include <optional>
#include <vector>

namespace config {

// Standalone proxy pool configuration (single Xray process, dynamic member injection)
struct StandalonePoolConfig {
    bool enabled = false;
    std::string mode = "pool";                    // "pool" | "select"
    int socksPort = 10809;
    int apiPort = 10810;
    std::string balancerStrategy = "leastPing";  // "random" | "leastPing" | "leastLoad"
    std::string probeUrl;                          // probe URL for the observatory; when set,
                                                   // buildPoolConfig reuses it (config.json test.url)
                                                   // instead of observatory.destination.
    struct {
        std::string type = "http";                // "http" | "ping"
        std::string destination = "https://www.google.com";
        int intervalSec = 5;
        int samplingCount = 10;
        int timeoutSec = 5;
    } observatory;
    struct {
        int intervalSec = 10;
        bool reportHealth = true;                 // a
        bool autoPruneDead = false;               // b
        int pruneFailStreak = 3;                  // b 阈值
        bool autoOptimize = false;                // c
        int probeWorkers = 2;                     // 常驻 Xray 探针 worker 数（非直连成员健康探测）
    } evaluate;
};

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
    std::string log_console_level;
    std::string log_file_level;
    std::string accelerator_url;
    std::vector<std::string> update_methods;
    bool check_auto_update_interval = false;
    int subscription_connect_timeout_ms = 10000;  // Default: 10s connect timeout
    int subscription_timeout_ms = 30000;          // Default: 30s total timeout
    std::vector<std::string> priority_subids;      // Subscriptions sorted to top of list on startup
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

    // ProxyProcessMonitor configuration
    struct {
        bool enabled{false};
        int checkIntervalMs{30000};
    } proxy_process_monitor;

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
        // Scoring weights (0.0-1.0, sum not required to equal 1.0)
        double scoring_delay_weight = 0.2;
        double scoring_stability_weight = 0.3;
        double scoring_history_weight = 0.5;
    } proxy;

    StandalonePoolConfig standalone_pool;
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