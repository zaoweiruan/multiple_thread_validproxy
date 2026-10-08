#include <gtest/gtest.h>
#include "config/ComponentRegistry.h"
#include "config/ComponentBundle.h"

using namespace config;

class ComponentRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry_ = &ComponentRegistry::instance();
        registry_->registerDefaultTypes();
    }
    
    void TearDown() override {
        registry_->clear();
    }
    
    ComponentRegistry* registry_;
};

// Test: ComponentType enum has 15 types
TEST_F(ComponentRegistryTest, ComponentTypeCount) {
    auto types = registry_->getAllTypes();
    EXPECT_EQ(types.size(), 15);
}

// Test: All types are registered after registerDefaultTypes
TEST_F(ComponentRegistryTest, AllTypesRegistered) {
    EXPECT_TRUE(registry_->isRegistered(ComponentType::ENV));
    EXPECT_TRUE(registry_->isRegistered(ComponentType::LOG));
    EXPECT_TRUE(registry_->isRegistered(ComponentType::API));
    EXPECT_TRUE(registry_->isRegistered(ComponentType::DNS));
    EXPECT_TRUE(registry_->isRegistered(ComponentType::ROUTING));
    EXPECT_TRUE(registry_->isRegistered(ComponentType::POLICY));
    EXPECT_TRUE(registry_->isRegistered(ComponentType::INBOUND));
    EXPECT_TRUE(registry_->isRegistered(ComponentType::OUTBOUND));
    EXPECT_TRUE(registry_->isRegistered(ComponentType::STATS));
    EXPECT_TRUE(registry_->isRegistered(ComponentType::FAKEDNS));
    EXPECT_TRUE(registry_->isRegistered(ComponentType::METRICS));
    EXPECT_TRUE(registry_->isRegistered(ComponentType::OBSERVATORY));
    EXPECT_TRUE(registry_->isRegistered(ComponentType::BURST_OBSERVATORY));
    EXPECT_TRUE(registry_->isRegistered(ComponentType::GEODATA));
    EXPECT_TRUE(registry_->isRegistered(ComponentType::VERSION));
}

// Test: Registry count is 15
TEST_F(ComponentRegistryTest, RegistryCount) {
    EXPECT_EQ(registry_->count(), 15);
}

// Test: Singleton components
TEST_F(ComponentRegistryTest, SingletonComponents) {
    // Singletons
    EXPECT_TRUE(registry_->isSingleton(ComponentType::ENV));
    EXPECT_TRUE(registry_->isSingleton(ComponentType::LOG));
    EXPECT_TRUE(registry_->isSingleton(ComponentType::API));
    EXPECT_TRUE(registry_->isSingleton(ComponentType::DNS));
    EXPECT_TRUE(registry_->isSingleton(ComponentType::ROUTING));
    EXPECT_TRUE(registry_->isSingleton(ComponentType::POLICY));
    EXPECT_TRUE(registry_->isSingleton(ComponentType::STATS));
    EXPECT_TRUE(registry_->isSingleton(ComponentType::FAKEDNS));
    EXPECT_TRUE(registry_->isSingleton(ComponentType::METRICS));
    EXPECT_TRUE(registry_->isSingleton(ComponentType::OBSERVATORY));
    EXPECT_TRUE(registry_->isSingleton(ComponentType::BURST_OBSERVATORY));
    EXPECT_TRUE(registry_->isSingleton(ComponentType::GEODATA));
    EXPECT_TRUE(registry_->isSingleton(ComponentType::VERSION));
    
    // Non-singletons (arrays)
    EXPECT_FALSE(registry_->isSingleton(ComponentType::INBOUND));
    EXPECT_FALSE(registry_->isSingleton(ComponentType::OUTBOUND));
}

