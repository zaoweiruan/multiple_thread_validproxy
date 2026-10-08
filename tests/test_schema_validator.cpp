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
    obj["templateType"] = boost::json::value(std::string("BASE"));
    obj["version"] = boost::json::value(std::int64_t(1));
    obj["enabled"] = boost::json::value(true);
    boost::json::array comps;
    boost::json::object comp;
    comp["type"] = boost::json::value(std::string("log"));
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
    obj["templateType"] = boost::json::value(std::string("BASE"));
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

TEST(SchemaValidatorTest, ComponentBundleBadTemplateType) {
    std::string error;
    boost::json::object obj = componentBundleJson().as_object();
    obj["templateType"] = boost::json::value(std::string("UNKNOWN"));
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "componentBundle", error));
    EXPECT_NE(error.find("templateType"), std::string::npos);
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

TEST(SchemaValidatorTest, UnknownKindFails) {
    std::string error;
    EXPECT_FALSE(SchemaValidator::validate(componentBundleJson(), "unknown_schema", error));
    EXPECT_NE(error.find("unknown"), std::string::npos);
}