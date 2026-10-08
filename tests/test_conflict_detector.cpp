#include <gtest/gtest.h>
#include "config/ConflictDetector.h"
#include "config/ComponentRegistry.h"
#include "config/ComponentBundle.h"
#include "config/TestConflict2.h"
#include <boost/json.hpp>
#include <cstdio>
#include <fstream>
#include <deque>
#include <list>
#include <type_traits>

using namespace config;
using namespace boost::json;

static void dbg(const char* msg) {
    (void)msg;
}

// 测试夹具
class ConflictDetectorTest : public ::testing::Test {
protected:
    void SetUp() override {
        dbg("=== SetUp start ===");
        registry_ = &ComponentRegistry::instance();
        dbg("=== instance() ok ===");
        registry_->registerDefaultTypes();
        dbg("=== registerDefaultTypes ok ===");
        detector_ = std::make_unique<ConflictDetector>(*registry_);
        dbg("=== detector_ created ok ===");
    }
    
    void TearDown() override {
        dbg("=== TearDown ===");
        detector_.reset();
    }
    
    // 创建测试组件
    ComponentConfig createComponent(ComponentType type, const std::string& tag = "") {
        ComponentConfig comp;
        comp.type = type;
        if (!tag.empty()) {
            comp.params["tag"] = boost::json::value(tag);
        }
        return comp;
    }
    
    // 创建测试组件（带 ID）
    ComponentConfig createComponentWithId(ComponentType type, const std::string& id, const std::string& tag = "") {
        ComponentConfig comp = createComponent(type, tag);
        comp.id = id;
        return comp;
    }
    
    // 创建路由组件（带规则）
    ComponentConfig createRoutingComponent(const std::vector<std::string>& inboundTags, const std::string& outboundTag) {
        ComponentConfig comp;
        comp.type = ComponentType::ROUTING;
        
        boost::json::array rules;
        boost::json::object rule;
        rule["type"] = "field";
        boost::json::array inTags;
        for (const auto& tag : inboundTags) {
            inTags.push_back(boost::json::value(tag));
        }
        rule["inboundTag"] = boost::json::value(inTags);
        rule["outboundTag"] = boost::json::value(outboundTag);
        rules.push_back(boost::json::value(rule));
        
        comp.params["rules"] = boost::json::value(rules);
        return comp;
    }
    
    std::unique_ptr<ConflictDetector> detector_;
    ComponentRegistry* registry_;
};

// 测试 1: 空组件列表
TEST_F(ConflictDetectorTest, DetectEmptyComponents) {
    std::vector<ComponentConfig> components;
    
    auto conflicts = detector_->detect(components);
    
    EXPECT_EQ(conflicts.size(), 0);
}

// 测试 1.5: 最小化 Conflict 创建测试
TEST_F(ConflictDetectorTest, TestConflictCreation) {
    Conflict conflict;
    conflict.type = ConflictType::SINGLETON_CONFLICT;
    conflict.component1 = "log";
    conflict.component2 = "log";
    conflict.message = "test message";
    conflict.index1 = 0;
    conflict.index2 = 1;
    conflict.sourceBundle1 = "";
    conflict.sourceBundle2 = "";
    conflict.isCrossBundle = false;
    conflict.severity = ConflictSeverity::ERROR;
    // conflict is destroyed when the test ends
    SUCCEED();
}

// 测试 1.5b: 空 vector
TEST(ConflictTest, TestEmptyConflictVector) {
    std::vector<Conflict> v;
    EXPECT_EQ(0u, v.size());
}

// 测试 1.5c: 单个元素 vector
TEST(ConflictTest, TestOneConflictVector) {
    std::vector<Conflict> v;
    Conflict c;
    v.push_back(c);
    EXPECT_EQ(1u, v.size());
}

TEST(ConflictTest, TestOneConflictVectorEmplace) {
    std::vector<Conflict> v;
    v.emplace_back();
    EXPECT_EQ(1u, v.size());
}

TEST(ConflictTest, TestOneConflictVectorWithField) {
    std::vector<Conflict> v;
    Conflict c;
    c.component1 = "test";
    v.push_back(c);
    EXPECT_EQ(1u, v.size());
    EXPECT_EQ("test", v[0].component1);
}

