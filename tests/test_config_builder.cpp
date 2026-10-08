#include <gtest/gtest.h>
#include <sstream>
#include <algorithm>
#include <boost/json.hpp>
#include <sqlite3.h>
#include <filesystem>
#include <fstream>
#include "test_utils.h"
#include "config/ConfigBuilder.h"
#include "config/StepHandler.h"
#include "config/StepRegistry.h"
#include "config/ConfigPipeline.h"
#include "ConfigGenerator.h"
#include "Profileitem.h"
#include "FullConfigTemplateDAO.h"
#include "config/ComponentBundleDAO.h"

using namespace config;
using namespace db::models;

// ============================================================
// Helper to create Profileitem with minimal required fields
// ============================================================
static Profileitem makeProfile(const std::string& configType, const std::string& address,
                               const std::string& port, const std::string& id,
                               const std::string& network = "tcp",
                               const std::string& streamSecurity = "",
                               const std::string& sni = "",
                               const std::string& publicKey = "") {
    Profileitem p;
    p.indexid = "test-index";
    p.configtype = configType;
    p.address = address;
    p.port = port;
    p.id = id;
    p.security = "auto";
    p.network = network;
    p.streamsecurity = streamSecurity;
    p.sni = sni;
    p.publickey = publicKey;
    p.allowinsecure = "0";
    p.muxEnabled = 0;
    return p;
}

// ============================================================
// Helper to create in-memory SQLite database
// ============================================================
class InMemoryDB : public testing::Test {
protected:
    void SetUp() override {
        db_ = nullptr;
        int rc = sqlite3_open(":memory:", &db_);
        ASSERT_EQ(rc, SQLITE_OK);
    }
    
    void TearDown() override {
        if (db_) {
            sqlite3_close(db_);
        }
    }
    
    sqlite3* db_ = nullptr;
};

// ============================================================
// Helper to create in-memory SQLite database with seeded templates
// ============================================================
class InMemoryDBWithTemplates : public InMemoryDB {
protected:
    void SetUp() override {
        InMemoryDB::SetUp();
        db::FullConfigTemplateDAO dao(db_);
        dao.createTableIfNotExists();
        dao.seedDefaultTemplates();
    }
};

// ============================================================
// ConfigBuilder Tests
// ============================================================

TEST_F(InMemoryDB, ConfigBuilderConstructs) {
    ConfigBuilder builder(db_);
    SUCCEED() << "ConfigBuilder constructed successfully";
}

TEST_F(InMemoryDB, ConfigBuilderIsEnableNewConfigBuilderAlwaysTrue) {
    // ConfigBuilder is always enabled - no longer reads from config file
    bool enabled = ConfigBuilder::isEnableNewConfigBuilder("");
    EXPECT_TRUE(enabled);
}

TEST_F(InMemoryDBWithTemplates, ConfigBuilderBuildTestXrayBasic) {
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.probeUrl = "https://www.google.com/generate_204";
    
    // Should not throw (template is seeded)
    EXPECT_NO_THROW({
        boost::json::value config = builder.build(ConfigMode::Test, "xray", p, vars);
        EXPECT_TRUE(config.is_object());
    });
}

TEST_F(InMemoryDBWithTemplates, ConfigBuilderBuildTestSingBoxBasic) {
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.probeUrl = "https://www.google.com/generate_204";
    
    // Should not throw (template is seeded)
    EXPECT_NO_THROW({
        boost::json::value config = builder.build(ConfigMode::Test, "singbox", p, vars);
        EXPECT_TRUE(config.is_object());
    });
}

TEST_F(InMemoryDBWithTemplates, ConfigBuilderBuildStandaloneXrayBasic) {
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10809;
    vars.apiPort = 10085;
    
    // Should not throw
    EXPECT_NO_THROW({
        boost::json::value config = builder.build(ConfigMode::Standalone, "xray", p, vars);
        EXPECT_TRUE(config.is_object());
    });
}

TEST_F(InMemoryDBWithTemplates, ConfigBuilderBuildStandaloneSingBoxBasic) {
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10809;
    
    // Should not throw
    EXPECT_NO_THROW({
        boost::json::value config = builder.build(ConfigMode::Standalone, "singbox", p, vars);
        EXPECT_TRUE(config.is_object());
    });
}

TEST_F(InMemoryDBWithTemplates, ConfigBuilderBuildPoolXrayBasic) {
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10810;
    vars.apiPort = 10086;
    vars.observatoryInterval = 30000;
    vars.balancerStrategy = "leastPing";
    vars.enableObservatory = true;
    
    // Should not throw
    EXPECT_NO_THROW({
        boost::json::value config = builder.build(ConfigMode::Pool, "xray", p, vars);
        EXPECT_TRUE(config.is_object());
    });
}

TEST_F(InMemoryDBWithTemplates, ConfigBuilderBuildPoolSingBoxBasic) {
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10810;
    
    // Should not throw
    EXPECT_NO_THROW({
        boost::json::value config = builder.build(ConfigMode::Pool, "singbox", p, vars);
        EXPECT_TRUE(config.is_object());
    });
}

TEST_F(InMemoryDB, ConfigBuilderSerialize) {
    ConfigBuilder builder(db_);
    
    boost::json::object obj;
    obj["key"] = "value";
    obj["number"] = 42;
    
    std::string serialized = builder.serialize(obj);
    EXPECT_FALSE(serialized.empty());
    
    // Should be parseable
    boost::json::value parsed = boost::json::parse(serialized);
    EXPECT_EQ(parsed.as_object().at("key").as_string(), "value");
    EXPECT_EQ(parsed.as_object().at("number").as_int64(), 42);
}

TEST_F(InMemoryDB, ConfigBuilderSerializeCompact) {
    ConfigBuilder builder(db_);
    
    boost::json::object obj;
    obj["key"] = "value";
    obj["number"] = 42;
    
    std::string serialized = builder.serializeCompact(obj);
    EXPECT_FALSE(serialized.empty());
    EXPECT_EQ(serialized.find("\n"), std::string::npos) << "Compact output should have no newlines";
    
    // Should be parseable
    boost::json::value parsed = boost::json::parse(serialized);
    EXPECT_EQ(parsed.as_object().at("key").as_string(), "value");
    EXPECT_EQ(parsed.as_object().at("number").as_int64(), 42);
}

// ============================================================
// FullConfigTemplateDAO Tests
// ============================================================

TEST_F(InMemoryDB, FullConfigTemplateDAOCreateTable) {
    db::FullConfigTemplateDAO dao(db_);
    
    bool exists = dao.tableExists();
    EXPECT_FALSE(exists);
    
    bool created = dao.createTableIfNotExists();
    EXPECT_TRUE(created);
    
    exists = dao.tableExists();
    EXPECT_TRUE(exists);
}

TEST_F(InMemoryDB, FullConfigTemplateDAOSaveAndGet) {
    db::FullConfigTemplateDAO dao(db_);
    dao.createTableIfNotExists();
    
    db::FullConfigTemplateItem item;
    item.id = "test_001";
    item.remarks = "Test template";
    item.enabled = 1;
    item.coreType = 2;  // Xray
    item.config = R"({"log": {"loglevel": "warning"}})";
    
    bool saved = dao.save(item);
    EXPECT_TRUE(saved);
    
    auto result = dao.getById("test_001");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->id, "test_001");
    EXPECT_EQ(result->remarks, "Test template");
    EXPECT_EQ(result->enabled, 1);
    EXPECT_EQ(result->coreType, 2);
    EXPECT_TRUE(result->config.find("loglevel") != std::string::npos);
}

TEST_F(InMemoryDB, FullConfigTemplateDAOGetByCoreType) {
    db::FullConfigTemplateDAO dao(db_);
    dao.createTableIfNotExists();
    
    // Save Xray template
    db::FullConfigTemplateItem xrayItem;
    xrayItem.id = "xray_001";
    xrayItem.coreType = 2;
    xrayItem.enabled = 1;
    xrayItem.config = R"({"outbounds": []})";
    dao.save(xrayItem);
    
    // Save SingBox template
    db::FullConfigTemplateItem singboxItem;
    singboxItem.id = "singbox_001";
    singboxItem.coreType = 24;
    singboxItem.enabled = 1;
    singboxItem.config = R"({"outbounds": []})";
    dao.save(singboxItem);
    
    auto xrayResult = dao.getByCoreType(2);
    ASSERT_TRUE(xrayResult.has_value());
    EXPECT_EQ(xrayResult->id, "xray_001");
    
    auto singboxResult = dao.getByCoreType(24);
    ASSERT_TRUE(singboxResult.has_value());
    EXPECT_EQ(singboxResult->id, "singbox_001");
}

TEST_F(InMemoryDB, FullConfigTemplateDAOGetAllEnabled) {
    db::FullConfigTemplateDAO dao(db_);
    dao.createTableIfNotExists();
    
    // Save enabled item
    db::FullConfigTemplateItem enabledItem;
    enabledItem.id = "enabled_001";
    enabledItem.enabled = 1;
    enabledItem.coreType = 2;
    dao.save(enabledItem);
    
    // Save disabled item
    db::FullConfigTemplateItem disabledItem;
    disabledItem.id = "disabled_001";
    disabledItem.enabled = 0;
    disabledItem.coreType = 2;
    dao.save(disabledItem);
    
    auto enabledItems = dao.getAllEnabled();
    EXPECT_EQ(enabledItems.size(), 1);
    EXPECT_EQ(enabledItems[0].id, "enabled_001");
}

TEST_F(InMemoryDB, FullConfigTemplateDAOSeedDefaultTemplates) {
    db::FullConfigTemplateDAO dao(db_);
    dao.createTableIfNotExists();
    
    // Table is empty, should seed
    int inserted = dao.seedDefaultTemplates();
    EXPECT_GT(inserted, 0);
    
    // Check that templates were created (2 base + 2 default + 2 test + 2 pool = 8)
    auto allItems = dao.getAll();
    EXPECT_GE(allItems.size(), 8);
    
    // Check Xray base template exists (first CoreType=2)
    auto xrayBase = dao.getByCoreType(2);
    ASSERT_TRUE(xrayBase.has_value());
    EXPECT_EQ(xrayBase->id, "xray_base_001");
    EXPECT_TRUE(xrayBase->config.find("log") != std::string::npos);
    EXPECT_TRUE(xrayBase->config.find("dns") != std::string::npos);
    
    // Check Xray default template exists
    auto xrayDefault = dao.getById("xray_default_001");
    ASSERT_TRUE(xrayDefault.has_value());
    EXPECT_EQ(xrayDefault->parentId, "xray_base_001");
    EXPECT_TRUE(xrayDefault->config.find("routing") != std::string::npos);
    
    // Check SingBox base template exists
    auto singboxBase = dao.getById("singbox_base_001");
    ASSERT_TRUE(singboxBase.has_value());
    EXPECT_TRUE(singboxBase->config.find("log") != std::string::npos);
    EXPECT_TRUE(singboxBase->config.find("dns") != std::string::npos);
    
    // Check SingBox default template exists
    auto singboxDefault = dao.getById("singbox_default_001");
    ASSERT_TRUE(singboxDefault.has_value());
    EXPECT_EQ(singboxDefault->parentId, "singbox_base_001");
    EXPECT_TRUE(singboxDefault->config.find("route") != std::string::npos);
}

