#include "config/ConfigBuilder.h"
#include "Logger.h"
#include "ConfigReader.h"
#include "FullConfigTemplateDAO.h"
#include "config/OutboundBuilderFactory.h"
#include "config/SingBoxOutboundBuilderFactory.h"
#include "config/XrayConfigAssembler.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <functional>
#include <boost/json.hpp>

namespace config {

// Helper: Convert ConfigVariables to a map for variable replacement
static std::unordered_map<std::string, std::string> variablesToMap(const ConfigVariables& vars) {
    std::unordered_map<std::string, std::string> map;
    map["socksPort"] = std::to_string(vars.socksPort);
    map["apiPort"] = std::to_string(vars.apiPort);
    map["probeUrl"] = vars.probeUrl;
    map["observatoryInterval"] = std::to_string(vars.observatoryInterval);
    map["balancerStrategy"] = vars.balancerStrategy;
    map["enableObservatory"] = vars.enableObservatory ? "true" : "false";
    map["enableCnDirect"] = vars.enableCnDirect ? "true" : "false";
    map["blockQuic"] = vars.blockQuic ? "true" : "false";
    return map;
}

// Helper: Replace ${key} placeholders in a string with values from the map
// If the placeholder is inside quotes and the value is a valid number, remove the quotes
static std::string replacePlaceholders(const std::string& input, 
                                        const std::unordered_map<std::string, std::string>& variables) {
    std::string result = input;
    
    for (const auto& [key, value] : variables) {
        std::string placeholder = "${" + key + "}";
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            // Check if the placeholder is inside quotes and the value is a valid number
            bool isQuoted = (pos > 0 && result[pos - 1] == '"') && 
                           (pos + placeholder.length() < result.length() && result[pos + placeholder.length()] == '"');
            bool isNumber = !value.empty() && 
                           std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); });
            
            if (isQuoted && isNumber) {
                // Remove the surrounding quotes and replace with unquoted number
                result.replace(pos - 1, placeholder.length() + 2, value);
                pos += value.length();
            } else {
                // Simple string replacement
                result.replace(pos, placeholder.length(), value);
                pos += value.length();
            }
        }
    }
    
    return result;
}

// 统一路由：移除现有 socks 入站规则，添加新的 socks→outbound 规则
// outboundTag: 出站标签 (proxy 或 balancer-out)
// useBalancerTag: 使用 balancerTag 而非 outboundTag (仅 Pool 模式)
static void mergeSocksInboundRule(boost::json::object& root,
                                  const std::string& outboundTag,
                                  bool useBalancerTag = false) {
    if (!root.contains("routing")) {
        root["routing"] = boost::json::object{{"domainStrategy", "AsIs"}};
    }
    boost::json::object& routing = root["routing"].as_object();
    if (!routing.contains("rules")) {
        routing["rules"] = boost::json::array{};
    }
    boost::json::array& rules = routing["rules"].as_array();

    // 移除现有 socks 入站规则
    boost::json::array filteredRules;
    for (const boost::json::value& rule : rules) {
        if (rule.is_object()) {
            const boost::json::object& ruleObj = rule.as_object();
            if (ruleObj.contains("inboundTag")) {
                const boost::json::value& inboundTagVal = ruleObj.at("inboundTag");
                if (inboundTagVal.is_array()) {
                    bool isSocks = false;
                    for (const boost::json::value& tagVal : inboundTagVal.as_array()) {
                        if (tagVal.is_string() && tagVal.as_string() == "socks") {
                            isSocks = true;
                            break;
                        }
                    }
                    if (isSocks) continue;
                }
            }
        }
        filteredRules.push_back(rule);
    }

    // 添加 socks 规则
    boost::json::object socksRule;
    socksRule["type"] = "field";
    boost::json::array inTag;
    inTag.push_back(boost::json::value("socks"));
    socksRule["inboundTag"] = inTag;
    if (useBalancerTag) {
        socksRule["balancerTag"] = outboundTag;
    } else {
        socksRule["outboundTag"] = outboundTag;
    }
    filteredRules.push_back(socksRule);

    routing["rules"] = std::move(filteredRules);
}