// Test: Combination strategies
TEST_F(ComponentRegistryTest, CombinationStrategies) {
    // Override strategy
    EXPECT_EQ(registry_->getStrategy(ComponentType::ENV), CombinationStrategy::OVERRIDE);
    EXPECT_EQ(registry_->getStrategy(ComponentType::LOG), CombinationStrategy::OVERRIDE);
    EXPECT_EQ(registry_->getStrategy(ComponentType::API), CombinationStrategy::OVERRIDE);
    EXPECT_EQ(registry_->getStrategy(ComponentType::DNS), CombinationStrategy::OVERRIDE);
    EXPECT_EQ(registry_->getStrategy(ComponentType::POLICY), CombinationStrategy::OVERRIDE);
    EXPECT_EQ(registry_->getStrategy(ComponentType::STATS), CombinationStrategy::OVERRIDE);
    EXPECT_EQ(registry_->getStrategy(ComponentType::FAKEDNS), CombinationStrategy::OVERRIDE);
    EXPECT_EQ(registry_->getStrategy(ComponentType::METRICS), CombinationStrategy::OVERRIDE);
    EXPECT_EQ(registry_->getStrategy(ComponentType::OBSERVATORY), CombinationStrategy::OVERRIDE);
    EXPECT_EQ(registry_->getStrategy(ComponentType::BURST_OBSERVATORY), CombinationStrategy::OVERRIDE);
    EXPECT_EQ(registry_->getStrategy(ComponentType::GEODATA), CombinationStrategy::OVERRIDE);
    EXPECT_EQ(registry_->getStrategy(ComponentType::VERSION), CombinationStrategy::OVERRIDE);
    
    // Merge strategy
    EXPECT_EQ(registry_->getStrategy(ComponentType::ROUTING), CombinationStrategy::MERGE);
    
    // Append strategy
    EXPECT_EQ(registry_->getStrategy(ComponentType::INBOUND), CombinationStrategy::APPEND);
    EXPECT_EQ(registry_->getStrategy(ComponentType::OUTBOUND), CombinationStrategy::APPEND);
}

// Test: Core type support - Xray supports all 15 types
TEST_F(ComponentRegistryTest, XraySupportsAllTypes) {
    for (const auto& type : registry_->getAllTypes()) {
        EXPECT_TRUE(registry_->supportsCoreType(type, CoreType::XRAY))
            << "Xray should support: " << registry_->typeToString(type);
    }
}

// Test: Core type support - SingBox supports only 5 types
TEST_F(ComponentRegistryTest, SingBoxSupportsLimitedTypes) {
    auto supported = registry_->getSupportedTypes(CoreType::SINGBOX);
    EXPECT_EQ(supported.size(), 5);
    
    // Supported types (from design doc: log, dns, routing, inbound, outbound)
    EXPECT_TRUE(registry_->supportsCoreType(ComponentType::LOG, CoreType::SINGBOX));
    EXPECT_TRUE(registry_->supportsCoreType(ComponentType::DNS, CoreType::SINGBOX));
    EXPECT_TRUE(registry_->supportsCoreType(ComponentType::ROUTING, CoreType::SINGBOX));
    EXPECT_TRUE(registry_->supportsCoreType(ComponentType::INBOUND, CoreType::SINGBOX));
    EXPECT_TRUE(registry_->supportsCoreType(ComponentType::OUTBOUND, CoreType::SINGBOX));
    
    // Not supported
    EXPECT_FALSE(registry_->supportsCoreType(ComponentType::ENV, CoreType::SINGBOX));
}