// 测试 1.5d: 同布局的局部结构体 vector
TEST(ConflictTest, TestLocalStructVector) {
    struct LocalConflict {
        ConflictType type;
        ConflictSeverity severity;
        std::string component1;
        std::string component2;
        std::string message;
        int index1;
        int index2;
        std::string sourceBundle1;
        std::string sourceBundle2;
        bool isCrossBundle;
    };
    
    std::vector<LocalConflict> v;
    LocalConflict c;
    c.type = ConflictType::SINGLETON_CONFLICT;
    c.severity = ConflictSeverity::ERROR;
    c.component1 = "test1";
    c.component2 = "test2";
    c.message = "test message";
    c.index1 = 0;
    c.index2 = 1;
    c.sourceBundle1 = "";
    c.sourceBundle2 = "";
    c.isCrossBundle = false;
    v.push_back(c);
    EXPECT_EQ(1u, v.size());
    EXPECT_EQ("test1", v[0].component1);
    EXPECT_EQ("test2", v[0].component2);
    EXPECT_EQ("test message", v[0].message);
}

TEST(ConflictTest, TestConflictSize) {
    EXPECT_EQ(sizeof(Conflict), sizeof(Conflict));
    EXPECT_EQ(alignof(Conflict), alignof(Conflict));
    std::printf("Conflict size: %zu, align: %zu\n", sizeof(Conflict), alignof(Conflict));
    
    // Check type traits
    std::printf("Conflict is_trivially_copyable: %d\n", (int)std::is_trivially_copyable<Conflict>::value);
    std::printf("Conflict is_standard_layout: %d\n", (int)std::is_standard_layout<Conflict>::value);
    std::printf("Conflict is_aggregate: %d\n", (int)std::is_aggregate<Conflict>::value);
    std::printf("Conflict is_trivially_default_constructible: %d\n", (int)std::is_trivially_default_constructible<Conflict>::value);
    std::printf("Conflict is_trivially_copy_assignable: %d\n", (int)std::is_trivially_copy_assignable<Conflict>::value);
    std::printf("Conflict is_trivially_destructible: %d\n", (int)std::is_trivially_destructible<Conflict>::value);
    SUCCEED();
}

TEST(ConflictTest, TestLocalStructSize) {
    struct LocalConflict {
        ConflictType type;
        ConflictSeverity severity;
        std::string component1;
        std::string component2;
        std::string message;
        int index1;
        int index2;
        std::string sourceBundle1;
        std::string sourceBundle2;
        bool isCrossBundle;
    };
    
    std::printf("LocalConflict size: %zu, align: %zu\n", sizeof(LocalConflict), alignof(LocalConflict));
    std::printf("LocalConflict is_trivially_copyable: %d\n", (int)std::is_trivially_copyable<LocalConflict>::value);
    std::printf("LocalConflict is_standard_layout: %d\n", (int)std::is_standard_layout<LocalConflict>::value);
    std::printf("LocalConflict is_aggregate: %d\n", (int)std::is_aggregate<LocalConflict>::value);
    std::printf("LocalConflict is_trivially_default_constructible: %d\n", (int)std::is_trivially_default_constructible<LocalConflict>::value);
    std::printf("LocalConflict is_trivially_copy_assignable: %d\n", (int)std::is_trivially_copy_assignable<LocalConflict>::value);
    std::printf("LocalConflict is_trivially_destructible: %d\n", (int)std::is_trivially_destructible<LocalConflict>::value);
    SUCCEED();
}

// Global struct with same layout as Conflict for testing
struct GlobalConflictStruct {
    ConflictSeverity severity;
    ConflictType type;
    std::string component1;
    std::string component2;
    std::string message;
    int index1;
    int index2;
    std::string sourceBundle1;
    std::string sourceBundle2;
    bool isCrossBundle;
};

TEST(ConflictTest, TestGlobalStructVector) {
    std::vector<GlobalConflictStruct> v;
    GlobalConflictStruct c;
    c.severity = ConflictSeverity::ERROR;
    c.type = ConflictType::SINGLETON_CONFLICT;
    c.component1 = "test1";
    c.component2 = "test2";
    v.push_back(c);
    EXPECT_EQ(1u, v.size());
    EXPECT_EQ("test1", v[0].component1);
}

TEST(ConflictTest, TestConflictResize) {
    std::vector<config::Conflict> v;
    v.resize(1);
    EXPECT_EQ(1u, v.size());
}

TEST(ConflictTest, TestConflictDeque) {
    std::deque<config::Conflict> dq;
    dq.push_back(config::Conflict());
    EXPECT_EQ(1u, dq.size());
}

