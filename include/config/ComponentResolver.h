#ifndef COMPONENT_RESOLVER_H
#define COMPONENT_RESOLVER_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <map>
#include <sqlite3.h>
#include <boost/json.hpp>
#include "config/ComponentBundle.h"
#include "config/ComponentRegistry.h"
#include "config/StepHandler.h"

namespace config {

/// Conflict type enumeration (ComponentResolver-local; distinct from
/// config::ConflictType in Conflict.h to avoid ODR violation)
enum class ResolverConflictType {
    SINGLETON_DUPLICATE,      // Singleton component duplicate
    ARRAY_TAG_DUPLICATE,      // Array component tag duplicate
    CROSS_PACKAGE_OVERRIDE    // Cross-package override (allowed, warning only)
};

/// Conflict information (ComponentResolver-local; distinct from
/// config::Conflict in Conflict.h to avoid ODR violation)
struct ResolverConflict {
    ResolverConflictType conflictType; // Conflict type
    std::string component1;            // Component 1 ID
    std::string component2;            // Component 2 ID (optional)
    std::string componentType;         // Component type name
    std::string message;               // Description
    bool isCrossPackage;               // Whether cross-package override
};

/// Component resolution result
struct ResolveResult {
    boost::json::value config;       // Resolved configuration
    std::vector<ResolverConflict> conflicts; // Detected conflicts
    bool success;                    // Whether successful
    std::string errorMessage;        // Error message (on failure)
};

/// Component resolver
/// Resolves a list of components into a final configuration JSON
/// Supports three combination strategies: OVERRIDE, MERGE, APPEND
class ComponentResolver {
public:
    explicit ComponentResolver(sqlite3* db);
    
    /// Resolve components to configuration
    /// 1. Replace placeholders (using ctx.vars)
    /// 2. Detect conflicts
    /// 3. Apply components to configuration according to strategy
    ResolveResult resolve(
        const std::vector<ComponentConfig>& components,
        const PipelineContext& ctx) const;
    
    /// Resolve components to configuration (simplified, no context)
    /// Uses default ConfigVariables
    ResolveResult resolve(
        const std::vector<ComponentConfig>& components) const;
    
    /// Detect component conflicts
    std::vector<ResolverConflict> detectConflicts(
        const std::vector<ComponentConfig>& components) const;
    
private:
    /// Convert std::map to boost::json::object
    static boost::json::object mapToJson(const std::map<std::string, boost::json::value>& map);
    
    /// Convert boost::json::object to std::map
    static std::map<std::string, boost::json::value> jsonToMap(const boost::json::object& obj);
    
    /// Replace placeholders in component parameters
    std::map<std::string, boost::json::value> replacePlaceholders(
        const std::map<std::string, boost::json::value>& params,
        const ConfigVariables& vars) const;
    
    /// Replace placeholders in a JSON string value
    boost::json::value replacePlaceholdersInValue(
        const boost::json::value& value,
        const ConfigVariables& vars) const;
    
    /// Group components by type
    std::unordered_map<ComponentType, std::vector<ComponentConfig>> 
        groupByType(const std::vector<ComponentConfig>& components) const;
    
    /// Apply array component (append strategy)
    /// For inbound/outbound, deduplicate by tag
    void applyArrayComponent(
        boost::json::object& config,
        const std::string& section,
        const std::vector<ComponentConfig>& components) const;
    
    /// Apply singleton component (override strategy)
    /// For log/api/dns/stats etc., later overrides earlier
    void applySingletonComponent(
        boost::json::object& config,
        const std::string& section,
        const ComponentConfig& component) const;
    
    /// Apply routing component (merge strategy)
    /// For routing, merge rules
    void applyRoutingComponent(
        boost::json::object& config,
        const ComponentConfig& component) const;
    
    /// Apply component to JSON object
    void applyComponent(
        boost::json::object& config,
        const ComponentConfig& component,
        const ComponentMetadata& metadata) const;
    
    sqlite3* db_;
};

} // namespace config

#endif // COMPONENT_RESOLVER_H