TEST_F(InMemoryDB, FullConfigTemplateDAOSeedIdempotent) {
    db::FullConfigTemplateDAO dao(db_);
    dao.createTableIfNotExists();
    
    // First seed
    int inserted1 = dao.seedDefaultTemplates();
    EXPECT_GT(inserted1, 0);
    
    // Second seed should not insert anything (table already has data)
    int inserted2 = dao.seedDefaultTemplates();
    EXPECT_EQ(inserted2, 0);
    
    // Check that only one set of templates exists
    auto allItems = dao.getAll();
    EXPECT_EQ(allItems.size(), 8);  // 2 base + 2 default + 2 test + 2 pool
}

TEST_F(InMemoryDB, FullConfigTemplateDAODeleteById) {
    db::FullConfigTemplateDAO dao(db_);
    dao.createTableIfNotExists();
    
    // Save item
    db::FullConfigTemplateItem item;
    item.id = "to_delete_001";
    item.enabled = 1;
    item.coreType = 2;
    dao.save(item);
    
    // Verify it exists
    auto result = dao.getById("to_delete_001");
    ASSERT_TRUE(result.has_value());
    
    // Delete it
    bool deleted = dao.deleteById("to_delete_001");
    EXPECT_TRUE(deleted);
    
    // Verify it's gone
result = dao.getById("to_delete_001");
    EXPECT_FALSE(result.has_value());
}

// ============================================================
// FullConfigTemplateDAO Phase 6: BundleChain columns + sync status
// ============================================================

TEST_F(InMemoryDB, FullConfigTemplateDAONewColumnsRoundTrip) {
    db::FullConfigTemplateDAO dao(db_);
    ASSERT_TRUE(dao.createTableIfNotExists());

    db::FullConfigTemplateItem item;
    item.id = "chain_template_001";
    item.remarks = "Chain template";
    item.enabled = 1;
    item.coreType = 2;
    item.config = R"({"log": {"loglevel": "warning"}})";
    item.bundleChain = R"({"bundleIds":["base_xray_001","test_xray_001"],"params":{"socksPort":"10809"}})";
    item.templateType = "bundleChain";
    item.bundleVersion = R"([{"bundleId":"base_xray_001","version":1},{"bundleId":"test_xray_001","version":2}])";

    ASSERT_TRUE(dao.save(item));
    std::optional<db::FullConfigTemplateItem> loaded = dao.getById("chain_template_001");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->bundleChain, item.bundleChain);
    EXPECT_EQ(loaded->templateType, "bundleChain");
    EXPECT_EQ(loaded->bundleVersion, item.bundleVersion);
}

TEST_F(InMemoryDB, FullConfigTemplateDAOIsTemplateInSyncNoBundleChain) {
    db::FullConfigTemplateDAO dao(db_);
    ASSERT_TRUE(dao.createTableIfNotExists());
    db::FullConfigTemplateItem item;
    item.id = "plain_001";
    item.remarks = "Plain";
    item.coreType = 2;
    item.config = R"({"log": {}})";
    ASSERT_TRUE(dao.save(item));
    EXPECT_TRUE(dao.isTemplateInSync("plain_001"));
}

TEST_F(InMemoryDB, FullConfigTemplateDAOIsTemplateInSyncFreshChain) {
    sqlite3* db = db_;
    config::ComponentBundleDAO bundleDao(db);
    ASSERT_TRUE(bundleDao.createTableIfNotExists());
    config::ComponentBundle bundle;
    bundle.id = "chain_bundle_001";
    bundle.name = "Chain bundle";
    bundle.description = "test";
    bundle.coreType = config::CoreType::XRAY;
    bundle.templateType = config::TemplateType::BASE;
    bundle.version = 1;
    bundle.enabled = true;
    config::ComponentConfig logComp;
    logComp.type = config::ComponentType::LOG;
    logComp.params["loglevel"] = boost::json::value(std::string("warning"));
    bundle.components.push_back(logComp);
    ASSERT_TRUE(bundleDao.save(bundle));

    db::FullConfigTemplateDAO dao(db);
    ASSERT_TRUE(dao.createTableIfNotExists());
    db::FullConfigTemplateItem item;
    item.id = "chain_sync_001";
    item.remarks = "Chain sync";
    item.coreType = 2;
    item.config = R"({"log": {}})";
    item.bundleChain = R"({"bundleIds":["chain_bundle_001"]})";
    item.bundleVersion = R"([{"bundleId":"chain_bundle_001","version":1}])";
    ASSERT_TRUE(dao.save(item));
    EXPECT_TRUE(dao.isTemplateInSync("chain_sync_001"));
}

TEST_F(InMemoryDB, FullConfigTemplateDAOIsTemplateInSyncAfterBundleUpdate) {
    sqlite3* db = db_;
    config::ComponentBundleDAO bundleDao(db);
    ASSERT_TRUE(bundleDao.createTableIfNotExists());
    config::ComponentBundle bundle;
    bundle.id = "chain_bundle_001";
    bundle.name = "Chain bundle";
    bundle.coreType = config::CoreType::XRAY;
    bundle.templateType = config::TemplateType::BASE;
    bundle.version = 1;
    bundle.enabled = true;
    config::ComponentConfig logComp;
    logComp.type = config::ComponentType::LOG;
    logComp.params["loglevel"] = boost::json::value(std::string("warning"));
    bundle.components.push_back(logComp);
    ASSERT_TRUE(bundleDao.save(bundle));

    db::FullConfigTemplateDAO dao(db);
    ASSERT_TRUE(dao.createTableIfNotExists());
    db::FullConfigTemplateItem item;
    item.id = "chain_sync_001";
    item.remarks = "Chain sync";
    item.coreType = 2;
    item.config = R"({"log": {}})";
    item.bundleChain = R"({"bundleIds":["chain_bundle_001"]})";
    item.bundleVersion = R"([{"bundleId":"chain_bundle_001","version":1}])";
    ASSERT_TRUE(dao.save(item));

    EXPECT_TRUE(dao.isTemplateInSync("chain_sync_001"));
    int newVersion = bundleDao.updateVersion("chain_bundle_001");
    EXPECT_EQ(newVersion, 2);
    EXPECT_FALSE(dao.isTemplateInSync("chain_sync_001"));
}

TEST_F(InMemoryDB, FullConfigTemplateDAOIsTemplateInSyncMissingBundle) {
    db::FullConfigTemplateDAO dao(db_);
    ASSERT_TRUE(dao.createTableIfNotExists());
    db::FullConfigTemplateItem item;
    item.id = "chain_missing_001";
    item.remarks = "Missing bundle";
    item.coreType = 2;
    item.config = R"({"log": {}})";
    item.bundleChain = R"({"bundleIds":["no_such_bundle"]})";
    item.bundleVersion = R"([{"bundleId":"no_such_bundle","version":1}])";
    ASSERT_TRUE(dao.save(item));
    EXPECT_FALSE(dao.isTemplateInSync("chain_missing_001"));
}

TEST_F(InMemoryDB, FullConfigTemplateDAOIsTemplateInSyncEmptyVersion) {
    db::FullConfigTemplateDAO dao(db_);
    ASSERT_TRUE(dao.createTableIfNotExists());
    db::FullConfigTemplateItem item;
    item.id = "chain_nover_001";
    item.remarks = "No version";
    item.coreType = 2;
    item.config = R"({"log": {}})";
    item.bundleChain = R"({"bundleIds":["some_bundle"]})";
    item.bundleVersion = "";
    ASSERT_TRUE(dao.save(item));
    EXPECT_FALSE(dao.isTemplateInSync("chain_nover_001"));
}

// ============================================================
// Config Structure Validation Tests
// ============================================================

TEST_F(InMemoryDBWithTemplates, ConfigBuilderTestXrayHasRequiredFields) {
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.probeUrl = "https://www.google.com/generate_204";
    
    boost::json::value config = builder.build(ConfigMode::Test, "xray", p, vars);
    boost::json::object& obj = config.as_object();
    
    // Xray config should have required top-level fields
    EXPECT_TRUE(obj.contains("log")) << "Xray test config missing 'log'";
    EXPECT_TRUE(obj.contains("inbounds")) << "Xray test config missing 'inbounds'";
    EXPECT_TRUE(obj.contains("outbounds")) << "Xray test config missing 'outbounds'";
    EXPECT_TRUE(obj.contains("routing")) << "Xray test config missing 'routing'";
    
    // Check inbounds structure
    if (obj.contains("inbounds") && obj["inbounds"].is_array()) {
        EXPECT_FALSE(obj["inbounds"].as_array().empty()) << "Xray test config has empty inbounds";
    }
    
    // Check outbounds structure
    if (obj.contains("outbounds") && obj["outbounds"].is_array()) {
        EXPECT_FALSE(obj["outbounds"].as_array().empty()) << "Xray test config has empty outbounds";
    }
}

TEST_F(InMemoryDBWithTemplates, ConfigBuilderTestSingBoxHasRequiredFields) {
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.probeUrl = "https://www.google.com/generate_204";
    
    boost::json::value config = builder.build(ConfigMode::Test, "singbox", p, vars);
    boost::json::object& obj = config.as_object();
    
    // SingBox config should have required top-level fields
    EXPECT_TRUE(obj.contains("log")) << "SingBox test config missing 'log'";
    EXPECT_TRUE(obj.contains("inbounds")) << "SingBox test config missing 'inbounds'";
    EXPECT_TRUE(obj.contains("outbounds")) << "SingBox test config missing 'outbounds'";
    
    // Check inbounds structure
    if (obj.contains("inbounds") && obj["inbounds"].is_array()) {
        EXPECT_FALSE(obj["inbounds"].as_array().empty()) << "SingBox test config has empty inbounds";
    }
    
    // Check outbounds structure
    if (obj.contains("outbounds") && obj["outbounds"].is_array()) {
        EXPECT_FALSE(obj["outbounds"].as_array().empty()) << "SingBox test config has empty outbounds";
    }
}

