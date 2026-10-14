#pragma once

#include <string>
#include <boost/json.hpp>
#include <sqlite3.h>
#include "Profileitem.h"
#include "config/ConfigBuilder.h"

namespace config {

/// 管线执行上下文（值类型，消除引用安全风险）
struct PipelineContext {
    ProxyType proxyType;
    db::models::Profileitem profile;
    bool hasProfile = false;
    ConfigVariables vars;
    sqlite3* db = nullptr;
    std::string clientName;
    /// 是否把 ${apiPort} 等占位符替换为 vars 里的实际值。
    /// 默认 true = 真实生成配置，替换后 Xray 才能启动。
    /// false = 保留占位符原文，供 UI 预览用——让人一眼看出运行期还需要
    /// 哪些参数（模板不存具体值，值由运行期上下文提供）。
    bool substitutePlaceholders = true;
};

/// 步骤处理接口
class StepHandler {
public:
    virtual ~StepHandler() = default;
    virtual std::string type() const = 0;
    virtual void execute(
        boost::json::object& config,
        const boost::json::object& params,
        const PipelineContext& ctx) const = 0;
};

} // namespace config
