#include "config/ConflictDetector.h"
#include <algorithm>
#include <cstdio>
#include <fstream>

namespace config {

static void dbg(const char* msg) {
    (void)msg;
}

ConflictDetector::ConflictDetector(const ComponentRegistry& registry)
    : registry_(registry) {
    // 注册表通过引用传入，确保已初始化
}

std::vector<Conflict> ConflictDetector::detect(
    const std::vector<ComponentConfig>& components) const {
    
    dbg("detect(components) called");
    
    // 创建来源信息（无来源包）
    std::vector<ComponentSource> sources;
    for (size_t i = 0; i < components.size(); ++i) {
        ComponentSource source;
        source.component = components[i];
        source.sourceBundleId = "";
        source.sourceIndex = static_cast<int>(i);
        sources.push_back(source);
    }
    
    dbg("sources created, calling detect(sources)");
    
    return detect(sources);
}

std::vector<Conflict> ConflictDetector::detect(
    const std::vector<ComponentSource>& sources) const {
    
    dbg("detect(sources) called");
    
    std::vector<Conflict> conflicts;
    
    // 1. 单例冲突检测 - 使用索引而不是复制 ComponentSource
    std::map<std::string, std::vector<size_t>> singletonIndexMap;
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& source = sources[i];
        dbg("checking isSingleton for component");
        if (isSingleton(source.component)) {
            std::string typeName = registry_.typeToString(source.component.type);
            dbg("singleton found");
            singletonIndexMap[typeName].push_back(i);
        }
    }
    
    dbg("singletonIndexMap populated");
    
    dbg("entering outer for loop");
    for (const auto& pair : singletonIndexMap) {
        dbg("outer for loop iteration");
        const std::string& typeName = pair.first;
        const std::vector<size_t>& indices = pair.second;
        
        if (indices.size() > 1) {
            dbg("duplicate singleton detected!");
            // 检查是否跨包
            bool isCrossBundle = false;
            for (size_t i = 1; i < indices.size(); ++i) {
                if (!sources[indices[0]].sourceBundleId.empty() && 
                    !sources[indices[i]].sourceBundleId.empty() &&
                    sources[indices[0]].sourceBundleId != sources[indices[i]].sourceBundleId) {
                    isCrossBundle = true;
                    break;
                }
            }
            
            for (size_t i = 1; i < indices.size(); ++i) {
                dbg("creating conflict for i");
                
                dbg("before emplace_back");
                conflicts.emplace_back();
                Conflict& conflict = conflicts.back();
                dbg("after emplace_back");
                
                conflict.type = ConflictType::SINGLETON_CONFLICT;
                conflict.component1 = typeName;
                conflict.component2 = typeName;
                conflict.message = "单例组件重复定义: " + typeName;
                conflict.index1 = sources[indices[0]].sourceIndex;
                conflict.index2 = sources[indices[i]].sourceIndex;
                conflict.sourceBundle1 = sources[indices[0]].sourceBundleId;
                conflict.sourceBundle2 = sources[indices[i]].sourceBundleId;
                conflict.isCrossBundle = isCrossBundle;
                
                // 跨包覆盖允许（WARN），同包重复错误（ERROR）
                if (isCrossBundle) {
                    conflict.severity = ConflictSeverity::WARN;
                    conflict.message += " (跨包覆盖，允许)";
                } else {
                    conflict.severity = ConflictSeverity::ERROR;
                    conflict.message += " (同包重复，错误)";
                }
                dbg("all fields set");
            }
            dbg("inner for loop done");
        }
        dbg("if block done");
    }
    dbg("outer for loop done");
    
    // 2. 数组组件 tag 冲突检测
    std::map<std::pair<std::string, std::string>, std::vector<size_t>> tagIndexMap;
    for (size_t i = 0; i < sources.size(); ++i) {
        const auto& source = sources[i];
        if (isArray(source.component)) {
            std::string typeName = registry_.typeToString(source.component.type);
            std::string tag = extractTag(source.component);
            if (!tag.empty()) {
                tagIndexMap[std::make_pair(typeName, tag)].push_back(i);
            }
        }
    }
    