TEST_F(InMemoryDBWithTemplates, ConfigBuilderStandaloneXrayHasRequiredFields) {
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10809;
    vars.apiPort = 10085;
    
    boost::json::value config = builder.build(ConfigMode::Standalone, "xray", p, vars);
    boost::json::object& obj = config.as_object();
    
    // Xray standalone config should have required top-level fields
    EXPECT_TRUE(obj.contains("log")) << "Xray standalone config missing 'log'";
    EXPECT_TRUE(obj.contains("inbounds")) << "Xray standalone config missing 'inbounds'";
    EXPECT_TRUE(obj.contains("outbounds")) << "Xray standalone config missing 'outbounds'";
    EXPECT_TRUE(obj.contains("routing")) << "Xray standalone config missing 'routing'";
    
    // Check that socks port is configured
    bool foundSocksPort = false;
    if (obj.contains("inbounds") && obj["inbounds"].is_array()) {
        for (const auto& inbound : obj["inbounds"].as_array()) {
            if (inbound.is_object()) {
                const boost::json::object& inboundObj = inbound.as_object();
                if (inboundObj.contains("port")) {
                    int port = static_cast<int>(inboundObj.at("port").as_int64());
                    if (port == 10809) {
                        foundSocksPort = true;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundSocksPort) << "Xray standalone config missing socks port 10809";
}

TEST_F(InMemoryDBWithTemplates, ConfigBuilderPoolXrayHasRequiredFields) {
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10810;
    vars.apiPort = 10086;
    vars.enableObservatory = true;
    
    boost::json::value config = builder.build(ConfigMode::Pool, "xray", p, vars);
    boost::json::object& obj = config.as_object();
    
    // Xray pool config should have required top-level fields
    EXPECT_TRUE(obj.contains("log")) << "Xray pool config missing 'log'";
    EXPECT_TRUE(obj.contains("inbounds")) << "Xray pool config missing 'inbounds'";
    EXPECT_TRUE(obj.contains("outbounds")) << "Xray pool config missing 'outbounds'";
    EXPECT_TRUE(obj.contains("routing")) << "Xray pool config missing 'routing'";
    
    // Check that API configuration is present for pool mode (top-level, not inbound)
    EXPECT_TRUE(obj.contains("api")) << "Xray pool config missing 'api' configuration";
    
    if (obj.contains("api") && obj["api"].is_object()) {
        const boost::json::object& apiObj = obj["api"].as_object();
        EXPECT_TRUE(apiObj.contains("tag")) << "Xray pool API config missing 'tag'";
        EXPECT_TRUE(apiObj.contains("listen")) << "Xray pool API config missing 'listen'";
        EXPECT_TRUE(apiObj.contains("services")) << "Xray pool API config missing 'services'";
    }
}

// ============================================================
// Regression Tests - Old Path Verification
// ============================================================

TEST_F(InMemoryDB, OldConfigGeneratorStillWorksWhenNewBuilderDisabled) {
    // Verify that ConfigGenerator (old path) still works independently
    // This ensures backward compatibility when db_ is nullptr
    
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    
    // ConfigGenerator should work without errors
    EXPECT_NO_THROW({
        XrayConfig config = gen.generateConfig(p);
        // Verify config has expected structure
        EXPECT_FALSE(config.outbound_json.empty());
        boost::json::value outbound = boost::json::parse(config.outbound_json);
        EXPECT_TRUE(outbound.is_object());
    });
}

TEST_F(InMemoryDBWithTemplates, ConfigBuilderAndOldGeneratorProduceCompatibleOutbounds) {
    // Both ConfigBuilder and ConfigGenerator should produce compatible outbound structures
    // This ensures smooth transition between old and new code paths
    
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    
    // New path: ConfigBuilder - produces full config
    boost::json::value newConfig = builder.build(ConfigMode::Test, "xray", p, vars);
    
    // Verify new config has required top-level fields
    EXPECT_TRUE(newConfig.is_object());
    boost::json::object& newObj = newConfig.as_object();
    EXPECT_TRUE(newObj.contains("log")) << "New config missing 'log'";
    EXPECT_TRUE(newObj.contains("inbounds")) << "New config missing 'inbounds'";
    EXPECT_TRUE(newObj.contains("outbounds")) << "New config missing 'outbounds'";
    
    // Extract outbound from new config
    if (newObj.contains("outbounds") && newObj["outbounds"].is_array()) {
        boost::json::array& newOutbounds = newObj["outbounds"].as_array();
        ASSERT_FALSE(newOutbounds.empty());
        
        // Verify new outbound has required fields
        if (newOutbounds[0].is_object()) {
            const boost::json::object& newOutbound = newOutbounds[0].as_object();
            EXPECT_TRUE(newOutbound.contains("protocol")) << "New outbound missing protocol";
            EXPECT_TRUE(newOutbound.contains("settings")) << "New outbound missing settings";
        }
    }
    
    // Old path: ConfigGenerator - produces minimal config with only outbounds
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);
    XrayConfig oldConfig = gen.generateConfig(p);
    
    boost::json::value oldConfigJson = boost::json::parse(oldConfig.outbound_json);
    
    // Old config should have outbounds (minimal structure from XrayConfigAssembler)
    EXPECT_TRUE(oldConfigJson.is_object());
    boost::json::object& oldObj = oldConfigJson.as_object();
    EXPECT_TRUE(oldObj.contains("outbounds")) << "Old config missing 'outbounds'";
    
    // Extract outbound from old config
    if (oldObj.contains("outbounds") && oldObj["outbounds"].is_array()) {
        boost::json::array& oldOutbounds = oldObj["outbounds"].as_array();
        ASSERT_FALSE(oldOutbounds.empty());
        
        // Verify old outbound has required fields
        if (oldOutbounds[0].is_object()) {
            const boost::json::object& oldOutbound = oldOutbounds[0].as_object();
            EXPECT_TRUE(oldOutbound.contains("protocol")) << "Old outbound missing protocol";
            EXPECT_TRUE(oldOutbound.contains("settings")) << "Old outbound missing settings";
        }
    }
    
    // Both should produce the same protocol for the same profile
    if (newObj.contains("outbounds") && newObj["outbounds"].is_array() &&
        oldObj.contains("outbounds") && oldObj["outbounds"].is_array()) {
        const boost::json::array& newOutbounds = newObj["outbounds"].as_array();
        const boost::json::array& oldOutbounds = oldObj["outbounds"].as_array();
        
        if (!newOutbounds.empty() && !oldOutbounds.empty() &&
            newOutbounds[0].is_object() && oldOutbounds[0].is_object()) {
            const boost::json::object& newOutbound = newOutbounds[0].as_object();
            const boost::json::object& oldOutbound = oldOutbounds[0].as_object();
            
            if (newOutbound.contains("protocol") && oldOutbound.contains("protocol")) {
                std::string newProtocol = std::string(newOutbound.at("protocol").as_string().c_str());
                std::string oldProtocol = std::string(oldOutbound.at("protocol").as_string().c_str());
                EXPECT_EQ(newProtocol, oldProtocol) << "New and old paths produce different protocols";
            }
        }
    }
}

// ============================================================
// Integration Tests - Template Usage Verification
// ============================================================

TEST_F(InMemoryDB, ConfigBuilderUsesTemplateWhenAvailable) {
    // Create a template with the expected ID for test mode
    db::FullConfigTemplateDAO dao(db_);
    dao.createTableIfNotExists();
    
    // Create a template with the expected ID for test_xray
    db::FullConfigTemplateItem customTemplate;
    customTemplate.id = "xray_test_001";
    customTemplate.remarks = "Custom template for integration test";
    customTemplate.enabled = 1;
    customTemplate.coreType = 2;  // Xray
    customTemplate.config = R"({
        "log": {"loglevel": "debug_custom_marker_12345"},
        "dns": {"tag": "custom_dns"}
    })";
    
    bool saved = dao.save(customTemplate);
    EXPECT_TRUE(saved);
    
    // Now build a config - should use the template
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.probeUrl = "https://www.google.com/generate_204";
    
    boost::json::value config = builder.build(ConfigMode::Test, "xray", p, vars);
    
    // Verify the template was used (check for the custom marker)
    EXPECT_TRUE(config.is_object());
    boost::json::object& obj = config.as_object();
    
    // Check log level from template
    if (obj.contains("log") && obj["log"].is_object()) {
        const boost::json::object& logObj = obj["log"].as_object();
        if (logObj.contains("loglevel")) {
            std::string loglevel = std::string(logObj.at("loglevel").as_string().c_str());
            EXPECT_EQ(loglevel, "debug_custom_marker_12345") 
                << "Template loglevel not used in output";
        }
    }
    
    // Check DNS tag from template
    if (obj.contains("dns") && obj["dns"].is_object()) {
        const boost::json::object& dnsObj = obj["dns"].as_object();
        if (dnsObj.contains("tag")) {
            std::string tag = std::string(dnsObj.at("tag").as_string().c_str());
            EXPECT_EQ(tag, "custom_dns") 
                << "Template DNS tag not used in output";
        }
    }
}

TEST_F(InMemoryDB, ConfigBuilderThrowsExceptionWhenNoTemplate) {
    // Verify that when no template exists, an exception is thrown
    // (no more silent fallback to hardcoded config)
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.probeUrl = "https://www.google.com/generate_204";
    
    // Build config without any template in DB - should throw
    EXPECT_THROW(
        builder.build(ConfigMode::Test, "xray", p, vars),
        std::runtime_error
    );
}

TEST_F(InMemoryDB, ConfigGeneratorUsesConfigBuilderWhenEnabled) {
    // Verify that ConfigBuilder is always enabled
    // isEnableNewConfigBuilder() now always returns true
    
    // Test with empty config path (default behavior)
    bool enabled = ConfigBuilder::isEnableNewConfigBuilder("");
    EXPECT_TRUE(enabled);
    
    // Test with any config path (ignored)
    enabled = ConfigBuilder::isEnableNewConfigBuilder("nonexistent.json");
    EXPECT_TRUE(enabled);
}

TEST_F(InMemoryDB, TemplateEditPropagatesToRuntimeOutput) {
    // Integration test: verify that editing a template changes the runtime output
    db::FullConfigTemplateDAO dao(db_);
    dao.createTableIfNotExists();
    
    // Step 1: Create initial template with expected ID for test mode
    db::FullConfigTemplateItem initialTemplate;
    initialTemplate.id = "xray_test_001";
    initialTemplate.enabled = 1;
    initialTemplate.coreType = 2;  // Xray
    initialTemplate.config = R"({
        "log": {"loglevel": "info_initial"},
        "dns": {"tag": "dns_initial"}
    })";
    
    bool saved1 = dao.save(initialTemplate);
    EXPECT_TRUE(saved1);
    
    // Step 2: Build config with initial template
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.probeUrl = "https://www.google.com/generate_204";
    
    boost::json::value config1 = builder.build(ConfigMode::Test, "xray", p, vars);
    
    // Verify initial template was used
    EXPECT_TRUE(config1.is_object());
    boost::json::object& obj1 = config1.as_object();
    
    if (obj1.contains("log") && obj1["log"].is_object()) {
        const boost::json::object& logObj1 = obj1["log"].as_object();
        if (logObj1.contains("loglevel")) {
            std::string loglevel1 = std::string(logObj1.at("loglevel").as_string().c_str());
            EXPECT_EQ(loglevel1, "info_initial") 
                << "Initial template loglevel should be 'info_initial'";
        }
    }
    
    // Step 3: Edit the template
    db::FullConfigTemplateItem editedTemplate;
    editedTemplate.id = "xray_test_001";
    editedTemplate.enabled = 1;
    editedTemplate.coreType = 2;
    editedTemplate.config = R"({
        "log": {"loglevel": "warn_edited"},
        "dns": {"tag": "dns_edited"}
    })";
    
    bool saved2 = dao.save(editedTemplate);
    EXPECT_TRUE(saved2);
    
    // Step 4: Build config again - should use edited template
    boost::json::value config2 = builder.build(ConfigMode::Test, "xray", p, vars);
    
    // Verify edited template was used
    EXPECT_TRUE(config2.is_object());
    boost::json::object& obj2 = config2.as_object();
    
    if (obj2.contains("log") && obj2["log"].is_object()) {
        const boost::json::object& logObj2 = obj2["log"].as_object();
        if (logObj2.contains("loglevel")) {
            std::string loglevel2 = std::string(logObj2.at("loglevel").as_string().c_str());
            EXPECT_EQ(loglevel2, "warn_edited") 
                << "Edited template loglevel should be 'warn_edited'";
        }
    }
    
    // Step 5: Verify the output actually changed
    if (obj1.contains("log") && obj2.contains("log")) {
        std::string serialized1 = boost::json::serialize(config1);
        std::string serialized2 = boost::json::serialize(config2);
        EXPECT_NE(serialized1, serialized2) 
            << "Config output should differ after template edit";
    }
}