TEST(ConflictTest, TestConflictList) {
    std::list<config::Conflict> lst;
    lst.push_back(config::Conflict());
    EXPECT_EQ(1u, lst.size());
}

TEST(ConflictTest, TestConflict2Vector) {
    std::vector<config::TestConflict2> v;
    config::TestConflict2 c;
    c.component1 = "test";
    v.push_back(c);
    EXPECT_EQ(1u, v.size());
    EXPECT_EQ("test", v[0].component1);
}

// 测试 1.6: 简单字符串结构体测试
struct TestStringStruct {
    std::string s1;
    std::string s2;
};

TEST_F(ConflictDetectorTest, TestStringStruct) {
    TestStringStruct ts;
    ts.s1 = "test1";
    ts.s2 = "test2";
    SUCCEED();
}

// 测试 1.7: 带字符串的枚举结构体测试
struct TestEnumStringStruct {
    int severity;
    int type;
    std::string component1;
    std::string component2;
    std::string message;
    int index1;
    int index2;
    std::string sourceBundle1;
    std::string sourceBundle2;
    bool isCrossBundle;
};

TEST_F(ConflictDetectorTest, TestEnumStringStruct) {
    TestEnumStringStruct tes;
    tes.severity = 0;
    tes.type = 0;
    tes.component1 = "log";
    tes.component2 = "log";
    tes.message = "test message";
    tes.index1 = 0;
    tes.index2 = 1;
    tes.sourceBundle1 = "";
    tes.sourceBundle2 = "";
    tes.isCrossBundle = false;
    SUCCEED();
}

// 测试 1.8: 带 enum class 的结构体测试
enum class TestSeverityClass {
    ERROR,
    WARN
};

enum class TestTypeClass {
    TYPE1,
    TYPE2
};

struct TestEnumClassStruct {
    TestSeverityClass severity;
    TestTypeClass type;
    std::string component1;
    std::string component2;
    std::string message;
    int index1;
    int index2;
    std::string sourceBundle1;
    std::string sourceBundle2;
    bool isCrossBundle;
};

TEST_F(ConflictDetectorTest, TestEnumClassStruct) {
    TestEnumClassStruct tes;
    tes.severity = TestSeverityClass::ERROR;
    tes.type = TestTypeClass::TYPE1;
    tes.component1 = "log";
    tes.component2 = "log";
    tes.message = "test message";
    tes.index1 = 0;
    tes.index2 = 1;
    tes.sourceBundle1 = "";
    tes.sourceBundle2 = "";
    tes.isCrossBundle = false;
    SUCCEED();
}

// 测试 1.9: 使用 ConflictSeverity/ConflictType 的结构体测试
struct TestConflictEnumStruct {
    ConflictSeverity severity;
    ConflictType type;
    std::string component1;
    std::string component2;
    std::string message;
    int index1;
    int index2;
    std::string sourceBundle1;
    std::string sourceBundle2;
    bool isCrossBundle;
};

TEST_F(ConflictDetectorTest, TestConflictEnumStruct) {
    TestConflictEnumStruct tes;
    tes.severity = ConflictSeverity::ERROR;
    tes.type = ConflictType::SINGLETON_CONFLICT;
    tes.component1 = "log";
    tes.component2 = "log";
    tes.message = "test message";
    tes.index1 = 0;
    tes.index2 = 1;
    tes.sourceBundle1 = "";
    tes.sourceBundle2 = "";
    tes.isCrossBundle = false;
    SUCCEED();
}

// 测试 2: 单个单例组件（无冲突）
TEST_F(ConflictDetectorTest, DetectSingleSingleton) {
    ComponentConfig comp = createComponent(ComponentType::LOG);
    std::vector<ComponentConfig> components = {comp};
    
    auto conflicts = detector_->detect(components);
    
    EXPECT_EQ(conflicts.size(), 0);
}

