#include <gtest/gtest.h>
#include <sqlite3.h>
#include "config/ComponentBundleDAO.h"
#include "config/ComponentBundle.h"
#include <boost/json.hpp>
#include <memory>
#include <thread>
#include <chrono>

using namespace config;

class ComponentBundleDAOTest : public testing::Test {
protected:
    sqlite3* db = nullptr;
    std::unique_ptr<ComponentBundleDAO> dao;
    
    void SetUp() override {
        // Create in-memory database for testing
        int rc = sqlite3_open(":memory:", &db);
        ASSERT_EQ(rc, SQLITE_OK);
        
        dao.reset(new ComponentBundleDAO(db));
        
        // Create table
        ASSERT_TRUE(dao->createTableIfNotExists());
    }
    
    void TearDown() override {
        if (db) {
            sqlite3_close(db);
        }
    }
    
    ComponentBundle createTestBundle() {
        ComponentBundle bundle;
        bundle.id = "test_bundle_001";
        bundle.name = "Test Bundle";
        bundle.description = "Test description";
        bundle.coreType = CoreType::XRAY;
        bundle.version = 1;
        bundle.enabled = true;
        
        // Add a log component
        ComponentConfig logComp;
        logComp.type = ComponentType::LOG;
        logComp.params["loglevel"] = boost::json::value("warning");
        bundle.components.push_back(logComp);
        
        return bundle;
    }
};

TEST_F(ComponentBundleDAOTest, TableExists) {
    // 验证表存在（SetUp 已创建表）
    EXPECT_TRUE(dao->tableExists());
}

TEST_F(ComponentBundleDAOTest, SaveAndGetById) {
    ComponentBundle bundle = createTestBundle();
    
    EXPECT_TRUE(dao->save(bundle));
    
    auto result = dao->getById("test_bundle_001");
    ASSERT_TRUE(result.has_value());
    
    EXPECT_EQ(result->id, "test_bundle_001");
    EXPECT_EQ(result->name, "Test Bundle");
    EXPECT_EQ(result->description, "Test description");
    EXPECT_EQ(result->coreType, CoreType::XRAY);
    EXPECT_EQ(result->version, 1);
    EXPECT_TRUE(result->enabled);
    EXPECT_EQ(result->components.size(), 1);
}

TEST_F(ComponentBundleDAOTest, GetByIdAndVersion) {
    ComponentBundle bundle = createTestBundle();
    bundle.version = 1;
    
    EXPECT_TRUE(dao->save(bundle));
    
    auto result = dao->getByIdAndVersion("test_bundle_001", 1);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->version, 1);
    
    auto notFound = dao->getByIdAndVersion("test_bundle_001", 2);
    EXPECT_FALSE(notFound.has_value());
}

TEST_F(ComponentBundleDAOTest, UpdateVersion) {
    ComponentBundle bundle = createTestBundle();
    bundle.version = 1;
    
    EXPECT_TRUE(dao->save(bundle));
    
    int newVersion = dao->updateVersion("test_bundle_001");
    EXPECT_EQ(newVersion, 2);
    
    auto result = dao->getById("test_bundle_001");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->version, 2);
}

TEST_F(ComponentBundleDAOTest, DeleteBundle) {
    ComponentBundle bundle = createTestBundle();
    
    EXPECT_TRUE(dao->save(bundle));
    EXPECT_TRUE(dao->exists("test_bundle_001"));
    
    EXPECT_TRUE(dao->deleteById("test_bundle_001"));
    EXPECT_FALSE(dao->exists("test_bundle_001"));
    
    auto result = dao->getById("test_bundle_001");
    EXPECT_FALSE(result.has_value());
}

TEST_F(ComponentBundleDAOTest, GetAll) {
    EXPECT_TRUE(dao->save(createTestBundle()));
    
    ComponentBundle bundle2 = createTestBundle();
    bundle2.id = "test_bundle_002";
    bundle2.name = "Test Bundle 2";
    EXPECT_TRUE(dao->save(bundle2));
    
    auto all = dao->getAll();
    EXPECT_EQ(all.size(), 2);
}

TEST_F(ComponentBundleDAOTest, GetAllEnabled) {
    ComponentBundle bundle1 = createTestBundle();
    bundle1.enabled = true;
    EXPECT_TRUE(dao->save(bundle1));
    
    ComponentBundle bundle2 = createTestBundle();
    bundle2.id = "test_bundle_002";
    bundle2.enabled = false;
    EXPECT_TRUE(dao->save(bundle2));
    
    auto enabled = dao->getAllEnabled();
    EXPECT_EQ(enabled.size(), 1);
    EXPECT_EQ(enabled[0].id, "test_bundle_001");
}

TEST_F(ComponentBundleDAOTest, GetByCoreType) {
    ComponentBundle xrayBundle = createTestBundle();
    xrayBundle.coreType = CoreType::XRAY;
    xrayBundle.id = "xray_bundle";
    EXPECT_TRUE(dao->save(xrayBundle));
    
    ComponentBundle singboxBundle = createTestBundle();
    singboxBundle.coreType = CoreType::SINGBOX;
    singboxBundle.id = "singbox_bundle";
    EXPECT_TRUE(dao->save(singboxBundle));
    
    auto xrayBundles = dao->getByCoreType(CoreType::XRAY);
    EXPECT_EQ(xrayBundles.size(), 1);
    EXPECT_EQ(xrayBundles[0].id, "xray_bundle");
    
    auto singboxBundles = dao->getByCoreType(CoreType::SINGBOX);
    EXPECT_EQ(singboxBundles.size(), 1);
    EXPECT_EQ(singboxBundles[0].id, "singbox_bundle");
}

