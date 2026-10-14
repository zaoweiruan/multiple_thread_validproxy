// test_schema_validator.cpp - Phase 10 SchemaValidator unit tests
#include <gtest/gtest.h>
#include <string>
#include <boost/json.hpp>
#include "config/SchemaValidator.h"

using namespace config;

namespace {

// -----------------------------------------------------------------
// Helpers to build JSON values
// -----------------------------------------------------------------
boost::json::value bundleChainJson() {
    boost::json::object obj;
    boost::json::array ids;
    ids.push_back(boost::json::value(std::string("base_xray_001")));
    ids.push_back(boost::json::value(std::string("test_xray_001")));
    obj["bundleIds"] = ids;
    boost::json::object params;
    params["socksPort"] = boost::json::value(std::string("10809"));
    obj["params"] = params;
    return boost::json::value(obj);
}

boost::json::value componentBundleJson() {
    boost::json::object obj;
    obj["id"] = boost::json::value(std::string("bundle_001"));
    obj["coreType"] = boost::json::value(std::int64_t(2));
    obj["version"] = boost::json::value(std::int64_t(1));
    obj["enabled"] = boost::json::value(true);
    boost::json::array comps;
    boost::json::object comp;
    comp["type"] = boost::json::value(std::int64_t(1));  // ComponentType::LOG（整数，与 DAO 落库格式一致）
    comps.push_back(comp);
    obj["components"] = comps;
    return boost::json::value(obj);
}

} // namespace

// -----------------------------------------------------------------
// BundleChain schema tests
// -----------------------------------------------------------------
TEST(SchemaValidatorTest, BundleChainValid) {
    std::string error;
    EXPECT_TRUE(SchemaValidator::validate(bundleChainJson(), "bundleChain", error));
}

TEST(SchemaValidatorTest, BundleChainMissingIds) {
    std::string error;
    boost::json::object obj;
    obj["params"] = boost::json::object();
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "bundleChain", error));
    EXPECT_NE(error.find("bundleIds"), std::string::npos);
}

TEST(SchemaValidatorTest, BundleChainEmptyIds) {
    std::string error;
    boost::json::object obj;
    obj["bundleIds"] = boost::json::array();
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "bundleChain", error));
    EXPECT_NE(error.find("empty"), std::string::npos);
}

TEST(SchemaValidatorTest, BundleChainNonStringEntry) {
    std::string error;
    boost::json::object obj;
    boost::json::array ids;
    ids.push_back(boost::json::value(std::int64_t(12)));
    obj["bundleIds"] = ids;
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "bundleChain", error));
    EXPECT_NE(error.find("string"), std::string::npos);
}

TEST(SchemaValidatorTest, BundleChainDuplicateEntry) {
    std::string error;
    boost::json::object obj;
    boost::json::array ids;
    ids.push_back(boost::json::value(std::string("dup_bundle")));
    ids.push_back(boost::json::value(std::string("dup_bundle")));
    obj["bundleIds"] = ids;
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "bundleChain", error));
    EXPECT_NE(error.find("unique"), std::string::npos);
}

TEST(SchemaValidatorTest, BundleChainBadParams) {
    std::string error;
    boost::json::object obj;
    boost::json::array ids;
    ids.push_back(boost::json::value(std::string("base_xray_001")));
    obj["bundleIds"] = ids;
    obj["params"] = boost::json::value(std::int64_t(3));
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "bundleChain", error));
    EXPECT_NE(error.find("params"), std::string::npos);
}

// -----------------------------------------------------------------
// ComponentBundle schema tests
// -----------------------------------------------------------------
TEST(SchemaValidatorTest, ComponentBundleValid) {
    std::string error;
    EXPECT_TRUE(SchemaValidator::validate(componentBundleJson(), "componentBundle", error));
}

TEST(SchemaValidatorTest, ComponentBundleMissingId) {
    std::string error;
    boost::json::object obj;
    obj["coreType"] = boost::json::value(std::int64_t(2));
    obj["components"] = boost::json::array();
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "componentBundle", error));
    EXPECT_NE(error.find("id"), std::string::npos);
}