// Test: Core type support - SingBox does not support Xray-only types
TEST_F(ComponentRegistryTest, SingBoxRejectsXrayOnlyTypes) {
    EXPECT_FALSE(registry_->supportsCoreType(ComponentType::API, CoreType::SINGBOX));
    EXPECT_FALSE(registry_->supportsCoreType(ComponentType::POLICY, CoreType::SINGBOX));
    EXPECT_FALSE(registry_->supportsCoreType(ComponentType::STATS, CoreType::SINGBOX));
    EXPECT_FALSE(registry_->supportsCoreType(ComponentType::FAKEDNS, CoreType::SINGBOX));
    EXPECT_FALSE(registry_->supportsCoreType(ComponentType::METRICS, CoreType::SINGBOX));
    EXPECT_FALSE(registry_->supportsCoreType(ComponentType::OBSERVATORY, CoreType::SINGBOX));
    EXPECT_FALSE(registry_->supportsCoreType(ComponentType::BURST_OBSERVATORY, CoreType::SINGBOX));
    EXPECT_FALSE(registry_->supportsCoreType(ComponentType::GEODATA, CoreType::SINGBOX));
    EXPECT_FALSE(registry_->supportsCoreType(ComponentType::VERSION, CoreType::SINGBOX));
}

// Test: Type to section conversion
TEST_F(ComponentRegistryTest, TypeToSection) {
    EXPECT_EQ(registry_->typeToSection(ComponentType::ENV), "env");
    EXPECT_EQ(registry_->typeToSection(ComponentType::LOG), "log");
    EXPECT_EQ(registry_->typeToSection(ComponentType::API), "api");
    EXPECT_EQ(registry_->typeToSection(ComponentType::DNS), "dns");
    EXPECT_EQ(registry_->typeToSection(ComponentType::ROUTING), "routing");
    EXPECT_EQ(registry_->typeToSection(ComponentType::POLICY), "policy");
    EXPECT_EQ(registry_->typeToSection(ComponentType::INBOUND), "inbounds");
    EXPECT_EQ(registry_->typeToSection(ComponentType::OUTBOUND), "outbounds");
    EXPECT_EQ(registry_->typeToSection(ComponentType::STATS), "stats");
    EXPECT_EQ(registry_->typeToSection(ComponentType::FAKEDNS), "fakedns");
    EXPECT_EQ(registry_->typeToSection(ComponentType::METRICS), "metrics");
    EXPECT_EQ(registry_->typeToSection(ComponentType::OBSERVATORY), "observatory");
    EXPECT_EQ(registry_->typeToSection(ComponentType::BURST_OBSERVATORY), "burstObservatory");
    EXPECT_EQ(registry_->typeToSection(ComponentType::GEODATA), "geodata");
    EXPECT_EQ(registry_->typeToSection(ComponentType::VERSION), "version");
}

// Test: Type to string conversion
TEST_F(ComponentRegistryTest, TypeToString) {
    EXPECT_EQ(registry_->typeToString(ComponentType::ENV), "env");
    EXPECT_EQ(registry_->typeToString(ComponentType::LOG), "log");
    EXPECT_EQ(registry_->typeToString(ComponentType::ROUTING), "routing");
    EXPECT_EQ(registry_->typeToString(ComponentType::INBOUND), "inbounds");
    EXPECT_EQ(registry_->typeToString(ComponentType::OUTBOUND), "outbounds");
}

// Test: String to type conversion
TEST_F(ComponentRegistryTest, StringToType) {
    EXPECT_EQ(registry_->stringToType("env"), ComponentType::ENV);
    EXPECT_EQ(registry_->stringToType("log"), ComponentType::LOG);
    EXPECT_EQ(registry_->stringToType("api"), ComponentType::API);
    EXPECT_EQ(registry_->stringToType("dns"), ComponentType::DNS);
    EXPECT_EQ(registry_->stringToType("routing"), ComponentType::ROUTING);
    EXPECT_EQ(registry_->stringToType("policy"), ComponentType::POLICY);
    EXPECT_EQ(registry_->stringToType("inbounds"), ComponentType::INBOUND);
    EXPECT_EQ(registry_->stringToType("outbounds"), ComponentType::OUTBOUND);
    EXPECT_EQ(registry_->stringToType("stats"), ComponentType::STATS);
    EXPECT_EQ(registry_->stringToType("fakedns"), ComponentType::FAKEDNS);
    EXPECT_EQ(registry_->stringToType("metrics"), ComponentType::METRICS);
    EXPECT_EQ(registry_->stringToType("observatory"), ComponentType::OBSERVATORY);
    EXPECT_EQ(registry_->stringToType("burstObservatory"), ComponentType::BURST_OBSERVATORY);
    EXPECT_EQ(registry_->stringToType("geodata"), ComponentType::GEODATA);
    EXPECT_EQ(registry_->stringToType("version"), ComponentType::VERSION);
}

