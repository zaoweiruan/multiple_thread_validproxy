#pragma once

#include <string>
#include <unordered_map>
#include <boost/json.hpp>
#include <sqlite3.h>
#include "config/PipelineConfig.h"
#include "config/StepHandler.h"
#include "config/ConfigBuilder.h"
#include "Profileitem.h"

namespace config {

/// 配置管线执行器
class ConfigPipeline {
public:
    explicit ConfigPipeline(sqlite3* db);
    ~ConfigPipeline() = default;

    /// 加载管线配置（从 config.json 的 config_pipeline 段）
    void loadConfig(const boost::json::value& pipelineConfig);

    /// 执行管线
    boost::json::value execute(
        ConfigMode mode,
        const std::string& clientName,
        const db::models::Profileitem& profile,
        const ConfigVariables& vars) const;

    /// 检查是否有配置的管线
    bool hasPipeline(ConfigMode mode, const std::string& clientName) const;

private:
    std::string modeKey(ConfigMode mode, const std::string& clientName) const;
    PipelineContext makeContext(ConfigMode mode, const std::string& clientName,
                                const db::models::Profileitem& profile,
                                const ConfigVariables& vars) const;

    std::unordered_map<std::string, PipelineDefinition> pipelines_;
    sqlite3* db_;
};

} // namespace config
