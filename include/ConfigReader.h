#ifndef CONFIG_READER_H
#define CONFIG_READER_H

#include <string>
#include <optional>

namespace config {

struct AppConfig {
    std::string database_path;
    std::string sql_query;
    std::string xray_executable;
    int xray_workers;
    int xray_start_port;
    int xray_api_port;
    std::string test_url;
    int test_timeout_ms;
    bool log_enabled;
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