// 配置深合并：父配置为基底，子配置覆盖
// 规则：对象递归合并，数组/标量覆盖
// maxDepth: 防止循环继承导致无限递归
boost::json::value ConfigBuilder::mergeConfig(
    const boost::json::value& parent,
    const boost::json::value& child,
    int maxDepth) {
    
    const int maxAllowedDepth = maxDepth;
    
    // 递归辅助函数
    std::function<boost::json::value(const boost::json::value&, const boost::json::value&, int)> mergeImpl =
        [&](const boost::json::value& p, const boost::json::value& c, int depth) -> boost::json::value {
            if (depth > maxAllowedDepth) {
                throw std::runtime_error("[ConfigBuilder] mergeConfig: max depth exceeded (possible circular inheritance)");
            }
            
            if (!p.is_object()) {
                return c;
            }
            
            if (!c.is_object()) {
                return c;
            }
            
            boost::json::object result = p.as_object();
            
            for (const auto& [key, childValue] : c.as_object()) {
                if (result.contains(key)) {
                    const boost::json::value& parentValue = result[key];
                    if (parentValue.is_object() && childValue.is_object()) {
                        boost::json::value merged = mergeImpl(parentValue, childValue, depth + 1);
                        result[key] = std::move(merged);
                    } else {
                        result[key] = childValue;
                    }
                } else {
                    result[key] = childValue;
                }
            }
            
            return boost::json::value(result);
        };
    
    return mergeImpl(parent, child, 0);
}

// 从数据库加载模板（按 ID），支持递归继承
boost::json::value ConfigBuilder::loadTemplateById(
    sqlite3* db,
    const std::string& templateId,
    int maxDepth) {
    
    // 使用递归辅助函数处理深度追踪
    std::function<boost::json::value(const std::string&, int)> loadImpl =
        [&](const std::string& tid, int depth) -> boost::json::value {
            if (depth > maxDepth) {
                throw std::runtime_error("[ConfigBuilder] loadTemplateById: max depth exceeded (possible circular inheritance)");
            }
            
            db::FullConfigTemplateDAO dao(db);
            auto templateItem = dao.getById(tid);
            
            if (!templateItem.has_value()) {
                throw std::runtime_error(
                    "[ConfigBuilder] Template not found: " + tid);
            }
            
            if (templateItem->config.empty()) {
                throw std::runtime_error(
                    "[ConfigBuilder] Template config is empty for id: " + tid);
            }
            
            // 解析子配置
            boost::json::value childConfig;
            try {
                childConfig = boost::json::parse(templateItem->config);
            } catch (const std::exception& e) {
                throw std::runtime_error(
                    "[ConfigBuilder] Failed to parse template JSON for " + tid +
                    ": " + std::string(e.what()));
            }
            
            // 如果有父模板，递归加载并合并
            if (!templateItem->parentId.empty()) {
                boost::json::value parentConfig = loadImpl(templateItem->parentId, depth + 1);
                return mergeConfig(parentConfig, childConfig, maxDepth);
            }
            
            // 无父模板，直接返回
            return childConfig;
        };
    
    return loadImpl(templateId, 0);
}

ConfigBuilder::ConfigBuilder(sqlite3* db) : db_(db) {
    // Configuration: could initialize proxy_clients table here
    // For now, we use default values
}

bool ConfigBuilder::isEnableNewConfigBuilder(const std::string& configPath) {
    // Always enabled - ConfigBuilder is the only path for config generation
    (void)configPath;
    return true;
}

ProxyType ConfigBuilder::getDefaultProxyType() const {
    // Default to Xray
    return ProxyType::Xray;
}

ProxyType ConfigBuilder::resolveProxyType(const std::string& clientName) const {
    if (clientName == "singbox") {
        return ProxyType::SingBox;
    }
    return ProxyType::Xray; // default
}

