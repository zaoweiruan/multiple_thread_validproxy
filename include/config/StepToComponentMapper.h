#ifndef STEP_TO_COMPONENT_MAPPER_H
#define STEP_TO_COMPONENT_MAPPER_H

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <boost/json.hpp>

#include "config/PipelineConfig.h"
#include "config/ComponentBundle.h"

namespace config {

/// 步骤转换结果分类
enum class ConvertOutcome {
    CONVERTED,    ///< 成功转换为组件
    IGNORED,      ///< 步骤无需生成组件（如 replace_variables 全局替换）
    UNSUPPORTED   ///< 无法映射为组件（如 modify_field 任意 JSON 操作）
};

/// 步骤类型 → 组件类型映射登记项（任务 7.1「StepToComponentMapping 表」）
struct StepToComponentMapping {
    std::string stepType;        ///< 步骤类型（StepConfig::type）
    ComponentType componentType; ///< 目标组件类型（仅 CONVERTED 生效）
    ConvertOutcome outcome;      ///< 转换结果分类
};

/// 步骤→组件转换器
/// 将 PipelineDefinition::steps（旧格式）自动转换为等效的 ComponentConfig 列表
/// （组件化重构双模式迁移的一部分，供 ConfigPipeline 加载旧格式时使用）
class StepToComponentMapper {
public:
    /// 返回全部注册的映射表（静态，10 种步骤类型）
    static const std::vector<StepToComponentMapping>& mappings();

    /// 查询步骤类型是否可转换（OUTCOME != UNSUPPORTED）
    bool isSupported(const std::string& stepType) const;

    /// 单步转换：out 接收组件；返回转换结果分类
    /// - CONVERTED   : out 已填充
    /// - IGNORED     : out 不填充（replace_variables）
    /// - UNSUPPORTED : out 不填充（modify_field 等）
    ConvertOutcome convert(const StepConfig& step, ComponentConfig& out) const;

    /// 批量转换：依次转换 steps；跳过 IGNORED；UNSUPPORTED 类型追加到 unsupported
    /// 返回转换成功的组件（保持输入顺序）
    std::vector<ComponentConfig> convertAll(
        const std::vector<StepConfig>& steps,
        std::vector<std::string>& unsupported) const;

    /// 是否所有步骤均可转换或可忽略（不含 UNSUPPORTED）
    bool canConvertAll(const std::vector<StepConfig>& steps) const;

private:
    /// 依据映射表构造组件参数（按步骤类型提取/包装 params 键）
    static std::map<std::string, boost::json::value> mappedParams(
        const StepConfig& step);
};

} // namespace config

#endif // STEP_TO_COMPONENT_MAPPER_H