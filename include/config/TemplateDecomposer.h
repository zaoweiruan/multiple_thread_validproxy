#ifndef TEMPLATE_DECOMPOSER_H
#define TEMPLATE_DECOMPOSER_H

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <sqlite3.h>
#include <boost/json.hpp>
#include "config/ComponentBundle.h"
#include "config/ComponentBundleDAO.h"
#include "FullConfigTemplateDAO.h"

namespace config {

/// TemplateDecomposer — 将 FullConfigTemplateItem 的 JSON 配置
/// 拆解为 ComponentBundle 条目（每个 JSON 节段 → 一个 ComponentConfig）。
///
/// 映射规则:
///   "log"          → LOG
///   "dns"          → DNS
///   "api"          → API
///   "stats"        → STATS
///   "policy"       → POLICY
///   "inbounds[]"   → INBOUND (每个数组项独立)
///   "outbounds[]"  → OUTBOUND (每个数组项独立)
///   "routing"      → ROUTING
///   "observatory"  → OBSERVATORY
///   "burstObservatory" → BURST_OBSERVATORY
///   "geodata"      → GEODATA
///   "version"      → VERSION
///   "fakedns"      → FAKEDNS
///   "metrics"      → METRICS
///   "env"          → ENV
///
/// 用法:
///   TemplateDecomposer dc(db);
///   auto results = dc.decomposeAll();
///   // results: vector of {bundleId, bundle, error}
class TemplateDecomposer {
public:
    struct DecomposeResult {
        std::string templateId;
        ComponentBundle bundle;
        /// 模板角色（base/test/standalone/pool）——由模板 ID 推断。
        /// 角色属于模板，不属于组件包：仅用于决定同名组件内容不一致时如何拆包。
        TemplateType role = TemplateType::BASE;
        bool success = false;
        std::string error;
    };

    explicit TemplateDecomposer(sqlite3* db);

    /// 拆解所有模板，返回结果列表
    std::vector<DecomposeResult> decomposeAll() const;

    /// 拆解单个模板
    DecomposeResult decompose(const std::string& templateId) const;

/// 拆解并保存（写入 ComponentBundle 表）
    /// @param overwrite 若 bundle 已存在是否覆盖
    /// @return 成功保存的数量
    int decomposeAndSave(bool overwrite = false) const;

    /// 最小粒度重建计划：组件包 + 每个模板的组件链
    struct BundlePlan {
        std::vector<ComponentBundle> bundles;                   // 最小粒度组件包
        std::map<std::string, std::vector<std::string>> chains;  // templateId → bundleIds
    };

    /// 按「代理类型 × 组件类型」规划最小粒度组件包与各模板组件链。
    ///
    /// 命名规则:
    ///   - 同一 (代理类型, 组件类型) 下所有模板内容一致 → 单包 `{core}_{TYPE}`
    ///   - 内容不一致 → 按角色拆包 `{core}_{TYPE}_{role}`（role ∈ base/test/standalone/pool）
    ///
    /// 组件链只包含模板自身的组件（按 ComponentType 枚举序），
    /// 不注入任何默认包；POOL 角色的链额外显式包含 `{core}_LOG` 与 `{core}_DNS`。
    BundlePlan planMinimalBundles() const;

    /// 执行 planMinimalBundles 的结果：删除计划外的旧包、写入新包、
    /// 回写每个模板的组件链与 bundleVersion 版本快照。
    /// @return 写入的组件包数量
    int rebuildBundlesAndChains() const;

    /// 用各模板自身的组件链重新解析配置并回写 Config 列。
    ///
    /// 配置生成的唯一来源是 Config 列，组件链是它的物化依据；
    /// 组件包内容变更后调用本方法，把最新解析结果同步进 Config。
    /// @param changedBundleId 仅同步引用了该组件包的模板；为空表示同步全部模板
    /// @return 实际更新的模板数
    int syncConfigsFromChains(const std::string& changedBundleId = std::string()) const;

    /// 用指定模板自身的组件链重新解析并回写它的 Config 列
    /// @return 该模板被更新返回 true
    bool syncConfigFromChain(const std::string& templateId) const;

    /// 映射模板 ID → TemplateType
    static TemplateType inferTemplateType(const std::string& templateId);

    /// 映射 JSON 节段名 → ComponentType
    /// 返回 nullopt 表示未知节段
    static std::optional<ComponentType> sectionToComponentType(
        const std::string& section);

private:
    sqlite3* db_;

    /// 将 JSON 对象转换为 params map (string → boost::json::value)
    static std::map<std::string, boost::json::value> objectToParams(
        const boost::json::object& obj);

    /// 从模板配置 JSON 构建 ComponentBundle
    ComponentBundle buildBundle(
        const std::string& templateId,
        int coreType,
        const std::string& configJson) const;
};

} // namespace config

#endif // TEMPLATE_DECOMPOSER_H
