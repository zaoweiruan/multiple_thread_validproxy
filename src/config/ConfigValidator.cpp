#include <filesystem>

#include "config/ConfigValidator.h"

namespace config {

ConfigValidator::ValidationResult ConfigValidator::validate(const AppConfig& config, const std::string& configPath) {
    (void)configPath;
    ValidationResult result;
    result.valid = true;

    const std::vector<std::string> knownRuntimePlaceholders = {"{subid}", "{blacklist_threshold}"};
    const std::string sqlQueries[2] = {config.sql_query, config.sql_by_subid};
    const std::string sqlNames[2] = {"sql_query", "sql_by_subid"};

    for (int idx = 0; idx < 2; ++idx) {
        const std::string& sql = sqlQueries[idx];
        for (size_t i = 0; i < sql.size(); ++i) {
            if (sql[i] == '{') {
                bool isKnown = false;
                for (size_t k = 0; k < knownRuntimePlaceholders.size(); ++k) {
                    const std::string& ph = knownRuntimePlaceholders[k];
                    if (sql.compare(i, ph.size(), ph) == 0) {
                        isKnown = true;
                        break;
                    }
                }
                if (!isKnown) {
                    size_t closingBrace = sql.find('}', i + 1);
                    if (closingBrace != std::string::npos) {
                        std::string unknownPlaceholder = sql.substr(i, closingBrace - i + 1);
                        result.warnings.push_back(unknownPlaceholder + " in " + sqlNames[idx] + " was not substituted");
                    }
                }
            }
        }
    }

    if (!config.database_path.empty()) {
        std::filesystem::path dbPath(config.database_path);
        if (!std::filesystem::exists(dbPath)) {
            result.warnings.push_back("Database file not found: " + config.database_path);
        }
    }

    if (!config.proxy.xray_executable.empty()) {
        std::filesystem::path xrayPath(config.proxy.xray_executable);
        if (!std::filesystem::exists(xrayPath)) {
            result.warnings.push_back("Xray executable not found: " + config.proxy.xray_executable);
        }

        std::string ext = xrayPath.extension().string();
        if (!ext.empty() && ext != ".exe") {
            result.warnings.push_back("xray executable should have .exe extension: " + config.proxy.xray_executable);
        }
    }

    return result;
}

} // namespace config
