// test_phase11_cross_bundle.cpp - Phase 11: Cross-bundle conflict tests
// Tests ConflictDetector with ComponentSource (cross-bundle vs same-bundle detection)
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <boost/json.hpp>
#include <memory>
#include <string>
#include <vector>

#include "config/ComponentBundleDAO.h"
#include "config/ComponentRegistry.h"
#include "config/ConflictDetector.h"

using config::ComponentBundle;
using config::ComponentBundleDAO;
using config::ComponentConfig;
using config::ComponentRegistry;
using config::ComponentSource;
using config::ComponentType;
using config::Conflict;
using config::ConflictDetector;
using config::ConflictSeverity;
using config::ConflictType;
using config::ConflictStrategy;
using config::CoreType;
using config::TemplateType;

namespace {

// -----------------------------------------------------------------
// Fixture
// -----------------------------------------------------------------
class CrossBundleConflictTest : public testing::Test {
protected:
    sqlite3* db = nullptr;
    std::unique_ptr<ComponentBundleDAO> dao;
    std::unique_ptr<ConflictDetector> detector;

    void SetUp() override {
        ASSERT_EQ(SQLITE_OK, sqlite3_open(":memory:", &db));
        dao.reset(new ComponentBundleDAO(db));
        ASSERT_TRUE(dao->createTableIfNotExists());
        ComponentRegistry::instance().registerDefaultTypes();
        detector.reset(new ConflictDetector(ComponentRegistry::instance()));
    }

    void TearDown() override {
        detector.reset();
        dao.reset();
        sqlite3_close(db);
    }

    ComponentConfig makeComp(ComponentType type, const std::string& key, const std::string& value)
    {
        ComponentConfig comp;
        comp.type = type;
        comp.params[key] = boost::json::value(value);
        return comp;
    }

    ComponentSource makeSource(ComponentConfig comp, const std::string& bundleId, int index)
    {
        ComponentSource src;
        src.component = comp;
        src.sourceBundleId = bundleId;
        src.sourceIndex = index;
        return src;
    }
};

// -----------------------------------------------------------------
// 1. Same singleton from different bundles -> WARN (cross-package override)
// -----------------------------------------------------------------
TEST_F(CrossBundleConflictTest, SingletonDifferentBundlesWarning) {
    std::vector<ComponentSource> sources;
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "log1.txt"), "bundle_a", 0));
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "log2.txt"), "bundle_b", 0));

    std::vector<Conflict> conflicts = detector->detect(sources);
    ASSERT_FALSE(conflicts.empty());

    bool foundSingletonWarn = false;
    for (const auto& c : conflicts) {
        if (c.type == ConflictType::SINGLETON_CONFLICT) {
            EXPECT_EQ(c.severity, ConflictSeverity::WARN);
            EXPECT_TRUE(c.isCrossBundle);
            EXPECT_EQ(c.sourceBundle1, "bundle_a");
            EXPECT_EQ(c.sourceBundle2, "bundle_b");
            foundSingletonWarn = true;
            break;
        }
    }
    EXPECT_TRUE(foundSingletonWarn);
}

// -----------------------------------------------------------------
// 2. Same singleton from same bundle -> ERROR (same-package duplicate)
// -----------------------------------------------------------------
TEST_F(CrossBundleConflictTest, SingletonSameBundleError) {
    std::vector<ComponentSource> sources;
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "log1.txt"), "bundle_a", 0));
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "log2.txt"), "bundle_a", 1));

    std::vector<Conflict> conflicts = detector->detect(sources);
    ASSERT_FALSE(conflicts.empty());

    bool foundSingletonError = false;
    for (const auto& c : conflicts) {
        if (c.type == ConflictType::SINGLETON_CONFLICT) {
            EXPECT_EQ(c.severity, ConflictSeverity::ERROR);
            EXPECT_FALSE(c.isCrossBundle);
            EXPECT_EQ(c.sourceBundle1, "bundle_a");
            foundSingletonError = true;
            break;
        }
    }
    EXPECT_TRUE(foundSingletonError);
}

