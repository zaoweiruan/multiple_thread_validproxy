#pragma once

#include <string>
#include <vector>
#include <boost/json.hpp>
#include "config/ComponentBundle.h"

namespace config {

/// 步骤配置
struct StepConfig {
    std::string type;
    boost::json::object params;
    std::string onError = "continue";
};

/// 管线定义
struct PipelineDefinition {
    std::string templateId;
    std::string bundleChain;   // Phase 6: BundleChain JSON text; empty = use templateId
    std::vector<StepConfig> steps;
    /// Phase 7: 旧格式 steps 在 loadConfig 时自动转换为组件列表
    std::vector<ComponentConfig> convertedComponents;
};

} // namespace config
