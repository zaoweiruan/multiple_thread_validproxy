// test_phase11_performance.cpp - Phase 11: Performance tests
// Tests BundleChainResolver with large bundle combinations for timing
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
using config::ComponentType;
using config::ConflictDetector;
using config::CoreType;
using config::SchemaValidator;
using config::TemplateType;

namespace {

// -----------------------------------------------------------------
// Fixture
// -----------------------------------------------------------------
class PerformanceTest : public testing::Test {
protected:
    sqlite3* db = nullptr;
    std::unique_ptr<ComponentBundleDAO> dao;
    std::unique_ptr<BundleChainResolver> resolver;

    void SetUp() override {
        ASSERT_EQ(SQLITE_OK, sqlite3_open(":memory:", &db));
        dao.reset(new ComponentBundleDAO(db));
        ASSERT_TRUE(dao->createTableIfNotExists());
        ComponentRegistry::instance().registerDefaultTypes();
        resolver.reset(new BundleChainResolver(db));
    }

    void TearDown() override {
        resolver.reset();
        dao.reset();
        sqlite3_close(db);
    }

    // Create N bundles each with M components (mix of types)
    int createBundles(int count, int componentsPerBundle)
    {
        const ComponentType types[] = {
            ComponentType::LOG,
            ComponentType::API,
            ComponentType::DNS,
            ComponentType::INBOUND,
            ComponentType::OUTBOUND,
            ComponentType::STATS,
            ComponentType::ROUTING
        };
        int typeCount = 7;
        int created = 0;

        for (int i = 0; i < count; i++) {
            ComponentBundle bundle;
            bundle.id = "perf_bundle_" + std::to_string(i);
            bundle.name = bundle.id;
            bundle.coreType = CoreType::XRAY;
            bundle.version = 1;
            bundle.enabled = true;

            for (int j = 0; j < componentsPerBundle; j++) {
                ComponentConfig comp;
                comp.type = types[(i * componentsPerBundle + j) % typeCount];
                comp.id = "comp_" + std::to_string(i) + "_" + std::to_string(j);
                comp.params["key"] = boost::json::value(
                    std::string("value_" + std::to_string(i) + "_" + std::to_string(j)));
                bundle.components.push_back(comp);
            }

            if (dao->save(bundle)) {
                created++;
            }
        }
        return created;
    }

