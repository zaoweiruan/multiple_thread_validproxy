#ifndef FULL_CONFIG_TEMPLATE_DAO_H
#define FULL_CONFIG_TEMPLATE_DAO_H

#include <string>
#include <optional>
#include <vector>
#include <sqlite3.h>

namespace db {

/// FullConfigTemplateItem data model
struct FullConfigTemplateItem {
    std::string id;
    std::string parentId;      // 父模板 ID（空字符串表示基础模板）
    std::string remarks;
    int enabled = 0;
    int coreType = 0;  // 2=Xray, 24=sing-box
    std::string config;
    std::string tunConfig;
    int addProxyOnly = 0;
    std::string proxyDetour;
    // Phase 6: BundleChain support
    std::string bundleChain;   // JSON: {"bundleIds":[...],"params":{...}}; empty = not used
    std::string templateType;  // "components" | "bundleChain" | "steps"
    std::string bundleVersion; // JSON: [{"bundleId":"...","version":N},...]
};

/// DAO for FullConfigTemplateItem table
class FullConfigTemplateDAO {
public:
    /// Constructor
    /// @param db SQLite database connection
    explicit FullConfigTemplateDAO(sqlite3* db);
    
    /// Destructor
    ~FullConfigTemplateDAO() = default;
    
    /// Get template by ID
    /// @param id Template ID
    /// @return Optional template item
    std::optional<FullConfigTemplateItem> getById(const std::string& id) const;
    
    /// Get template by core type
    /// @param coreType Core type (2=Xray, 24=sing-box)
    /// @return Optional template item (first match)
    std::optional<FullConfigTemplateItem> getByCoreType(int coreType) const;
    
    /// Get all enabled templates
    /// @return Vector of enabled template items
    std::vector<FullConfigTemplateItem> getAllEnabled() const;
    
    /// Get all templates
    /// @return Vector of all template items
    std::vector<FullConfigTemplateItem> getAll() const;
    
    /// Insert or update template
    /// @param item Template item to save
    /// @return True on success
    bool save(const FullConfigTemplateItem& item) const;
    
    /// Delete template by ID
    /// @param id Template ID
    /// @return True on success
    bool deleteById(const std::string& id) const;
    
    /// Check if table exists
    /// @return True if table exists
    bool tableExists() const;
    
    /// Create table if not exists
    /// @return True on success
    bool createTableIfNotExists() const;
    
    /// Seed default templates if table is empty
    /// Inserts default Xray (CoreType=2) and SingBox (CoreType=24) config templates
    /// @return Number of templates inserted (0 if table already has data)
    int seedDefaultTemplates() const;

    /// Check whether the template is in sync with its referenced Bundles
    /// (returns false when any referenced Bundle version has advanced)
    bool isTemplateInSync(const std::string& templateId) const;

private:
    sqlite3* db_;
    
    /// Prepare statement helper
    bool prepareStatement(sqlite3_stmt** stmt, const std::string& sql) const;
    
    /// Finalize statement helper
    void finalizeStatement(sqlite3_stmt* stmt) const;
};

} // namespace db

#endif // FULL_CONFIG_TEMPLATE_DAO_H