// 测试 3: 单例组件重复（同包，ERROR）
TEST_F(ConflictDetectorTest, DetectSingletonDuplicate) {
    dbg("=== Test 3 start ===");
    ComponentConfig comp1 = createComponent(ComponentType::LOG);
    dbg("=== comp1 created ===");
    ComponentConfig comp2 = createComponent(ComponentType::LOG);
    dbg("=== comp2 created ===");
    std::vector<ComponentConfig> components = {comp1, comp2};
    dbg("=== components vector created ===");
    
    dbg("=== calling detect ===");
    auto conflicts = detector_->detect(components);
    dbg("=== detect returned ===");
    
    EXPECT_GE(conflicts.size(), 1);
    EXPECT_EQ(conflicts[0].type, ConflictType::SINGLETON_CONFLICT);
    EXPECT_EQ(conflicts[0].severity, ConflictSeverity::ERROR);
    EXPECT_FALSE(conflicts[0].isCrossBundle);
    
    dbg("=== checking conflict strings ===");
    dbg(("component1: " + conflicts[0].component1).c_str());
    dbg(("component2: " + conflicts[0].component2).c_str());
    dbg(("message: " + conflicts[0].message).c_str());
    dbg(("sourceBundle1: " + conflicts[0].sourceBundle1).c_str());
    dbg(("sourceBundle2: " + conflicts[0].sourceBundle2).c_str());
    
    dbg("=== clearing conflicts ===");
    conflicts.clear();
    dbg("=== conflicts cleared ===");
    dbg("=== Test 3 end ===");
}

// 测试 4: 单例组件跨包覆盖（WARN）
TEST_F(ConflictDetectorTest, DetectSingletonCrossBundle) {
    dbg("=== Test 4 start ===");
    ComponentSource source1;
    source1.component = createComponent(ComponentType::LOG);
    source1.sourceBundleId = "base_bundle_1";
    source1.sourceIndex = 0;
    
    ComponentSource source2;
    source2.component = createComponent(ComponentType::LOG);
    source2.sourceBundleId = "test_bundle_1";
    source2.sourceIndex = 0;
    
    std::vector<ComponentSource> sources = {source1, source2};
    dbg("=== sources created ===");
    
    dbg("=== calling detect(sources) ===");
    auto conflicts = detector_->detect(sources);
    dbg("=== detect returned ===");
    
    EXPECT_GE(conflicts.size(), 1);
    EXPECT_EQ(conflicts[0].type, ConflictType::SINGLETON_CONFLICT);
    EXPECT_EQ(conflicts[0].severity, ConflictSeverity::WARN);
    EXPECT_TRUE(conflicts[0].isCrossBundle);
    dbg("=== Test 4 end ===");
}

// 测试 5: 数组组件 tag 重复（ERROR）
TEST_F(ConflictDetectorTest, DetectArrayTagDuplicate) {
    ComponentConfig comp1 = createComponent(ComponentType::INBOUND, "socks-in");
    ComponentConfig comp2 = createComponent(ComponentType::INBOUND, "socks-in");
    std::vector<ComponentConfig> components = {comp1, comp2};
    
    auto conflicts = detector_->detect(components);
    
    EXPECT_GE(conflicts.size(), 1);
    EXPECT_EQ(conflicts[0].type, ConflictType::TAG_CONFLICT);
    EXPECT_EQ(conflicts[0].severity, ConflictSeverity::ERROR);
}

// 测试 6: 数组组件不同 tag（无冲突）
TEST_F(ConflictDetectorTest, DetectArrayDifferentTags) {
    ComponentConfig comp1 = createComponent(ComponentType::INBOUND, "socks-in");
    ComponentConfig comp2 = createComponent(ComponentType::INBOUND, "dokodemo-in");
    std::vector<ComponentConfig> components = {comp1, comp2};
    
    auto conflicts = detector_->detect(components);
    
    EXPECT_EQ(conflicts.size(), 0);
}

// 测试 7: 依赖缺失 - routing 引用不存在的 inbound
TEST_F(ConflictDetectorTest, DetectMissingInbound) {
    ComponentConfig inComp = createComponent(ComponentType::INBOUND, "socks-in");
    ComponentConfig routeComp = createRoutingComponent({"nonexistent-inbound"}, "proxy");
    std::vector<ComponentConfig> components = {inComp, routeComp};
    
    auto conflicts = detector_->detectDependencies(components);
    
    EXPECT_GE(conflicts.size(), 1);
    EXPECT_EQ(conflicts[0].type, ConflictType::DEPENDENCY_MISSING);
    EXPECT_EQ(conflicts[0].severity, ConflictSeverity::WARN);
    EXPECT_EQ(conflicts[0].component1, "routing");
    EXPECT_EQ(conflicts[0].component2, "inbound");
}