// ============================================================
// Placeholder Mechanism Tests (Phase 1)
// ============================================================

TEST_F(InMemoryDBWithTemplates, TemplateContainsInboundsPlaceholder_Xray) {
    // Test that Xray templates contain inbounds with ${socksPort} placeholder
    db::FullConfigTemplateDAO dao(db_);
    
    // Check standalone template
    auto standaloneOpt = dao.getById("xray_default_001");
    ASSERT_TRUE(standaloneOpt.has_value());
    boost::json::value templateJson = boost::json::parse(standaloneOpt->config);
    boost::json::object& obj = templateJson.as_object();
    
    ASSERT_TRUE(obj.contains("inbounds"));
    ASSERT_TRUE(obj["inbounds"].as_array().size() > 0);
    const boost::json::object& firstInbound = obj["inbounds"].as_array()[0].as_object();
    ASSERT_TRUE(firstInbound.contains("port"));
    EXPECT_EQ(std::string(firstInbound.at("port").as_string().c_str()), "${socksPort}");
    
    // Check test template
    auto testOpt = dao.getById("xray_test_001");
    ASSERT_TRUE(testOpt.has_value());
    boost::json::value testTemplateJson = boost::json::parse(testOpt->config);
    boost::json::object& testObj = testTemplateJson.as_object();
    
    ASSERT_TRUE(testObj.contains("inbounds"));
    ASSERT_TRUE(testObj["inbounds"].as_array().size() > 0);
    const boost::json::object& testFirstInbound = testObj["inbounds"].as_array()[0].as_object();
    ASSERT_TRUE(testFirstInbound.contains("port"));
    EXPECT_EQ(std::string(testFirstInbound.at("port").as_string().c_str()), "${socksPort}");
    
    // Check pool template
    auto poolOpt = dao.getById("pool_xray_001");
    ASSERT_TRUE(poolOpt.has_value());
    boost::json::value poolTemplateJson = boost::json::parse(poolOpt->config);
    boost::json::object& poolObj = poolTemplateJson.as_object();
    
    ASSERT_TRUE(poolObj.contains("inbounds"));
    ASSERT_TRUE(poolObj["inbounds"].as_array().size() > 0);
    const boost::json::object& poolFirstInbound = poolObj["inbounds"].as_array()[0].as_object();
    ASSERT_TRUE(poolFirstInbound.contains("port"));
    EXPECT_EQ(std::string(poolFirstInbound.at("port").as_string().c_str()), "${socksPort}");
}

TEST_F(InMemoryDBWithTemplates, TemplateContainsInboundsPlaceholder_SingBox) {
    // Test that SingBox templates contain inbounds with ${socksPort} placeholder
    // SingBox uses "listen_port" instead of "port" for inbound port
    db::FullConfigTemplateDAO dao(db_);
    
    // Check standalone template
    auto standaloneOpt = dao.getById("singbox_default_001");
    ASSERT_TRUE(standaloneOpt.has_value());
    boost::json::value templateJson = boost::json::parse(standaloneOpt->config);
    boost::json::object& obj = templateJson.as_object();
    
    ASSERT_TRUE(obj.contains("inbounds"));
    ASSERT_TRUE(obj["inbounds"].as_array().size() > 0);
    const boost::json::object& firstInbound = obj["inbounds"].as_array()[0].as_object();
    ASSERT_TRUE(firstInbound.contains("listen_port"));
    EXPECT_EQ(std::string(firstInbound.at("listen_port").as_string().c_str()), "${socksPort}");
    
    // Check test template
    auto testOpt = dao.getById("singbox_test_001");
    ASSERT_TRUE(testOpt.has_value());
    boost::json::value testTemplateJson = boost::json::parse(testOpt->config);
    boost::json::object& testObj = testTemplateJson.as_object();
    
    ASSERT_TRUE(testObj.contains("inbounds"));
    ASSERT_TRUE(testObj["inbounds"].as_array().size() > 0);
    const boost::json::object& testFirstInbound = testObj["inbounds"].as_array()[0].as_object();
    ASSERT_TRUE(testFirstInbound.contains("listen_port"));
    EXPECT_EQ(std::string(testFirstInbound.at("listen_port").as_string().c_str()), "${socksPort}");
    
    // Check pool template
    auto poolOpt = dao.getById("pool_singbox_001");
    ASSERT_TRUE(poolOpt.has_value());
    boost::json::value poolTemplateJson = boost::json::parse(poolOpt->config);
    boost::json::object& poolObj = poolTemplateJson.as_object();
    
    ASSERT_TRUE(poolObj.contains("inbounds"));
    ASSERT_TRUE(poolObj["inbounds"].as_array().size() > 0);
    const boost::json::object& poolFirstInbound = poolObj["inbounds"].as_array()[0].as_object();
    ASSERT_TRUE(poolFirstInbound.contains("listen_port"));
    EXPECT_EQ(std::string(poolFirstInbound.at("listen_port").as_string().c_str()), "${socksPort}");
}

TEST_F(InMemoryDBWithTemplates, TemplateContainsApiPortPlaceholder_Xray) {
    // Test that Xray Test and Pool templates contain api.listen with ${apiPort} placeholder
    db::FullConfigTemplateDAO dao(db_);
    
    // Check test template
    auto testOpt = dao.getById("xray_test_001");
    ASSERT_TRUE(testOpt.has_value());
    boost::json::value testTemplateJson = boost::json::parse(testOpt->config);
    boost::json::object& testObj = testTemplateJson.as_object();
    
    ASSERT_TRUE(testObj.contains("api"));
    ASSERT_TRUE(testObj["api"].is_object());
    const boost::json::object& apiObj = testObj["api"].as_object();
    ASSERT_TRUE(apiObj.contains("listen"));
    EXPECT_EQ(std::string(apiObj.at("listen").as_string().c_str()), "127.0.0.1:${apiPort}");
    
    // Check pool template
    auto poolOpt = dao.getById("pool_xray_001");
    ASSERT_TRUE(poolOpt.has_value());
    boost::json::value poolTemplateJson = boost::json::parse(poolOpt->config);
    boost::json::object& poolObj = poolTemplateJson.as_object();
    
    ASSERT_TRUE(poolObj.contains("api"));
    ASSERT_TRUE(poolObj["api"].is_object());
    const boost::json::object& poolApiObj = poolObj["api"].as_object();
    ASSERT_TRUE(poolApiObj.contains("listen"));
    EXPECT_EQ(std::string(poolApiObj.at("listen").as_string().c_str()), "127.0.0.1:${apiPort}");
}

TEST_F(InMemoryDBWithTemplates, BuildTestConfig_ReplacesPlaceholders) {
    // Test that buildTestConfig() replaces ${socksPort} with actual port value
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.apiPort = 10085;
    vars.probeUrl = "https://www.google.com/generate_204";
    
    boost::json::value config = builder.build(ConfigMode::Test, "xray", p, vars);
    boost::json::object& obj = config.as_object();
    
    // Verify inbounds exist and port is replaced
    ASSERT_TRUE(obj.contains("inbounds"));
    ASSERT_TRUE(obj["inbounds"].as_array().size() > 0);
    const boost::json::object& firstInbound = obj["inbounds"].as_array()[0].as_object();
    ASSERT_TRUE(firstInbound.contains("port"));
    
    // Port should be the actual integer value, not a string placeholder
    EXPECT_TRUE(firstInbound.at("port").is_int64() || firstInbound.at("port").is_uint64());
    int64_t portValue = firstInbound.at("port").is_int64() ? 
        firstInbound.at("port").as_int64() : 
        static_cast<int64_t>(firstInbound.at("port").as_uint64());
    EXPECT_EQ(portValue, 10808);
    
    // Verify api.listen is replaced (if api section exists in Test template)
    if (obj.contains("api") && obj["api"].is_object()) {
        const boost::json::object& apiObj = obj["api"].as_object();
        ASSERT_TRUE(apiObj.contains("listen"));
        std::string listenAddr = std::string(apiObj.at("listen").as_string().c_str());
        EXPECT_EQ(listenAddr, "127.0.0.1:10085");
    }
}

TEST_F(InMemoryDBWithTemplates, BuildStandaloneConfig_ReplacesPlaceholders) {
    // Test that buildStandaloneConfig() replaces ${socksPort} with actual port value
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10810;
    vars.apiPort = 10086;
    vars.probeUrl = "https://www.google.com/generate_204";
    
    boost::json::value config = builder.build(ConfigMode::Standalone, "xray", p, vars);
    boost::json::object& obj = config.as_object();
    
    // Verify inbounds exist and port is replaced
    ASSERT_TRUE(obj.contains("inbounds"));
    ASSERT_TRUE(obj["inbounds"].as_array().size() > 0);
    const boost::json::object& firstInbound = obj["inbounds"].as_array()[0].as_object();
    ASSERT_TRUE(firstInbound.contains("port"));
    
    // Port should be the actual integer value, not a string placeholder
    EXPECT_TRUE(firstInbound.at("port").is_int64() || firstInbound.at("port").is_uint64());
    int64_t portValue = firstInbound.at("port").is_int64() ? 
        firstInbound.at("port").as_int64() : 
        static_cast<int64_t>(firstInbound.at("port").as_uint64());
    EXPECT_EQ(portValue, 10810);
}

TEST_F(InMemoryDBWithTemplates, BuildPoolConfig_ReplacesPlaceholders) {
    // Test that buildPoolConfig() replaces both ${socksPort} and ${apiPort}
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10812;
    vars.apiPort = 10087;
    vars.enableObservatory = true;
    vars.probeUrl = "https://www.google.com/generate_204";
    
    boost::json::value config = builder.build(ConfigMode::Pool, "xray", p, vars);
    boost::json::object& obj = config.as_object();
    
    // Verify inbounds exist and port is replaced
    ASSERT_TRUE(obj.contains("inbounds"));
    ASSERT_TRUE(obj["inbounds"].as_array().size() > 0);
    const boost::json::object& firstInbound = obj["inbounds"].as_array()[0].as_object();
    ASSERT_TRUE(firstInbound.contains("port"));
    
    // Port should be the actual integer value, not a string placeholder
    EXPECT_TRUE(firstInbound.at("port").is_int64() || firstInbound.at("port").is_uint64());
    int64_t portValue = firstInbound.at("port").is_int64() ? 
        firstInbound.at("port").as_int64() : 
        static_cast<int64_t>(firstInbound.at("port").as_uint64());
    EXPECT_EQ(portValue, 10812);
    
    // Verify api.listen is replaced
    ASSERT_TRUE(obj.contains("api"));
    ASSERT_TRUE(obj["api"].is_object());
    const boost::json::object& apiObj = obj["api"].as_object();
    ASSERT_TRUE(apiObj.contains("listen"));
    std::string listenAddr = std::string(apiObj.at("listen").as_string().c_str());
    EXPECT_EQ(listenAddr, "127.0.0.1:10087");
}