    // Build a BundleChain with N bundle IDs
    BundleChain buildChain(int count)
    {
        BundleChain chain;
        for (int i = 0; i < count; i++) {
            chain.bundleIds.push_back("perf_bundle_" + std::to_string(i));
        }
        chain.params["socksPort"] = boost::json::value(std::int64_t(10809));
        return chain;
    }
};

// -----------------------------------------------------------------
// 1. 10 bundles x 5 components: resolve in < 2s
// -----------------------------------------------------------------
TEST_F(PerformanceTest, SmallScaleResolution) {
    int n = createBundles(10, 5);
    ASSERT_EQ(n, 10);

    BundleChain chain = buildChain(10);

    auto start = std::chrono::high_resolution_clock::now();
    BundleChainResolveResult result = resolver->resolveBundleChain(chain);
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    ASSERT_TRUE(result.success);
    // 10 bundles should resolve quickly (well under 2 seconds)
    EXPECT_LT(elapsed, 2000);
    EXPECT_GT(result.components.size(), 0u);
}

// -----------------------------------------------------------------
// 2. 50 bundles x 5 components: resolve in < 5s
// -----------------------------------------------------------------
TEST_F(PerformanceTest, MediumScaleResolution) {
    int n = createBundles(50, 5);
    ASSERT_EQ(n, 50);

    BundleChain chain = buildChain(50);

    auto start = std::chrono::high_resolution_clock::now();
    BundleChainResolveResult result = resolver->resolveBundleChain(chain);
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    ASSERT_TRUE(result.success);
    // 50 bundles should resolve under 5 seconds
    EXPECT_LT(elapsed, 5000);
    EXPECT_GT(result.components.size(), 0u);
}

// -----------------------------------------------------------------
// 3. 100 bundles x 3 components: resolve in < 10s
// -----------------------------------------------------------------
TEST_F(PerformanceTest, LargeScaleResolution) {
    int n = createBundles(100, 3);
    ASSERT_EQ(n, 100);

    BundleChain chain = buildChain(100);

    auto start = std::chrono::high_resolution_clock::now();
    BundleChainResolveResult result = resolver->resolveBundleChain(chain);
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    ASSERT_TRUE(result.success);
    // 100 bundles should resolve under 10 seconds
    EXPECT_LT(elapsed, 10000);
    EXPECT_GT(result.components.size(), 0u);
}

// -----------------------------------------------------------------
// 4. 20 bundles x 10 components: component density test
// -----------------------------------------------------------------
TEST_F(PerformanceTest, HighDensityResolution) {
    int n = createBundles(20, 10);
    ASSERT_EQ(n, 20);

    BundleChain chain = buildChain(20);

    auto start = std::chrono::high_resolution_clock::now();
    BundleChainResolveResult result = resolver->resolveBundleChain(chain);
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    ASSERT_TRUE(result.success);
    EXPECT_LT(elapsed, 5000);
    // 20 bundles * 10 components = 200 components before dedup
    EXPECT_GE(result.components.size(), 10u);
}

// -----------------------------------------------------------------
// 5. SchemaValidator performance: validate 100 bundleChain JSONs
// -----------------------------------------------------------------
TEST_F(PerformanceTest, SchemaValidationPerformance) {
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 100; i++) {
        boost::json::object obj;
        boost::json::array ids;
        for (int j = 0; j < 5; j++) {
            ids.push_back(boost::json::value(
                std::string("bundle_" + std::to_string(i * 5 + j))));
        }
        obj["bundleIds"] = ids;
        obj["params"] = boost::json::object();

        std::string error;
        bool valid = SchemaValidator::validate(boost::json::value(obj), "bundleChain", error);
        EXPECT_TRUE(valid);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    // 100 validations should be fast (< 1 second)
    EXPECT_LT(elapsed, 1000);
}

// -----------------------------------------------------------------
// 6. Component bundle schema validation performance
// -----------------------------------------------------------------
TEST_F(PerformanceTest, ComponentBundleValidationPerformance) {
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 100; i++) {
        boost::json::object obj;
        obj["id"] = boost::json::value(std::string("bundle_" + std::to_string(i)));
        obj["coreType"] = boost::json::value(std::int64_t(2));
        obj["version"] = boost::json::value(std::int64_t(1));
        obj["enabled"] = boost::json::value(true);

        boost::json::array comps;
        for (int j = 0; j < 3; j++) {
            boost::json::object comp;
            comp["type"] = boost::json::value(std::int64_t(1));  // ComponentType::LOG（整数枚举）
            comps.push_back(comp);
        }
        obj["components"] = comps;

        std::string error;
        bool valid = SchemaValidator::validate(boost::json::value(obj), "componentBundle", error);
        EXPECT_TRUE(valid);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LT(elapsed, 1000);
}

// -----------------------------------------------------------------
// 7. resolveToConfig performance (includes ComponentResolver)
// -----------------------------------------------------------------
TEST_F(PerformanceTest, ResolveToConfigPerformance) {
    int n = createBundles(10, 5);
    ASSERT_EQ(n, 10);

    BundleChain chain = buildChain(10);

    auto start = std::chrono::high_resolution_clock::now();
    BundleChainResolveResult result = resolver->resolveToConfig(chain);
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    ASSERT_TRUE(result.success);
    EXPECT_LT(elapsed, 3000);
    ASSERT_FALSE(result.config.is_null());
}

// -----------------------------------------------------------------
// 8. Version check performance: getCurrentVersions for large chain
// -----------------------------------------------------------------
TEST_F(PerformanceTest, VersionCheckPerformance) {
    int n = createBundles(50, 5);
    ASSERT_EQ(n, 50);

    BundleChain chain = buildChain(50);

    auto start = std::chrono::high_resolution_clock::now();
    auto versions = resolver->getCurrentVersions(chain);
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_EQ(versions.size(), 50u);
    EXPECT_LT(elapsed, 2000);
}

// -----------------------------------------------------------------
// 9. Placeholder extraction performance
// -----------------------------------------------------------------
TEST_F(PerformanceTest, PlaceholderExtractionPerformance) {
    // Create bundles with placeholders
    for (int i = 0; i < 20; i++) {
        ComponentBundle bundle;
        bundle.id = "placeholder_bundle_" + std::to_string(i);
        bundle.coreType = CoreType::XRAY;
        bundle.version = 1;
        bundle.enabled = true;

        ComponentConfig comp;
        comp.type = ComponentType::LOG;
        comp.params["access"] = boost::json::value(
            std::string("${logDir}/log_" + std::to_string(i) + ".txt"));
        comp.params["level"] = boost::json::value(std::string("${logLevel}"));
        bundle.components.push_back(comp);

        dao->save(bundle);
    }

    BundleChain chain;
    for (int i = 0; i < 20; i++) {
        chain.bundleIds.push_back("placeholder_bundle_" + std::to_string(i));
    }

    auto start = std::chrono::high_resolution_clock::now();
    auto placeholders = resolver->extractPlaceholders(chain);
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_GE(placeholders.size(), 2u);  // at least logDir and logLevel
    EXPECT_LT(elapsed, 1000);
}

// -----------------------------------------------------------------
// 10. Memory efficiency: large chain doesn't leak (basic sanity)
// -----------------------------------------------------------------
TEST_F(PerformanceTest, LargeChainMemorySanity) {
    int n = createBundles(100, 5);
    ASSERT_EQ(n, 100);

    BundleChain chain = buildChain(100);

    // Run resolve multiple times to check for memory growth issues
    for (int iter = 0; iter < 3; iter++) {
        BundleChainResolveResult result = resolver->resolveBundleChain(chain);
        ASSERT_TRUE(result.success);
        // Components should be reasonable (dedup should reduce count)
        EXPECT_GT(result.components.size(), 0u);
        EXPECT_LT(result.components.size(), 500u);  // 100*5=500 max before dedup
    }
}

} // namespace