std::optional<ProxyClientInfo> ConfigBuilder::getClientInfo(const std::string& name) const {
    ProxyClientInfo info;
    info.name = name;
    
    if (name == "xray") {
        info.executable = "bin/xray.exe";
        info.templateDir = "bin";
        info.apiPortBase = 10115;
        info.socksPortBase = 10808;
        info.assetDir = "bin";
        info.isDefault = true;
        info.enabled = true;
    } else if (name == "singbox") {
        info.executable = "bin/singbox.exe";
        info.templateDir = "bin";
        info.apiPortBase = 0; // sing-box doesn't use API port
        info.socksPortBase = 10808;
        info.assetDir = "bin";
        info.isDefault = false;
        info.enabled = true;
    } else {
        return std::nullopt;
    }
    
    return info;
}

boost::json::value ConfigBuilder::loadTemplate(const std::string& templateKey) const {
    // Map templateKey to template ID for precise lookup
    // templateKey format: "<mode>_<backend>" e.g., "test_xray", "standalone_singbox", "pool_xray"
    // Template IDs in DB: "xray_default_001", "xray_test_001", "pool_xray_001", etc.
    
    std::string templateId;
    
    if (templateKey == "standalone_xray") {
        templateId = "xray_default_001";
    } else if (templateKey == "standalone_singbox") {
        templateId = "singbox_default_001";
    } else if (templateKey == "test_xray") {
        templateId = "xray_test_001";
    } else if (templateKey == "test_singbox") {
        templateId = "singbox_test_001";
    } else if (templateKey == "pool_xray") {
        templateId = "pool_xray_001";
    } else if (templateKey == "pool_singbox") {
        templateId = "pool_singbox_001";
    } else {
        throw std::runtime_error(
            "[ConfigBuilder] Unknown templateKey: " + templateKey +
            ". Supported: standalone_xray, standalone_singbox, test_xray, test_singbox, pool_xray, pool_singbox");
    }
    
    // Load template from database (with inheritance support)
    try {
        boost::json::value templateJson = loadTemplateById(db_, templateId);
        
        // Log successful template load
        Logger::write("[ConfigBuilder] Loaded template from DB: " + templateKey + 
                     " (id=" + templateId + ")", LogLevel::DEBUG);
        
        return templateJson;
    } catch (const std::exception& e) {
        throw std::runtime_error(
            "[ConfigBuilder] Failed to load template for key: " + templateKey +
            " (id=" + templateId + "): " + std::string(e.what()));
    }
}

boost::json::value ConfigBuilder::replaceVariables(
    const boost::json::value& config,
    const ConfigVariables& vars) const {
    // Convert ConfigVariables to a map
    auto variables = variablesToMap(vars);
    
    // Serialize the config to a string
    std::string jsonStr = boost::json::serialize(config);
    
    // Replace placeholders
    std::string replacedStr = replacePlaceholders(jsonStr, variables);
    
    // Parse the string back to a JSON value
    try {
        return boost::json::parse(replacedStr);
    } catch (const std::exception& e) {
        Logger::write("[ConfigBuilder] Failed to parse replaced JSON: " + std::string(e.what()), LogLevel::ERR);
        return config; // Return original config if parsing fails
    }
}

boost::json::value ConfigBuilder::build(
    ConfigMode mode,
    const db::models::Profileitem& profile,
    const ConfigVariables& vars) {
    
    ProxyType proxyType = getDefaultProxyType();
    
    switch (mode) {
        case ConfigMode::Test:
            return buildTestConfig(proxyType, profile, vars);
        case ConfigMode::Standalone:
            return buildStandaloneConfig(proxyType, profile, vars);
        case ConfigMode::Pool:
            return buildPoolConfig(proxyType, profile, vars);
        default:
            throw std::runtime_error("Unknown config mode");
    }
}

boost::json::value ConfigBuilder::build(
    ConfigMode mode,
    const std::string& clientName,
    const db::models::Profileitem& profile,
    const ConfigVariables& vars) {
    
    ProxyType proxyType = resolveProxyType(clientName);
    
    switch (mode) {
        case ConfigMode::Test:
            return buildTestConfig(proxyType, profile, vars);
        case ConfigMode::Standalone:
            return buildStandaloneConfig(proxyType, profile, vars);
        case ConfigMode::Pool:
            return buildPoolConfig(proxyType, profile, vars);
        default:
            throw std::runtime_error("Unknown config mode");
    }
}