// 测试 8: 依赖缺失 - routing 引用不存在的 outbound
TEST_F(ConflictDetectorTest, DetectMissingOutbound) {
    ComponentConfig outComp = createComponent(ComponentType::OUTBOUND, "proxy");
    ComponentConfig routeComp = createRoutingComponent({}, "nonexistent-outbound");
    std::vector<ComponentConfig> components = {outComp, routeComp};
    
    auto conflicts = detector_->detectDependencies(components);
    
    EXPECT_GE(conflicts.size(), 1);
    EXPECT_EQ(conflicts[0].type, ConflictType::DEPENDENCY_MISSING);
    EXPECT_EQ(conflicts[0].severity, ConflictSeverity::WARN);
    EXPECT_EQ(conflicts[0].component1, "routing");
    EXPECT_EQ(conflicts[0].component2, "outbound");
}

// 测试 9: 依赖完整（无冲突）
TEST_F(ConflictDetectorTest, DetectCompleteDependencies) {
    ComponentConfig inComp = createComponent(ComponentType::INBOUND, "socks-in");
    ComponentConfig outComp = createComponent(ComponentType::OUTBOUND, "proxy");
    ComponentConfig routeComp = createRoutingComponent({"socks-in"}, "proxy");
    std::vector<ComponentConfig> components = {inComp, outComp, routeComp};
    
    auto conflicts = detector_->detectDependencies(components);
    
    EXPECT_EQ(conflicts.size(), 0);
}

// 测试 10: direct 和 block 是特殊标签（不检测）
TEST_F(ConflictDetectorTest, DetectSpecialTags) {
    ComponentConfig inComp = createComponent(ComponentType::INBOUND, "socks-in");
    ComponentConfig routeComp = createRoutingComponent({"socks-in"}, "direct");
    std::vector<ComponentConfig> components = {inComp, routeComp};
    
    auto conflicts = detector_->detectDependencies(components);
    
    // direct 不需要检查
    EXPECT_EQ(conflicts.size(), 0);
}

// 测试 11: 冲突处理策略 - FAIL_FAST
TEST_F(ConflictDetectorTest, HandleConflictsFailFast) {
    std::vector<Conflict> conflicts;
    
    Conflict errorConflict;
    errorConflict.severity = ConflictSeverity::ERROR;
    errorConflict.type = ConflictType::SINGLETON_CONFLICT;
    conflicts.push_back(errorConflict);
    
    EXPECT_FALSE(detector_->handleConflicts(conflicts, ConflictStrategy::FAIL_FAST));
}

// 测试 12: 冲突处理策略 - WARN_CONTINUE
TEST_F(ConflictDetectorTest, HandleConflictsWarnContinue) {
    std::vector<Conflict> conflicts;
    
    Conflict errorConflict;
    errorConflict.severity = ConflictSeverity::ERROR;
    errorConflict.type = ConflictType::SINGLETON_CONFLICT;
    conflicts.push_back(errorConflict);
    
    EXPECT_TRUE(detector_->handleConflicts(conflicts, ConflictStrategy::WARN_CONTINUE));
}

// 测试 13: 冲突处理策略 - IGNORE
TEST_F(ConflictDetectorTest, HandleConflictsIgnore) {
    std::vector<Conflict> conflicts;
    
    Conflict errorConflict;
    errorConflict.severity = ConflictSeverity::ERROR;
    errorConflict.type = ConflictType::SINGLETON_CONFLICT;
    conflicts.push_back(errorConflict);
    
    EXPECT_TRUE(detector_->handleConflicts(conflicts, ConflictStrategy::IGNORE));
}

// 测试 14: 无冲突（所有策略都通过）
TEST_F(ConflictDetectorTest, HandleNoConflicts) {
    std::vector<Conflict> conflicts;
    
    EXPECT_TRUE(detector_->handleConflicts(conflicts, ConflictStrategy::FAIL_FAST));
    EXPECT_TRUE(detector_->handleConflicts(conflicts, ConflictStrategy::WARN_CONTINUE));
    EXPECT_TRUE(detector_->handleConflicts(conflicts, ConflictStrategy::IGNORE));
}

// 测试 15: hasErrors
TEST_F(ConflictDetectorTest, HasErrors) {
    std::vector<Conflict> conflicts;
    
    EXPECT_FALSE(detector_->hasErrors(conflicts));
    
    Conflict errorConflict;
    errorConflict.severity = ConflictSeverity::ERROR;
    conflicts.push_back(errorConflict);
    
    EXPECT_TRUE(detector_->hasErrors(conflicts));
}

// 测试 16: hasWarnings
TEST_F(ConflictDetectorTest, HasWarnings) {
    std::vector<Conflict> conflicts;
    
    EXPECT_FALSE(detector_->hasWarnings(conflicts));
    
    Conflict warnConflict;
    warnConflict.severity = ConflictSeverity::WARN;
    conflicts.push_back(warnConflict);
    
    EXPECT_TRUE(detector_->hasWarnings(conflicts));
}

