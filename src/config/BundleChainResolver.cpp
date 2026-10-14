#include "config/BundleChainResolver.h"

#include <algorithm>
#include <regex>
#include <set>
#include <stdexcept>
#include <utility>

#include "config/ComponentBundleDAO.h"
#include "config/ComponentRegistry.h"

namespace config {

BundleChainResolver::BundleChainResolver(sqlite3* db) : db_(db) {
    // 确保组件注册表已初始化（默认类型由调用方/测试 SetUp 注册）
    ComponentRegistry::instance();
}

std::optional<ComponentBundle> BundleChainResolver::loadBundle(const std::string& bundleId) const {
    ComponentBundleDAO dao(db_);
    return dao.getById(bundleId);
}

std::vector<ComponentConfig> BundleChainResolver::mergeComponents(
    const std::vector<ComponentConfig>& base,
    const std::vector<ComponentConfig>& override) const
{
    std::vector<ComponentConfig> result = base;
    ComponentRegistry& registry = ComponentRegistry::instance();

    for (const ComponentConfig& comp : override) {
        const ComponentMetadata* metadata = registry.getMetadata(comp.type);
        CombinationStrategy strategy = CombinationStrategy::OVERRIDE;
        if (metadata != nullptr) {
            strategy = metadata->strategy;
        }

        if (strategy == CombinationStrategy::APPEND) {
            // 数组组件：同 type + 同 tag 整体替换，无 tag 直接追加
            std::string tag = extractTag(comp);
            if (!tag.empty()) {
                for (std::vector<ComponentConfig>::iterator rit = result.begin();
                     rit != result.end();)
                {
                    if (rit->type == comp.type && extractTag(*rit) == tag) {
                        rit = result.erase(rit);
                    } else {
                        ++rit;
                    }
                }
            }
            result.push_back(comp);
        } else if (strategy == CombinationStrategy::MERGE) {
            // routing：保留两者，最终由 ComponentResolver 合并 rules
            result.push_back(comp);
        } else {
            // OVERRIDE 单例：删除同类型旧组件，后加载者胜出
            for (std::vector<ComponentConfig>::iterator rit = result.begin();
                 rit != result.end();)
            {
                if (rit->type == comp.type) {
                    rit = result.erase(rit);
                } else {
                    ++rit;
                }
            }
            result.push_back(comp);
        }
    }
    return result;
}

std::string BundleChainResolver::extractTag(const ComponentConfig& comp) {
    const std::map<std::string, boost::json::value>& params = comp.params;
    const std::map<std::string, boost::json::value>::const_iterator it = params.find("tag");
    if (it != params.end() && it->second.is_string()) {
        return boost::json::value_to<std::string>(it->second);
    }
    return std::string();
}

BundleChainResolveResult BundleChainResolver::resolveBundleChain(const BundleChain& chain) const {
    BundleChainResolveResult result;

    // 1. 按顺序加载组件包并合并
    std::vector<ComponentConfig> merged;
    std::vector<ComponentSource> sources;
    int sourceIndex = 0;

    for (const std::string& bundleId : chain.bundleIds) {
        const std::optional<ComponentBundle> bundle = loadBundle(bundleId);
        if (!bundle.has_value()) {
            result.success = false;
            result.errorMessage = "Bundle 不存在: " + bundleId;
            return result;
        }

        // 收集源信息（用于冲突检测，区分跨包覆盖）
        for (const ComponentConfig& comp : bundle->components) {
            ComponentSource source;
            source.component = comp;
            source.sourceBundleId = bundleId;
            source.sourceIndex = sourceIndex++;
            sources.push_back(source);
        }

        merged = mergeComponents(merged, bundle->components);
    }

    // 2. 冲突检测（跨包覆盖 WARN / 同包重复 ERROR）
    ConflictDetector detector(ComponentRegistry::instance());
    result.conflicts = detector.detect(sources);

    // 3. 结果
    result.components = merged;
    result.sources = sources;
    result.success = true;
    return result;
}

BundleChainResolveResult BundleChainResolver::resolveToConfig(
    const BundleChain& chain, const PipelineContext& ctx) const
{
    BundleChainResolveResult chainResult = resolveBundleChain(chain);
    if (!chainResult.success) {
        return chainResult;
    }

    ComponentResolver resolver(db_);
    const ResolveResult resolveResult = resolver.resolve(chainResult.components, ctx);
    chainResult.config = resolveResult.config;
    if (!resolveResult.success) {
        chainResult.success = false;
        chainResult.errorMessage = resolveResult.errorMessage;
    }
    return chainResult;
}

BundleChainResolveResult BundleChainResolver::resolveToConfig(const BundleChain& chain) const {
    PipelineContext ctx;
    ctx.proxyType = ProxyType::Xray;
    ctx.hasProfile = false;
    ctx.vars = ConfigVariables();
    ctx.db = db_;
    ctx.clientName = "default";
    return resolveToConfig(chain, ctx);
}

std::vector<std::string> BundleChainResolver::extractPlaceholders(const BundleChain& chain) const {
    std::set<std::string> names;
    // ${name} 与 ${name:default} 两种形式
    const std::regex pattern("\\$\\{([^}:]+)(?::[^}]*)?\\}");

    for (const std::string& bundleId : chain.bundleIds) {
        const std::optional<ComponentBundle> bundle = loadBundle(bundleId);
        if (!bundle.has_value()) {
            continue;
        }
        for (const ComponentConfig& comp : bundle->components) {
            for (const std::pair<const std::string, boost::json::value>& kv : comp.params) {
                const std::string text = boost::json::serialize(kv.second);
                std::smatch match;
                std::string::const_iterator cursor = text.cbegin();
                while (std::regex_search(cursor, text.cend(), match, pattern)) {
                    names.insert(match[1].str());
                    cursor = match.suffix().first;
                }
            }
        }
    }

    return std::vector<std::string>(names.begin(), names.end());
}

std::vector<BundleVersionInfo> BundleChainResolver::getCurrentVersions(const BundleChain& chain) const {
    std::vector<BundleVersionInfo> versions;
    for (const std::string& bundleId : chain.bundleIds) {
        const std::optional<ComponentBundle> bundle = loadBundle(bundleId);
        if (!bundle.has_value()) {
            continue;
        }
        BundleVersionInfo info;
        info.bundleId = bundleId;
        info.version = bundle->version;
        versions.push_back(info);
    }
    return versions;
}

bool BundleChainResolver::isChainStale(const BundleChain& chain,
                                       const std::vector<BundleVersionInfo>& recordedVersions) const {
    std::map<std::string, int> recordedMap;
    for (const BundleVersionInfo& info : recordedVersions) {
        recordedMap[info.bundleId] = info.version;
    }

    for (const std::string& bundleId : chain.bundleIds) {
        const std::optional<ComponentBundle> bundle = loadBundle(bundleId);
        if (!bundle.has_value()) {
            return true; // Bundle 缺失 → 过期
        }
        const std::map<std::string, int>::const_iterator rit = recordedMap.find(bundleId);
        if (rit == recordedMap.end()) {
            return true; // 当前链含未记录版本 → 过期
        }
        if (bundle->version > rit->second) {
            return true; // 版本已更新 → 过期
        }
    }
    return false;
}

} // namespace config