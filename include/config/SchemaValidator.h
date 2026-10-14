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

    /// 解析「原始 JSON 文本」并校验 componentBundle 结构。
    ///
    /// 存在的理由：UI 的 onBundleSave 直接消费用户在文本框里输入的**字符串**。
    /// 早前它用单参 boost::json::parse，语法错误会抛异常逃逸出 wx 事件处理器 →
    /// 进程闪崩，且落库在其后故配置一并丢失；原 `is_null()` 检查是死路径。
    /// 本方法用 error_code 重载，**任何输入都不会抛异常**，语法错误只是返回
    /// false + error 描述。把它收敛到此处，既消除崩溃，也让测试能以「字符串入参」
    /// 真正覆盖保存路径（此前只有 validate(value) 一层，属假覆盖）。
    ///
    /// @param json  用户输入的原始文本
    /// @param out   解析成功时写入已解析的 JSON 值（失败时不修改）
    /// @param error 失败原因（面向用户展示）
    /// @return 通过返回 true
    static bool parseAndValidateComponentBundle(const std::string& json,
                                                boost::json::value& out,
                                                std::string& error);

private:
    static bool validateBundleChain(const boost::json::value& value, std::string& error);
    static bool validateComponentBundle(const boost::json::value& value, std::string& error);
};

} // namespace config