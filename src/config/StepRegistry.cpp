#include "config/StepRegistry.h"
#include <utility>

namespace config {

StepRegistry& StepRegistry::instance() {
    static StepRegistry registry;
    return registry;
}

void StepRegistry::registerHandler(const std::string& type, std::unique_ptr<StepHandler> handler) {
    std::shared_ptr<StepHandler> shared = std::move(handler);
    handlers_[type] = shared;
}

const std::shared_ptr<const StepHandler> StepRegistry::getHandler(const std::string& type) const {
    auto it = handlers_.find(type);
    if (it != handlers_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> StepRegistry::getRegisteredTypes() const {
    std::vector<std::string> types;
    types.reserve(handlers_.size());
    for (const auto& kv : handlers_) {
        types.push_back(kv.first);
    }
    return types;
}

} // namespace config