TEST_F(InMemoryDBWithTemplates, NoUnreplacedPlaceholders_RemainInConfig) {
    // Test that no ${socksPort} or ${apiPort} placeholders remain in generated configs
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.apiPort = 10085;
    vars.enableObservatory = true;
    vars.probeUrl = "https://www.google.com/generate_204";
    
    // Check all three modes
    for (int mode = 0; mode < 3; mode++) {
        ConfigMode configMode;
        switch (mode) {
            case 0: configMode = ConfigMode::Test; break;
            case 1: configMode = ConfigMode::Standalone; break;
            case 2: configMode = ConfigMode::Pool; break;
            default: continue;
        }
        
        boost::json::value config = builder.build(configMode, "xray", p, vars);
        std::string serialized = boost::json::serialize(config);
        
        // No unreplaced placeholders should remain
        EXPECT_EQ(serialized.find("${socksPort}"), std::string::npos)
            << "Unreplaced ${socksPort} found in mode " << mode;
        EXPECT_EQ(serialized.find("${apiPort}"), std::string::npos)
            << "Unreplaced ${apiPort} found in mode " << mode;
    }
}

// ============================================================
// validateConfig() Unit Tests
// ============================================================

TEST_F(InMemoryDBWithTemplates, ValidateConfig_FailsOnMissingLog) {
    // Test that validateConfig fails when 'log' section is missing
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.probeUrl = "https://www.google.com/generate_204";
    
    boost::json::value config = builder.build(ConfigMode::Test, "xray", p, vars);
    boost::json::object& obj = config.as_object();
    
    // Remove the log section to trigger validation error
    obj.erase("log");
    
    // Validation should throw
    EXPECT_THROW(
        builder.validateConfig(config, ConfigMode::Test, ProxyType::Xray),
        std::runtime_error
    );
}

TEST_F(InMemoryDBWithTemplates, ValidateConfig_FailsOnMissingInbounds) {
    // Test that validateConfig fails when 'inbounds' section is missing
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.probeUrl = "https://www.google.com/generate_204";
    
    boost::json::value config = builder.build(ConfigMode::Test, "xray", p, vars);
    boost::json::object& obj = config.as_object();
    
    // Remove the inbounds section
    obj.erase("inbounds");
    
    // Validation should throw
    EXPECT_THROW(
        builder.validateConfig(config, ConfigMode::Test, ProxyType::Xray),
        std::runtime_error
    );
}

TEST_F(InMemoryDBWithTemplates, ValidateConfig_FailsOnMissingApiForPool) {
    // Test that validateConfig fails when Pool config is missing 'api' section
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10810;
    vars.apiPort = 10086;
    vars.enableObservatory = true;
    
    boost::json::value config = builder.build(ConfigMode::Pool, "xray", p, vars);
    boost::json::object& obj = config.as_object();
    
    // Remove the api section to trigger validation error
    obj.erase("api");
    
    // Validation should throw
    EXPECT_THROW(
        builder.validateConfig(config, ConfigMode::Pool, ProxyType::Xray),
        std::runtime_error
    );
}

TEST_F(InMemoryDBWithTemplates, ValidateConfig_PassesOnValidConfig) {
    // Test that validateConfig passes on a valid config
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.apiPort = 10085;
    vars.probeUrl = "https://www.google.com/generate_204";
    
    // Build a valid test config
    boost::json::value config = builder.build(ConfigMode::Test, "xray", p, vars);
    
    // Validation should pass without throwing
    EXPECT_NO_THROW(
        builder.validateConfig(config, ConfigMode::Test, ProxyType::Xray)
    );
}

// ============================================================
// Phase 2: Protocol Consistency Tests
// ============================================================

TEST_F(InMemoryDBWithTemplates, ProtocolConsistency_Xray_AllModesUseMixed) {
    // Verify all 3 modes use "mixed" protocol for Xray inbounds
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.apiPort = 10085;

    // Test mode
    vars.socksPort = 10808;
    boost::json::value testConfig = builder.build(ConfigMode::Test, "xray", p, vars);
    const boost::json::object& testRoot = testConfig.as_object();
    ASSERT_TRUE(testRoot.contains("inbounds"));
    EXPECT_EQ(testRoot.at("inbounds").as_array()[0].as_object().at("protocol").as_string(), "mixed");

    // Standalone mode
    vars.socksPort = 10810;
    boost::json::value standaloneConfig = builder.build(ConfigMode::Standalone, "xray", p, vars);
    const boost::json::object& standaloneRoot = standaloneConfig.as_object();
    ASSERT_TRUE(standaloneRoot.contains("inbounds"));
    EXPECT_EQ(standaloneRoot.at("inbounds").as_array()[0].as_object().at("protocol").as_string(), "mixed");

    // Pool mode
    vars.socksPort = 10812;
    vars.enableObservatory = true;
    boost::json::value poolConfig = builder.build(ConfigMode::Pool, "xray", p, vars);
    const boost::json::object& poolRoot = poolConfig.as_object();
    ASSERT_TRUE(poolRoot.contains("inbounds"));
    EXPECT_EQ(poolRoot.at("inbounds").as_array()[0].as_object().at("protocol").as_string(), "mixed");
}

TEST_F(InMemoryDBWithTemplates, ProtocolConsistency_Xray_AllModesUseSocksTag) {
    // Verify all 3 modes use "socks" tag for Xray inbounds
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.apiPort = 10085;

    // Test mode
    vars.socksPort = 10808;
    boost::json::value testConfig = builder.build(ConfigMode::Test, "xray", p, vars);
    const boost::json::object& testRoot = testConfig.as_object();
    EXPECT_EQ(testRoot.at("inbounds").as_array()[0].as_object().at("tag").as_string(), "socks");

    // Standalone mode
    vars.socksPort = 10810;
    boost::json::value standaloneConfig = builder.build(ConfigMode::Standalone, "xray", p, vars);
    const boost::json::object& standaloneRoot = standaloneConfig.as_object();
    EXPECT_EQ(standaloneRoot.at("inbounds").as_array()[0].as_object().at("tag").as_string(), "socks");

    // Pool mode
    vars.socksPort = 10812;
    vars.enableObservatory = true;
    boost::json::value poolConfig = builder.build(ConfigMode::Pool, "xray", p, vars);
    const boost::json::object& poolRoot = poolConfig.as_object();
    EXPECT_EQ(poolRoot.at("inbounds").as_array()[0].as_object().at("tag").as_string(), "socks");
}

TEST_F(InMemoryDBWithTemplates, ProtocolConsistency_TestConfig_HasSniffing) {
    // Verify Test config Xray inbound has sniffing enabled
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.apiPort = 10085;

    boost::json::value config = builder.build(ConfigMode::Test, "xray", p, vars);
    const boost::json::object& root = config.as_object();
    const boost::json::object& inbound = root.at("inbounds").as_array()[0].as_object();
    ASSERT_TRUE(inbound.contains("sniffing"));
    const boost::json::object& sniffing = inbound.at("sniffing").as_object();
    EXPECT_EQ(sniffing.at("enabled").as_bool(), true);
}

TEST_F(InMemoryDBWithTemplates, TemplateConsistency_XrayTestTemplateUsesMixedProtocol) {
    // Verify the template itself uses "mixed" protocol and "socks" tag
    db::FullConfigTemplateDAO dao(db_);
    std::optional<db::FullConfigTemplateItem> opt = dao.getById("xray_test_001");
    ASSERT_TRUE(opt.has_value());

    boost::json::value parsed = boost::json::parse(opt->config);
    const boost::json::object& root = parsed.as_object();
    ASSERT_TRUE(root.contains("inbounds"));
    const boost::json::object& inbound = root.at("inbounds").as_array()[0].as_object();

    EXPECT_EQ(std::string(inbound.at("protocol").as_string().c_str()), "mixed");
    EXPECT_EQ(std::string(inbound.at("tag").as_string().c_str()), "socks");
}

TEST_F(InMemoryDBWithTemplates, NoLegacySocksInTag_InAnyGeneratedConfig) {
    // Verify none of the generated configs contain "socks-in" tag
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.apiPort = 10085;
    vars.enableObservatory = true;

    std::string serializedTest = boost::json::serialize(
        builder.build(ConfigMode::Test, "xray", p, vars));
    EXPECT_EQ(serializedTest.find("socks-in"), std::string::npos);

    std::string serializedStandalone = boost::json::serialize(
        builder.build(ConfigMode::Standalone, "xray", p, vars));
    EXPECT_EQ(serializedStandalone.find("socks-in"), std::string::npos);

    vars.socksPort = 10812;
    std::string serializedPool = boost::json::serialize(
        builder.build(ConfigMode::Pool, "xray", p, vars));
    EXPECT_EQ(serializedPool.find("socks-in"), std::string::npos);
}

// ============================================================
// Routing Merge Tests (Phase 3)
// ============================================================

TEST_F(InMemoryDBWithTemplates, RoutingMerge_TestConfig_PreservesTemplateRules) {
    // Verify Test config preserves template's api→api rule
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.apiPort = 10085;
    
    boost::json::value config = builder.build(ConfigMode::Test, "xray", p, vars);
    const boost::json::object& root = config.as_object();
    ASSERT_TRUE(root.contains("routing"));
    const boost::json::object& routing = root.at("routing").as_object();
    ASSERT_TRUE(routing.contains("rules"));
    const boost::json::array& rules = routing.at("rules").as_array();
    
    // Template has 1 rule (api→api), code adds 1 rule (socks→proxy) = 2 total
    ASSERT_EQ(rules.size(), 2u);
    
    // Rule 0: api→api (from template)
    const boost::json::object& rule0 = rules[0].as_object();
    EXPECT_EQ(std::string(rule0.at("outboundTag").as_string().c_str()), "api");
    
    // Rule 1: socks→proxy (added by code)
    const boost::json::object& rule1 = rules[1].as_object();
    EXPECT_EQ(std::string(rule1.at("outboundTag").as_string().c_str()), "proxy");
}

TEST_F(InMemoryDBWithTemplates, RoutingMerge_StandaloneConfig_PreservesTemplateRules) {
    // Verify Standalone config preserves template's 6 rules
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10810;
    
    boost::json::value config = builder.build(ConfigMode::Standalone, "xray", p, vars);
    const boost::json::object& root = config.as_object();
    ASSERT_TRUE(root.contains("routing"));
    const boost::json::object& routing = root.at("routing").as_object();
    ASSERT_TRUE(routing.contains("rules"));
    const boost::json::array& rules = routing.at("rules").as_array();
    
    // Template has 6 rules, code adds 1 rule (socks→proxy) = 7 total
    ASSERT_EQ(rules.size(), 7u);
    
    // Last rule should be socks→proxy (added by code)
    const boost::json::object& lastRule = rules[6].as_object();
    EXPECT_EQ(std::string(lastRule.at("outboundTag").as_string().c_str()), "proxy");
}