TEST(SchemaValidatorTest, ComponentBundleBadCoreType) {
    std::string error;
    boost::json::object obj = componentBundleJson().as_object();
    obj["coreType"] = boost::json::value(std::int64_t(3));
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "componentBundle", error));
    EXPECT_NE(error.find("coreType"), std::string::npos);
}

TEST(SchemaValidatorTest, ComponentBundleTemplateTypeNoLongerRequired) {
    // 组件包只由「代理类型 × 组件类型」决定，不再从属于模板角色：
    // templateType 已从模型移除，缺失时必须通过校验
    std::string error;
    boost::json::object obj = componentBundleJson().as_object();
    EXPECT_TRUE(SchemaValidator::validate(boost::json::value(obj), "componentBundle", error)) << error;
}

TEST(SchemaValidatorTest, ComponentBundleBadVersion) {
    std::string error;
    boost::json::object obj = componentBundleJson().as_object();
    obj["version"] = boost::json::value(std::int64_t(0));
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "componentBundle", error));
    EXPECT_NE(error.find("version"), std::string::npos);
}

TEST(SchemaValidatorTest, ComponentBundleComponentMissingType) {
    std::string error;
    boost::json::object obj = componentBundleJson().as_object();
    boost::json::array comps;
    comps.push_back(boost::json::object());
    obj["components"] = comps;
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "componentBundle", error));
    EXPECT_NE(error.find("components"), std::string::npos);
}

// 回归（2026-10-10）：组件包配置页「保存」闪崩。根因是 onBundleSave 用单参
// boost::json::parse，语法错误会抛异常并逃逸 wx 事件处理器；且合法但非对象的
// JSON（数组/字符串/数字）在 as_object() 处同样抛异常。修复改用 error_code 重载
// + 显式 is_object() 判型 + 结构校验。本组用例锁死「保存前必须拦下」的输入集合。
TEST(SchemaValidatorTest, ComponentBundleRejectsNonObjectRoot) {
    // 合法 JSON 但根节点不是对象 —— 保存路径必须在 as_object() 之前拦下
    std::string error;
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(boost::json::array()), "componentBundle", error));
    EXPECT_NE(error.find("must be a JSON object"), std::string::npos);
}

TEST(SchemaValidatorTest, ComponentBundleRejectsStringComponentType) {
    // 旧契约要求 type 为字符串，但 DAO/UI 实际落库是整数枚举值；
    // 字符串 type 无法被 DAO 反序列化，必须拦下而不是静默存进去
    std::string error;
    boost::json::object obj = componentBundleJson().as_object();
    boost::json::array comps;
    boost::json::object comp;
    comp["type"] = boost::json::value(std::string("log"));
    comps.push_back(comp);
    obj["components"] = comps;
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "componentBundle", error));
    EXPECT_NE(error.find("integer 'type'"), std::string::npos);
}

TEST(SchemaValidatorTest, ComponentBundleRejectsComponentTypeOutOfRange) {
    std::string error;
    boost::json::object obj = componentBundleJson().as_object();
    boost::json::array comps;
    boost::json::object comp;
    comp["type"] = boost::json::value(std::int64_t(99));  // 枚举只有 0..14
    comps.push_back(comp);
    obj["components"] = comps;
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "componentBundle", error));
    EXPECT_NE(error.find("out of range"), std::string::npos);
}

TEST(SchemaValidatorTest, ComponentBundleAcceptsAllEnumBoundaryTypes) {
    // 0 与 14 是合法边界，必须放行
    std::string error;
    for (std::int64_t t : {std::int64_t(0), std::int64_t(14)}) {
        boost::json::object obj = componentBundleJson().as_object();
        boost::json::array comps;
        boost::json::object comp;
        comp["type"] = boost::json::value(t);
        comps.push_back(comp);
        obj["components"] = comps;
        EXPECT_TRUE(SchemaValidator::validate(boost::json::value(obj), "componentBundle", error))
            << "type=" << t << " error=" << error;
    }
}

