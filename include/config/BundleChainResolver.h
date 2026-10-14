#ifndef BUNDLE_CHAIN_RESOLVER_H
#define BUNDLE_CHAIN_RESOLVER_H

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <sqlite3.h>
#include <boost/json.hpp>

#include "config/ComponentBundle.h"
#include "config/ConflictDetector.h"
#include "config/ComponentResolver.h"

namespace config {

/// BundleChain 解析结果
struct BundleChainResolveResult {
    /// 合并后的扁平组件列表（保留加载顺序，单例已去重）
    std::vector<ComponentConfig> components;
    /// 来源信息（组件 → 来源 Bundle，用于 UI「来源 Bundle」列）
    std::vector<ComponentSource> sources;
    /// 冲突检测结果（跨包覆盖 WARN / 同包重复 ERROR）
    std::vector<Conflict> conflicts;
    /// 最终生成的配置（resolveToConfig 时填充）
    boost::json::value config;
    bool success = false;
    std::string errorMessage;
};

/// Bundle 版本快照（模板引用时记录的各 Bundle 版本）
struct BundleVersionInfo {
    std::string bundleId;
    int version = 0;
};

/// BundleChain 解析器
/// 按 bundleIds 顺序加载组件包并合并为最终配置
/// - 单例组件：后加载覆盖先加载
/// - 数组组件：追加并按 tag 去重
/// - routing 组件：保留（后续由 ComponentResolver 合并 rules）
/// BundleChain 组合为非继承关系，无 ParentId（见设计文档 §4.3）
class BundleChainResolver {
public:
    explicit BundleChainResolver(sqlite3* db);

    /// 解析 BundleChain：加载、合并、冲突检测
    BundleChainResolveResult resolveBundleChain(const BundleChain& chain) const;

    /// 解析 BundleChain 并生成最终配置（占位符替换 + 组件应用）
    BundleChainResolveResult resolveToConfig(const BundleChain& chain,
                                             const PipelineContext& ctx) const;

    /// 解析 BundleChain 生成配置（默认上下文：Xray / 无 Profile）
    BundleChainResolveResult resolveToConfig(const BundleChain& chain) const;

    /// 提取链中所有组件参数中的占位符名称（去重、稳定排序）
    /// 支持 ${name} 与 ${name:default} 两种形式
    std::vector<std::string> extractPlaceholders(const BundleChain& chain) const;

    /// 获取链内每个 Bundle 的当前版本
    std::vector<BundleVersionInfo> getCurrentVersions(const BundleChain& chain) const;

    /// 版本同步检查：任一 Bundle 当前版本高于 recordedVersion，
    /// 或 Bundle 缺失 / 未记录时，链已过期（调用方应标记模板 ⚠️已过期）
    bool isChainStale(const BundleChain& chain,
                      const std::vector<BundleVersionInfo>& recordedVersions) const;

private:
    /// 加载单个组件包（缺失返回 nullopt）
    std::optional<ComponentBundle> loadBundle(const std::string& bundleId) const;

    /// 合并两个组件列表：override 按策略覆盖/追加 base
    std::vector<ComponentConfig> mergeComponents(
        const std::vector<ComponentConfig>& base,
        const std::vector<ComponentConfig>& override) const;

    /// 提取组件参数中的 tag（无则返回空串）
    static std::string extractTag(const ComponentConfig& comp);

    sqlite3* db_;
};

} // namespace config

#endif // BUNDLE_CHAIN_RESOLVER_H