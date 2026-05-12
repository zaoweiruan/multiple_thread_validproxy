#ifndef CONFIG_READER_H
#define CONFIG_READER_H

#include <string>
#include <optional>

namespace config {

struct AppConfig {
    std::string database_path;
    std::string sql_query;
    std::string sql_by_subid;
    std::string xray_executable;
    int xray_workers;
    int xray_start_port;
    int xray_api_port;
    std::string test_url;
    int test_timeout_ms;
    bool log_enabled;
    bool log_network_failures;
    std::string log_console_level;
    std::string log_file_level;
    std::string priority_mode;
    bool check_auto_update_interval = false;
    bool dedup_enabled;
    bool dedup_after_update;
    std::vector<std::string> dedup_subids;
    int blacklist_threshold;  // Number of consecutive failures before blacklisting
    bool notification_enabled;
    bool notification_on_update;
    bool notification_on_test;

    // Sync configuration
    struct {
        std::string source_db;
        std::string target_db;
        bool sync_skip_subids = false;  // When true, -S skips proxies whose Subid is in dedup_subids
    } sync;
};

class ConfigReader {
public:
    static std::optional<AppConfig> load(const std::string& configPath);
    static std::string getDefaultConfigPath();

private:
    ConfigReader() = default;
};

} // namespace config

#endif // CONFIG_READER_H
