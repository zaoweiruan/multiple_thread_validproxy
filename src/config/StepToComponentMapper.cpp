#include "config/StepToComponentMapper.h"

namespace config {

// ---------------------------------------------------------------------------
// StepToComponentMapping 表（任务 7.1）
// 将旧格式 PipelineDefinition::steps 的类型与组件类型对应。
// replace_variables：全局变量替换，组件解析阶段等效于占位符替换 → 忽略。
// modify_field：任意 JSON 路径操作，无法映射为组件 → 不支持。
// ---------------------------------------------------------------------------
const std::vector<StepToComponentMapping>& StepToComponentMapper::mappings() {
    static const std::vector<StepToComponentMapping> table = {
        { "replace_variables", ComponentType::ENV,      ConvertOutcome::IGNORED },
        { "add_outbound",      ComponentType::OUTBOUND, ConvertOutcome::CONVERTED },
        { "merge_routing",     ComponentType::ROUTING,  ConvertOutcome::CONVERTED },
        { "add_inbound",       ComponentType::INBOUND,  ConvertOutcome::CONVERTED },
        { "add_observatory",   ComponentType::OBSERVATORY, ConvertOutcome::CONVERTED },
        { "add_balancer",      ComponentType::ROUTING,  ConvertOutcome::CONVERTED },
        { "add_stats",         ComponentType::STATS,    ConvertOutcome::CONVERTED },
        { "add_policy",        ComponentType::POLICY,   ConvertOutcome::CONVERTED },
        { "add_api",           ComponentType::API,      ConvertOutcome::CONVERTED },
        { "modify_field",      ComponentType::ENV,      ConvertOutcome::UNSUPPORTED }
    };
    return table;
}

bool StepToComponentMapper::isSupported(const std::string& stepType) const {
    for (const StepToComponentMapping& entry : mappings()) {
        if (entry.stepType == stepType) {
            return entry.outcome != ConvertOutcome::UNSUPPORTED;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// 依据步骤类型构造组件参数（仅提取映射相关键，保持步骤 params 语义）
// ---------------------------------------------------------------------------
std::map<std::string, boost::json::value> StepToComponentMapper::mappedParams(
    const StepConfig& step)
{
    std::map<std::string, boost::json::value> result;

    if (step.type == "add_balancer") {
        // add_balancer 将 balancer 包装进 routing.balancers 数组
        boost::json::object balancer;
        for (const boost::json::key_value_pair& kv : step.params) {
            const std::string key = std::string(kv.key());
            if (key == "tag" || key == "selector" ||
                key == "strategy" || key == "observation") {
                balancer[key] = kv.value();
            }
        }
        boost::json::array balancers;
        balancers.push_back(balancer);
        result["balancers"] = balancers;
        return result;
    }

    // 其它已支持类型：原样透传所有参数（ComponentResolver 消费方按组件语义解释）
    for (const boost::json::key_value_pair& kv : step.params) {
        result[std::string(kv.key())] = kv.value();
    }
    return result;
}

ConvertOutcome StepToComponentMapper::convert(const StepConfig& step, ComponentConfig& out) const {
    for (const StepToComponentMapping& entry : mappings()) {
        if (entry.stepType != step.type) {
            continue;
        }
        if (entry.outcome == ConvertOutcome::UNSUPPORTED) {
            return ConvertOutcome::UNSUPPORTED;
        }
        if (entry.outcome == ConvertOutcome::IGNORED) {
            return ConvertOutcome::IGNORED;
        }
        out.type = entry.componentType;
        out.params = mappedParams(step);
        out.id = "";
        return ConvertOutcome::CONVERTED;
    }
    // 未登记类型：视为不支持（防御）
    return ConvertOutcome::UNSUPPORTED;
}

std::vector<ComponentConfig> StepToComponentMapper::convertAll(
    const std::vector<StepConfig>& steps,
    std::vector<std::string>& unsupported) const
{
    std::vector<ComponentConfig> components;
    unsupported.clear();

    for (const StepConfig& step : steps) {
        ComponentConfig out;
        ConvertOutcome outcome = convert(step, out);
        if (outcome == ConvertOutcome::CONVERTED) {
            components.push_back(out);
        } else if (outcome == ConvertOutcome::UNSUPPORTED) {
            unsupported.push_back(step.type);
        }
    }
    return components;
}

bool StepToComponentMapper::canConvertAll(const std::vector<StepConfig>& steps) const {
    for (const StepConfig& step : steps) {
        if (!isSupported(step.type)) {
            return false;
        }
    }
    return true;
}

} // namespace config