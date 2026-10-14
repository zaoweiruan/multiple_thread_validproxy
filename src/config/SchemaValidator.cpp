#include "config/SchemaValidator.h"

#include <set>

namespace config {

bool SchemaValidator::validate(const boost::json::value& value,
                               const std::string& schemaKind,
                               std::string& error) {
    if (schemaKind == "bundleChain") {
        return validateBundleChain(value, error);
    }
    if (schemaKind == "componentBundle") {
        return validateComponentBundle(value, error);
    }
    error = "SchemaValidator: unknown schema kind: " + schemaKind;
    return false;
}

bool SchemaValidator::parseAndValidateComponentBundle(const std::string& json,
                                                      boost::json::value& out,
                                                      std::string& error) {
    // error_code 重载：语法错误返回 ec 而不抛异常（单参重载会抛，
    // 在 wx 事件处理器里会逃逸并终止进程）。
    boost::system::error_code ec;
    boost::json::value parsed = boost::json::parse(json, ec);
    if (ec) {
        error = "JSON 语法错误: " + ec.message();
        return false;
    }
    // 合法 JSON 也未必是对象（数组/字符串/数字都能解析成功），
    // 必须在 as_object() 之前判型，否则同样抛异常。
    if (!parsed.is_object()) {
        error = "根节点必须是 JSON 对象 { ... }";
        return false;
    }
    if (!validateComponentBundle(parsed, error)) {
        return false;
    }
    out = std::move(parsed);
    return true;
}

bool SchemaValidator::validateBundleChain(const boost::json::value& value,
                                          std::string& error) {
    if (!value.is_object()) {
        error = "BundleChain must be a JSON object";
        return false;
    }
    const boost::json::object& obj = value.as_object();

    if (!obj.contains("bundleIds") || !obj.at("bundleIds").is_array()) {
        error = "BundleChain missing required 'bundleIds' array";
        return false;
    }
    const boost::json::array& bundleIds = obj.at("bundleIds").as_array();
    if (bundleIds.empty()) {
        error = "BundleChain 'bundleIds' must not be empty";
        return false;
    }

    std::set<std::string> seen;
    for (const boost::json::value& idVal : bundleIds) {
        if (!idVal.is_string()) {
            error = "BundleChain 'bundleIds' entries must be strings";
            return false;
        }
        std::string id = std::string(idVal.get_string().c_str());
        if (id.empty()) {
            error = "BundleChain 'bundleIds' entries must not be empty";
            return false;
        }
        if (!seen.insert(id).second) {
            error = "BundleChain 'bundleIds' must be unique: " + id;
            return false;
        }
    }

    if (obj.contains("params") && !obj.at("params").is_object()) {
        error = "BundleChain 'params' must be an object";
        return false;
    }
    return true;
}

bool SchemaValidator::validateComponentBundle(const boost::json::value& value,
                                              std::string& error) {
    if (!value.is_object()) {
        error = "ComponentBundle must be a JSON object";
        return false;
    }
    const boost::json::object& obj = value.as_object();

    if (!obj.contains("id") || !obj.at("id").is_string()) {
        error = "ComponentBundle missing required 'id' string";
        return false;
    }
    if (std::string(obj.at("id").get_string().c_str()).empty()) {
        error = "ComponentBundle 'id' must not be empty";
        return false;
    }

    if (!obj.contains("coreType") || !obj.at("coreType").is_int64()) {
        error = "ComponentBundle missing required 'coreType' integer";
        return false;
    }
    const int coreType = static_cast<int>(obj.at("coreType").as_int64());
    if (coreType != 2 && coreType != 24) {
        error = "ComponentBundle 'coreType' must be 2 (Xray) or 24 (SingBox)";
        return false;
    }

    if (obj.contains("version")) {
        if (!obj.at("version").is_int64() || obj.at("version").as_int64() < 1) {
            error = "ComponentBundle 'version' must be an integer >= 1";
            return false;
        }
    }

    if (obj.contains("enabled") && !obj.at("enabled").is_bool()) {
        error = "ComponentBundle 'enabled' must be a boolean";
        return false;
    }

    if (!obj.contains("components") || !obj.at("components").is_array()) {
        error = "ComponentBundle missing required 'components' array";
        return false;
    }
    const boost::json::array& components = obj.at("components").as_array();
    int idx = 0;
    for (const boost::json::value& compVal : components) {
        if (!compVal.is_object()) {
            error = "ComponentBundle 'components[" + std::to_string(idx) + "]' must be an object";
            return false;
        }
        const boost::json::object& compObj = compVal.as_object();
        if (!compObj.contains("type") || !compObj.at("type").is_int64()) {
            error = "ComponentBundle 'components[" + std::to_string(idx) +
                    "]' missing required integer 'type' (ComponentType enum value)";
            return false;
        }
        const std::int64_t compType = compObj.at("type").as_int64();
        if (compType < 0 || compType > 14) {
            error = "ComponentBundle 'components[" + std::to_string(idx) +
                    "]' type out of range 0..14: " + std::to_string(compType);
            return false;
        }
        if (compObj.contains("id") && !compObj.at("id").is_string()) {
            error = "ComponentBundle 'components[" + std::to_string(idx) + "]' id must be a string";
            return false;
        }
        if (compObj.contains("params") && !compObj.at("params").is_object()) {
            error = "ComponentBundle 'components[" + std::to_string(idx) + "]' params must be an object";
            return false;
        }
        ++idx;
    }
    return true;
}

} // namespace config