#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <sqlite3.h>
#include <boost/json.hpp>

#include "config/BundleChainResolver.h"
#include "config/ComponentBundleDAO.h"
#include "config/ComponentRegistry.h"

namespace {

using config::BundleChain;
using config::BundleChainResolver;
using config::BundleVersionInfo;
using config::ComponentBundle;
using config::ComponentBundleDAO;
using config::ComponentConfig;
using config::ComponentRegistry;
using config::ComponentType;
using config::Conflict;
using config::ConflictSeverity;
using config::ConflictType;
using config::CoreType;
using config::TemplateType;

ComponentBundle makeBundle(const std::string& id, int version) {
    ComponentBundle bundle;
    bundle.id = id;
    bundle.name = id;
    bundle.description = "test bundle for " + id;
    bundle.coreType = CoreType::XRAY;
    bundle.version = version;
    bundle.enabled = true;
    return bundle;
}

ComponentConfig makeComponent(ComponentType type) {
    ComponentConfig comp;
    comp.type = type;
    comp.id = "";
    return comp;
}

void addParam(ComponentConfig& comp, const std::string& key, const std::string& value) {
    comp.params[key] = boost::json::value(value);
}

void addParam(ComponentConfig& comp, const std::string& key, int value) {
    comp.params[key] = boost::json::value(static_cast<std::int64_t>(value));
}

const ComponentConfig* findByTag(const std::vector<ComponentConfig>& components,
                                 ComponentType type, const std::string& tag) {
    for (const ComponentConfig& comp : components) {
        if (comp.type != type) {
            continue;
        }
        const std::map<std::string, boost::json::value>::const_iterator it = comp.params.find("tag");
        if (it != comp.params.end() && it->second.is_string()) {
            if (boost::json::value_to<std::string>(it->second) == tag) {
                return &comp;
            }
        }
    }
    return nullptr;
}

class BundleChainResolverTest : public testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(SQLITE_OK, sqlite3_open(":memory:", &db));
        dao = std::unique_ptr<ComponentBundleDAO>(new ComponentBundleDAO(db));
        ASSERT_TRUE(dao->createTableIfNotExists());
        ComponentRegistry::instance().registerDefaultTypes();
        resolver = std::unique_ptr<BundleChainResolver>(new BundleChainResolver(db));
    }

    void TearDown() override {
        resolver.reset();
        dao.reset();
        sqlite3_close(db);
        db = nullptr;
    }

    sqlite3* db = nullptr;
    std::unique_ptr<ComponentBundleDAO> dao;
    std::unique_ptr<BundleChainResolver> resolver;
};

} // namespace

// ---------------------------------------------------------------------------
// resolveBundleChain：加载与合并
// ---------------------------------------------------------------------------

TEST_F(BundleChainResolverTest, ResolveLoadsBundlesInOrder) {
    ComponentBundle base = makeBundle("base_xray", 1);
    base.components.push_back(makeComponent(ComponentType::LOG));
    base.components.push_back(makeComponent(ComponentType::DNS));
    ASSERT_TRUE(dao->save(base));

    ComponentBundle testBundle = makeBundle("test_chain", 1);
    testBundle.components.push_back(makeComponent(ComponentType::INBOUND));
    ASSERT_TRUE(dao->save(testBundle));

    BundleChain chain;
    chain.bundleIds.push_back("base_xray");
    chain.bundleIds.push_back("test_chain");

    const config::BundleChainResolveResult result = resolver->resolveBundleChain(chain);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.components.size(), 3u);
    EXPECT_EQ(result.components[0].type, ComponentType::LOG);
    EXPECT_EQ(result.components[1].type, ComponentType::DNS);
    EXPECT_EQ(result.components[2].type, ComponentType::INBOUND);
    EXPECT_TRUE(result.conflicts.empty());
}

TEST_F(BundleChainResolverTest, ResolveSingletonOverrideCrossPackageWarn) {
    ComponentBundle base = makeBundle("base_log", 1);
    ComponentConfig log1 = makeComponent(ComponentType::LOG);
    addParam(log1, "loglevel", "warning");
    base.components.push_back(log1);
    ASSERT_TRUE(dao->save(base));

    ComponentBundle overrideBundle = makeBundle("override_log", 1);
    ComponentConfig log2 = makeComponent(ComponentType::LOG);
    addParam(log2, "loglevel", "debug");
    overrideBundle.components.push_back(log2);
    ASSERT_TRUE(dao->save(overrideBundle));

    BundleChain chain;
    chain.bundleIds.push_back("base_log");
    chain.bundleIds.push_back("override_log");

    const config::BundleChainResolveResult result = resolver->resolveBundleChain(chain);

    // 后加载单例覆盖先加载
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.components[0].type, ComponentType::LOG);
    EXPECT_EQ(boost::json::value_to<std::string>(result.components[0].params.at("loglevel")), "debug");

    // 跨包覆盖 → WARN（允许）
    ASSERT_EQ(result.conflicts.size(), 1u);
    EXPECT_EQ(result.conflicts[0].type, ConflictType::SINGLETON_CONFLICT);
    EXPECT_EQ(result.conflicts[0].severity, ConflictSeverity::WARN);
    EXPECT_TRUE(result.conflicts[0].isCrossBundle);
}