// -----------------------------------------------------------------
// 3. Same array tag from same bundle -> ERROR
// -----------------------------------------------------------------
TEST_F(CrossBundleConflictTest, ArrayTagSameBundleError) {
    std::vector<ComponentSource> sources;
    sources.push_back(makeSource(
        makeComp(ComponentType::INBOUND, "tag", "socks"), "bundle_a", 0));
    sources.push_back(makeSource(
        makeComp(ComponentType::INBOUND, "tag", "socks"), "bundle_a", 1));

    std::vector<Conflict> conflicts = detector->detect(sources);
    ASSERT_FALSE(conflicts.empty());

    bool foundTagError = false;
    for (const auto& c : conflicts) {
        if (c.type == ConflictType::TAG_CONFLICT) {
            EXPECT_EQ(c.severity, ConflictSeverity::ERROR);
            EXPECT_FALSE(c.isCrossBundle);
            foundTagError = true;
            break;
        }
    }
    EXPECT_TRUE(foundTagError);
}

// -----------------------------------------------------------------
// 4. Same array tag from different bundles -> WARN
// -----------------------------------------------------------------
TEST_F(CrossBundleConflictTest, ArrayTagDifferentBundlesWarning) {
    std::vector<ComponentSource> sources;
    sources.push_back(makeSource(
        makeComp(ComponentType::INBOUND, "tag", "socks"), "bundle_a", 0));
    sources.push_back(makeSource(
        makeComp(ComponentType::INBOUND, "tag", "socks"), "bundle_b", 0));

    std::vector<Conflict> conflicts = detector->detect(sources);
    ASSERT_FALSE(conflicts.empty());

    bool foundTagWarn = false;
    for (const auto& c : conflicts) {
        if (c.type == ConflictType::TAG_CONFLICT) {
            EXPECT_EQ(c.severity, ConflictSeverity::WARN);
            EXPECT_TRUE(c.isCrossBundle);
            foundTagWarn = true;
            break;
        }
    }
    EXPECT_TRUE(foundTagWarn);
}

// -----------------------------------------------------------------
// 5. Different array tags (no conflict) from same bundle
// -----------------------------------------------------------------
TEST_F(CrossBundleConflictTest, DifferentTagsNoConflict) {
    std::vector<ComponentSource> sources;
    sources.push_back(makeSource(
        makeComp(ComponentType::INBOUND, "tag", "socks"), "bundle_a", 0));
    sources.push_back(makeSource(
        makeComp(ComponentType::INBOUND, "tag", "trojan"), "bundle_a", 1));

    std::vector<Conflict> conflicts = detector->detect(sources);
    EXPECT_TRUE(conflicts.empty());
    EXPECT_FALSE(detector->hasErrors(conflicts));
}

// -----------------------------------------------------------------
// 6. Mixed: one error + one warning from different bundles
// -----------------------------------------------------------------
TEST_F(CrossBundleConflictTest, MixedErrorsAndWarnings) {
    std::vector<ComponentSource> sources;
    // Same singleton, different bundles -> WARN
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "log1.txt"), "bundle_a", 0));
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "log2.txt"), "bundle_b", 0));
    // Same singleton, same bundle -> ERROR
    sources.push_back(makeSource(
        makeComp(ComponentType::API, "tag", "api1"), "bundle_a", 1));
    sources.push_back(makeSource(
        makeComp(ComponentType::API, "tag", "api2"), "bundle_a", 2));

    std::vector<Conflict> conflicts = detector->detect(sources);
    ASSERT_GE(conflicts.size(), 2u);

    bool foundWarn = false;
    bool foundError = false;
    for (const auto& c : conflicts) {
        if (c.severity == ConflictSeverity::WARN) foundWarn = true;
        if (c.severity == ConflictSeverity::ERROR) foundError = true;
    }
    EXPECT_TRUE(foundWarn);
    EXPECT_TRUE(foundError);
    EXPECT_TRUE(detector->hasErrors(conflicts));
    EXPECT_TRUE(detector->hasWarnings(conflicts));
}

// -----------------------------------------------------------------
// 7. ConflictStrategy::FAIL_FAST with errors returns false
// -----------------------------------------------------------------
TEST_F(CrossBundleConflictTest, FailFastWithErrors) {
    std::vector<ComponentSource> sources;
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "log1.txt"), "bundle_a", 0));
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "log2.txt"), "bundle_a", 1));

    std::vector<Conflict> conflicts = detector->detect(sources);
    EXPECT_FALSE(detector->handleConflicts(conflicts, ConflictStrategy::FAIL_FAST));
}