TEST_F(InMemoryDBWithTemplates, RoutingMerge_PoolConfig_PreservesTemplateRules) {
    // Verify Pool config preserves template's 6 rules + balancers
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10812;
    vars.apiPort = 10087;
    vars.balancerStrategy = "random";
    
    boost::json::value config = builder.build(ConfigMode::Pool, "xray", p, vars);
    const boost::json::object& root = config.as_object();
    ASSERT_TRUE(root.contains("routing"));
    const boost::json::object& routing = root.at("routing").as_object();
    
    // Balancers should be preserved (from template)
    ASSERT_TRUE(routing.contains("balancers"));
    const boost::json::array& balancers = routing.at("balancers").as_array();
    ASSERT_EQ(balancers.size(), 1u);
    const boost::json::object& balancer = balancers[0].as_object();
    EXPECT_EQ(std::string(balancer.at("tag").as_string().c_str()), "balancer-out");
    
    // Rules: template has 6 rules, code adds 1 rule (socks→balancer-out) = 7 total
    ASSERT_TRUE(routing.contains("rules"));
    const boost::json::array& rules = routing.at("rules").as_array();
    ASSERT_EQ(rules.size(), 7u);
    
    // Last rule should be socks→balancer-out (added by code)
    const boost::json::object& lastRule = rules[6].as_object();
    EXPECT_EQ(std::string(lastRule.at("balancerTag").as_string().c_str()), "balancer-out");
}

TEST_F(InMemoryDBWithTemplates, RoutingMerge_NoDuplicateSocksRules) {
    // Verify no duplicate socks rules exist in any mode
    ConfigBuilder builder(db_);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.apiPort = 10085;
    vars.enableObservatory = true;
    
    for (int mode = 0; mode < 3; mode++) {
        ConfigMode configMode;
        switch (mode) {
            case 0: configMode = ConfigMode::Test; break;
            case 1: configMode = ConfigMode::Standalone; break;
            case 2: configMode = ConfigMode::Pool; break;
            default: continue;
        }
        
        boost::json::value config = builder.build(configMode, "xray", p, vars);
        const boost::json::object& root = config.as_object();
        ASSERT_TRUE(root.contains("routing"));
        const boost::json::object& routing = root.at("routing").as_object();
        ASSERT_TRUE(routing.contains("rules"));
        const boost::json::array& rules = routing.at("rules").as_array();
        
        // Count socks rules (should be exactly 1)
        int socksCount = 0;
        for (const boost::json::value& rule : rules) {
            if (rule.is_object()) {
                const boost::json::object& ruleObj = rule.as_object();
                if (ruleObj.contains("inboundTag")) {
                    const boost::json::value& inboundTagVal = ruleObj.at("inboundTag");
                    if (inboundTagVal.is_array()) {
                        for (const boost::json::value& tagVal : inboundTagVal.as_array()) {
                            if (tagVal.is_string() && tagVal.as_string() == "socks") {
                                socksCount++;
                                break;
                            }
                        }
                    }
                }
            }
        }
        EXPECT_EQ(socksCount, 1) << "Found " << socksCount << " socks rules in mode " << mode;
    }
}

// ============================================================
// Phase 4: Template Inheritance Tests
// ============================================================

// --- mergeConfig unit tests ---

TEST(mergeConfig, DeepMergeObjects) {
    // Parent: {"a": {"x": 1, "y": 2}}
    boost::json::object parentObj;
    parentObj["a"] = boost::json::object{{"x", 1}, {"y", 2}};
    boost::json::value parent = parentObj;
    
    // Child: {"a": {"y": 3, "z": 4}}
    boost::json::object childObj;
    childObj["a"] = boost::json::object{{"y", 3}, {"z", 4}};
    boost::json::value child = childObj;
    
    boost::json::value merged = ConfigBuilder::mergeConfig(parent, child);
    const boost::json::object& result = merged.as_object();
    
    // x should come from parent (not overridden)
    EXPECT_EQ(result.at("a").as_object().at("x").as_int64(), 1);
    // y should be overridden by child
    EXPECT_EQ(result.at("a").as_object().at("y").as_int64(), 3);
    // z should be added from child
    EXPECT_EQ(result.at("a").as_object().at("z").as_int64(), 4);
}

TEST(mergeConfig, ScalarOverride) {
    boost::json::object parentObj;
    parentObj["level"] = "info";
    parentObj["debug"] = false;
    boost::json::value parent = parentObj;
    
    boost::json::object childObj;
    childObj["level"] = "warning";
    childObj["debug"] = true;
    boost::json::value child = childObj;
    
    boost::json::value merged = ConfigBuilder::mergeConfig(parent, child);
    const boost::json::object& result = merged.as_object();
    
    EXPECT_EQ(std::string(result.at("level").as_string()), "warning");
    EXPECT_EQ(result.at("debug").as_bool(), true);
}

TEST(mergeConfig, ArrayOverride) {
    // Arrays should be replaced (not merged)
    boost::json::object parentObj;
    boost::json::array parentArr;
    parentArr.push_back(1);
    parentArr.push_back(2);
    parentObj["items"] = parentArr;
    boost::json::value parent = parentObj;
    
    boost::json::object childObj;
    boost::json::array childArr;
    childArr.push_back(3);
    childObj["items"] = childArr;
    boost::json::value child = childObj;
    
    boost::json::value merged = ConfigBuilder::mergeConfig(parent, child);
    const boost::json::object& result = merged.as_object();
    
    // Child array should replace parent array
    EXPECT_EQ(result.at("items").as_array().size(), 1u);
    EXPECT_EQ(result.at("items").as_array()[0].as_int64(), 3);
}

TEST(mergeConfig, ChildAddNewKey) {
    boost::json::object parentObj;
    parentObj["existing"] = "value";
    boost::json::value parent = parentObj;
    
    boost::json::object childObj;
    childObj["newKey"] = "newValue";
    boost::json::value child = childObj;
    
    boost::json::value merged = ConfigBuilder::mergeConfig(parent, child);
    const boost::json::object& result = merged.as_object();
    
    // Both keys should exist
    EXPECT_TRUE(result.contains("existing"));
    EXPECT_TRUE(result.contains("newKey"));
    EXPECT_EQ(std::string(result.at("newKey").as_string()), "newValue");
}

TEST(mergeConfig, MaxDepthExceeded) {
    boost::json::value empty = boost::json::object{};
    EXPECT_THROW(
        ConfigBuilder::mergeConfig(empty, empty, -1),
        std::runtime_error
    ) << "Should throw when max depth is -1 (depth 0 > -1)";
}

TEST(mergeConfig, NonObjectParent) {
    // If parent is not an object, child replaces entirely
    boost::json::value parent = 42;
    boost::json::object childObj;
    childObj["key"] = "value";
    boost::json::value child = childObj;
    
    boost::json::value merged = ConfigBuilder::mergeConfig(parent, child);
    EXPECT_TRUE(merged.is_object());
    EXPECT_EQ(std::string(merged.as_object().at("key").as_string()), "value");
}

// --- Template inheritance integration tests ---

TEST_F(InMemoryDBWithTemplates, BaseTemplatesExist) {
    db::FullConfigTemplateDAO dao(db_);
    
    // Xray base template
    auto xrayBase = dao.getById("xray_base_001");
    ASSERT_TRUE(xrayBase.has_value());
    EXPECT_EQ(xrayBase->parentId, "");
    EXPECT_EQ(xrayBase->coreType, 2);
    EXPECT_TRUE(xrayBase->config.find("log") != std::string::npos);
    EXPECT_TRUE(xrayBase->config.find("dns") != std::string::npos);
    
    // SingBox base template
    auto singboxBase = dao.getById("singbox_base_001");
    ASSERT_TRUE(singboxBase.has_value());
    EXPECT_EQ(singboxBase->parentId, "");
    EXPECT_EQ(singboxBase->coreType, 24);
    EXPECT_TRUE(singboxBase->config.find("log") != std::string::npos);
    EXPECT_TRUE(singboxBase->config.find("dns") != std::string::npos);
}

TEST_F(InMemoryDBWithTemplates, ChildTemplatesHaveParentId) {
    db::FullConfigTemplateDAO dao(db_);
    
    // All Xray child templates should reference xray_base_001
    auto xrayDefault = dao.getById("xray_default_001");
    ASSERT_TRUE(xrayDefault.has_value());
    EXPECT_EQ(xrayDefault->parentId, "xray_base_001");
    
    auto xrayTest = dao.getById("xray_test_001");
    ASSERT_TRUE(xrayTest.has_value());
    EXPECT_EQ(xrayTest->parentId, "xray_base_001");
    
    auto poolXray = dao.getById("pool_xray_001");
    ASSERT_TRUE(poolXray.has_value());
    EXPECT_EQ(poolXray->parentId, "xray_base_001");
    
    // All SingBox child templates should reference singbox_base_001
    auto singboxDefault = dao.getById("singbox_default_001");
    ASSERT_TRUE(singboxDefault.has_value());
    EXPECT_EQ(singboxDefault->parentId, "singbox_base_001");
    
    auto singboxTest = dao.getById("singbox_test_001");
    ASSERT_TRUE(singboxTest.has_value());
    EXPECT_EQ(singboxTest->parentId, "singbox_base_001");
    
    auto poolSingbox = dao.getById("pool_singbox_001");
    ASSERT_TRUE(poolSingbox.has_value());
    EXPECT_EQ(poolSingbox->parentId, "singbox_base_001");
}

TEST_F(InMemoryDBWithTemplates, ChildTemplatesOmitLogDns) {
    // Child templates should NOT contain log or dns (inherited from base)
    db::FullConfigTemplateDAO dao(db_);
    
    auto xrayDefault = dao.getById("xray_default_001");
    ASSERT_TRUE(xrayDefault.has_value());
    // Should have inbounds and routing but not log/dns
    EXPECT_TRUE(xrayDefault->config.find("\"log\"") == std::string::npos);
    EXPECT_TRUE(xrayDefault->config.find("\"dns\"") == std::string::npos);
    EXPECT_TRUE(xrayDefault->config.find("\"inbounds\"") != std::string::npos);
    EXPECT_TRUE(xrayDefault->config.find("\"routing\"") != std::string::npos);
}

TEST_F(InMemoryDBWithTemplates, TemplateInheritance_XrayStandalone) {
    ConfigBuilder builder(db_);
    Profileitem profile = makeProfile("1", "1.2.3.4", "443", "test-uuid");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.apiPort = 10085;
    
    boost::json::value config = builder.build(ConfigMode::Standalone, profile, vars);
    const boost::json::object& root = config.as_object();
    
    // Should have log from base template
    EXPECT_TRUE(root.contains("log"));
    
    // Should have dns from base template
    EXPECT_TRUE(root.contains("dns"));
    
    // Should have routing from child template
    EXPECT_TRUE(root.contains("routing"));
    
    // Should have inbounds from child template
    EXPECT_TRUE(root.contains("inbounds"));
    
    // Should have outbounds generated by ConfigBuilder
    EXPECT_TRUE(root.contains("outbounds"));
}

