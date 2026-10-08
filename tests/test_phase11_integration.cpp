// test_phase11_integration.cpp - Phase 11: End-to-end integration tests
// Tests full pipeline: SchemaValidator → BundleChainResolver → ConflictDetector
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <boost/json.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "config/BundleChainResolver.h"
#include "config/ComponentBundleDAO.h"
#include "config/ComponentRegistry.h"
#include "config/ConflictDetector.h"
#include "config/SchemaValidator.h"

using config::BundleChain;
using config::BundleChainResolveResult;
using config::BundleChainResolver;
using config::ComponentBundle;
using config::ComponentBundleDAO;
using config::ComponentConfig;
using config::ComponentRegistry;
using config::ComponentSource;
using config::ComponentType;
using config::Conflict;
using config::ConflictDetector;
using config::CoreType;
using config::SchemaValidator;
using config::TemplateType;

namespace {

// -----------------------------------------------------------------
// Fixture: in-memory SQLite + DAO + Registry + Resolvers
// -----------------------------------------------------------------
class IntegrationTest : public testing::Test {
protected:
    sqlite3* db = nullptr;
    std::unique_ptr<ComponentBundleDAO> dao;
    std::unique_ptr<BundleChainResolver> resolver;
    std::unique_ptr<ConflictDetector> conflictDetector;

    void SetUp() override {
        ASSERT_EQ(SQLITE_OK, sqlite3_open(":memory:", &db));
        dao.reset(new ComponentBundleDAO(db));
        ASSERT_TRUE(dao->createTableIfNotExists());
        ComponentRegistry::instance().registerDefaultTypes();
        resolver.reset(new BundleChainResolver(db));
        conflictDetector.reset(new ConflictDetector(ComponentRegistry::instance()));
    }

    void TearDown() override {
        resolver.reset();
        conflictDetector.reset();
        dao.reset();
        sqlite3_close(db);
    }

    // Build and save a bundle with given id, coreType, templateType, version
    ComponentBundle makeAndSaveBundle(
        const std::string& id,
        CoreType core,
        TemplateType tmpl,
        int version,
        const std::vector<ComponentConfig>& components)
    {
        ComponentBundle bundle;
        bundle.id = id;
        bundle.name = id;
        bundle.description = "integration test bundle";
        bundle.coreType = core;
        bundle.templateType = tmpl;
        bundle.version = version;
        bundle.enabled = true;
        bundle.components = components;
        if (!dao->save(bundle)) {
            ADD_FAILURE() << "save failed for " << id;
            return bundle;
        }
        return bundle;
    }

    // Helper: build a component with type, optional id, and key-value params
    ComponentConfig makeComponent(
        ComponentType type,
        const std::string& id,
        const std::vector<std::pair<std::string, boost::json::value>>& params = {})
    {
        ComponentConfig comp;
        comp.type = type;
        comp.id = id;
        for (const auto& p : params) {
            comp.params[p.first] = p.second;
        }
        return comp;
    }

    // Helper: simple params (string key, string value)
    ComponentConfig makeComponentStr(
        ComponentType type,
        const std::string& id,
        const std::string& key,
        const std::string& value)
    {
        return makeComponent(type, id, {{key, boost::json::value(value)}});
    }

