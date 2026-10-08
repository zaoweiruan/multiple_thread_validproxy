#ifndef TEMPLATE_DECOMPOSER_H
#define TEMPLATE_DECOMPOSER_H

#include <string>
#include <vector>
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
