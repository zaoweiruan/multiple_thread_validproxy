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
