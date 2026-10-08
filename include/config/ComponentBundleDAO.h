#ifndef COMPONENT_BUNDLE_DAO_H
#define COMPONENT_BUNDLE_DAO_H

#include <string>
#include <optional>
#include <vector>
#include <sqlite3.h>
#include "ComponentBundle.h"

namespace config {

/// DAO for ComponentBundle table
class ComponentBundleDAO {
public:
    /// Constructor
    /// @param db SQLite database connection
    explicit ComponentBundleDAO(sqlite3* db);
    
    /// Destructor
    ~ComponentBundleDAO() = default;
    
    /// Get bundle by ID
    /// @param id Bundle ID
    /// @return Optional bundle item
    std::optional<ComponentBundle> getById(const std::string& id) const;
    
    /// Get bundle by ID and version
    /// @param id Bundle ID
    /// @param version Version number
    /// @return Optional bundle item
    std::optional<ComponentBundle> getByIdAndVersion(
        const std::string& id, int version) const;
    
    /// Get all bundles
    /// @return Vector of all bundles
    std::vector<ComponentBundle> getAll() const;
    
    /// Get all enabled bundles
    /// @return Vector of enabled bundles
    std::vector<ComponentBundle> getAllEnabled() const;
    
    /// Get all bundles by core type
    /// @param coreType Core type (Xray or SingBox)
    /// @return Vector of bundles matching core type
    std::vector<ComponentBundle> getByCoreType(CoreType coreType) const;
    
    /// Get all bundles by template type
    /// @param templateType Template type (base/test/standalone/pool)
    /// @return Vector of bundles matching template type
    std::vector<ComponentBundle> getByTemplateType(TemplateType templateType) const;
    
    /// Save bundle (insert or update)
    /// @param bundle Bundle to save
    /// @return True on success
    bool save(const ComponentBundle& bundle) const;
    
    /// Update bundle version (increment version)
    /// @param id Bundle ID
    /// @return New version number, or -1 on failure
    int updateVersion(const std::string& id) const;
    
    /// Delete bundle by ID
    /// @param id Bundle ID
    /// @return True on success
    bool deleteById(const std::string& id) const;
    
    /// Check if bundle exists
    /// @param id Bundle ID
    /// @return True if bundle exists
    bool exists(const std::string& id) const;
    
    /// Get referenced count (how many templates reference this bundle)
    /// @param id Bundle ID
    /// @return Number of references
    int getReferencedCount(const std::string& id) const;
    
    /// Check if table exists
    /// @return True if table exists
    bool tableExists() const;
    
    /// Create table if not exists
    /// @return True on success
    bool createTableIfNotExists() const;
    
    /// Seed default bundles if table is empty
    /// @return Number of bundles inserted (0 if table already has data)
    int seedDefaultBundles() const;
    
private:
    sqlite3* db_;
    
    /// Prepare statement helper
    bool prepareStatement(sqlite3_stmt** stmt, const std::string& sql) const;
    
    /// Finalize statement helper
    void finalizeStatement(sqlite3_stmt* stmt) const;
    
    /// Serialize bundle to JSON string
    /// @param bundle Bundle to serialize
    /// @return JSON string
    std::string serializeBundle(const ComponentBundle& bundle) const;
    
    /// Deserialize bundle from JSON string
    /// @param json JSON string
    /// @return Bundle item
    ComponentBundle deserializeBundle(const std::string& json) const;
};

} // namespace config

#endif // COMPONENT_BUNDLE_DAO_H