TEST_F(BundleChainResolverTest, ResolveArrayTagDedup) {
    ComponentBundle base = makeBundle("base_inbound", 1);
    ComponentConfig socks1 = makeComponent(ComponentType::INBOUND);
    addParam(socks1, "tag", "socks");
    addParam(socks1, "port", 10809);
    base.components.push_back(socks1);
    ASSERT_TRUE(dao->save(base));

    ComponentBundle overrideBundle = makeBundle("override_inbound", 1);
    ComponentConfig socks2 = makeComponent(ComponentType::INBOUND);
    addParam(socks2, "tag", "socks");
    addParam(socks2, "port", 10810);
    ComponentConfig api = makeComponent(ComponentType::INBOUND);
    addParam(api, "tag", "api");
    addParam(api, "port", 10811);
    overrideBundle.components.push_back(socks2);
    overrideBundle.components.push_back(api);
    ASSERT_TRUE(dao->save(overrideBundle));

    BundleChain chain;
    chain.bundleIds.push_back("base_inbound");
    chain.bundleIds.push_back("override_inbound");

    const config::BundleChainResolveResult result = resolver->resolveBundleChain(chain);

    ASSERT_TRUE(result.success);
    // 同 tag 被替换，新 tag 追加
    const ComponentConfig* socks = findByTag(result.components, ComponentType::INBOUND, "socks");
    ASSERT_NE(socks, nullptr);
    EXPECT_EQ(socks->params.at("port").as_int64(), 10810);
    EXPECT_NE(findByTag(result.components, ComponentType::INBOUND, "api"), nullptr);
    // 跨包数组 tag 重复 → WARN
    ASSERT_EQ(result.conflicts.size(), 1u);
    EXPECT_EQ(result.conflicts[0].type, ConflictType::TAG_CONFLICT);
    EXPECT_EQ(result.conflicts[0].severity, ConflictSeverity::WARN);
}

TEST_F(BundleChainResolverTest, ResolveSamePackageSingletonConflictError) {
    ComponentBundle bundle = makeBundle("dup_log", 1);
    bundle.components.push_back(makeComponent(ComponentType::LOG));
    bundle.components.push_back(makeComponent(ComponentType::LOG));
    ASSERT_TRUE(dao->save(bundle));

    BundleChain chain;
    chain.bundleIds.push_back("dup_log");

    const config::BundleChainResolveResult result = resolver->resolveBundleChain(chain);

    // 同包重复 → ERROR（跨包覆盖才允许）
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.conflicts.size(), 1u);
    EXPECT_EQ(result.conflicts[0].type, ConflictType::SINGLETON_CONFLICT);
    EXPECT_EQ(result.conflicts[0].severity, ConflictSeverity::ERROR);
    EXPECT_FALSE(result.conflicts[0].isCrossBundle);
    // 合并后单例只剩最后一个
    ASSERT_EQ(result.components.size(), 1u);
}

TEST_F(BundleChainResolverTest, ResolveMissingBundleFails) {
    BundleChain chain;
    chain.bundleIds.push_back("missing_bundle");

    const config::BundleChainResolveResult result = resolver->resolveBundleChain(chain);

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.errorMessage.find("missing_bundle"), std::string::npos);
}

TEST_F(BundleChainResolverTest, ResolveEmptyChainSucceeds) {
    BundleChain chain;
    const config::BundleChainResolveResult result = resolver->resolveBundleChain(chain);

    ASSERT_TRUE(result.success);
    EXPECT_TRUE(result.components.empty());
    EXPECT_TRUE(result.conflicts.empty());
}

// ---------------------------------------------------------------------------
// resolveToConfig：生成最终配置
// ---------------------------------------------------------------------------

