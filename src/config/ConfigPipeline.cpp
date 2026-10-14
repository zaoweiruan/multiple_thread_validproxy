#include "config/ConfigPipeline.h"
#include "config/StepRegistry.h"
#include "config/ConfigBuilder.h"
#include "config/BundleChainResolver.h"
#include "config/ComponentBundle.h"
#include "config/StepToComponentMapper.h"
#include "config/ConfigGenerator.h"
#include "Logger.h"
#include <stdexcept>
#include <utility>
#include <boost/json.hpp>

namespace config {

ConfigPipeline::ConfigPipeline(sqlite3* db) : db_(db) {}

void ConfigPipeline::loadConfig(const boost::json::value& pipelineConfig) {
    if (!pipelineConfig.is_object()) {
        Logger::write("[ConfigPipeline] config_pipeline is not a JSON object, ignored", LogLevel::WARN);
        return;
    }

    const boost::json::object& obj = pipelineConfig.as_object();
    for (const auto& [modeKey, defVal] : obj) {
        if (!defVal.is_object()) continue;
        const boost::json::object& defObj = defVal.as_object();

        PipelineDefinition def;
        if (defObj.contains("template") && defObj.at("template").is_string()) {
            def.templateId = defObj.at("template").get_string();
        }
        if (defObj.contains("bundleChain") && defObj.at("bundleChain").is_object()) {
            def.bundleChain = boost::json::serialize(defObj.at("bundleChain"));
        }

        if (defObj.contains("steps") && defObj.at("steps").is_array()) {
            for (const boost::json::value& stepVal : defObj.at("steps").as_array()) {
                if (!stepVal.is_object()) continue;
                const boost::json::object& stepObj = stepVal.as_object();

                StepConfig step;
                if (stepObj.contains("type") && stepObj.at("type").is_string()) {
                    step.type = stepObj.at("type").get_string();
                }
                if (stepObj.contains("params") && stepObj.at("params").is_object()) {
                    step.params = stepObj.at("params").as_object();
                }
                if (stepObj.contains("onError") && stepObj.at("onError").is_string()) {
                    step.onError = stepObj.at("onError").get_string();
                }
                if (!step.type.empty()) {
                    def.steps.push_back(std::move(step));
                }
            }
        }

        // Phase 7: 自动转换旧格式 steps 为组件列表（检测旧格式，向后兼容，执行路径不变）
        if (!def.steps.empty()) {
            StepToComponentMapper mapper;
            std::vector<std::string> unsupported;
            def.convertedComponents = mapper.convertAll(def.steps, unsupported);
            if (!unsupported.empty()) {
                std::string unsupportedList;
                for (const std::string& type : unsupported) {
                    if (!unsupportedList.empty()) unsupportedList += ", ";
                    unsupportedList += type;
                }
                Logger::write("[ConfigPipeline] StepToComponentMapper: unsupported steps: " + unsupportedList,
                    LogLevel::DEBUG);
            }
            if (!def.convertedComponents.empty()) {
                Logger::write("[ConfigPipeline] Auto-converted " +
                    std::to_string(def.convertedComponents.size()) + " steps to components", LogLevel::DEBUG);
            }
        }

        if ((!def.templateId.empty() || !def.bundleChain.empty()) && !def.steps.empty()) {
            std::string mk = std::string(modeKey);
            pipelines_[mk] = std::move(def);
            Logger::write("[ConfigPipeline] Loaded: " + mk +
                " template=" + def.templateId + " steps=" + std::to_string(def.steps.size()),
                LogLevel::DEBUG);
        }
    }
}

std::string ConfigPipeline::modeKey(ConfigMode mode, const std::string& clientName) const {
    std::string modeStr;
    switch (mode) {
        case ConfigMode::Test: modeStr = "test"; break;
        case ConfigMode::Standalone: modeStr = "standalone"; break;
        case ConfigMode::Pool: modeStr = "pool"; break;
    }
    if (clientName == "singbox") {
        return modeStr + "_singbox";
    }
    return modeStr;
}

bool ConfigPipeline::hasPipeline(ConfigMode mode, const std::string& clientName) const {
    return pipelines_.find(modeKey(mode, clientName)) != pipelines_.end();
}

PipelineContext ConfigPipeline::makeContext(ConfigMode mode, const std::string& clientName,
                                             const db::models::Profileitem& profile,
                                             const ConfigVariables& vars) const {
    (void)mode;
    PipelineContext ctx;
    ctx.proxyType = (clientName == "singbox") ? ProxyType::SingBox : ProxyType::Xray;
    ctx.profile = profile;
    ctx.hasProfile = !profile.address.empty();
    ctx.vars = vars;
    ctx.db = db_;
    ctx.clientName = clientName;
    return ctx;
}

boost::json::value ConfigPipeline::execute(
    ConfigMode mode,
    const std::string& clientName,
    const db::models::Profileitem& profile,
    const ConfigVariables& vars) const {

    const std::string key = modeKey(mode, clientName);
    auto it = pipelines_.find(key);
    if (it == pipelines_.end()) {
        throw std::runtime_error("Pipeline not found: " + key);
    }
    const PipelineDefinition& def = it->second;

    PipelineContext ctx = makeContext(mode, clientName, profile, vars);

    // Phase 9: 统一配置生成（bundleChain > template + 占位符替换 + 缓存）
PipelineConfigGenerator configGen(db_);
        boost::json::value config = configGen.generate(def, ctx);

    for (const auto& step : def.steps) {
        auto handler = StepRegistry::instance().getHandler(step.type);
        if (!handler) {
            Logger::write("[ConfigPipeline] Step type not registered: " + step.type +
                " (onError=" + step.onError + ")", LogLevel::WARN);
            continue;
        }

        try {
            handler->execute(config.as_object(), step.params, ctx);
            Logger::write("[ConfigPipeline] Step OK: " + step.type, LogLevel::DEBUG);
        } catch (const std::exception& e) {
            std::string msg = "[ConfigPipeline] Step FAILED: " + step.type + " - " + e.what();
            if (step.onError == "fail_fast") {
                throw std::runtime_error(msg);
            } else if (step.onError == "fallback") {
                throw std::runtime_error("PIPELINE_FALLBACK: " + msg);
            } else {
                Logger::write(msg, LogLevel::WARN);
            }
        }
    }

    return config;
}

} // namespace config