std::string ConfigBuilder::serialize(const boost::json::value& config) const {
    // Return compact JSON string
    return boost::json::serialize(config);
}

std::string ConfigBuilder::serializeCompact(const boost::json::value& config) const {
    return boost::json::serialize(config);
}

bool ConfigBuilder::validateConfig(
    const boost::json::value& config,
    ConfigMode mode,
    ProxyType proxyType) const {
    
    if (!config.is_object()) {
        throw std::runtime_error(
            "[ConfigBuilder] validateConfig: config is not a JSON object");
    }
    
    const boost::json::object& root = config.as_object();
    std::string errors;
    
    // Check required top-level fields
    if (!root.contains("log")) {
        errors += "  - missing 'log' section\n";
    }
    
    if (!root.contains("inbounds")) {
        errors += "  - missing 'inbounds' section\n";
    } else {
        const boost::json::value& inboundsVal = root.at("inbounds");
        if (!inboundsVal.is_array()) {
            errors += "  - 'inbounds' is not an array\n";
        } else if (inboundsVal.as_array().empty()) {
            errors += "  - 'inbounds' is empty\n";
        }
    }
    
    if (!root.contains("outbounds")) {
        errors += "  - missing 'outbounds' section\n";
    } else {
        const boost::json::value& outboundsVal = root.at("outbounds");
        if (!outboundsVal.is_array()) {
            errors += "  - 'outbounds' is not an array\n";
        } else if (outboundsVal.as_array().empty()) {
            errors += "  - 'outbounds' is empty\n";
        }
    }
    
    // Mode-specific validation
    if (mode == ConfigMode::Test || mode == ConfigMode::Pool) {
        if (proxyType == ProxyType::Xray) {
            // Xray test/pool configs require API section
            if (!root.contains("api")) {
                std::string modeStr = (mode == ConfigMode::Test) ? "Test" : "Pool";
                errors += std::string("  - missing 'api' section (required for Xray ") + modeStr + " mode)\n";
            } else {
                const boost::json::value& apiVal = root.at("api");
                if (!apiVal.is_object()) {
                    errors += "  - 'api' is not an object\n";
                } else {
                    const boost::json::object& apiObj = apiVal.as_object();
                    if (!apiObj.contains("tag")) {
                        errors += "  - 'api' missing 'tag'\n";
                    }
                    if (mode == ConfigMode::Pool && !apiObj.contains("listen")) {
                        errors += "  - 'api' missing 'listen' (required for Pool mode)\n";
                    }
                    if (!apiObj.contains("services")) {
                        errors += "  - 'api' missing 'services'\n";
                    }
                }
            }
            
            // Check stats and policy for test mode
            if (mode == ConfigMode::Test) {
                if (!root.contains("stats")) {
                    errors += "  - missing 'stats' section (required for Test mode)\n";
                }
                if (!root.contains("policy")) {
                    errors += "  - missing 'policy' section (required for Test mode)\n";
                }
            }
        }
    }
    
    // Check routing for Xray
    if (proxyType == ProxyType::Xray) {
        if (!root.contains("routing")) {
            errors += "  - missing 'routing' section\n";
        }
    }
    
    // Check route for SingBox
    if (proxyType == ProxyType::SingBox) {
        if (!root.contains("route")) {
            errors += "  - missing 'route' section\n";
        }
    }
    
    if (!errors.empty()) {
        Logger::write("[ConfigBuilder] Config validation failed:\n" + errors, LogLevel::ERR);
        std::string modeStr = (mode == ConfigMode::Test) ? "Test" : 
                              (mode == ConfigMode::Standalone) ? "Standalone" : "Pool";
        std::string proxyStr = (proxyType == ProxyType::Xray) ? "Xray" : "SingBox";
        std::string errorMsg = std::string("[ConfigBuilder] Config validation failed for mode=") + modeStr +
                              ", proxyType=" + proxyStr + ":\n" + errors;
        throw std::runtime_error(errorMsg);
    }
    
    Logger::write("[ConfigBuilder] Config validation passed", LogLevel::DEBUG);
    return true;
}