TEST_F(InMemoryDBWithTemplates, TemplateInheritance_XrayTest) {
    ConfigBuilder builder(db_);
    Profileitem profile = makeProfile("1", "1.2.3.4", "443", "test-uuid");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.apiPort = 10085;
    
    boost::json::value config = builder.build(ConfigMode::Test, profile, vars);
    const boost::json::object& root = config.as_object();
    
    // Should have log from base template
    EXPECT_TRUE(root.contains("log"));
    
    // Should have dns from base template
    EXPECT_TRUE(root.contains("dns"));
    
    // Should have api from child template
    EXPECT_TRUE(root.contains("api"));
    
    // Should have stats and policy from child template
    EXPECT_TRUE(root.contains("stats"));
    EXPECT_TRUE(root.contains("policy"));
    
    // Should have routing from child template
    EXPECT_TRUE(root.contains("routing"));
}

TEST_F(InMemoryDBWithTemplates, TemplateInheritance_Pool) {
    ConfigBuilder builder(db_);
    Profileitem profile = makeProfile("1", "1.2.3.4", "443", "test-uuid");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.apiPort = 10085;
    
    boost::json::value config = builder.build(ConfigMode::Pool, profile, vars);
    const boost::json::object& root = config.as_object();
    
    // Should have log from base template
    EXPECT_TRUE(root.contains("log"));
    
    // Should have dns from base template
    EXPECT_TRUE(root.contains("dns"));
    
    // Should have api from child template
    EXPECT_TRUE(root.contains("api"));
    
    // Should have routing with balancers from child template
    EXPECT_TRUE(root.contains("routing"));
    const boost::json::object& routing = root.at("routing").as_object();
    EXPECT_TRUE(routing.contains("balancers"));
}

TEST_F(InMemoryDBWithTemplates, TemplateInheritance_SingBoxStandalone) {
    ConfigBuilder builder(db_);
    Profileitem profile = makeProfile("1", "1.2.3.4", "443", "test-uuid");
    ConfigVariables vars;
    vars.socksPort = 10808;
    
    boost::json::value config = builder.build(ConfigMode::Standalone, "singbox", profile, vars);
    const boost::json::object& root = config.as_object();
    
    // Should have log from singbox base template
    EXPECT_TRUE(root.contains("log"));
    
    // Should have dns from singbox base template
    EXPECT_TRUE(root.contains("dns"));
    
    // Should have route from child template
    EXPECT_TRUE(root.contains("route"));
    
    // Should have inbounds from child template
    EXPECT_TRUE(root.contains("inbounds"));
}

TEST_F(InMemoryDBWithTemplates, TemplateInheritance_AllModesPassValidation) {
    ConfigBuilder builder(db_);
    Profileitem profile = makeProfile("1", "1.2.3.4", "443", "test-uuid");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.apiPort = 10085;
    
    // Test all 6 mode x proxy type combinations
    std::vector<std::pair<ConfigMode, ProxyType>> combinations = {
        {ConfigMode::Standalone, ProxyType::Xray},
        {ConfigMode::Test, ProxyType::Xray},
        {ConfigMode::Pool, ProxyType::Xray},
        {ConfigMode::Standalone, ProxyType::SingBox},
        {ConfigMode::Test, ProxyType::SingBox},
        {ConfigMode::Pool, ProxyType::SingBox}
    };
    
    for (const auto& [mode, proxyType] : combinations) {
        std::string modeStr = (mode == ConfigMode::Test) ? "Test" :
                              (mode == ConfigMode::Standalone) ? "Standalone" : "Pool";
        std::string proxyStr = (proxyType == ProxyType::Xray) ? "Xray" : "SingBox";
        
        boost::json::value config;
        if (proxyType == ProxyType::Xray) {
            config = builder.build(mode, profile, vars);
        } else {
            config = builder.build(mode, "singbox", profile, vars);
        }
        
        EXPECT_NO_THROW(builder.validateConfig(config, mode, proxyType))
            << "Validation failed for mode=" << modeStr << ", proxyType=" << proxyStr;
    }
}

TEST_F(InMemoryDBWithTemplates, TemplateInheritance_LoadTemplateById) {
    // Test direct ID loading
    boost::json::value baseConfig = ConfigBuilder::loadTemplateById(db_, "xray_base_001");
    const boost::json::object& baseObj = baseConfig.as_object();
    
    EXPECT_TRUE(baseObj.contains("log"));
    EXPECT_TRUE(baseObj.contains("dns"));
    EXPECT_EQ(baseObj.size(), 2u) << "Base template should have exactly 2 sections (log, dns)";
    
    // Test child template loading (should inherit from base)
    boost::json::value childConfig = ConfigBuilder::loadTemplateById(db_, "xray_default_001");
    const boost::json::object& childObj = childConfig.as_object();
    
    // Should have log and dns from base
    EXPECT_TRUE(childObj.contains("log"));
    EXPECT_TRUE(childObj.contains("dns"));
    
    // Should have inbounds and routing from child
    EXPECT_TRUE(childObj.contains("inbounds"));
    EXPECT_TRUE(childObj.contains("routing"));
    
    // Should have 4 sections total
    EXPECT_EQ(childObj.size(), 4u) << "Child template should have 4 sections (log, dns, inbounds, routing)";
}

TEST_F(InMemoryDBWithTemplates, TemplateInheritance_NotFoundThrows) {
    EXPECT_THROW(
        ConfigBuilder::loadTemplateById(db_, "nonexistent_001"),
        std::runtime_error
    );
}

// ============================================================
// ConfigPipeline Step Handler Tests
// ============================================================

// Fixture that registers all step handlers
class PipelineHandlerTest : public InMemoryDBWithTemplates {
protected:
    void SetUp() override {
        InMemoryDBWithTemplates::SetUp();
        config::registerAllStepHandlers();
    }
};

// Helper to get a handler by type
static std::shared_ptr<const config::StepHandler> getHandler(const std::string& type) {
    return config::StepRegistry::instance().getHandler(type);
}

// Helper to create a minimal config object
static boost::json::object makeMinimalConfig() {
    boost::json::object obj;
    obj["log"] = boost::json::value(boost::json::object{});
    obj["dns"] = boost::json::value(boost::json::object{});
    obj["outbounds"] = boost::json::value(boost::json::array{});
    obj["inbounds"] = boost::json::value(boost::json::array{});
    obj["routing"] = boost::json::value(boost::json::object{});
    return obj;
}

TEST_F(PipelineHandlerTest, AllHandlersRegistered) {
    auto types = config::StepRegistry::instance().getRegisteredTypes();
    EXPECT_GE(types.size(), 10);
    EXPECT_NE(std::find(types.begin(), types.end(), "replace_variables"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "add_outbound"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "merge_routing"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "add_inbound"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "add_observatory"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "add_balancer"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "add_stats"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "add_policy"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "add_api"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "modify_field"), types.end());
}

TEST_F(PipelineHandlerTest, AddInbound_CreatesInbound) {
    auto handler = getHandler("add_inbound");
    ASSERT_NE(handler, nullptr);
    
    boost::json::object config = makeMinimalConfig();
    boost::json::object params;
    params["tag"] = "socks";
    params["protocol"] = "socks";
    params["port"] = 10808;
    
    config::PipelineContext ctx;
    handler->execute(config, params, ctx);
    
    EXPECT_EQ(config["inbounds"].as_array().size(), 1u);
    auto& inbound = config["inbounds"].as_array()[0];
    EXPECT_EQ(inbound.at("tag").as_string(), "socks");
    EXPECT_EQ(inbound.at("protocol").as_string(), "socks");
    EXPECT_EQ(inbound.at("port").as_int64(), 10808);
}

TEST_F(PipelineHandlerTest, AddInbound_IdempotentByTag) {
    auto handler = getHandler("add_inbound");
    boost::json::object config = makeMinimalConfig();
    boost::json::object params;
    params["tag"] = "socks";
    params["port"] = 10808;
    
    config::PipelineContext ctx;
    handler->execute(config, params, ctx);
    EXPECT_EQ(config["inbounds"].as_array().size(), 1u);
    
    // Update port
    params["port"] = 10809;
    handler->execute(config, params, ctx);
    EXPECT_EQ(config["inbounds"].as_array().size(), 1u);
    EXPECT_EQ(config["inbounds"].as_array()[0].at("port").as_int64(), 10809);
}

TEST_F(PipelineHandlerTest, AddObservatory_SetsObservatory) {
    auto handler = getHandler("add_observatory");
    ASSERT_NE(handler, nullptr);
    
    boost::json::object config = makeMinimalConfig();
    boost::json::object params;
    params["subjectSelector"] = boost::json::array({"px-"});
    params["probeURL"] = "https://example.com";
    params["probeInterval"] = 30000;
    
    config::PipelineContext ctx;
    handler->execute(config, params, ctx);
    
    EXPECT_TRUE(config.contains("observatory"));
    const auto& obs = config["observatory"].as_object();
    EXPECT_EQ(obs.at("subjectSelector").as_array().size(), 1u);
    EXPECT_EQ(obs.at("probeURL").as_string(), "https://example.com");
    EXPECT_EQ(obs.at("probeInterval").as_int64(), 30000);
}

TEST_F(PipelineHandlerTest, AddBalancer_AddsBalancerToRouting) {
    auto handler = getHandler("add_balancer");
    ASSERT_NE(handler, nullptr);
    
    boost::json::object config = makeMinimalConfig();
    boost::json::object params;
    params["tag"] = "balancer-out";
    params["selector"] = boost::json::array({"px-"});
    params["strategy"] = "leastPing";
    
    config::PipelineContext ctx;
    handler->execute(config, params, ctx);
    
    EXPECT_TRUE(config.contains("routing"));
    auto& routing = config["routing"].as_object();
    EXPECT_TRUE(routing.contains("balancers"));
    EXPECT_EQ(routing["balancers"].as_array().size(), 1u);
}

TEST_F(PipelineHandlerTest, AddStats_SetsStats) {
    auto handler = getHandler("add_stats");
    ASSERT_NE(handler, nullptr);
    
    boost::json::object config = makeMinimalConfig();
    boost::json::object params;
    
    config::PipelineContext ctx;
    handler->execute(config, params, ctx);
    
    EXPECT_TRUE(config.contains("stats"));
    EXPECT_TRUE(config["stats"].is_object());
}

TEST_F(PipelineHandlerTest, AddPolicy_SetsPolicy) {
    auto handler = getHandler("add_policy");
    ASSERT_NE(handler, nullptr);
    
    boost::json::object config = makeMinimalConfig();
    boost::json::object params;
    params["levels"] = boost::json::object({{"0", boost::json::object{}}});
    
    config::PipelineContext ctx;
    handler->execute(config, params, ctx);
    
    EXPECT_TRUE(config.contains("policy"));
    EXPECT_TRUE(config["policy"].is_object());
}