// -----------------------------------------------------------------
// 8. ConflictStrategy::WARN_CONTINUE with errors returns true (logs but continues)
// -----------------------------------------------------------------
TEST_F(CrossBundleConflictTest, WarnContinueWithErrors) {
    std::vector<ComponentSource> sources;
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "log1.txt"), "bundle_a", 0));
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "log2.txt"), "bundle_a", 1));

    std::vector<Conflict> conflicts = detector->detect(sources);
    EXPECT_TRUE(detector->handleConflicts(conflicts, ConflictStrategy::WARN_CONTINUE));
}

// -----------------------------------------------------------------
// 9. ConflictStrategy::IGNORE always returns true
// -----------------------------------------------------------------
TEST_F(CrossBundleConflictTest, IgnoreAlwaysSucceeds) {
    std::vector<ComponentSource> sources;
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "log1.txt"), "bundle_a", 0));
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "log2.txt"), "bundle_a", 1));

    std::vector<Conflict> conflicts = detector->detect(sources);
    EXPECT_TRUE(detector->handleConflicts(conflicts, ConflictStrategy::IGNORE));
}

// -----------------------------------------------------------------
// 10. Three-bundle chain: A->B override, B->C override, A->C cross
// -----------------------------------------------------------------
TEST_F(CrossBundleConflictTest, ThreeBundleOverrideChain) {
    std::vector<ComponentSource> sources;
    // LOG from A, B, C (all different bundles) -> 2 WARN conflicts
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "a.log"), "A", 0));
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "b.log"), "B", 0));
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "c.log"), "C", 0));

    std::vector<Conflict> conflicts = detector->detect(sources);
    // Should detect cross-bundle conflicts (A-B, A-C, B-C or just A-C depending on impl)
    ASSERT_FALSE(conflicts.empty());
    for (const auto& c : conflicts) {
        EXPECT_EQ(c.type, ConflictType::SINGLETON_CONFLICT);
        EXPECT_EQ(c.severity, ConflictSeverity::WARN);
        EXPECT_TRUE(c.isCrossBundle);
    }
}

// -----------------------------------------------------------------
// 11. Multiple different types across bundles -> no conflict
// -----------------------------------------------------------------
TEST_F(CrossBundleConflictTest, DifferentTypesNoConflict) {
    std::vector<ComponentSource> sources;
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "log.txt"), "bundle_a", 0));
    sources.push_back(makeSource(
        makeComp(ComponentType::API, "tag", "api"), "bundle_b", 0));
    sources.push_back(makeSource(
        makeComp(ComponentType::DNS, "tag", "dns"), "bundle_c", 0));

    std::vector<Conflict> conflicts = detector->detect(sources);
    EXPECT_TRUE(conflicts.empty());
}

// -----------------------------------------------------------------
// 12. Singleton base + array override from different bundle
// -----------------------------------------------------------------
TEST_F(CrossBundleConflictTest, SingletonBaseArrayOverride) {
    std::vector<ComponentSource> sources;
    // LOG singleton from base bundle
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "base.log"), "base", 0));
    // INBOUND array from test bundle (different type, no conflict)
    sources.push_back(makeSource(
        makeComp(ComponentType::INBOUND, "tag", "socks"), "test", 0));
    // LOG singleton from test bundle (cross-bundle override -> WARN)
    sources.push_back(makeSource(
        makeComp(ComponentType::LOG, "access", "test.log"), "test", 1));

    std::vector<Conflict> conflicts = detector->detect(sources);
    ASSERT_FALSE(conflicts.empty());
    for (const auto& c : conflicts) {
        if (c.type == ConflictType::SINGLETON_CONFLICT) {
            EXPECT_EQ(c.severity, ConflictSeverity::WARN);
            EXPECT_TRUE(c.isCrossBundle);
            EXPECT_EQ(c.sourceBundle1, "base");
            EXPECT_EQ(c.sourceBundle2, "test");
        }
    }
}

} // namespace