boost::json::value ConfigBuilder::buildTestConfig(
    ProxyType proxyType,
    const db::models::Profileitem& profile,
    const ConfigVariables& vars) {
    
    // Test config: base configuration only (inbounds, routing, dns, log)
    // Proxy outbound is added dynamically via gRPC API, not in the base config.
    // So we don't need to validate the profile address here.
    
    // Load template from database (throws on failure)
    std::string backend = (proxyType == ProxyType::Xray) ? "xray" : "singbox";
    std::string templateKey = "test_" + backend;
    boost::json::value templateJson = loadTemplate(templateKey);
    
    // Replace placeholders with runtime values
    boost::json::value configJson = replaceVariables(templateJson, vars);
    boost::json::object root = configJson.as_object();
    Logger::write("[ConfigBuilder] Using template from DB for test config: " + templateKey, LogLevel::DEBUG);
    
    // Add direct outbound (for test mode, we need a direct route)
    boost::json::array outbounds;
    
    // Only add proxy outbound if profile has valid address (for backward compatibility)
    // Proxy outbound is added first to match the order expected by routing rules
    if (!profile.address.empty()) {
        if (proxyType == ProxyType::Xray) {
            OutboundBuilderFactory factory;
            boost::json::object proxyOutbound = factory.create(profile, "proxy");
            outbounds.push_back(proxyOutbound);
        } else if (proxyType == ProxyType::SingBox) {
            SingBoxOutboundBuilderFactory factory;
            boost::json::object proxyOutbound = factory.create(profile, "proxy");
            outbounds.push_back(proxyOutbound);
        }
    }
    
    // Add direct outbound (always last, for direct route)
    boost::json::object directOut;
    directOut["tag"] = "direct";
    directOut["protocol"] = "freedom";
    boost::json::object directSettings;
    directSettings["domainStrategy"] = "AsIs";
    directOut["settings"] = directSettings;
    outbounds.push_back(directOut);
    
    root["outbounds"] = outbounds;
    
    // Merge routing: keep template's rules, add socks→proxy
    mergeSocksInboundRule(root, "proxy");

    return boost::json::value(root);
}

boost::json::value ConfigBuilder::buildStandaloneConfig(
    ProxyType proxyType,
    const db::models::Profileitem& profile,
    const ConfigVariables& vars) {
    
    // Validate profile
    if (profile.address.empty()) {
        Logger::write("[ConfigBuilder] Standalone config: proxy address is empty", LogLevel::ERR);
        return boost::json::object{};
    }
    
    // Load template from database (throws on failure)
    std::string backend = (proxyType == ProxyType::Xray) ? "xray" : "singbox";
    std::string templateKey = "standalone_" + backend;
    boost::json::value templateJson = loadTemplate(templateKey);
    
    // Replace placeholders with runtime values
    boost::json::value configJson = replaceVariables(templateJson, vars);
    boost::json::object root = configJson.as_object();
    Logger::write("[ConfigBuilder] Using template from DB for standalone config: " + templateKey, LogLevel::DEBUG);
    
    // Outbound: Generate proxy outbound using appropriate factory
    boost::json::array outbounds;
    
    if (proxyType == ProxyType::Xray) {
        OutboundBuilderFactory factory;
        boost::json::object proxyOutbound = factory.create(profile, "proxy");
        outbounds.push_back(proxyOutbound);
    } else if (proxyType == ProxyType::SingBox) {
        SingBoxOutboundBuilderFactory factory;
        boost::json::object proxyOutbound = factory.create(profile, "proxy");
        outbounds.push_back(proxyOutbound);
    }
    
    // Add direct outbound
    boost::json::object directOut;
    directOut["tag"] = "direct";
    directOut["protocol"] = "freedom";
    outbounds.push_back(directOut);
    
    // Add block outbound (matches template)
    boost::json::object blockOut;
    blockOut["tag"] = "block";
    blockOut["protocol"] = "blackhole";
    outbounds.push_back(blockOut);
    
    root["outbounds"] = outbounds;
    
    // Routing (aligned with template)
    // Merge routing: keep template's rules, add socks→proxy
    mergeSocksInboundRule(root, "proxy");

    return root;
}