// 测试 17: 复杂场景 - 多类型冲突
TEST_F(ConflictDetectorTest, DetectComplexConflicts) {
    // log 重复（单例）
    ComponentConfig log1 = createComponent(ComponentType::LOG);
    ComponentConfig log2 = createComponent(ComponentType::LOG);
    
    // inbound tag 重复
    ComponentConfig in1 = createComponent(ComponentType::INBOUND, "socks-in");
    ComponentConfig in2 = createComponent(ComponentType::INBOUND, "socks-in");
    
    // routing 引用不存在的 outbound
    ComponentConfig route = createRoutingComponent({"socks-in"}, "nonexistent");
    
    std::vector<ComponentConfig> components = {log1, log2, in1, in2, route};
    
    auto conflicts = detector_->detect(components);
    
    // 应该有单例冲突和 tag 冲突
    EXPECT_GE(conflicts.size(), 2);
    
    bool hasSingletonConflict = false;
    bool hasTagConflict = false;
    
    for (const auto& conflict : conflicts) {
        if (conflict.type == ConflictType::SINGLETON_CONFLICT) {
            hasSingletonConflict = true;
        }
        if (conflict.type == ConflictType::TAG_CONFLICT) {
            hasTagConflict = true;
        }
    }
    
    EXPECT_TRUE(hasSingletonConflict);
    EXPECT_TRUE(hasTagConflict);
}

// 测试 18: 跨包单例覆盖（允许）
TEST_F(ConflictDetectorTest, CrossBundleSingletonOverride) {
    ComponentSource source1;
    source1.component = createComponent(ComponentType::DNS);
    source1.sourceBundleId = "base_bundle_1";
    source1.sourceIndex = 0;
    
    ComponentSource source2;
    source2.component = createComponent(ComponentType::DNS);
    source2.sourceBundleId = "test_bundle_1";
    source2.sourceIndex = 0;
    
    std::vector<ComponentSource> sources = {source1, source2};
    
    auto conflicts = detector_->detect(sources);
    
    EXPECT_EQ(conflicts.size(), 1);
    EXPECT_EQ(conflicts[0].type, ConflictType::SINGLETON_CONFLICT);
    EXPECT_EQ(conflicts[0].severity, ConflictSeverity::WARN);
    EXPECT_TRUE(conflicts[0].isCrossBundle);
    
    // 允许继续
    EXPECT_TRUE(detector_->handleConflicts(conflicts, ConflictStrategy::FAIL_FAST));
}

// 测试 19: 同包单例重复（错误）
TEST_F(ConflictDetectorTest, SameBundleSingletonDuplicate) {
    ComponentSource source1;
    source1.component = createComponent(ComponentType::API);
    source1.sourceBundleId = "test_bundle_1";
    source1.sourceIndex = 0;
    
    ComponentSource source2;
    source2.component = createComponent(ComponentType::API);
    source2.sourceBundleId = "test_bundle_1";
    source2.sourceIndex = 1;
    
    std::vector<ComponentSource> sources = {source1, source2};
    
    auto conflicts = detector_->detect(sources);
    
    EXPECT_EQ(conflicts.size(), 1);
    EXPECT_EQ(conflicts[0].type, ConflictType::SINGLETON_CONFLICT);
    EXPECT_EQ(conflicts[0].severity, ConflictSeverity::ERROR);
    EXPECT_FALSE(conflicts[0].isCrossBundle);
    
    // 不允许继续
    EXPECT_FALSE(detector_->handleConflicts(conflicts, ConflictStrategy::FAIL_FAST));
}

// 测试 20: 多个 inbound/outbound 无冲突
TEST_F(ConflictDetectorTest, MultipleArrayComponents) {
    ComponentConfig in1 = createComponent(ComponentType::INBOUND, "socks-in");
    ComponentConfig in2 = createComponent(ComponentType::INBOUND, "dokodemo-in");
    ComponentConfig out1 = createComponent(ComponentType::OUTBOUND, "proxy");
    ComponentConfig out2 = createComponent(ComponentType::OUTBOUND, "direct");
    
    std::vector<ComponentConfig> components = {in1, in2, out1, out2};
    
    auto conflicts = detector_->detect(components);
    
    EXPECT_EQ(conflicts.size(), 0);
}
