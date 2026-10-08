#include "config/ComponentRegistry.h"
#include <algorithm>
#include <stdexcept>

namespace config {

ComponentRegistry& ComponentRegistry::instance() {
    static ComponentRegistry registry;
    return registry;
}

void ComponentRegistry::registerType(const ComponentMetadata& metadata) {
    metadata_[metadata.type] = metadata;
    sectionMap_[metadata.configSection] = metadata.type;
}

const std::vector<ComponentType>& ComponentRegistry::getAllTypes() const {
    static std::vector<ComponentType> allTypes = {
        ComponentType::ENV, ComponentType::LOG, ComponentType::API,
        ComponentType::DNS, ComponentType::ROUTING, ComponentType::POLICY,
        ComponentType::INBOUND, ComponentType::OUTBOUND, ComponentType::STATS,
        ComponentType::FAKEDNS, ComponentType::METRICS, ComponentType::OBSERVATORY,
        ComponentType::BURST_OBSERVATORY, ComponentType::GEODATA, ComponentType::VERSION
    };
    return allTypes;
}

const ComponentMetadata* ComponentRegistry::getMetadata(ComponentType type) const {
    auto it = metadata_.find(type);
    if (it != metadata_.end()) {
        return &it->second;
    }
    return nullptr;
}

const ComponentMetadata* ComponentRegistry::getMetadataBySection(const std::string& section) const {
    auto it = sectionMap_.find(section);
    if (it != sectionMap_.end()) {
        auto typeIt = metadata_.find(it->second);
        if (typeIt != metadata_.end()) {
            return &typeIt->second;
        }
    }
    return nullptr;
}

bool ComponentRegistry::isRegistered(ComponentType type) const {
    return metadata_.find(type) != metadata_.end();
}

bool ComponentRegistry::isSingleton(ComponentType type) const {
    auto it = metadata_.find(type);
    if (it != metadata_.end()) {
        return it->second.isSingleton;
    }
    return false;
}

CombinationStrategy ComponentRegistry::getStrategy(ComponentType type) const {
    auto it = metadata_.find(type);
    if (it != metadata_.end()) {
        return it->second.strategy;
    }
    return CombinationStrategy::OVERRIDE;
}

bool ComponentRegistry::supportsCoreType(ComponentType type, CoreType core) const {
    auto it = metadata_.find(type);
    if (it != metadata_.end()) {
        if (core == CoreType::XRAY) {
            return it->second.supportsXray;
        } else {
            return it->second.supportsSingBox;
        }
    }
    return false;
}

std::vector<ComponentType> ComponentRegistry::getSupportedTypes(CoreType core) const {
    std::vector<ComponentType> supported;
    for (const auto& type : getAllTypes()) {
        if (supportsCoreType(type, core)) {
            supported.push_back(type);
        }
    }
    return supported;
}

std::string ComponentRegistry::typeToSection(ComponentType type) const {
    auto it = metadata_.find(type);
    if (it != metadata_.end()) {
        return it->second.configSection;
    }
    return "";
}

std::string ComponentRegistry::typeToString(ComponentType type) const {
    auto it = metadata_.find(type);
    if (it != metadata_.end()) {
        return it->second.configSection;
    }
    return "unknown";
}

ComponentType ComponentRegistry::stringToType(const std::string& str) const {
    auto it = sectionMap_.find(str);
    if (it != sectionMap_.end()) {
        return it->second;
    }
    // Try matching against all types
    for (const auto& type : getAllTypes()) {
        auto typeIt = metadata_.find(type);
        if (typeIt != metadata_.end() && typeIt->second.configSection == str) {
            return type;
        }
    }
    throw std::invalid_argument("Unknown component type: " + str);
}

void ComponentRegistry::registerDefaultTypes() {
    // Clear existing registrations
    clear();
    
    // Register all 15 component types with their metadata
    registerType({
        ComponentType::ENV, "env", "", true, CombinationStrategy::OVERRIDE,
        true, false, "Environment Variables", "Environment variables configuration"
    });
    
    registerType({
        ComponentType::LOG, "log", "log", true, CombinationStrategy::OVERRIDE,
        true, true, "Logging", "Logging configuration"
    });
    
    registerType({
        ComponentType::API, "api", "", true, CombinationStrategy::OVERRIDE,
        true, false, "API", "API server configuration"
    });
    
    registerType({
        ComponentType::DNS, "dns", "dns", true, CombinationStrategy::OVERRIDE,
        true, true, "DNS", "DNS configuration"
    });
    
    registerType({
        ComponentType::ROUTING, "routing", "route", true, CombinationStrategy::MERGE,
        true, true, "Routing", "Routing rules configuration"
    });
    
    registerType({
        ComponentType::POLICY, "policy", "", true, CombinationStrategy::OVERRIDE,
        true, false, "Policy", "System policy configuration"
    });
    
    registerType({
        ComponentType::INBOUND, "inbounds", "inbounds", false, CombinationStrategy::APPEND,
        true, true, "Inbounds", "Inbound listeners array"
    });
    
    registerType({
        ComponentType::OUTBOUND, "outbounds", "outbounds", false, CombinationStrategy::APPEND,
        true, true, "Outbounds", "Outbound connections array"
    });
    
    registerType({
        ComponentType::STATS, "stats", "", true, CombinationStrategy::OVERRIDE,
        true, false, "Statistics", "Statistics configuration"
    });
    
    registerType({
        ComponentType::FAKEDNS, "fakedns", "", true, CombinationStrategy::OVERRIDE,
        true, false, "FakeDNS", "Fake DNS configuration"
    });
    
    registerType({
        ComponentType::METRICS, "metrics", "", true, CombinationStrategy::OVERRIDE,
        true, false, "Metrics", "Metrics configuration"
    });
    
    registerType({
        ComponentType::OBSERVATORY, "observatory", "", true, CombinationStrategy::OVERRIDE,
        true, false, "Observatory", "Observatory configuration"
    });
    
    registerType({
        ComponentType::BURST_OBSERVATORY, "burstObservatory", "", true, CombinationStrategy::OVERRIDE,
        true, false, "Burst Observatory", "Burst observatory configuration"
    });
    
    registerType({
        ComponentType::GEODATA, "geodata", "", true, CombinationStrategy::OVERRIDE,
        true, false, "GeoData", "GeoData configuration"
    });
    
    registerType({
        ComponentType::VERSION, "version", "", true, CombinationStrategy::OVERRIDE,
        true, false, "Version", "Version information"
    });
}

void ComponentRegistry::clear() {
    metadata_.clear();
    sectionMap_.clear();
}

size_t ComponentRegistry::count() const {
    return metadata_.size();
}

} // namespace config
