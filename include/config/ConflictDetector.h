#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <sqlite3.h>
#include <boost/json.hpp>
#include "config/ComponentBundle.h"
#include "config/ComponentRegistry.h"
#include "config/Conflict.h"

namespace config {

/// 组件来源信息
struct ComponentSource {
    ComponentConfig component;       // 组件配置
    std::string sourceBundleId;      // 来源包 ID
    int sourceIndex;                 // 来源包中的索引
};

/// 冲突检测器
/// 检测组件列表中的冲突，支持同包 vs 跨包区分
class ConflictDetector {
public:
    explicit ConflictDetector(const ComponentRegistry& registry);
    
    /// 检测冲突（基础版本）
    std::vector<Conflict> detect(
        const std::vector<ComponentConfig>& components) const;
    
    /// 检测冲突（带来源信息）
    std::vector<Conflict> detect(
        const std::vector<ComponentSource>& sources) const;
    
    /// 检测依赖缺失（routing 引用不存在的 inbound/outbound）
    std::vector<Conflict> detectDependencies(
        const std::vector<ComponentConfig>& components) const;
    
    /// 根据策略处理冲突
    bool handleConflicts(
        const std::vector<Conflict>& conflicts,
        ConflictStrategy strategy) const;
    
    /// 检查是否有 ERROR 级别冲突
    bool hasErrors(const std::vector<Conflict>& conflicts) const;
    
    /// 检查是否有 WARN 级别冲突
    bool hasWarnings(const std::vector<Conflict>& conflicts) const;
    
private:
    /// 提取组件 tag
    std::string extractTag(const ComponentConfig& comp) const;
    
    /// 检查是否是单例类型
    bool isSingleton(const ComponentConfig& comp) const;
    
    /// 检查是否是数组类型
    bool isArray(const ComponentConfig& comp) const;
    
    const ComponentRegistry& registry_;
};

} // namespace config