    // Helper: serialize a BundleChain to JSON for SchemaValidator
    boost::json::value bundleChainToJson(const BundleChain& chain)
    {
        boost::json::object obj;
        boost::json::array ids;
        for (const auto& id : chain.bundleIds) {
            ids.push_back(boost::json::value(id));
        }
        obj["bundleIds"] = ids;
        boost::json::object params;
        for (const auto& p : chain.params) {
            params[p.first] = p.second;
        }
        obj["params"] = params;
        return boost::json::value(obj);
    }
};

// -----------------------------------------------------------------
// 1. Full E2E: validate bundleChain JSON → resolve → get config
// -----------------------------------------------------------------
TEST_F(IntegrationTest, FullPipelineValidateAndResolve) {
    // Create base bundle with LOG + API components
    std::vector<ComponentConfig> baseComps;
    baseComps.push_back(makeComponentStr(ComponentType::LOG, "log", "access", "log.txt"));
    baseComps.push_back(makeComponentStr(ComponentType::API, "api", "tag", "api"));

    // Create test bundle with OUTBOUND component
    std::vector<ComponentConfig> testComps;
    testComps.push_back(makeComponentStr(ComponentType::OUTBOUND, "socks", "tag", "socks-in"));

    makeAndSaveBundle("base_xray_001", CoreType::XRAY, TemplateType::BASE, 1, baseComps);
    makeAndSaveBundle("test_xray_001", CoreType::XRAY, TemplateType::TEST, 1, testComps);

    // Build BundleChain
    BundleChain chain;
    chain.bundleIds = {"base_xray_001", "test_xray_001"};
    chain.params["socksPort"] = boost::json::value(std::int64_t(10809));

    // Step 1: Validate with SchemaValidator
    boost::json::value chainJson = bundleChainToJson(chain);
    std::string error;
    EXPECT_TRUE(SchemaValidator::validate(chainJson, "bundleChain", error));
    if (!error.empty()) {
        ADD_FAILURE() << "SchemaValidator failed: " << error;
    }

    // Step 2: Resolve via BundleChainResolver
    BundleChainResolveResult result = resolver->resolveBundleChain(chain);
    ASSERT_TRUE(result.success);

    // Step 3: Verify merged components
    EXPECT_FALSE(result.components.empty());
    // Should have LOG + API from base, OUTBOUND from test = 3 components
    EXPECT_GE(result.components.size(), 3u);

    // Step 4: Verify conflict detection on merged components
    std::vector<Conflict> conflicts = conflictDetector->detect(result.components);
    // No conflicts expected for distinct component types
    EXPECT_FALSE(conflictDetector->hasErrors(conflicts));
}

// -----------------------------------------------------------------
// 2. Placeholder extraction from bundle chain
// -----------------------------------------------------------------
TEST_F(IntegrationTest, PlaceholderExtraction) {
    std::vector<ComponentConfig> comps;
    comps.push_back(makeComponentStr(ComponentType::LOG, "log", "access", "${logDir}/xray.log"));
    comps.push_back(makeComponentStr(ComponentType::API, "api", "listen", "127.0.0.1:${apiPort}"));
    makeAndSaveBundle("placeholder_bundle", CoreType::XRAY, TemplateType::BASE, 1, comps);

    BundleChain chain;
    chain.bundleIds = {"placeholder_bundle"};

    std::vector<std::string> placeholders = resolver->extractPlaceholders(chain);
    EXPECT_GE(placeholders.size(), 2u);
    // Verify known placeholders found
    bool foundLogDir = false;
    bool foundApiPort = false;
    for (const auto& p : placeholders) {
        if (p == "logDir") foundLogDir = true;
        if (p == "apiPort") foundApiPort = true;
    }
    EXPECT_TRUE(foundLogDir);
    EXPECT_TRUE(foundApiPort);
}

// -----------------------------------------------------------------
// 3. Version sync detection: bundle updated after chain recorded
// -----------------------------------------------------------------
TEST_F(IntegrationTest, VersionStalenessDetection) {
    std::vector<ComponentConfig> comps;
    comps.push_back(makeComponentStr(ComponentType::LOG, "log", "access", "log.txt"));
    makeAndSaveBundle("version_bundle", CoreType::XRAY, TemplateType::BASE, 1, comps);

    BundleChain chain;
    chain.bundleIds = {"version_bundle"};

    // Record current version
    auto versions = resolver->getCurrentVersions(chain);
    ASSERT_EQ(versions.size(), 1u);
    EXPECT_EQ(versions[0].bundleId, "version_bundle");
    EXPECT_EQ(versions[0].version, 1);

    // Chain is NOT stale with matching version
    EXPECT_FALSE(resolver->isChainStale(chain, versions));

    // Update bundle to version 2
    int newVersion = dao->updateVersion("version_bundle");
    ASSERT_EQ(newVersion, 2);

    // Chain IS stale now
    EXPECT_TRUE(resolver->isChainStale(chain, versions));
}

// -----------------------------------------------------------------
// 4. Missing bundle in chain
// -----------------------------------------------------------------
TEST_F(IntegrationTest, MissingBundleInChain) {
    BundleChain chain;
    chain.bundleIds = {"nonexistent_bundle"};

    BundleChainResolveResult result = resolver->resolveBundleChain(chain);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
}

// -----------------------------------------------------------------
// 5. Empty chain (no bundle IDs)
// -----------------------------------------------------------------
TEST_F(IntegrationTest, EmptyChain) {
    BundleChain chain;
    // bundleIds is empty

    BundleChainResolveResult result = resolver->resolveBundleChain(chain);
    // Empty chain should succeed but produce no components
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.components.empty());
}