    for (const auto& tagPair : tagIndexMap) {
        const std::string& typeName = tagPair.first.first;
        const std::string& tag = tagPair.first.second;
        const std::vector<size_t>& tagIndices = tagPair.second;
        
        if (tagIndices.size() > 1) {
            // 检查是否跨包
            bool isCrossBundle = false;
            for (size_t i = 1; i < tagIndices.size(); ++i) {
                if (!sources[tagIndices[0]].sourceBundleId.empty() && 
                    !sources[tagIndices[i]].sourceBundleId.empty() &&
                    sources[tagIndices[0]].sourceBundleId != sources[tagIndices[i]].sourceBundleId) {
                    isCrossBundle = true;
                    break;
                }
            }
            
            for (size_t i = 1; i < tagIndices.size(); ++i) {
                conflicts.emplace_back();
                Conflict& conflict = conflicts.back();
                
                conflict.type = ConflictType::TAG_CONFLICT;
                conflict.component1 = typeName + ":" + tag;
                conflict.component2 = typeName + ":" + tag;
                conflict.message = "数组组件 tag 重复: " + tag;
                conflict.index1 = sources[tagIndices[0]].sourceIndex;
                conflict.index2 = sources[tagIndices[i]].sourceIndex;
                conflict.sourceBundle1 = sources[tagIndices[0]].sourceBundleId;
                conflict.sourceBundle2 = sources[tagIndices[i]].sourceBundleId;
                conflict.isCrossBundle = isCrossBundle;
                
                // 跨包覆盖允许（WARN），同包重复错误（ERROR）
                if (isCrossBundle) {
                    conflict.severity = ConflictSeverity::WARN;
                    conflict.message += " (跨包覆盖，允许)";
                } else {
                    conflict.severity = ConflictSeverity::ERROR;
                    conflict.message += " (同包重复，错误)";
                }
            }
        }
    }
    
    dbg("detect(sources) returning");
    
    return conflicts;
}