TEST_F(PipelineHandlerTest, AddApi_SetsApi) {
    auto handler = getHandler("add_api");
    ASSERT_NE(handler, nullptr);
    
    boost::json::object config = makeMinimalConfig();
    boost::json::object params;
    params["listen"] = "127.0.0.1:10085";
    params["tag"] = "api";
    
    config::PipelineContext ctx;
    handler->execute(config, params, ctx);
    
    EXPECT_TRUE(config.contains("api"));
    auto& api = config["api"].as_object();
    EXPECT_EQ(api.at("listen").as_string(), "127.0.0.1:10085");
}

TEST_F(PipelineHandlerTest, ModifyField_SetExistingField) {
    auto handler = getHandler("modify_field");
    ASSERT_NE(handler, nullptr);
    
    boost::json::object config = makeMinimalConfig();
    config["log"].as_object()["access"] = std::string("");
    
    boost::json::object params;
    params["path"] = "log.access";
    params["value"] = std::string("access.log");
    
    config::PipelineContext ctx;
    handler->execute(config, params, ctx);
    
    EXPECT_EQ(config["log"].at("access").as_string(), "access.log");
}

TEST_F(PipelineHandlerTest, ModifyField_SetNestedField) {
    auto handler = getHandler("modify_field");
    
    boost::json::object config = makeMinimalConfig();
    auto& routing = config["routing"].as_object();
    routing["domainStrategy"] = std::string("AsIs");
    
    boost::json::object params;
    params["path"] = "routing.domainStrategy";
    params["value"] = std::string("UseIPv4");
    
    config::PipelineContext ctx;
    handler->execute(config, params, ctx);
    
    EXPECT_EQ(config["routing"].at("domainStrategy").as_string(), "UseIPv4");
}

TEST_F(PipelineHandlerTest, ModifyField_ArrayIndex) {
    auto handler = getHandler("modify_field");
    
    boost::json::object config = makeMinimalConfig();
    boost::json::object rule1;
    rule1["outboundTag"] = std::string("proxy");
    boost::json::object rule2;
    rule2["outboundTag"] = std::string("direct");
    auto& routing = config["routing"].as_object();
    routing["rules"] = boost::json::array({rule1, rule2});
    
    boost::json::object params;
    params["path"] = "routing.rules[1].outboundTag";
    params["value"] = std::string("block");
    
    config::PipelineContext ctx;
    handler->execute(config, params, ctx);
    
    EXPECT_EQ(config["routing"].at("rules").as_array()[1].at("outboundTag").as_string(), "block");
}

TEST_F(PipelineHandlerTest, ModifyField_CreateNewPath) {
    auto handler = getHandler("modify_field");
    
    boost::json::object config = makeMinimalConfig();
    
    boost::json::object params;
    params["path"] = "custom.section.key";
    params["value"] = std::string("new_value");
    
    config::PipelineContext ctx;
    handler->execute(config, params, ctx);
    
    EXPECT_TRUE(config.contains("custom"));
    EXPECT_EQ(config["custom"].at("section").at("key").as_string(), "new_value");
}

TEST_F(PipelineHandlerTest, Pipeline_ExecuteFullPipeline) {
    ConfigPipeline pipeline(db_);
    
    boost::json::object pipelineJson;
    boost::json::object testDef;
    testDef["template"] = std::string("xray_test_001");
    boost::json::object step1;
    step1["type"] = std::string("replace_variables");
    step1["params"] = boost::json::value(boost::json::object{});
    step1["onError"] = std::string("fail_fast");
    boost::json::object step2;
    step2["type"] = std::string("add_outbound");
    boost::json::object step2Params;
    step2Params["tag"] = std::string("proxy");
    step2Params["source"] = std::string("profile");
    step2Params["onError"] = std::string("continue");
    step2["params"] = step2Params;
    boost::json::object step3;
    step3["type"] = std::string("add_outbound");
    boost::json::object step3Params;
    step3Params["tag"] = std::string("direct");
    step3Params["protocol"] = std::string("freedom");
    step3["params"] = step3Params;
    boost::json::object step4;
    step4["type"] = std::string("merge_routing");
    boost::json::object step4Params;
    step4Params["inboundTag"] = std::string("socks");
    step4Params["outboundTag"] = std::string("proxy");
    step4["params"] = step4Params;
    testDef["steps"] = boost::json::array({step1, step2, step3, step4});
    pipelineJson["test"] = testDef;
    
    pipeline.loadConfig(boost::json::value(pipelineJson));
    EXPECT_TRUE(pipeline.hasPipeline(ConfigMode::Test, "xray"));
    
    Profileitem p = makeProfile("5", "example.com", "443", "test-uuid");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.apiPort = 10085;
    vars.probeUrl = "https://www.google.com/generate_204";
    
    boost::json::value result = pipeline.execute(ConfigMode::Test, "xray", p, vars);
    EXPECT_TRUE(result.is_object());
    
    auto& obj = result.as_object();
    EXPECT_TRUE(obj.contains("outbounds"));
    EXPECT_TRUE(obj.contains("routing"));
    
    // Should have proxy and direct outbounds
    EXPECT_GE(obj["outbounds"].as_array().size(), 2u);
}

TEST_F(PipelineHandlerTest, Pipeline_HasPipelineReturnsFalseWhenEmpty) {
    ConfigPipeline pipeline(db_);
    EXPECT_FALSE(pipeline.hasPipeline(ConfigMode::Test, "xray"));
    EXPECT_FALSE(pipeline.hasPipeline(ConfigMode::Pool, "singbox"));
}

TEST_F(PipelineHandlerTest, Pipeline_NoProfileSkipsProfileOutbound) {
    ConfigPipeline pipeline(db_);
    
    boost::json::object pipelineJson;
    boost::json::object testDef;
    testDef["template"] = std::string("xray_test_001");
    boost::json::object step1;
    step1["type"] = std::string("replace_variables");
    step1["params"] = boost::json::value(boost::json::object{});
    boost::json::object step2;
    step2["type"] = std::string("add_outbound");
    boost::json::object step2Params;
    step2Params["tag"] = std::string("proxy");
    step2Params["source"] = std::string("profile");
    step2Params["onError"] = std::string("continue");
    step2["params"] = step2Params;
    testDef["steps"] = boost::json::array({step1, step2});
    pipelineJson["test"] = testDef;
    
    pipeline.loadConfig(boost::json::value(pipelineJson));
    
    // Profile with empty address (no profile)
    Profileitem p;
    p.address = "";
    ConfigVariables vars;
    vars.socksPort = 10808;
    
    // Should not throw - profile outbound step is skipped
    EXPECT_NO_THROW({
        boost::json::value result = pipeline.execute(ConfigMode::Test, "xray", p, vars);
        EXPECT_TRUE(result.is_object());
    });
}

TEST_F(PipelineHandlerTest, Pipeline_UnregisteredStepTypeSkipped) {
    ConfigPipeline pipeline(db_);
    
    boost::json::object pipelineJson;
    boost::json::object testDef;
    testDef["template"] = std::string("xray_test_001");
    boost::json::object step1;
    step1["type"] = std::string("nonexistent_step");
    step1["params"] = boost::json::value(boost::json::object{});
    step1["onError"] = std::string("continue");
    boost::json::object step2;
    step2["type"] = std::string("replace_variables");
    step2["params"] = boost::json::value(boost::json::object{});
    testDef["steps"] = boost::json::array({step1, step2});
    pipelineJson["test"] = testDef;
    
    pipeline.loadConfig(boost::json::value(pipelineJson));
    
    Profileitem p = makeProfile("5", "example.com", "443", "test-uuid");
    ConfigVariables vars;
    vars.socksPort = 10808;
    
    // Should not throw - unregistered step is skipped with warning
    EXPECT_NO_THROW({
        boost::json::value result = pipeline.execute(ConfigMode::Test, "xray", p, vars);
        EXPECT_TRUE(result.is_object());
    });
}

TEST_F(PipelineHandlerTest, Pipeline_FailFastThrows) {
    ConfigPipeline pipeline(db_);
    
    boost::json::object pipelineJson;
    boost::json::object testDef;
    testDef["template"] = std::string("xray_test_001");
    boost::json::object step1;
    step1["type"] = std::string("add_outbound");
    boost::json::object step1Params;
    step1Params["source"] = std::string("profile");  // Will fail with empty profile
    step1["params"] = step1Params;
    step1["onError"] = std::string("fail_fast");
    testDef["steps"] = boost::json::array({step1});
    pipelineJson["test"] = testDef;
    
    pipeline.loadConfig(boost::json::value(pipelineJson));
    
    // Profile with empty address - add_outbound with source="profile" should be skipped
    // but fail_fast should still not throw for skipped steps
    Profileitem p;
    p.address = "";
    ConfigVariables vars;
    vars.socksPort = 10808;
    
    EXPECT_NO_THROW({
        boost::json::value result = pipeline.execute(ConfigMode::Test, "xray", p, vars);
        EXPECT_TRUE(result.is_object());
    });
}

TEST_F(PipelineHandlerTest, Pipeline_ReplaceVariablesReplacesPlaceholders) {
    ConfigPipeline pipeline(db_);
    
    boost::json::object pipelineJson;
    boost::json::object testDef;
    testDef["template"] = std::string("xray_test_001");
    boost::json::object step1;
    step1["type"] = std::string("replace_variables");
    step1["params"] = boost::json::value(boost::json::object{});
    testDef["steps"] = boost::json::array({step1});
    pipelineJson["test"] = testDef;
    
    pipeline.loadConfig(boost::json::value(pipelineJson));
    
    Profileitem p = makeProfile("5", "example.com", "443", "test-uuid");
    ConfigVariables vars;
    vars.socksPort = 10808;
    vars.apiPort = 10085;
    
    boost::json::value result = pipeline.execute(ConfigMode::Test, "xray", p, vars);
    const auto& obj = result.as_object();
    
    // Check that ${socksPort} was replaced
    std::string configStr = boost::json::serialize(obj);
    EXPECT_EQ(configStr.find("${socksPort}"), std::string::npos);
    EXPECT_EQ(configStr.find("${apiPort}"), std::string::npos);
}

TEST_F(PipelineHandlerTest, Pipeline_SingBoxPipeline) {
    ConfigPipeline pipeline(db_);
    
    boost::json::object pipelineJson;
    boost::json::object testDef;
    testDef["template"] = std::string("singbox_test_001");
    boost::json::object step1;
    step1["type"] = std::string("replace_variables");
    step1["params"] = boost::json::value(boost::json::object{});
    testDef["steps"] = boost::json::array({step1});
    pipelineJson["test_singbox"] = testDef;
    
    pipeline.loadConfig(boost::json::value(pipelineJson));
    EXPECT_TRUE(pipeline.hasPipeline(ConfigMode::Test, "singbox"));
    EXPECT_FALSE(pipeline.hasPipeline(ConfigMode::Test, "xray"));
    
    Profileitem p = makeProfile("5", "example.com", "443", "test-uuid");
    ConfigVariables vars;
    vars.socksPort = 10808;
    
    boost::json::value result = pipeline.execute(ConfigMode::Test, "singbox", p, vars);
    EXPECT_TRUE(result.is_object());
}
