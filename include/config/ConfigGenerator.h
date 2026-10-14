#pragma once

#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <boost/json.hpp>
#include <sqlite3.h>

#include "config/PipelineConfig.h"
#include "config/StepHandler.h"
#include "config/ConfigBuilder.h"

namespace config {

/// 线程安全配置缓存（shared_mutex 读写锁）
class ConfigCache {
public:
    /// 查询缓存，命中返回 true 并写出 out
    bool get(const std::string& key, boost::json::value& out) const;

    /// 写入缓存
    void put(const std::string& key, const boost::json::value& value);

    /// 使单个键失效
    void invalidate(const std::string& key);

    /// 清空全部缓存
    void clear();

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, boost::json::value> cache_;
};

/// 配置生成器（Phase 9）：按管线定义生成最终配置。
/// 生成流程：bundleChain 优先（经 BundleChainResolver），否则 templateId（经 ConfigBuilder）；
/// 对生成结果递归替换 ${var} 占位符（replaceJsonValue）；结果按缓存键缓存。
/// 命名说明：既有 src/ConfigGenerator.cpp 已定义 config::ConfigGenerator，
/// 为避免链接期 multiple definition 冲突，本生成器命名为 PipelineConfigGenerator。
class PipelineConfigGenerator {
public:
    explicit PipelineConfigGenerator(sqlite3* db);
    ~PipelineConfigGenerator() = default;

    /// 生成管线配置。编辑时预检失败或运行时生成失败均抛 std::runtime_error。
    /// 缓存命中直接返回（键含 vars 摘要，不同变量集不误命中）。
    boost::json::value generate(const PipelineDefinition& def, const PipelineContext& ctx) const;

    /// 轻量预检：管线是否可生成（bundleChain 非空需 parse 成功且含 bundleIds；否则需 templateId 非空）。
    bool canGenerate(const PipelineDefinition& def) const;

    /// 递归替换 JSON 值中的 ${name} / ${name:default} 占位符（9.2）。
    /// 数字型替换（socksPort/apiPort/observatoryInterval）整体替换为 int64 JSON 数字；
    /// 其余替换保持字符串；未识别占位符保留原文；递归进入 object/array。
    static boost::json::value replaceJsonValue(const boost::json::value& value,
                                               const ConfigVariables& vars);

    /// 使单键缓存失效（编辑时调用）。
    void invalidateCache(const std::string& key);

    /// 清空全部缓存。
    void clearCache();

private:
    /// 缓存键：bundleChain 原文 JSON，或 templateId + 变量摘要（防不同 vars 误命中）。
    static std::string cacheKey(const PipelineDefinition& def);
    static std::string varsSummary(const ConfigVariables& vars);

    sqlite3* db_;
    mutable ConfigCache cache_;
};

} // namespace config