TEST_F(BundleChainResolverTest, ResolveToConfigProducesConfig) {
    ComponentBundle base = makeBundle("base_xray", 1);
    base.components.push_back(makeComponent(ComponentType::LOG));
    base.components.push_back(makeComponent(ComponentType::DNS));
    ASSERT_TRUE(dao->save(base));

    ComponentBundle testBundle = makeBundle("test_chain", 1);
    testBundle.components.push_back(makeComponent(ComponentType::API));
    ComponentConfig inbound = makeComponent(ComponentType::INBOUND);
    addParam(inbound, "tag", "socks");
    addParam(inbound, "port", 10809);
    testBundle.components.push_back(inbound);
    ASSERT_TRUE(dao->save(testBundle));

    BundleChain chain;
    chain.bundleIds.push_back("base_xray");
    chain.bundleIds.push_back("test_chain");

    const config::BundleChainResolveResult result = resolver->resolveToConfig(chain);

    ASSERT_TRUE(result.success);
    ASSERT_TRUE(result.config.is_object());
    const boost::json::object& obj = result.config.as_object();
    EXPECT_TRUE(obj.contains("log"));
    EXPECT_TRUE(obj.contains("dns"));
    EXPECT_TRUE(obj.contains("api"));
    ASSERT_TRUE(obj.contains("inbounds"));
    EXPECT_EQ(obj.at("inbounds").as_array().size(), 1u);
}

TEST_F(BundleChainResolverTest, ResolveToConfigOverridesInboundPort) {
    ComponentBundle base = makeBundle("base_inbound", 1);
    ComponentConfig socks1 = makeComponent(ComponentType::INBOUND);
    addParam(socks1, "tag", "socks");
    addParam(socks1, "port", 10809);
    base.components.push_back(socks1);
    ASSERT_TRUE(dao->save(base));

    ComponentBundle overrideBundle = makeBundle("override_inbound", 1);
    ComponentConfig socks2 = makeComponent(ComponentType::INBOUND);
    addParam(socks2, "tag", "socks");
    addParam(socks2, "port", 10810);
    overrideBundle.components.push_back(socks2);
    ASSERT_TRUE(dao->save(overrideBundle));

    BundleChain chain;
    chain.bundleIds.push_back("base_inbound");
    chain.bundleIds.push_back("override_inbound");

    const config::BundleChainResolveResult result = resolver->resolveToConfig(chain);

    ASSERT_TRUE(result.success);
    const boost::json::object& obj = result.config.as_object();
    ASSERT_TRUE(obj.contains("inbounds"));
    EXPECT_EQ(obj.at("inbounds").as_array()[0].as_object().at("port").as_int64(), 10810);
}

TEST_F(BundleChainResolverTest, ResolveToConfigMergesRoutingRules) {
    ComponentBundle base = makeBundle("base_routing", 1);
    ComponentConfig routing1 = makeComponent(ComponentType::ROUTING);
    boost::json::array rules1;
    boost::json::object rule1;
    rule1["type"] = "field";
    rule1["outboundTag"] = "proxy";
    rules1.push_back(rule1);
    routing1.params["rules"] = rules1;
    base.components.push_back(routing1);
    ASSERT_TRUE(dao->save(base));

    ComponentBundle overrideBundle = makeBundle("override_routing", 1);
    ComponentConfig routing2 = makeComponent(ComponentType::ROUTING);
    boost::json::array rules2;
    boost::json::object rule2;
    rule2["type"] = "field";
    rule2["outboundTag"] = "direct";
    rules2.push_back(rule2);
    routing2.params["rules"] = rules2;
    overrideBundle.components.push_back(routing2);
    ASSERT_TRUE(dao->save(overrideBundle));

    BundleChain chain;
    chain.bundleIds.push_back("base_routing");
    chain.bundleIds.push_back("override_routing");

    const config::BundleChainResolveResult result = resolver->resolveToConfig(chain);

    ASSERT_TRUE(result.success);
    const boost::json::object& obj = result.config.as_object();
    ASSERT_TRUE(obj.contains("routing"));
    ASSERT_TRUE(obj.at("routing").as_object().contains("rules"));
    EXPECT_EQ(obj.at("routing").as_object().at("rules").as_array().size(), 2u);
}

TEST_F(BundleChainResolverTest, ResolveToConfigMissingBundleFails) {
    BundleChain chain;
    chain.bundleIds.push_back("nope_chain");
    const config::BundleChainResolveResult result = resolver->resolveToConfig(chain);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.config.is_object());
}

// ---------------------------------------------------------------------------
// 占位符提取
// ---------------------------------------------------------------------------

