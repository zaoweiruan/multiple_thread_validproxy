#pragma once

#include <string>
#include <vector>
#include <boost/json.hpp>

namespace config {

/// JSON Schema 校验器（Phase 10）
/// 基于 boost::json 的手写轻量校验，支持两类内置 schema：
///   - "bundleChain"    对应 schemas/BundleChain.schema.json
///   - "componentBundle" 对应 schemas/ComponentBundle.schema.json
class SchemaValidator {
public:
    /// 校验指定 kind 的 JSON 值；失败时 error 携带首个错误描述
    static bool validate(const boost::json::value& value,
                         const std::string& schemaKind,
                         std::string& error);

private:
    static bool validateBundleChain(const boost::json::value& value, std::string& error);
    static bool validateComponentBundle(const boost::json::value& value, std::string& error);
};

} // namespace config