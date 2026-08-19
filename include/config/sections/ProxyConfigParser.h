#ifndef CONFIG_SECTIONS_PROXY_H
#define CONFIG_SECTIONS_PROXY_H

#include <boost/json.hpp>
#include <filesystem>
#include <string>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class ProxyConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir);
};

inline void ProxyConfigParser::parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir) {
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();
    if (obj.contains("proxy") && obj.at("proxy").is_object()) {
        const boost::json::object& proxy = obj.at("proxy").as_object();
        if (proxy.contains("socks_base_port") && proxy.at("socks_base_port").is_int64()) {
            config.proxy.socks_base_port = static_cast<int>(proxy.at("socks_base_port").as_int64());
            if (config.proxy.socks_base_port <= 0) config.proxy.socks_base_port = 10808;
        } else if (proxy.contains("socks_base_port")) {
            Logger::write("WARNING: config.proxy.socks_base_port has wrong type (expected int64), using default", LogLevel::WARN);
        }
        if (proxy.contains("xray_executable") && proxy.at("xray_executable").is_string()) {
            std::string rawPath = proxy.at("xray_executable").as_string().c_str();
            std::filesystem::path p(rawPath);
            if (!p.is_absolute()) p = std::filesystem::path(exeDir) / p;
            config.proxy.xray_executable = p.string();
        } else if (proxy.contains("xray_executable")) {
            Logger::write("WARNING: config.proxy.xray_executable has wrong type (expected string), using default", LogLevel::WARN);
        }
        if (proxy.contains("use_singbox") && proxy.at("use_singbox").is_bool()) {
            config.proxy.use_singbox = proxy.at("use_singbox").as_bool();
        } else if (proxy.contains("use_singbox")) {
            Logger::write("WARNING: config.proxy.use_singbox has wrong type (expected bool), using default", LogLevel::WARN);
        }
        if (proxy.contains("xray_asset_dir") && proxy.at("xray_asset_dir").is_string()) {
            std::string rawPath = proxy.at("xray_asset_dir").as_string().c_str();
            std::filesystem::path p(rawPath);
            if (!p.is_absolute()) p = std::filesystem::path(exeDir) / p;
            config.proxy.xray_asset_dir = p.string();
        } else if (proxy.contains("xray_asset_dir")) {
            Logger::write("WARNING: config.proxy.xray_asset_dir has wrong type (expected string), using default", LogLevel::WARN);
        }
        if (proxy.contains("template_config_path") && proxy.at("template_config_path").is_string()) {
            std::string rawPath = proxy.at("template_config_path").as_string().c_str();
            std::filesystem::path p(rawPath);
            if (!p.is_absolute()) p = std::filesystem::path(exeDir) / p;
            config.proxy.template_config_path = p.string();
        } else if (proxy.contains("template_config_path")) {
            Logger::write("WARNING: config.proxy.template_config_path has wrong type (expected string), using default", LogLevel::WARN);
        }
        // sing-box configuration
        // Scoring weights (0.0 to 1.0)
        if (proxy.contains("scoring_delay_weight") && proxy.at("scoring_delay_weight").is_number()) {
            double v = proxy.at("scoring_delay_weight").as_double();
            if (v >= 0.0 && v <= 1.0) config.proxy.scoring_delay_weight = v;
        }
        if (proxy.contains("scoring_stability_weight") && proxy.at("scoring_stability_weight").is_number()) {
            double v = proxy.at("scoring_stability_weight").as_double();
            if (v >= 0.0 && v <= 1.0) config.proxy.scoring_stability_weight = v;
        }
        if (proxy.contains("scoring_history_weight") && proxy.at("scoring_history_weight").is_number()) {
            double v = proxy.at("scoring_history_weight").as_double();
            if (v >= 0.0 && v <= 1.0) config.proxy.scoring_history_weight = v;
        }
        if (proxy.contains("singbox_executable") && proxy.at("singbox_executable").is_string()) {
            std::string rawPath = proxy.at("singbox_executable").as_string().c_str();
            std::filesystem::path p(rawPath);
            if (!p.is_absolute()) p = std::filesystem::path(exeDir) / p;
            config.proxy.singbox_executable = p.string();
        } else if (proxy.contains("singbox_executable")) {
            Logger::write("WARNING: config.proxy.singbox_executable has wrong type (expected string), using default", LogLevel::WARN);
        }
        if (proxy.contains("singbox_asset_dir") && proxy.at("singbox_asset_dir").is_string()) {
            std::string rawPath = proxy.at("singbox_asset_dir").as_string().c_str();
            std::filesystem::path p(rawPath);
            if (!p.is_absolute()) p = std::filesystem::path(exeDir) / p;
            config.proxy.singbox_asset_dir = p.string();
        } else if (proxy.contains("singbox_asset_dir")) {
            Logger::write("WARNING: config.proxy.singbox_asset_dir has wrong type (expected string), using default", LogLevel::WARN);
        }
        if (proxy.contains("singbox_template_config_path") && proxy.at("singbox_template_config_path").is_string()) {
            std::string rawPath = proxy.at("singbox_template_config_path").as_string().c_str();
            std::filesystem::path p(rawPath);
            if (!p.is_absolute()) p = std::filesystem::path(exeDir) / p;
            config.proxy.singbox_template_config_path = p.string();
        } else if (proxy.contains("singbox_template_config_path")) {
            Logger::write("WARNING: config.proxy.singbox_template_config_path has wrong type (expected string), using default", LogLevel::WARN);
        }
    } else if (obj.contains("proxy")) {
        Logger::write("WARNING: config.proxy has wrong type (expected object), using default", LogLevel::WARN);
    }
}

} // namespace config
#endif