// -----------------------------------------------------------------
// 6. Single-component bundle resolves to valid config
// -----------------------------------------------------------------
TEST_F(IntegrationTest, SingleComponentResolution) {
    std::vector<ComponentConfig> comps;
    comps.push_back(makeComponentStr(ComponentType::LOG, "log", "access", "xray.log"));
    makeAndSaveBundle("single_log", CoreType::XRAY, TemplateType::BASE, 1, comps);

    BundleChain chain;
    chain.bundleIds = {"single_log"};

    BundleChainResolveResult result = resolver->resolveBundleChain(chain);
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.components[0].type, ComponentType::LOG);
}

// -----------------------------------------------------------------
// 7. Schema validation rejects malformed bundleChain
// -----------------------------------------------------------------
TEST_F(IntegrationTest, SchemaRejectsMalformedChain) {
    // Missing bundleIds field
    boost::json::object badJson;
    badJson["params"] = boost::json::object();
    std::string error;
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(badJson), "bundleChain", error));
    EXPECT_FALSE(error.empty());
}

// -----------------------------------------------------------------
// 8. Schema validation rejects duplicate bundle IDs
// -----------------------------------------------------------------
TEST_F(IntegrationTest, SchemaRejectsDuplicateBundleIds) {
    boost::json::object obj;
    boost::json::array ids;
    ids.push_back(boost::json::value(std::string("bundle_a")));
    ids.push_back(boost::json::value(std::string("bundle_a")));  // duplicate
    obj["bundleIds"] = ids;
    obj["params"] = boost::json::object();
    std::string error;
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "bundleChain", error));
    EXPECT_FALSE(error.empty());
}

// -----------------------------------------------------------------
// 9. Cross-bundle singleton override: later bundle overrides earlier
// -----------------------------------------------------------------
TEST_F(IntegrationTest, SingletonOverrideAcrossBundles) {
    // Base bundle: LOG with access=log.txt
    std::vector<ComponentConfig> baseComps;
    baseComps.push_back(makeComponentStr(ComponentType::LOG, "log", "access", "log.txt"));
    makeAndSaveBundle("base", CoreType::XRAY, TemplateType::BASE, 1, baseComps);

    // Override bundle: LOG with access=override.log
    std::vector<ComponentConfig> overrideComps;
    overrideComps.push_back(makeComponentStr(ComponentType::LOG, "log", "access", "override.log"));
    makeAndSaveBundle("override", CoreType::XRAY, TemplateType::TEST, 1, overrideComps);

    BundleChain chain;
    chain.bundleIds = {"base", "override"};

    BundleChainResolveResult result = resolver->resolveBundleChain(chain);
    ASSERT_TRUE(result.success);
    ASSERT_FALSE(result.components.empty());

    // Find LOG component
    bool foundLog = false;
    for (const auto& comp : result.components) {
        if (comp.type == ComponentType::LOG) {
            foundLog = true;
            auto it = comp.params.find("access");
            ASSERT_NE(it, comp.params.end());
            // Should be overridden value
            EXPECT_EQ(std::string(it->second.as_string()), "override.log");
            break;
        }
    }
    EXPECT_TRUE(foundLog);
}

// -----------------------------------------------------------------
// 10. Array components (inbounds/outbounds) append across bundles
// -----------------------------------------------------------------
TEST_F(IntegrationTest, ArrayComponentAppendAcrossBundles) {
    // Base: OUTBOUND socks
    std::vector<ComponentConfig> baseComps;
    baseComps.push_back(makeComponentStr(ComponentType::OUTBOUND, "socks", "tag", "socks-in"));
    makeAndSaveBundle("base_out", CoreType::XRAY, TemplateType::BASE, 1, baseComps);

    // Override: OUTBOUND trojan (different tag, should append)
    std::vector<ComponentConfig> overrideComps;
    overrideComps.push_back(makeComponentStr(ComponentType::OUTBOUND, "trojan", "tag", "trojan-in"));
    makeAndSaveBundle("extra_out", CoreType::XRAY, TemplateType::TEST, 1, overrideComps);

    BundleChain chain;
    chain.bundleIds = {"base_out", "extra_out"};

    BundleChainResolveResult result = resolver->resolveBundleChain(chain);
    ASSERT_TRUE(result.success);

    // Count OUTBOUND components
    int outboundCount = 0;
    for (const auto& comp : result.components) {
        if (comp.type == ComponentType::OUTBOUND) {
            outboundCount++;
        }
    }
    EXPECT_EQ(outboundCount, 2);
}