boost::json::value ConfigBuilder::buildPoolConfig(
    ProxyType proxyType,
    const db::models::Profileitem& profile,
    const ConfigVariables& vars) {
    
    // Validate profile
    if (profile.address.empty()) {
        Logger::write("[ConfigBuilder] Pool config: proxy address is empty", LogLevel::ERR);
        return boost::json::object{};
    }
    
    // Load template from database (throws on failure)
    std::string backend = (proxyType == ProxyType::Xray) ? "xray" : "singbox";
    std::string templateKey = "pool_" + backend;
    boost::json::value templateJson = loadTemplate(templateKey);
    
    // Replace placeholders with runtime values
    boost::json::value configJson = replaceVariables(templateJson, vars);
    boost::json::object root = configJson.as_object();
    Logger::write("[ConfigBuilder] Using template from DB for pool config: " + templateKey, LogLevel::DEBUG);
    
    // Outbound: Generate proxy outbound using appropriate factory
    boost::json::array outbounds;
    
    // Direct outbound
    boost::json::object directOut;
    directOut["tag"] = "direct";
    directOut["protocol"] = "freedom";
    boost::json::object directSettings;
    directSettings["domainStrategy"] = "AsIs";
    directOut["settings"] = directSettings;
    outbounds.push_back(directOut);
    
    // Block outbound
    boost::json::object blockOut;
    blockOut["tag"] = "block";
    blockOut["protocol"] = "blackhole";
    outbounds.push_back(blockOut);
    
    // Note: No proxy outbound is generated from the profile for pool config.
    // The pool starts with no members; proxies are injected later via 
    // injectMember() with tags like "px-<indexId>". The balancer selector "px-"
    // matches only injected members, not a static "proxy" outbound.
    
    root["outbounds"] = outbounds;
    
    // Observatory (always add/update)
    boost::json::object observatory;
    boost::json::array obsSelector;
    obsSelector.push_back(boost::json::value("px-"));
    observatory["subjectSelector"] = obsSelector;
    observatory["probeURL"] = vars.probeUrl;
    observatory["probeInterval"] = std::to_string(vars.observatoryInterval / 1000) + "s";
    root["observatory"] = observatory;
    
    // Balancer declaration (xray 26.x): the legacy outbound protocol
    // "balancing" is no longer registered, so the balancer lives under
    // routing.balancers. Members are selected by the "px-" prefix.
    boost::json::array balancers;
    boost::json::object balancer;
    balancer["tag"] = "balancer-out";
    boost::json::array selector;
    selector.push_back(boost::json::value("px-"));
    balancer["selector"] = selector;
    boost::json::object strategy;
    strategy["type"] = vars.balancerStrategy;
    balancer["strategy"] = strategy;
    // Wire an observatory directly to the balancer. xray 26.x only surfaces an
    // outbound via GetOutboundStatus when its observatory is attached to a
    // balancer; a bare top-level observatory is not reported.
    boost::json::object observation;
    boost::json::array obsSubject;
    obsSubject.push_back(boost::json::value("px-"));
    observation["subjectSelector"] = obsSubject;
    observation["probeURL"] = vars.probeUrl;
    observation["probeInterval"] = std::to_string(vars.observatoryInterval / 1000) + "s";
    balancer["observation"] = observation;
    balancers.push_back(balancer);
    
    // Update routing: preserve template's rules, add balancers, add socks rule
    if (!root.contains("routing")) {
        root["routing"] = boost::json::object{{"domainStrategy", "AsIs"}};
    }
    boost::json::object& routing = root["routing"].as_object();
    routing["balancers"] = balancers;
    
    // Add socks→balancer-out rule
    mergeSocksInboundRule(root, "balancer-out", true);

    return root;
}

} // namespace config