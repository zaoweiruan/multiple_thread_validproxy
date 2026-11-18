#ifndef CONFIG_SECTIONS_UI_H
#define CONFIG_SECTIONS_UI_H

#include <boost/json.hpp>
#include <string>

#include "ConfigReader.h"
#include "Logger.h"
#include "i18n/Language.h"

namespace config {

class UiConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir);
};

inline void UiConfigParser::parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir) {
    (void)exeDir;
    config.ui.language = "zh-CN";  // default

    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();

    if (obj.contains("ui")) {
        if (!obj.at("ui").is_object()) {
            Logger::write("WARNING: config.ui has wrong type (expected object), using default", LogLevel::WARN);
            return;
        }
        const boost::json::object& ui = obj.at("ui").as_object();
        if (ui.contains("language")) {
            if (ui.at("language").is_string()) {
                const std::string raw(ui.at("language").as_string().c_str());
                i18n::Language lang = i18n::Language::ZhCN;
                if (i18n::parseLanguage(raw, lang)) {
                    config.ui.language = i18n::languageCode(lang);
                } else {
                    Logger::write("WARNING: config.ui.language has invalid value '" + raw +
                                      "' (expected \"zh-CN\" or \"en-US\"), using default",
                                  LogLevel::WARN);
                }
            } else {
                Logger::write("WARNING: config.ui.language has wrong type (expected string), using default",
                              LogLevel::WARN);
            }
        }
    }
}

} // namespace config
#endif