std::vector<Conflict> ConflictDetector::detectDependencies(
    const std::vector<ComponentConfig>& components) const {
    
    std::vector<Conflict> conflicts;
    
    // 收集所有 inbound 和 outbound 的 tag
    std::unordered_set<std::string> inboundTags;
    std::unordered_set<std::string> outboundTags;
    
    for (const auto& comp : components) {
        std::string typeName = registry_.typeToString(comp.type);
        if (typeName == "inbounds") {
            std::string tag = extractTag(comp);
            if (!tag.empty()) {
                inboundTags.insert(tag);
            }
        } else if (typeName == "outbounds") {
            std::string tag = extractTag(comp);
            if (!tag.empty()) {
                outboundTags.insert(tag);
            }
        }
    }
    
    // 检测 routing 规则中的依赖
    for (size_t i = 0; i < components.size(); ++i) {
        const auto& comp = components[i];
        std::string typeName = registry_.typeToString(comp.type);
        
        if (typeName == "routing") {
            auto rulesIt = comp.params.find("rules");
            if (rulesIt != comp.params.end() && rulesIt->second.is_array()) {
                for (const auto& rule : rulesIt->second.as_array()) {
                    if (!rule.is_object()) continue;
                    
                    const boost::json::object& ruleObj = rule.as_object();
                    
                    // 检查 inboundTag
                    auto inTagIt = ruleObj.find("inboundTag");
                    if (inTagIt != ruleObj.end() && inTagIt->value().is_array()) {
                        for (const auto& tagVal : inTagIt->value().as_array()) {
                            if (tagVal.is_string()) {
                                std::string tag(tagVal.as_string().c_str());
                                if (inboundTags.find(tag) == inboundTags.end()) {
                                    Conflict conflict;
                                    conflict.type = ConflictType::DEPENDENCY_MISSING;
                                    conflict.severity = ConflictSeverity::WARN;
                                    conflict.component1 = "routing";
                                    conflict.component2 = "inbound";
                                    conflict.message = "Routing 规则引用不存在的 inbound: " + tag;
                                    conflict.index1 = static_cast<int>(i);
                                    conflict.index2 = -1;
                                    conflict.isCrossBundle = false;
                                    conflicts.push_back(conflict);
                                }
                            }
                        }
                    }
                    
                    // 检查 outboundTag
                    auto outTagIt = ruleObj.find("outboundTag");
                    if (outTagIt != ruleObj.end() && outTagIt->value().is_string()) {
                        std::string tag(outTagIt->value().as_string().c_str());
                        // direct 和 block 是特殊标签，不需要检查
                        if (tag != "direct" && tag != "block" && 
                            outboundTags.find(tag) == outboundTags.end()) {
                            Conflict conflict;
                            conflict.type = ConflictType::DEPENDENCY_MISSING;
                            conflict.severity = ConflictSeverity::WARN;
                            conflict.component1 = "routing";
                            conflict.component2 = "outbound";
                            conflict.message = "Routing 规则引用不存在的 outbound: " + tag;
                            conflict.index1 = static_cast<int>(i);
                            conflict.index2 = -1;
                            conflict.isCrossBundle = false;
                            conflicts.push_back(conflict);
                        }
                    }
                    
                    // 检查 balancerTag
                    auto balTagIt = ruleObj.find("balancerTag");
                    if (balTagIt != ruleObj.end() && balTagIt->value().is_string()) {
                        // balancerTag 需要在 balancers 中定义
                        // 这里简化处理，不检测
                    }
                }
            }
        }
    }
    
    return conflicts;
}

bool ConflictDetector::handleConflicts(
    const std::vector<Conflict>& conflicts,
    ConflictStrategy strategy) const {
    
    if (conflicts.empty()) {
        return true;
    }
    
    bool hasError = false;
    
    for (const auto& conflict : conflicts) {
        if (conflict.severity == ConflictSeverity::ERROR) {
            hasError = true;
        }
    }
    
    switch (strategy) {
        case ConflictStrategy::FAIL_FAST:
            // 有 ERROR 则返回 false
            return !hasError;
            
        case ConflictStrategy::WARN_CONTINUE:
            // 允许所有冲突继续（WARN 和 ERROR 都继续）
            return true;
            
        case ConflictStrategy::IGNORE:
            // 忽略所有冲突
            return true;
    }
    
    return true;
}

bool ConflictDetector::hasErrors(
    const std::vector<Conflict>& conflicts) const {
    
    for (const auto& conflict : conflicts) {
        if (conflict.severity == ConflictSeverity::ERROR) {
            return true;
        }
    }
    return false;
}

bool ConflictDetector::hasWarnings(
    const std::vector<Conflict>& conflicts) const {
    
    for (const auto& conflict : conflicts) {
        if (conflict.severity == ConflictSeverity::WARN) {
            return true;
        }
    }
    return false;
}

std::string ConflictDetector::extractTag(const ComponentConfig& comp) const {
    
    auto tagIt = comp.params.find("tag");
    if (tagIt != comp.params.end() && tagIt->second.is_string()) {
        return std::string(tagIt->second.as_string().c_str());
    }
    return "";
}

bool ConflictDetector::isSingleton(const ComponentConfig& comp) const {
    
    const ComponentMetadata* metadata = registry_.getMetadata(comp.type);
    if (metadata) {
        return metadata->isSingleton;
    }
    return false;
}

bool ConflictDetector::isArray(const ComponentConfig& comp) const {
    
    const ComponentMetadata* metadata = registry_.getMetadata(comp.type);
    if (metadata) {
        return !metadata->isSingleton;
    }
    return false;
}

} // namespace config