// -----------------------------------------------------------------
// 11. Component bundle JSON validates correctly
// -----------------------------------------------------------------
TEST_F(IntegrationTest, ComponentBundleSchemaValid) {
    boost::json::object obj;
    obj["id"] = boost::json::value(std::string("valid_bundle"));
    obj["coreType"] = boost::json::value(std::int64_t(2));
    obj["templateType"] = boost::json::value(std::string("BASE"));
    obj["version"] = boost::json::value(std::int64_t(1));
    obj["enabled"] = boost::json::value(true);
    boost::json::array comps;
    boost::json::object comp;
    comp["type"] = boost::json::value(std::string("log"));
    comps.push_back(comp);
    obj["components"] = comps;

    std::string error;
    EXPECT_TRUE(SchemaValidator::validate(boost::json::value(obj), "componentBundle", error));
}

// -----------------------------------------------------------------
// 12. Component bundle JSON rejects invalid coreType
// -----------------------------------------------------------------
TEST_F(IntegrationTest, ComponentBundleSchemaRejectsInvalidCoreType) {
    boost::json::object obj;
    obj["id"] = boost::json::value(std::string("bad_bundle"));
    obj["coreType"] = boost::json::value(std::int64_t(99));  // invalid
    obj["templateType"] = boost::json::value(std::string("BASE"));
    obj["version"] = boost::json::value(std::int64_t(1));
    obj["enabled"] = boost::json::value(true);
    boost::json::array comps;
    obj["components"] = comps;

    std::string error;
    EXPECT_FALSE(SchemaValidator::validate(boost::json::value(obj), "componentBundle", error));
}

// -----------------------------------------------------------------
// 13. E2E: resolveToConfig produces non-empty config JSON
// -----------------------------------------------------------------
TEST_F(IntegrationTest, ResolveToConfigProducesJson) {
    std::vector<ComponentConfig> comps;
    comps.push_back(makeComponentStr(ComponentType::LOG, "log", "access", "xray.log"));
    comps.push_back(makeComponentStr(ComponentType::API, "api", "tag", "api"));
    makeAndSaveBundle("config_bundle", CoreType::XRAY, TemplateType::BASE, 1, comps);

    BundleChain chain;
    chain.bundleIds = {"config_bundle"};

    BundleChainResolveResult result = resolver->resolveToConfig(chain);
    ASSERT_TRUE(result.success);
    ASSERT_FALSE(result.config.is_null());
    // Config should be a JSON object
    EXPECT_TRUE(result.config.is_object());
}

// -----------------------------------------------------------------
// 14. Dependency missing detection (routing refs non-existent tag)
// -----------------------------------------------------------------
TEST_F(IntegrationTest, DependencyMissingDetection) {
    // Routing component referencing non-existent inbound tag "ghost-in"
    std::vector<ComponentConfig> comps;
    ComponentConfig routing;
    routing.type = ComponentType::ROUTING;
    routing.id = "routing";
    boost::json::array rules;
    boost::json::object rule;
    rule["type"] = boost::json::value(std::string("field"));
    // inboundTag must be an array of strings (per ConflictDetector implementation)
    boost::json::array inTags;
    inTags.push_back(boost::json::value(std::string("ghost-in")));
    rule["inboundTag"] = boost::json::value(inTags);
    rules.push_back(rule);
    routing.params["rules"] = rules;
    comps.push_back(routing);

    std::vector<Conflict> conflicts = conflictDetector->detectDependencies(comps);
    EXPECT_FALSE(conflicts.empty());

    bool foundDepMissing = false;
    for (const auto& c : conflicts) {
        if (c.type == config::ConflictType::DEPENDENCY_MISSING) {
            foundDepMissing = true;
            break;
        }
    }
    EXPECT_TRUE(foundDepMissing);
}

// -----------------------------------------------------------------
// 15. No conflicts for fully disjoint components
// -----------------------------------------------------------------
TEST_F(IntegrationTest, NoConflictsForDisjointComponents) {
    std::vector<ComponentConfig> comps;
    comps.push_back(makeComponentStr(ComponentType::LOG, "log", "access", "log.txt"));
    comps.push_back(makeComponentStr(ComponentType::API, "api", "tag", "api"));
    comps.push_back(makeComponentStr(ComponentType::DNS, "dns", "tag", "dns"));

    std::vector<Conflict> conflicts = conflictDetector->detect(comps);
    EXPECT_TRUE(conflicts.empty());
    EXPECT_FALSE(conflictDetector->hasErrors(conflicts));
    EXPECT_FALSE(conflictDetector->hasWarnings(conflicts));
}

} // namespace
