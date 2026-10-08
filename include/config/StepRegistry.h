#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "config/StepHandler.h"

namespace config {

/// 步骤注册表（Meyer's Singleton）
/// 初始化阶段调用 registerHandler，之后只读（线程安全）
class StepRegistry {
public:
    static StepRegistry& instance();

    void registerHandler(const std::string& type, std::unique_ptr<StepHandler> handler);
    const std::shared_ptr<const StepHandler> getHandler(const std::string& type) const;
    std::vector<std::string> getRegisteredTypes() const;

private:
    StepRegistry() = default;
    std::unordered_map<std::string, std::shared_ptr<const StepHandler>> handlers_;
};

/// 注册所有内置步骤处理器
void registerAllStepHandlers();

} // namespace config
