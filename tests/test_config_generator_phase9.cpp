#include <gtest/gtest.h>
#include <sqlite3.h>
#include <boost/json.hpp>
#include <string>

#include "config/ConfigGenerator.h"
#include "config/PipelineConfig.h"
#include "config/ConfigBuilder.h"
#include "config/ComponentBundle.h"
#include "config/ComponentBundleDAO.h"
#include "FullConfigTemplateDAO.h"

using namespace config;

// ============================================================
// ConfigVariables helper
// ============================================================
namespace {

ConfigVariables makeVars() {
    ConfigVariables vars;
    vars.socksPort = 10809;
    vars.apiPort = 10080;
    vars.probeUrl = "https://www.google.com/generate_204";
    vars.observatoryInterval = 30000;
    vars.balancerStrategy = "leastPing";
    vars.enableObservatory = true;
    vars.enableCnDirect = true;
    vars.blockQuic = true;
    return vars;
}

} // namespace

// ============================================================
// replaceJsonValue tests
// ============================================================
TEST(ConfigGeneratorReplaceTest, SinglePlaceholder) {
    boost::json::value v("${socksPort}");
    boost::json::value out = PipelineConfigGenerator::replaceJsonValue(v, makeVars());
    ASSERT_TRUE(out.is_int64());
    EXPECT_EQ(out.as_int64(), 10809);
}

TEST(ConfigGeneratorReplaceTest, WithDefault) {
    boost::json::value v("${probeUrl:https://example.com}");
    boost::json::value out = PipelineConfigGenerator::replaceJsonValue(v, makeVars());
    ASSERT_TRUE(out.is_string());
    EXPECT_EQ(std::string(out.get_string()), "https://www.google.com/generate_204");
}

TEST(ConfigGeneratorReplaceTest, RecursiveObject) {
    boost::json::object obj;
    obj["port"] = boost::json::value("${socksPort}");
    obj["interval"] = boost::json::value("${observatoryInterval}");
    boost::json::value v(obj);
    boost::json::value out = PipelineConfigGenerator::replaceJsonValue(v, makeVars());
    ASSERT_TRUE(out.is_object());
    const boost::json::object& o = out.as_object();
    ASSERT_TRUE(o.at("port").is_int64());
    EXPECT_EQ(o.at("port").as_int64(), 10809);
    ASSERT_TRUE(o.at("interval").is_int64());
    EXPECT_EQ(o.at("interval").as_int64(), 30000);
}

TEST(ConfigGeneratorReplaceTest, RecursiveArray) {
    boost::json::array arr;
    arr.push_back(boost::json::value("${socksPort}"));
    arr.push_back(boost::json::value("${apiPort}"));
    boost::json::value v(arr);
    boost::json::value out = PipelineConfigGenerator::replaceJsonValue(v, makeVars());
    ASSERT_TRUE(out.is_array());
    const boost::json::array& a = out.as_array();
    ASSERT_EQ(a.size(), 2u);
    EXPECT_EQ(a[0].as_int64(), 10809);
    EXPECT_EQ(a[1].as_int64(), 10080);
}

TEST(ConfigGeneratorReplaceTest, UnknownKept) {
    boost::json::value v("${unknownVar}");
    boost::json::value out = PipelineConfigGenerator::replaceJsonValue(v, makeVars());
    ASSERT_TRUE(out.is_string());
    EXPECT_EQ(std::string(out.get_string()), "${unknownVar}");
}

// ============================================================
// ConfigCache tests
// ============================================================
TEST(ConfigCacheTest, PutGet) {
    ConfigCache cache;
    boost::json::value v("hello");
    cache.put("k1", v);
    boost::json::value out;
    EXPECT_TRUE(cache.get("k1", out));
    EXPECT_TRUE(out.is_string());
    EXPECT_EQ(std::string(out.get_string()), "hello");
}

TEST(ConfigCacheTest, Invalidate) {
    ConfigCache cache;
    cache.put("k1", boost::json::value("v1"));
    cache.invalidate("k1");
    boost::json::value out;
    EXPECT_FALSE(cache.get("k1", out));
}

TEST(ConfigCacheTest, Clear) {
    ConfigCache cache;
    cache.put("k1", boost::json::value("v1"));
    cache.put("k2", boost::json::value("v2"));
    cache.clear();
    boost::json::value out;
    EXPECT_FALSE(cache.get("k1", out));
    EXPECT_FALSE(cache.get("k2", out));
}

// ============================================================
// ConfigGenerator::generate against in-memory DB
// ============================================================
class ConfigGeneratorDbTest : public testing::Test {
protected:
    void SetUp() override {
        db_ = nullptr;
        int rc = sqlite3_open(":memory:", &db_);
        ASSERT_EQ(rc, SQLITE_OK);
        db::FullConfigTemplateDAO tplDao(db_);
        tplDao.createTableIfNotExists();
        tplDao.seedDefaultTemplates();
    }

    void TearDown() override {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    sqlite3* db_;
};

TEST_F(ConfigGeneratorDbTest, GenerateFromTemplate) {
    PipelineConfigGenerator generator(db_);
    config::PipelineDefinition def;
    def.templateId = "xray_test_001";

    config::PipelineContext ctx;
    ctx.proxyType = config::ProxyType::Xray;
    ctx.hasProfile = false;
    ctx.vars = makeVars();
    ctx.db = db_;
    ctx.clientName = "default";

    EXPECT_TRUE(generator.canGenerate(def));
    boost::json::value out = generator.generate(def, ctx);
    ASSERT_TRUE(out.is_object());
    const boost::json::object& obj = out.as_object();
    if (obj.contains("inbounds") && obj.at("inbounds").is_array()) {
        const boost::json::array& inbounds = obj.at("inbounds").as_array();
        if (!inbounds.empty() && inbounds[0].is_object()) {
            const boost::json::object& in0 = inbounds[0].as_object();
            if (in0.contains("port")) {
                EXPECT_EQ(in0.at("port").as_int64(), 10809);
            }
        }
    }
    if (obj.contains("api") && obj.at("api").is_object()) {
        const boost::json::object& api = obj.at("api").as_object();
        if (api.contains("listen") && api.at("listen").is_string()) {
            EXPECT_NE(std::string(api.at("listen").get_string()).find("10080"),
                      std::string::npos);
        }
    }
}

TEST_F(ConfigGeneratorDbTest, CanGenerateTemplateTrue) {
    PipelineConfigGenerator generator(db_);
    config::PipelineDefinition def;
    def.templateId = "xray_base_001";
    EXPECT_TRUE(generator.canGenerate(def));
}

TEST_F(ConfigGeneratorDbTest, CanGenerateMissingBundleChainFalse) {
    PipelineConfigGenerator generator(db_);
    config::PipelineDefinition def;
    def.templateId = "";
    def.bundleChain = "{broken json";
    EXPECT_FALSE(generator.canGenerate(def));
}

TEST_F(ConfigGeneratorDbTest, CanGenerateEmptyFalse) {
    PipelineConfigGenerator generator(db_);
    config::PipelineDefinition def;
    EXPECT_FALSE(generator.canGenerate(def));
}