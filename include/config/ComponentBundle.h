#ifndef COMPONENT_BUNDLE_H
#define COMPONENT_BUNDLE_H

#include <string>
#include <vector>
#include <map>
#include <boost/json.hpp>

namespace config {

/// Component type enumeration (15 types matching Xray config sections)
enum class ComponentType {
    ENV,                    // env
    LOG,                    // log
    API,                    // api
    DNS,                    // dns
    ROUTING,                // routing (merge strategy)
    POLICY,                 // policy
    INBOUND,                // inbound[] (array, append strategy)
    OUTBOUND,               // outbound[] (array, append strategy)
    STATS,                  // stats
    FAKEDNS,                // fakedns
    METRICS,                // metrics
    OBSERVATORY,            // observatory
    BURST_OBSERVATORY,      // burstObservatory
    GEODATA,                // geodata
    VERSION                 // version
};

/// Template type enumeration
enum class TemplateType {
    BASE,           // base - shared base components
    TEST,           // test - batch test configuration
    STANDALONE,     // standalone - standalone proxy configuration
    POOL            // pool - proxy pool configuration
};

/// Core type enumeration
enum class CoreType {
    XRAY = 2,       // Xray core
    SINGBOX = 24    // SingBox core
};

/// Component configuration
struct ComponentConfig {
    ComponentType type;
    std::map<std::string, boost::json::value> params;  // component parameters
    std::string id;  // optional unique identifier
};

/// Component bundle data model
struct ComponentBundle {
    std::string id;              // unique identifier (e.g., base_xray_001)
    std::string name;            // display name
    std::string description;     // description (optional)
    CoreType coreType;           // core type (Xray or SingBox)
    TemplateType templateType;   // template type (base/test/standalone/pool)
    int version = 1;             // version number (increment on update)
    bool enabled = true;         // whether enabled
    std::vector<ComponentConfig> components;  // component list
};

/// BundleChain - combination of bundle IDs with parameters
struct BundleChain {
    std::vector<std::string> bundleIds;  // bundle IDs in order
    std::map<std::string, boost::json::value> params;  // placeholder parameters
};

} // namespace config

#endif // COMPONENT_BUNDLE_H