// Test: String to type throws for unknown type
TEST_F(ComponentRegistryTest, StringToTypeThrowsForUnknown) {
    EXPECT_THROW(registry_->stringToType("unknown"), std::invalid_argument);
    EXPECT_THROW(registry_->stringToType(""), std::invalid_argument);
}

// Test: Get metadata by section
TEST_F(ComponentRegistryTest, GetMetadataBySection) {
    const ComponentMetadata* meta = registry_->getMetadataBySection("env");
    EXPECT_NE(meta, nullptr);
    EXPECT_EQ(meta->type, ComponentType::ENV);
    EXPECT_EQ(meta->configSection, "env");
    
    meta = registry_->getMetadataBySection("log");
    EXPECT_NE(meta, nullptr);
    EXPECT_EQ(meta->type, ComponentType::LOG);
}

// Test: Get metadata by section returns nullptr for unknown
TEST_F(ComponentRegistryTest, GetMetadataBySectionReturnsNullForUnknown) {
    const ComponentMetadata* meta = registry_->getMetadataBySection("unknown");
    EXPECT_EQ(meta, nullptr);
}

// Test: Get metadata returns valid metadata
TEST_F(ComponentRegistryTest, GetMetadataReturnsValidMetadata) {
    const ComponentMetadata* meta = registry_->getMetadata(ComponentType::ROUTING);
    EXPECT_NE(meta, nullptr);
    EXPECT_EQ(meta->type, ComponentType::ROUTING);
    EXPECT_EQ(meta->configSection, "routing");
    EXPECT_EQ(meta->singboxSection, "route");
    EXPECT_TRUE(meta->isSingleton);
    EXPECT_EQ(meta->strategy, CombinationStrategy::MERGE);
    EXPECT_TRUE(meta->supportsXray);
    EXPECT_TRUE(meta->supportsSingBox);
}

// Test: Get metadata returns nullptr for unregistered type
TEST_F(ComponentRegistryTest, GetMetadataReturnsNullForUnregistered) {
    registry_->clear();
    const ComponentMetadata* meta = registry_->getMetadata(ComponentType::ENV);
    EXPECT_EQ(meta, nullptr);
}

// Test: Clear removes all registrations
TEST_F(ComponentRegistryTest, ClearRemovesAllRegistrations) {
    registry_->clear();
    EXPECT_EQ(registry_->count(), 0);
    EXPECT_FALSE(registry_->isRegistered(ComponentType::ENV));
}

// Test: Register custom type
TEST_F(ComponentRegistryTest, RegisterCustomType) {
    ComponentMetadata custom = {
        ComponentType::ENV,  // Reuse existing type for test
        "customEnv", "customEnv", true, CombinationStrategy::OVERRIDE,
        true, false, "Custom Env", "Custom environment configuration"
    };
    registry_->registerType(custom);
    
    const ComponentMetadata* meta = registry_->getMetadata(ComponentType::ENV);
    EXPECT_NE(meta, nullptr);
    EXPECT_EQ(meta->configSection, "customEnv");
    EXPECT_EQ(meta->displayName, "Custom Env");
}

