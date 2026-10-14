#include "config/ConfigGenerator.h"
#include "config/BundleChainResolver.h"
#include "config/ConfigBuilder.h"
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace {

// --- 解析 BundleChain JSON 文本 ------------------------------------------
config::BundleChain parseBundleChainText(const std::string& jsonText) {
    config::BundleChain chain;
    boost::system::error_code ec;
    boost::json::value v = boost::json::parse(jsonText, ec);
    if (ec || !v.is_object()) {
        return chain;
    }
    const boost::json::object& obj = v.as_object();
    if (obj.contains("bundleIds") && obj.at("bundleIds").is_array()) {
        for (const boost::json::value& idVal : obj.at("bundleIds").as_array()) {
            if (idVal.is_string()) {
                chain.bundleIds.push_back(std::string(idVal.get_string().c_str()));
            }
        }
    }
    // 不再解析 params 段：模板不存具体参数值（占位符由运行期上下文填值）。
    // 历史模板里的 params 段直接忽略。
    return chain;
}

// --- 字符串占位符替换：${name} / ${name:default} -------------------------
// 仅识别 8 个内置变量；未知占位符保留原文。
std::string replaceInString(const std::string& input, const config::ConfigVariables& vars) {
    std::string result;
    result.reserve(input.size());
    std::size_t pos = 0;
    while (pos < input.size()) {
        std::size_t start = input.find("${", pos);
        if (start == std::string::npos) {
            result.append(input, pos, std::string::npos);
            break;
        }
        result.append(input, pos, start - pos);
        std::size_t end = input.find('}', start + 2);
        if (end == std::string::npos) {
            result.append(input, start, std::string::npos);
            break;
        }
        std::string name = input.substr(start + 2, end - (start + 2));
        std::size_t colon = name.find(':');
        if (colon != std::string::npos) {
            name = name.substr(0, colon);
        }
        std::string replacement;
        bool known = true;
        if (name == "socksPort") {
            replacement = std::to_string(vars.socksPort);
        } else if (name == "apiPort") {
            replacement = std::to_string(vars.apiPort);
        } else if (name == "probeUrl") {
            replacement = vars.probeUrl;
        } else if (name == "observatoryInterval") {
            replacement = std::to_string(vars.observatoryInterval);
        } else if (name == "balancerStrategy") {
            replacement = vars.balancerStrategy;
        } else if (name == "enableObservatory") {
            replacement = vars.enableObservatory ? "true" : "false";
        } else if (name == "enableCnDirect") {
            replacement = vars.enableCnDirect ? "true" : "false";
        } else if (name == "blockQuic") {
            replacement = vars.blockQuic ? "true" : "false";
        } else {
            known = false;
        }
        if (known) {
            result.append(replacement);
        } else {
            result.append(input, start, end - start + 1);
        }
        pos = end + 1;
    }
    return result;
}

} // namespace

namespace config {

// ---------------------------------------------------------------------------
// ConfigCache
// ---------------------------------------------------------------------------

bool ConfigCache::get(const std::string& key, boost::json::value& out) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::unordered_map<std::string, boost::json::value>::const_iterator it = cache_.find(key);
    if (it == cache_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

void ConfigCache::put(const std::string& key, const boost::json::value& value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    cache_[key] = value;
}

void ConfigCache::invalidate(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    cache_.erase(key);
}

void ConfigCache::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    cache_.clear();
}

// ---------------------------------------------------------------------------
// ConfigGenerator
// ---------------------------------------------------------------------------

PipelineConfigGenerator::PipelineConfigGenerator(sqlite3* db) : db_(db) {}

bool PipelineConfigGenerator::canGenerate(const PipelineDefinition& def) const {
    if (!def.bundleChain.empty()) {
        boost::system::error_code ec;
        boost::json::value v = boost::json::parse(def.bundleChain, ec);
        if (ec || !v.is_object()) {
            return false;
        }
        const boost::json::object& obj = v.as_object();
        if (!obj.contains("bundleIds") || !obj.at("bundleIds").is_array()) {
            return false;
        }
        return !obj.at("bundleIds").as_array().empty();
    }
    return !def.templateId.empty();
}

boost::json::value PipelineConfigGenerator::replaceJsonValue(const boost::json::value& value,
                                                     const ConfigVariables& vars) {
    if (value.is_string()) {
        const std::string text = std::string(value.get_string().c_str());
        std::string replaced = replaceInString(text, vars);
        if (replaced == text) {
            return value;
        }
        char* endPtr = nullptr;
        long long num = std::strtoll(replaced.c_str(), &endPtr, 10);
        if (endPtr != replaced.c_str() && endPtr != nullptr && *endPtr == '\0') {
            return boost::json::value(static_cast<std::int64_t>(num));
        }
        return boost::json::value(replaced);
    }
    if (value.is_object()) {
        boost::json::object obj = value.as_object();
        for (boost::json::object::iterator it = obj.begin(); it != obj.end(); ++it) {
            it->value() = replaceJsonValue(it->value(), vars);
        }
        return boost::json::value(std::move(obj));
    }
    if (value.is_array()) {
        boost::json::array arr = value.as_array();
        for (std::size_t i = 0; i < arr.size(); ++i) {
            arr[i] = replaceJsonValue(arr[i], vars);
        }
        return boost::json::value(std::move(arr));
    }
    return value;
}

std::string PipelineConfigGenerator::varsSummary(const ConfigVariables& vars) {
    std::string s;
    s += std::to_string(vars.socksPort);
    s += ",";
    s += std::to_string(vars.apiPort);
    s += ",";
    s += vars.probeUrl;
    s += ",";
    s += std::to_string(vars.observatoryInterval);
    s += ",";
    s += vars.balancerStrategy;
    s += ",";
    s += vars.enableObservatory ? "1" : "0";
    s += ",";
    s += vars.enableCnDirect ? "1" : "0";
    s += ",";
    s += vars.blockQuic ? "1" : "0";
    return s;
}

std::string PipelineConfigGenerator::cacheKey(const PipelineDefinition& def) {
    if (!def.bundleChain.empty()) {
        return "chain|" + def.bundleChain;
    }
    return "template|" + def.templateId;
}

boost::json::value PipelineConfigGenerator::generate(const PipelineDefinition& def,
                                             const PipelineContext& ctx) const {
    if (!canGenerate(def)) {
        throw std::runtime_error("ConfigGenerator: cannot generate for pipeline definition");
    }

    const std::string key = cacheKey(def) + "|" + varsSummary(ctx.vars);
    boost::json::value cached;
    if (cache_.get(key, cached)) {
        return cached;
    }

    boost::json::value config;
    if (!def.bundleChain.empty()) {
        BundleChain chain = parseBundleChainText(def.bundleChain);
        BundleChainResolver resolver(db_);
        BundleChainResolveResult rr = resolver.resolveToConfig(chain, ctx);
        if (!rr.success) {
            throw std::runtime_error("Pipeline bundleChain resolve failed: " + rr.errorMessage);
        }
        config = rr.config;
    } else {
        config = ConfigBuilder::loadTemplateById(db_, def.templateId);
    }

    boost::json::value replaced = replaceJsonValue(config, ctx.vars);
    cache_.put(key, replaced);
    return replaced;
}

void PipelineConfigGenerator::invalidateCache(const std::string& key) {
    cache_.invalidate(key);
}

void PipelineConfigGenerator::clearCache() {
    cache_.clear();
}

} // namespace config