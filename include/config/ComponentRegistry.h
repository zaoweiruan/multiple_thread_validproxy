#ifndef COMPONENT_REGISTRY_H
#define COMPONENT_REGISTRY_H

#include "ComponentBundle.h"
#include <string>
#include <map>
#include <unordered_map>
#include <vector>

namespace config {

/// Combination strategy for component merging
enum class CombinationStrategy {
    OVERRIDE,  // Override - later value replaces earlier
    MERGE,     // Merge - combine values (e.g., routing rules append)
    APPEND     // Append - add to array (e.g., inbounds[], outbounds[])
};

/// Component metadata describing behavior and compatibility
struct ComponentMetadata {
    ComponentType type;
    std::string configSection;          // Xray config section name (e.g., "env", "log")
    std::string singboxSection;         // SingBox config section name (e.g., "route" for routing)
    bool isSingleton;                   // Whether only one instance allowed
    CombinationStrategy strategy;       // How to merge multiple instances
    bool supportsXray;                  // Supported by Xray core
    bool supportsSingBox;               // Supported by SingBox core
    std::string displayName;            // Human-readable name
    std::string description;            // Description
};

/// Component registry - singleton for component type metadata
class ComponentRegistry {
public:
    /// Get singleton instance
    static ComponentRegistry& instance();
    
    /// Register component metadata (called during initialization)
    void registerType(const ComponentMetadata& metadata);
    
    /// Get all registered component types
    const std::vector<ComponentType>& getAllTypes() const;
    
    /// Get metadata for a component type
    const ComponentMetadata* getMetadata(ComponentType type) const;
    
    /// Get metadata by config section name
    const ComponentMetadata* getMetadataBySection(const std::string& section) const;
    
    /// Check if component type is registered
    bool isRegistered(ComponentType type) const;
    
    /// Check if component type is singleton
    bool isSingleton(ComponentType type) const;
    
    /// Get combination strategy for component type
    CombinationStrategy getStrategy(ComponentType type) const;
    
    /// Check if component type is supported by core type
    bool supportsCoreType(ComponentType type, CoreType core) const;
    
    /// Get all component types supported by core type
    std::vector<ComponentType> getSupportedTypes(CoreType core) const;
    
    /// Convert ComponentType to config section name
    std::string typeToSection(ComponentType type) const;
    
    /// Convert ComponentType to string
    std::string typeToString(ComponentType type) const;
    
    /// Convert string to ComponentType
    ComponentType stringToType(const std::string& str) const;
    
    /// Register all default component types
    void registerDefaultTypes();
    
    /// Clear all registrations
    void clear();
    
    /// Get total number of registered types
    size_t count() const;

private:
    ComponentRegistry() = default;
    ~ComponentRegistry() = default;
    ComponentRegistry(const ComponentRegistry&) = delete;
    ComponentRegistry& operator=(const ComponentRegistry&) = delete;

    /// 首次访问时幂等注册默认组件类型（见 .cpp 中 instance() 说明）
    void ensureDefaults();

    std::unordered_map<ComponentType, ComponentMetadata> metadata_;
    std::unordered_map<std::string, ComponentType> sectionMap_;
    bool defaultsRegistered_{false};
};

} // namespace config

#endif // COMPONENT_REGISTRY_H