TEST_F(BundleChainResolverTest, ExtractPlaceholdersFindsNames) {
    ComponentBundle bundle1 = makeBundle("ph_bundle1", 1);
    ComponentConfig log1 = makeComponent(ComponentType::LOG);
    addParam(log1, "port", "${socksPort}");
    bundle1.components.push_back(log1);
    ASSERT_TRUE(dao->save(bundle1));

    ComponentBundle bundle2 = makeBundle("ph_bundle2", 1);
    ComponentConfig log2 = makeComponent(ComponentType::LOG);
    addParam(log2, "url", "${probeUrl:https://example.com}");
    addParam(log2, "port", "${socksPort}");
    bundle2.components.push_back(log2);
    ASSERT_TRUE(dao->save(bundle2));

    BundleChain chain;
    chain.bundleIds.push_back("ph_bundle1");
    chain.bundleIds.push_back("ph_bundle2");

    const std::vector<std::string> names = resolver->extractPlaceholders(chain);

    // ${name} 与 ${name:default} 均提取，跨 Bundle 去重
    std::set<std::string> nameSet(names.begin(), names.end());
    EXPECT_EQ(nameSet.size(), 2u);
    EXPECT_NE(nameSet.find("socksPort"), nameSet.end());
    EXPECT_NE(nameSet.find("probeUrl"), nameSet.end());
}

TEST_F(BundleChainResolverTest, ExtractPlaceholdersEmpty) {
    ComponentBundle bundle = makeBundle("ph_empty", 1);
    ComponentConfig log = makeComponent(ComponentType::LOG);
    addParam(log, "loglevel", "warning");
    bundle.components.push_back(log);
    ASSERT_TRUE(dao->save(bundle));

    BundleChain chain;
    chain.bundleIds.push_back("ph_empty");

    const std::vector<std::string> names = resolver->extractPlaceholders(chain);
    EXPECT_TRUE(names.empty());
}

// ---------------------------------------------------------------------------
// 版本同步
// ---------------------------------------------------------------------------

TEST_F(BundleChainResolverTest, GetCurrentVersionsReturnsBundleVersions) {
    ComponentBundle bundleA = makeBundle("chain_a", 2);
    bundleA.components.push_back(makeComponent(ComponentType::LOG));
    ASSERT_TRUE(dao->save(bundleA));
    ComponentBundle bundleB = makeBundle("chain_b", 1);
    bundleB.components.push_back(makeComponent(ComponentType::DNS));
    ASSERT_TRUE(dao->save(bundleB));

    BundleChain chain;
    chain.bundleIds.push_back("chain_a");
    chain.bundleIds.push_back("chain_b");

    const std::vector<BundleVersionInfo> versions = resolver->getCurrentVersions(chain);

    ASSERT_EQ(versions.size(), 2u);
    EXPECT_EQ(versions[0].bundleId, "chain_a");
    EXPECT_EQ(versions[0].version, 2);
    EXPECT_EQ(versions[1].bundleId, "chain_b");
    EXPECT_EQ(versions[1].version, 1);
}

TEST_F(BundleChainResolverTest, IsChainStaleWhenVersionsMatch) {
    ComponentBundle bundleA = makeBundle("chain_a", 1);
    bundleA.components.push_back(makeComponent(ComponentType::LOG));
    ASSERT_TRUE(dao->save(bundleA));

    BundleChain chain;
    chain.bundleIds.push_back("chain_a");

    const std::vector<BundleVersionInfo> recorded = resolver->getCurrentVersions(chain);
    EXPECT_FALSE(resolver->isChainStale(chain, recorded));
}

TEST_F(BundleChainResolverTest, IsChainStaleAfterBundleUpdate) {
    ComponentBundle bundleA = makeBundle("chain_a", 1);
    bundleA.components.push_back(makeComponent(ComponentType::LOG));
    ASSERT_TRUE(dao->save(bundleA));

    BundleChain chain;
    chain.bundleIds.push_back("chain_a");

    const std::vector<BundleVersionInfo> recorded = resolver->getCurrentVersions(chain);
    ASSERT_EQ(dao->updateVersion("chain_a"), 2);
    EXPECT_TRUE(resolver->isChainStale(chain, recorded));
}

TEST_F(BundleChainResolverTest, IsChainStaleWhenBundleMissing) {
    ComponentBundle bundleA = makeBundle("chain_a", 1);
    bundleA.components.push_back(makeComponent(ComponentType::LOG));
    ASSERT_TRUE(dao->save(bundleA));

    BundleChain chain;
    chain.bundleIds.push_back("chain_a");

    const std::vector<BundleVersionInfo> recorded = resolver->getCurrentVersions(chain);
    ASSERT_TRUE(dao->deleteById("chain_a"));
    EXPECT_TRUE(resolver->isChainStale(chain, recorded));
}

TEST_F(BundleChainResolverTest, IsChainStaleWhenRecordMissing) {
    ComponentBundle bundleA = makeBundle("chain_a", 1);
    bundleA.components.push_back(makeComponent(ComponentType::LOG));
    ASSERT_TRUE(dao->save(bundleA));

    BundleChain chain;
    chain.bundleIds.push_back("chain_a");

    std::vector<BundleVersionInfo> emptyRecord;
    EXPECT_TRUE(resolver->isChainStale(chain, emptyRecord));
}