// Test: ComponentMetadata has all required fields
TEST_F(ComponentRegistryTest, ComponentMetadataFields) {
    const ComponentMetadata* meta = registry_->getMetadata(ComponentType::ROUTING);
    EXPECT_NE(meta, nullptr);
    EXPECT_EQ(meta->type, ComponentType::ROUTING);
    EXPECT_EQ(meta->configSection, "routing");
    EXPECT_EQ(meta->singboxSection, "route");
    EXPECT_TRUE(meta->isSingleton);
    EXPECT_EQ(meta->strategy, CombinationStrategy::MERGE);
    EXPECT_TRUE(meta->supportsXray);
    EXPECT_TRUE(meta->supportsSingBox);
    EXPECT_EQ(meta->displayName, "Routing");
    EXPECT_FALSE(meta->description.empty());
}

// Test: Singleton components list
TEST_F(ComponentRegistryTest, SingletonComponentsList) {
    std::vector<ComponentType> singletons;
    for (const auto& type : registry_->getAllTypes()) {
        if (registry_->isSingleton(type)) {
            singletons.push_back(type);
        }
    }
    EXPECT_EQ(singletons.size(), 13);  // 15 total - 2 arrays (inbound, outbound)
}

// Test: Array components list
TEST_F(ComponentRegistryTest, ArrayComponentsList) {
    std::vector<ComponentType> arrays;
    for (const auto& type : registry_->getAllTypes()) {
        if (!registry_->isSingleton(type)) {
            arrays.push_back(type);
        }
    }
    EXPECT_EQ(arrays.size(), 2);  // inbound, outbound
    EXPECT_EQ(arrays[0], ComponentType::INBOUND);
    EXPECT_EQ(arrays[1], ComponentType::OUTBOUND);
}

// Test: Combination strategy counts
TEST_F(ComponentRegistryTest, CombinationStrategyCounts) {
    int overrideCount = 0;
    int mergeCount = 0;
    int appendCount = 0;
    
    for (const auto& type : registry_->getAllTypes()) {
        auto strategy = registry_->getStrategy(type);
        switch (strategy) {
            case CombinationStrategy::OVERRIDE: overrideCount++; break;
            case CombinationStrategy::MERGE: mergeCount++; break;
            case CombinationStrategy::APPEND: appendCount++; break;
        }
    }
    
    EXPECT_EQ(overrideCount, 12);
    EXPECT_EQ(mergeCount, 1);
    EXPECT_EQ(appendCount, 2);
}

// ---------------------------------------------------------------------------
// 生产路径回归：instance() 必须在未显式 registerDefaultTypes() 时也已注册，
// 否则 UI 组件列表全显示 unknown，且 mergeComponents 拿不到 APPEND/MERGE
// 策略导致 inbounds/outbounds 被误按 OVERRIDE 覆盖（bug 2026-10-08）。
// ---------------------------------------------------------------------------
TEST(ComponentRegistryAutoRegister, InstanceRegistersDefaultsWithoutExplicitCall) {
    ComponentRegistry& registry = ComponentRegistry::instance();
    // 不调用 registerDefaultTypes()，模拟生产代码首次访问
    EXPECT_EQ(registry.count(), 15);
    EXPECT_TRUE(registry.isRegistered(ComponentType::INBOUND));
    EXPECT_TRUE(registry.isRegistered(ComponentType::OUTBOUND));

    // 关键：数组组件策略必须为 APPEND（否则链合并会覆盖而非追加）
    EXPECT_EQ(registry.getStrategy(ComponentType::INBOUND),
              CombinationStrategy::APPEND);
    EXPECT_EQ(registry.getStrategy(ComponentType::OUTBOUND),
              CombinationStrategy::APPEND);
    EXPECT_EQ(registry.getStrategy(ComponentType::ROUTING),
              CombinationStrategy::MERGE);

    // UI 依赖的显示字段不可为空
    const ComponentMetadata* inMeta =
        registry.getMetadata(ComponentType::INBOUND);
    ASSERT_NE(inMeta, nullptr);
    EXPECT_FALSE(inMeta->displayName.empty());
    EXPECT_EQ(inMeta->configSection, "inbounds");
    EXPECT_EQ(registry.typeToString(ComponentType::OUTBOUND), "outbounds");
}
