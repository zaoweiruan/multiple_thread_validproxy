#include "config/ComponentResolver.h"
#include <algorithm>
#include <sstream>
#include <regex>

namespace config {

ComponentResolver::ComponentResolver(sqlite3* db)
    : db_(db) {
    // Ensure registry is initialized
    ComponentRegistry::instance();
}

// Convert std::map to boost::json::object
boost::json::object ComponentResolver::mapToJson(
    const std::map<std::string, boost::json::value>& map) {
    boost::json::object obj;
    for (const auto& pair : map) {
        obj[pair.first] = pair.second;
    }
    return obj;
}

// Convert boost::json::object to std::map
std::map<std::string, boost::json::value> ComponentResolver::jsonToMap(
    const boost::json::object& obj) {
    std::map<std::string, boost::json::value> map;
    for (const auto& pair : obj) {
        map[pair.key()] = pair.value();
    }
    return map;
}

ResolveResult ComponentResolver::resolve(
    const std::vector<ComponentConfig>& components,
    const PipelineContext& ctx) const {
    
    ResolveResult result;
    result.success = false;
    
    // 1. Detect conflicts
    result.conflicts = detectConflicts(components);
    
    // 2. Create configuration object
    boost::json::object config;
    
    // 3. Replace placeholders and apply components
    for (const auto& comp : components) {
        // Get component metadata
        const ComponentMetadata* metadata = ComponentRegistry::instance().getMetadata(comp.type);
        if (!metadata) {
            result.errorMessage = "未知组件类型: " + ComponentRegistry::instance().typeToString(comp.type);
            return result;
        }
        
        // Replace placeholders
        std::map<std::string, boost::json::value> resolvedParams = 
            replacePlaceholders(comp.params, ctx.vars);
        
        // Create component copy with resolved params
        ComponentConfig resolvedComp = comp;
        resolvedComp.params = resolvedParams;
        
        // Apply component
        applyComponent(config, resolvedComp, *metadata);
    }
    
    // 4. Set result
    result.config = boost::json::value(config);
    result.success = true;
    
    return result;
}

ResolveResult ComponentResolver::resolve(
    const std::vector<ComponentConfig>& components) const {
    
    // Create default context
    PipelineContext ctx;
    ctx.proxyType = ProxyType::Xray;
    ctx.hasProfile = false;
    ctx.vars = ConfigVariables();
    ctx.db = db_;
    ctx.clientName = "default";
    
    return resolve(components, ctx);
}

std::vector<ResolverConflict> ComponentResolver::detectConflicts(
    const std::vector<ComponentConfig>& components) const {

    std::vector<ResolverConflict> conflicts;
    
    // Group by type
    auto grouped = groupByType(components);
    
    // Detect singleton component duplicates
    for (const auto& pair : grouped) {
        ComponentType type = pair.first;
        const std::vector<ComponentConfig>& comps = pair.second;
        std::string typeName = ComponentRegistry::instance().typeToString(type);
        
        // Check if it's a singleton component
        bool isSingleton = ComponentRegistry::instance().isSingleton(type);
        
        if (isSingleton && comps.size() > 1) {
            // Singleton component duplicate
            for (size_t i = 1; i < comps.size(); ++i) {
                ResolverConflict conflict;
                conflict.conflictType = ResolverConflictType::SINGLETON_DUPLICATE;
                conflict.component1 = comps[0].id.empty() ? typeName : comps[0].id;
                conflict.component2 = comps[i].id.empty() ? typeName : comps[i].id;
                conflict.componentType = typeName;
                conflict.message = "单例组件重复: " + typeName + " (保留最后一个)";
                conflict.isCrossPackage = false;
                conflicts.push_back(conflict);
            }
        }
        
        // Detect array component tag duplicates
        if (!isSingleton) {
            std::unordered_map<std::string, size_t> tagIndex;
            for (size_t i = 0; i < comps.size(); ++i) {
                const auto& comp = comps[i];
                auto tagIt = comp.params.find("tag");
                if (tagIt != comp.params.end() && tagIt->second.is_string()) {
                    std::string tag(tagIt->second.as_string());
                    if (tagIndex.find(tag) != tagIndex.end()) {
                        // Tag duplicate
                        ResolverConflict conflict;
                        conflict.conflictType = ResolverConflictType::ARRAY_TAG_DUPLICATE;
                        conflict.component1 = comps[tagIndex[tag]].id.empty() ? 
                            (typeName + ":" + tag) : comps[tagIndex[tag]].id;
                        conflict.component2 = comp.id.empty() ? 
                            (typeName + ":" + tag) : comp.id;
                        conflict.componentType = typeName;
                        conflict.message = "数组组件 tag 重复: " + tag;
                        conflict.isCrossPackage = false;
                        conflicts.push_back(conflict);
                    } else {
                        tagIndex[tag] = i;
                    }
                }
            }
        }
    }
    
    return conflicts;
}

boost::json::value ComponentResolver::replacePlaceholdersInValue(
    const boost::json::value& value,
    const ConfigVariables& vars) const {
    
    if (value.is_string()) {
        std::string str(value.as_string());
        
        // Replace ${socksPort}
        str = std::regex_replace(str, std::regex(R"(\$\{socksPort\})"), std::to_string(vars.socksPort));
        // Replace ${apiPort}
        str = std::regex_replace(str, std::regex(R"(\$\{apiPort\})"), std::to_string(vars.apiPort));
        // Replace ${probeUrl}
        str = std::regex_replace(str, std::regex(R"(\$\{probeUrl\})"), vars.probeUrl);
        // Replace ${observatoryInterval}
        str = std::regex_replace(str, std::regex(R"(\$\{observatoryInterval\})"), std::to_string(vars.observatoryInterval));
        // Replace ${balancerStrategy}
        str = std::regex_replace(str, std::regex(R"(\$\{balancerStrategy\})"), vars.balancerStrategy);
        // Replace ${enableObservatory}
        str = std::regex_replace(str, std::regex(R"(\$\{enableObservatory\})"), vars.enableObservatory ? "true" : "false");
        // Replace ${enableCnDirect}
        str = std::regex_replace(str, std::regex(R"(\$\{enableCnDirect\})"), vars.enableCnDirect ? "true" : "false");
        // Replace ${blockQuic}
        str = std::regex_replace(str, std::regex(R"(\$\{blockQuic\})"), vars.blockQuic ? "true" : "false");
        
        // Try to convert to number
        try {
            size_t pos = 0;
            int num = std::stoi(str, &pos);
            if (pos == str.size()) {
                return boost::json::value(num);
            }
        } catch (...) {
            // Not a number, keep as string
        }
        
        return boost::json::value(str);
    } else if (value.is_object()) {
        // Recursively process object
        boost::json::object newObj;
        for (const auto& pair : value.as_object()) {
            newObj[pair.key()] = replacePlaceholdersInValue(pair.value(), vars);
        }
        return boost::json::value(newObj);
    } else if (value.is_array()) {
        // Process array
        boost::json::array newArr;
        for (const auto& item : value.as_array()) {
            newArr.push_back(replacePlaceholdersInValue(item, vars));
        }
        return boost::json::value(newArr);
    }
    
    return value;
}

std::map<std::string, boost::json::value> ComponentResolver::replacePlaceholders(
    const std::map<std::string, boost::json::value>& params,
    const ConfigVariables& vars) const {
    
    std::map<std::string, boost::json::value> result;
    
    for (const auto& pair : params) {
        result[pair.first] = replacePlaceholdersInValue(pair.second, vars);
    }
    
    return result;
}

std::unordered_map<ComponentType, std::vector<ComponentConfig>> 
ComponentResolver::groupByType(const std::vector<ComponentConfig>& components) const {
    
    std::unordered_map<ComponentType, std::vector<ComponentConfig>> grouped;
    
    for (const auto& comp : components) {
        grouped[comp.type].push_back(comp);
    }
    
    return grouped;
}

void ComponentResolver::applyArrayComponent(
    boost::json::object& config,
    const std::string& section,
    const std::vector<ComponentConfig>& components) const {
    
    // Get or create array
    if (!config.contains(section)) {
        boost::json::array arr;
        config[section] = boost::json::value(arr);
    }
    
    boost::json::array& arr = config[section].as_array();
    
    // Deduplicate by tag
    for (const auto& comp : components) {
        boost::json::object compObj = mapToJson(comp.params);
        
        auto tagIt = comp.params.find("tag");
        if (tagIt != comp.params.end() && tagIt->second.is_string()) {
            std::string newTag(tagIt->second.as_string());
            
            // Check if same tag already exists
            bool found = false;
            for (auto& item : arr) {
                if (item.is_object()) {
                    const boost::json::object& itemObj = item.as_object();
                    auto existingTag = itemObj.find("tag");
                    if (existingTag != itemObj.end() && 
                        existingTag->value().is_string() &&
                        std::string(existingTag->value().as_string()) == newTag) {
                        // Replace existing item
                        item = boost::json::value(compObj);
                        found = true;
                        break;
                    }
                }
            }
            
            if (!found) {
                arr.push_back(boost::json::value(compObj));
            }
        } else {
            // Component without tag, just append
            arr.push_back(boost::json::value(compObj));
        }
    }
}

void ComponentResolver::applySingletonComponent(
    boost::json::object& config,
    const std::string& section,
    const ComponentConfig& component) const {
    
    // Direct override
    boost::json::object compObj = mapToJson(component.params);
    config[section] = boost::json::value(compObj);
}

void ComponentResolver::applyRoutingComponent(
    boost::json::object& config,
    const ComponentConfig& component) const {
    
    // Routing merge: merge rules and balancers
    if (!config.contains("routing")) {
        boost::json::object routing;
        config["routing"] = boost::json::value(routing);
    }
    
    boost::json::object& routing = config["routing"].as_object();
    boost::json::object compParams = mapToJson(component.params);
    
    // Merge rules
    auto rulesIt = compParams.find("rules");
    if (rulesIt != compParams.end() && rulesIt->value().is_array()) {
        if (!routing.contains("rules")) {
            boost::json::array rules;
            routing["rules"] = boost::json::value(rules);
        }
        
        boost::json::array& existingRules = routing["rules"].as_array();
        for (const auto& rule : rulesIt->value().as_array()) {
            existingRules.push_back(rule);
        }
    }
    
    // Merge balancers
    auto balancersIt = compParams.find("balancers");
    if (balancersIt != compParams.end() && balancersIt->value().is_array()) {
        if (!routing.contains("balancers")) {
            boost::json::array balancers;
            routing["balancers"] = boost::json::value(balancers);
        }
        
        boost::json::array& existingBalancers = routing["balancers"].as_array();
        for (const auto& balancer : balancersIt->value().as_array()) {
            existingBalancers.push_back(balancer);
        }
    }
}

void ComponentResolver::applyComponent(
    boost::json::object& config,
    const ComponentConfig& component,
    const ComponentMetadata& metadata) const {
    
    CombinationStrategy strategy = metadata.strategy;
    
    if (strategy == CombinationStrategy::APPEND) {
        // Array component (inbound/outbound)
        applyArrayComponent(config, metadata.configSection, {component});
    } else if (strategy == CombinationStrategy::MERGE) {
        // Routing component (merge)
        applyRoutingComponent(config, component);
    } else {
        // Singleton component (override)
        applySingletonComponent(config, metadata.configSection, component);
    }
}

} // namespace config