TEST(SchemaValidatorTest, ComponentBundleAcceptsEmptyComponentsArray) {
    // 「新增」按钮创建的骨架包就是 components: []，必须能通过校验并保存
    std::string error;
    boost::json::object obj = componentBundleJson().as_object();
    obj["components"] = boost::json::array();
    EXPECT_TRUE(SchemaValidator::validate(boost::json::value(obj), "componentBundle", error)) << error;
}

TEST(SchemaValidatorTest, ComponentBundleRejectsNonObjectComponentEntry) {
    std::string error;
    boost::json::object obj = componentBundleJson().as_object();
    boost::json::array comps;
    comps.push_back(boost::json::value(std::int64_t(1)));  // 数组里塞了裸整数
    obj["components"] = comps;
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "componentBundle", error));
    EXPECT_NE(error.find("must be an object"), std::string::npos);
}

TEST(SchemaValidatorTest, ComponentBundleRejectsBadParams) {
    std::string error;
    boost::json::object obj = componentBundleJson().as_object();
    boost::json::array comps;
    boost::json::object comp;
    comp["type"] = boost::json::value(std::int64_t(1));
    comp["params"] = boost::json::value(std::int64_t(5));  // params 必须是对象
    comps.push_back(comp);
    obj["components"] = comps;
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "componentBundle", error));
    EXPECT_NE(error.find("params"), std::string::npos);
}

// -------------------------------------------------------------------------
// 保存路径闸门（字符串入参）——覆盖 onBundleSave 真正消费的输入形态
//
// 此前 6 个用例都停在 validate(value) 这一层，传入的是已构造好的
// boost::json::value，**不经过字符串解析**，因此无法覆盖闪崩根因：
// onBundleSave 原本用单参 boost::json::parse，语法错误会抛异常并逃逸出
// wx 事件处理器 → 进程终止。下面这组用例以「原始文本」入参，
// 断言任何畸形输入都只返回 false 而不抛异常——这正是修复后的契约。
// -------------------------------------------------------------------------
TEST(SchemaValidatorTest, ParseAndValidate_ValidBundleTextPasses) {
    std::string error;
    boost::json::value out;
    const std::string text =
        R"({"id":"b1","coreType":2,"version":1,"enabled":true,"components":[{"type":1}]})";
    EXPECT_TRUE(SchemaValidator::parseAndValidateComponentBundle(text, out, error)) << error;
    ASSERT_TRUE(out.is_object());
    EXPECT_EQ("b1", std::string(out.as_object().at("id").as_string().c_str()));
}

TEST(SchemaValidatorTest, ParseAndValidate_SyntaxErrorReturnsFalseNotThrow) {
    // 单参 parse 在此会抛 boost::system::system_error；
    // 若该用例以异常终止，说明闸门回退成了抛异常实现。
    const std::string truncated("{\"id\":\"b1\",");
    std::string error;
    boost::json::value out;
    EXPECT_NO_THROW({
        EXPECT_FALSE(SchemaValidator::parseAndValidateComponentBundle(
            truncated, out, error));
    });
    EXPECT_NE(std::string::npos, error.find("语法"));
}

TEST(SchemaValidatorTest, ParseAndValidate_TruncatedJsonReturnsFalse) {
    std::string error;
    boost::json::value out;
    EXPECT_FALSE(SchemaValidator::parseAndValidateComponentBundle(
        R"({"id":"b1","coreType":2,)", out, error));
    EXPECT_NE(std::string::npos, error.find("语法"));
}

TEST(SchemaValidatorTest, ParseAndValidate_EmptyStringReturnsFalse) {
    std::string error;
    boost::json::value out;
    EXPECT_FALSE(SchemaValidator::parseAndValidateComponentBundle("", out, error));
}

TEST(SchemaValidatorTest, ParseAndValidate_NonObjectRootsRejected) {
    std::string error;
    boost::json::value out;
    // 数组/字符串/数字：解析成功但不是对象，走 as_object() 会抛异常，必须拦下
    for (const char* text : {"[1,2,3]", "\"hello\"", "123", "true", "null"}) {
        EXPECT_FALSE(SchemaValidator::parseAndValidateComponentBundle(text, out, error))
            << "text=" << text;
        EXPECT_NE(std::string::npos, error.find("对象")) << "text=" << text;
    }
}