TEST_F(ComponentBundleDAOTest, CreateTableDropsLegacyTemplateTypeColumn) {
    // 旧库结构：TemplateType 为 NOT NULL 且无默认值
    char* err = nullptr;
    ASSERT_EQ(SQLITE_OK, sqlite3_exec(db,
        "DROP TABLE IF EXISTS ComponentBundle;"
        "CREATE TABLE ComponentBundle ("
        "Id varchar PRIMARY KEY, Name varchar NOT NULL, Description varchar,"
        "CoreType INTEGER NOT NULL, TemplateType varchar NOT NULL,"
        "Version INTEGER NOT NULL DEFAULT 1, Enabled INTEGER NOT NULL DEFAULT 1,"
        "Components varchar NOT NULL, CreatedAt TEXT NOT NULL, UpdatedAt TEXT NOT NULL);",
        nullptr, nullptr, &err));
    if (err) sqlite3_free(err);

    ASSERT_TRUE(dao->createTableIfNotExists());

    // 列已移除：新写入（不再提供 TemplateType）必须成功
    ComponentBundle bundle = createTestBundle();
    EXPECT_TRUE(dao->save(bundle));

    // 幂等：再次调用不应失败
    EXPECT_TRUE(dao->createTableIfNotExists());
    EXPECT_EQ(1u, dao->getAll().size());
}

TEST_F(ComponentBundleDAOTest, ComponentSerialization) {
    ComponentBundle bundle = createTestBundle();
    EXPECT_TRUE(dao->save(bundle));
    
    auto result = dao->getById("test_bundle_001");
    ASSERT_TRUE(result.has_value());
    
    // Verify component serialization/deserialization
    EXPECT_EQ(result->components.size(), 1);
    EXPECT_EQ(result->components[0].type, ComponentType::LOG);
    EXPECT_TRUE(result->components[0].params.find("loglevel") != result->components[0].params.end());
}

TEST_F(ComponentBundleDAOTest, ComplexComponentSerialization) {
    ComponentBundle bundle;
    bundle.id = "complex_bundle";
    bundle.name = "Complex Bundle";
    bundle.coreType = CoreType::XRAY;
    bundle.version = 1;
    bundle.enabled = true;
    
    // Add routing component with complex params
    ComponentConfig routingComp;
    routingComp.type = ComponentType::ROUTING;
    boost::json::object routingParams;
    routingParams["domainStrategy"] = "AsIs";
    boost::json::array rules;
    boost::json::object rule;
    rule["type"] = "field";
    rule["inboundTag"] = boost::json::array({"socks"});
    rule["outboundTag"] = "proxy";
    rules.push_back(rule);
    routingParams["rules"] = rules;
    routingComp.params["routing"] = routingParams;
    bundle.components.push_back(routingComp);
    
    EXPECT_TRUE(dao->save(bundle));
    
    auto result = dao->getById("complex_bundle");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->components.size(), 1);
    EXPECT_EQ(result->components[0].type, ComponentType::ROUTING);
}

TEST_F(ComponentBundleDAOTest, ComponentWithId) {
    ComponentBundle bundle = createTestBundle();
    
    ComponentConfig inboundComp;
    inboundComp.type = ComponentType::INBOUND;
    inboundComp.id = "socks";
    inboundComp.params["tag"] = boost::json::value("socks");
    bundle.components.push_back(inboundComp);
    
    EXPECT_TRUE(dao->save(bundle));
    
    auto result = dao->getById("test_bundle_001");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->components.size(), 2);
    
    // Find the inbound component
    bool found = false;
    for (const auto& comp : result->components) {
        if (comp.type == ComponentType::INBOUND) {
            EXPECT_EQ(comp.id, "socks");
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(ComponentBundleDAOTest, GetReferencedCount) {
    // Note: This test would need FullConfigTemplateItem table to work
    // For now, just verify the method returns 0
    ComponentBundle bundle = createTestBundle();
    EXPECT_TRUE(dao->save(bundle));
    
    int count = dao->getReferencedCount("test_bundle_001");
    EXPECT_EQ(count, 0);
}

TEST_F(ComponentBundleDAOTest, UpdateVersionNonExistent) {
    int result = dao->updateVersion("non_existent_bundle");
    EXPECT_EQ(result, -1);
}

TEST_F(ComponentBundleDAOTest, DeleteNonExistent) {
    // Should not throw, just return false or do nothing
    bool result = dao->deleteById("non_existent_bundle");
    // Depending on implementation, this may return true (SQL doesn't care if row exists)
    // Just verify it doesn't crash
    (void)result;
}

TEST_F(ComponentBundleDAOTest, ExistsNonExistent) {
    EXPECT_FALSE(dao->exists("non_existent_bundle"));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