TEST(SchemaValidatorTest, ParseAndValidate_StructuralErrorsRejected) {
    std::string error;
    boost::json::value out;
    EXPECT_FALSE(SchemaValidator::parseAndValidateComponentBundle(
        R"({"coreType":2,"components":[]})", out, error));          // 缺 id
    EXPECT_NE(std::string::npos, error.find("id"));

    EXPECT_FALSE(SchemaValidator::parseAndValidateComponentBundle(
        R"({"id":"","coreType":2,"components":[]})", out, error));  // id 空串
    EXPECT_NE(std::string::npos, error.find("id"));

    EXPECT_FALSE(SchemaValidator::parseAndValidateComponentBundle(
        R"({"id":"b","coreType":3,"components":[]})", out, error)); // 非法 coreType
    EXPECT_NE(std::string::npos, error.find("coreType"));

    EXPECT_FALSE(SchemaValidator::parseAndValidateComponentBundle(
        R"({"id":"b","coreType":2})", out, error));                 // 缺 components
    EXPECT_NE(std::string::npos, error.find("components"));
}

TEST(SchemaValidatorTest, ParseAndValidate_FailureLeavesOutUntouched) {
    // 失败时不得污染调用方已有的 out，避免半成品被后续代码误用
    std::string error;
    boost::json::value out = boost::json::value(std::int64_t(42));
    EXPECT_FALSE(SchemaValidator::parseAndValidateComponentBundle(
        R"({"broken":)", out, error));
    ASSERT_TRUE(out.is_int64());
    EXPECT_EQ(42, out.as_int64());
}

TEST(SchemaValidatorTest, ParseAndValidate_AcceptsNewlyCreatedSkeleton) {
    // 「新增」按钮落库的骨架包（components 为空数组）必须能原样再保存
    std::string error;
    boost::json::value out;
    const std::string skeleton =
        R"J({"id":"xray_ENV_geodata","name":"xray_ENV_geodata","description":"Manually created bundle (Xray)","coreType":2,"version":1,"enabled":true,"components":[]})J";
    EXPECT_TRUE(SchemaValidator::parseAndValidateComponentBundle(skeleton, out, error))
        << error;
}

// 回归（2026-10-13）：用户在「新增组件包」后手工往 components 里补组件，
// 漏写 `type` 字段 → 保存被拒并弹
// 「ComponentBundle 'components[0]' missing integer 'type'」。
// 修复在 UI 侧（新增时预置带 type 的组件），但**契约**由本用例锁定：
// 缺 type 必须被拒，带 type（任意合法枚举值 0..14）必须被接受。
TEST(SchemaValidatorTest, ParseAndValidate_ComponentMissingTypeIsRejected) {
    std::string error;
    boost::json::value out;
    const std::string noType = "{\"id\":\"b1\",\"coreType\":2,\"version\":1,\"enabled\":true,\"components\":[{\"params\":{\"loglevel\":\"warning\"}}]}";
    const bool ok = SchemaValidator::parseAndValidateComponentBundle(noType, out, error);
    EXPECT_FALSE(ok) << error;
    EXPECT_NE(std::string::npos, error.find("components[0]"));
    EXPECT_NE(std::string::npos, error.find("'type'"));
}

TEST(SchemaValidatorTest, ParseAndValidate_AllEnumTypesAccepted) {
    // 「新增」对话框列出 15 种类型，任选其一预置都必须能保存
    std::string error;
    for (std::int64_t t = 0; t <= 14; ++t) {
        boost::json::value out;
        const std::string text = std::string("{\"id\":\"b") + std::to_string(t)
            + "\",\"coreType\":2,\"version\":1,\"enabled\":true,"
              "\"components\":[{\"type\":" + std::to_string(t) + ",\"params\":{}}]}";
        EXPECT_TRUE(SchemaValidator::parseAndValidateComponentBundle(text, out, error))
            << "type=" << t << " error=" << error;
    }
}

TEST(SchemaValidatorTest, UnknownKindFails) {
    std::string error;
    EXPECT_FALSE(SchemaValidator::validate(componentBundleJson(), "unknown_schema", error));
    EXPECT_NE(error.find("unknown"), std::string::npos);
}