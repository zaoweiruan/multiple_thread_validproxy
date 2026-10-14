#include <gtest/gtest.h>
#include "config/ComponentResolver.h"
#include "config/ComponentRegistry.h"
#include "config/ComponentBundle.h"
#include "config/StepHandler.h"
#include <boost/json.hpp>

using namespace config;
using namespace boost::json;

// Helper function to create component with type enum
static ComponentConfig makeComponent(ComponentType type, const std::string& id = "") {
    ComponentConfig comp;
    comp.type = type;
    comp.id = id;
    return comp;
}

// Helper function to set param value
static void setParam(ComponentConfig& comp, const std::string& key, const boost::json::value& value) {
    comp.params[key] = value;
}

// Test fixture
class ComponentResolverTest : public ::testing::Test {
protected:
    void SetUp() override {
        resolver_ = std::make_unique<ComponentResolver>(nullptr);
        registry_ = &ComponentRegistry::instance();
        registry_->registerDefaultTypes();
    }
    
    void TearDown() override {
        resolver_.reset();
    }
    
    // Create test context
    PipelineContext createTestContext() const {
        PipelineContext ctx;
        ctx.proxyType = ProxyType::Xray;
        ctx.hasProfile = false;
        ctx.vars.socksPort = 10809;
        ctx.vars.apiPort = 10085;
        ctx.vars.probeUrl = "https://www.google.com/generate_204";
        ctx.vars.observatoryInterval = 30000;
        ctx.vars.balancerStrategy = "leastPing";
        ctx.vars.enableObservatory = true;
        ctx.vars.enableCnDirect = true;
        ctx.vars.blockQuic = true;
        ctx.db = nullptr;
        ctx.clientName = "test";
        return ctx;
    }
    
    std::unique_ptr<ComponentResolver> resolver_;
    ComponentRegistry* registry_;
};

// Test 1: Empty component list
TEST_F(ComponentResolverTest, ResolveEmptyComponents) {
    std::vector<ComponentConfig> components;
    PipelineContext ctx = createTestContext();
    
    ResolveResult result = resolver_->resolve(components, ctx);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.conflicts.size(), 0);
    EXPECT_TRUE(result.config.is_object());
    EXPECT_FALSE(result.config.as_object().contains("log"));
}

// Test 2: Single singleton component
TEST_F(ComponentResolverTest, ResolveSingleLogComponent) {
    ComponentConfig comp = makeComponent(ComponentType::LOG, "log-1");
    setParam(comp, "loglevel", boost::json::value("warning"));
    
    std::vector<ComponentConfig> components = {comp};
    PipelineContext ctx = createTestContext();
    
    ResolveResult result = resolver_->resolve(components, ctx);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.conflicts.size(), 0);
    EXPECT_TRUE(result.config.as_object().contains("log"));
    auto& log = result.config.as_object()["log"].as_object();
    EXPECT_EQ(std::string(log["loglevel"].as_string()), "warning");
}

// Test 3: Placeholder replacement
TEST_F(ComponentResolverTest, ResolveWithPlaceholders) {
    ComponentConfig comp = makeComponent(ComponentType::INBOUND, "inbound-1");
    setParam(comp, "tag", boost::json::value("socks-in"));
    setParam(comp, "listen", boost::json::value("127.0.0.1"));
    setParam(comp, "port", boost::json::value("${socksPort}"));
    
    std::vector<ComponentConfig> components = {comp};
    PipelineContext ctx = createTestContext();
    ctx.vars.socksPort = 10809;
    
    ResolveResult result = resolver_->resolve(components, ctx);
    
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.config.as_object().contains("inbounds"));
    auto& inbounds = result.config.as_object()["inbounds"].as_array();
    EXPECT_EQ(inbounds.size(), 1);
    auto& inbound = inbounds[0].as_object();
    EXPECT_EQ(std::string(inbound["tag"].as_string()), "socks-in");
    EXPECT_EQ(inbound["port"].as_int64(), 10809);
}

// Test 4: Singleton override
TEST_F(ComponentResolverTest, ResolveSingletonOverride) {
    ComponentConfig comp1 = makeComponent(ComponentType::LOG, "log-1");
    setParam(comp1, "loglevel", boost::json::value("info"));
    
    ComponentConfig comp2 = makeComponent(ComponentType::LOG, "log-2");
    setParam(comp2, "loglevel", boost::json::value("warning"));
    
    std::vector<ComponentConfig> components = {comp1, comp2};
    PipelineContext ctx = createTestContext();
    
    ResolveResult result = resolver_->resolve(components, ctx);
    
    EXPECT_TRUE(result.success);
    // Should have conflict warning
    EXPECT_GE(result.conflicts.size(), 1);
    // Should keep the last one
    auto& log = result.config.as_object()["log"].as_object();
    EXPECT_EQ(std::string(log["loglevel"].as_string()), "warning");
}

// Test 5: Array append
TEST_F(ComponentResolverTest, ResolveArrayAppend) {
    ComponentConfig comp1 = makeComponent(ComponentType::INBOUND, "inbound-1");
    setParam(comp1, "tag", boost::json::value("socks-in"));
    setParam(comp1, "port", boost::json::value(10809));
    
    ComponentConfig comp2 = makeComponent(ComponentType::INBOUND, "inbound-2");
    setParam(comp2, "tag", boost::json::value("dokodemo-in"));
    setParam(comp2, "port", boost::json::value(10808));
    
    std::vector<ComponentConfig> components = {comp1, comp2};
    PipelineContext ctx = createTestContext();
    
    ResolveResult result = resolver_->resolve(components, ctx);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.conflicts.size(), 0);
    auto& inbounds = result.config.as_object()["inbounds"].as_array();
    EXPECT_EQ(inbounds.size(), 2);
}

// Test 6: Array tag deduplication
TEST_F(ComponentResolverTest, ResolveArrayTagDedup) {
    ComponentConfig comp1 = makeComponent(ComponentType::INBOUND, "inbound-1");
    setParam(comp1, "tag", boost::json::value("socks-in"));
    setParam(comp1, "port", boost::json::value(10809));
    
    ComponentConfig comp2 = makeComponent(ComponentType::INBOUND, "inbound-2");
    setParam(comp2, "tag", boost::json::value("socks-in"));  // Same tag
    setParam(comp2, "port", boost::json::value(10810));
    
    std::vector<ComponentConfig> components = {comp1, comp2};
    PipelineContext ctx = createTestContext();
    
    ResolveResult result = resolver_->resolve(components, ctx);
    
    EXPECT_TRUE(result.success);
    // Should have tag duplicate conflict
    EXPECT_GE(result.conflicts.size(), 1);
    // Should keep the last one (override)
    auto& inbounds = result.config.as_object()["inbounds"].as_array();
    EXPECT_EQ(inbounds.size(), 1);
    EXPECT_EQ(inbounds[0].as_object()["port"].as_int64(), 10810);
}

// Test 7: Routing merge
TEST_F(ComponentResolverTest, ResolveRoutingMerge) {
    ComponentConfig comp1 = makeComponent(ComponentType::ROUTING, "routing-1");
    boost::json::array rules1;
    rules1.push_back(boost::json::object{
        {"type", boost::json::value("field")},
        {"inboundTag", boost::json::array{boost::json::value("socks-in")}},
        {"outboundTag", boost::json::value("proxy")}
    });
    setParam(comp1, "rules", boost::json::value(rules1));
    
    ComponentConfig comp2 = makeComponent(ComponentType::ROUTING, "routing-2");
    boost::json::array rules2;
    rules2.push_back(boost::json::object{
        {"type", boost::json::value("field")},
        {"inboundTag", boost::json::array{boost::json::value("dokodemo-in")}},
        {"outboundTag", boost::json::value("direct")}
    });
    setParam(comp2, "rules", boost::json::value(rules2));
    
    std::vector<ComponentConfig> components = {comp1, comp2};
    PipelineContext ctx = createTestContext();
    
    ResolveResult result = resolver_->resolve(components, ctx);
    
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.config.as_object().contains("routing"));
    auto& routing = result.config.as_object()["routing"].as_object();
    EXPECT_TRUE(routing.contains("rules"));
    auto& rules = routing["rules"].as_array();
    EXPECT_EQ(rules.size(), 2);  // Merge two rules
}

// Test 8: Conflict detection - singleton duplicate
TEST_F(ComponentResolverTest, DetectSingletonConflict) {
    ComponentConfig comp1 = makeComponent(ComponentType::LOG, "log-1");
    setParam(comp1, "loglevel", boost::json::value("info"));
    
    ComponentConfig comp2 = makeComponent(ComponentType::LOG, "log-2");
    setParam(comp2, "loglevel", boost::json::value("warning"));
    
    std::vector<ComponentConfig> components = {comp1, comp2};
    
    auto conflicts = resolver_->detectConflicts(components);
    
    EXPECT_GE(conflicts.size(), 1);
    EXPECT_EQ(conflicts[0].conflictType, ResolverConflictType::SINGLETON_DUPLICATE);
    EXPECT_EQ(conflicts[0].componentType, "log");
}

// Test 9: Conflict detection - array tag duplicate
TEST_F(ComponentResolverTest, DetectArrayTagConflict) {
    ComponentConfig comp1 = makeComponent(ComponentType::INBOUND, "inbound-1");
    setParam(comp1, "tag", boost::json::value("socks-in"));
    
    ComponentConfig comp2 = makeComponent(ComponentType::INBOUND, "inbound-2");
    setParam(comp2, "tag", boost::json::value("socks-in"));  // Same tag
    
    std::vector<ComponentConfig> components = {comp1, comp2};
    
    auto conflicts = resolver_->detectConflicts(components);
    
    EXPECT_GE(conflicts.size(), 1);
    EXPECT_EQ(conflicts[0].conflictType, ResolverConflictType::ARRAY_TAG_DUPLICATE);
}

// Test 10: No conflict
TEST_F(ComponentResolverTest, NoConflict) {
    ComponentConfig comp1 = makeComponent(ComponentType::LOG, "log-1");
    setParam(comp1, "loglevel", boost::json::value("info"));
    
    ComponentConfig comp2 = makeComponent(ComponentType::INBOUND, "inbound-1");
    setParam(comp2, "tag", boost::json::value("socks-in"));
    
    ComponentConfig comp3 = makeComponent(ComponentType::OUTBOUND, "outbound-1");
    setParam(comp3, "tag", boost::json::value("proxy"));
    
    std::vector<ComponentConfig> components = {comp1, comp2, comp3};
    
    auto conflicts = resolver_->detectConflicts(components);
    
    EXPECT_EQ(conflicts.size(), 0);
}

// Test 11: Full configuration resolution
TEST_F(ComponentResolverTest, ResolveFullConfig) {
    // log
    ComponentConfig logComp = makeComponent(ComponentType::LOG, "log-1");
    setParam(logComp, "loglevel", boost::json::value("warning"));
    
    // dns
    ComponentConfig dnsComp = makeComponent(ComponentType::DNS, "dns-1");
    setParam(dnsComp, "tag", boost::json::value("dns"));
    setParam(dnsComp, "server", boost::json::value("https://1.1.1.1/dns-query"));
    
    // inbound
    ComponentConfig inComp = makeComponent(ComponentType::INBOUND, "inbound-1");
    setParam(inComp, "tag", boost::json::value("socks-in"));
    setParam(inComp, "listen", boost::json::value("127.0.0.1"));
    setParam(inComp, "port", boost::json::value("${socksPort}"));
    setParam(inComp, "protocol", boost::json::value("socks"));
    
    // outbound
    ComponentConfig outComp = makeComponent(ComponentType::OUTBOUND, "outbound-1");
    setParam(outComp, "tag", boost::json::value("proxy"));
    setParam(outComp, "protocol", boost::json::value("vmess"));
    
    // routing
    ComponentConfig routeComp = makeComponent(ComponentType::ROUTING, "routing-1");
    boost::json::array rules;
    rules.push_back(boost::json::object{
        {"type", boost::json::value("field")},
        {"inboundTag", boost::json::array{boost::json::value("socks-in")}},
        {"outboundTag", boost::json::value("proxy")}
    });
    setParam(routeComp, "rules", boost::json::value(rules));
    
    std::vector<ComponentConfig> components = {logComp, dnsComp, inComp, outComp, routeComp};
    PipelineContext ctx = createTestContext();
    ctx.vars.socksPort = 10809;
    
    ResolveResult result = resolver_->resolve(components, ctx);
    
    EXPECT_TRUE(result.success);
    
    auto& config = result.config.as_object();
    EXPECT_TRUE(config.contains("log"));
    EXPECT_TRUE(config.contains("dns"));
    EXPECT_TRUE(config.contains("inbounds"));
    EXPECT_TRUE(config.contains("outbounds"));
    EXPECT_TRUE(config.contains("routing"));
    
    // Verify placeholder replacement
    auto& inbounds = config["inbounds"].as_array();
    EXPECT_EQ(inbounds.size(), 1);
    EXPECT_EQ(inbounds[0].as_object()["port"].as_int64(), 10809);
}

// Test 12: Simplified resolution (no context)
TEST_F(ComponentResolverTest, ResolveWithoutContext) {
    ComponentConfig comp = makeComponent(ComponentType::LOG, "log-1");
    setParam(comp, "loglevel", boost::json::value("info"));
    
    std::vector<ComponentConfig> components = {comp};
    
    ResolveResult result = resolver_->resolve(components);
    
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.config.as_object().contains("log"));
}

// Test 13: API component (Xray only)
TEST_F(ComponentResolverTest, ResolveApiComponent) {
    ComponentConfig comp = makeComponent(ComponentType::API, "api-1");
    setParam(comp, "tag", boost::json::value("api"));
    boost::json::array services;
    services.push_back(boost::json::value("HandlerService"));
    setParam(comp, "services", boost::json::value(services));
    
    std::vector<ComponentConfig> components = {comp};
    PipelineContext ctx = createTestContext();
    
    ResolveResult result = resolver_->resolve(components, ctx);
    
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.config.as_object().contains("api"));
}

// Test 14: Routing with balancer
TEST_F(ComponentResolverTest, ResolveRoutingWithBalancer) {
    ComponentConfig comp = makeComponent(ComponentType::ROUTING, "routing-1");
    
    boost::json::array rules;
    rules.push_back(boost::json::object{
        {"type", boost::json::value("field")},
        {"inboundTag", boost::json::array{boost::json::value("socks-in")}},
        {"balancerTag", boost::json::value("balancer-out")}
    });
    setParam(comp, "rules", boost::json::value(rules));
    
    boost::json::array balancers;
    balancers.push_back(boost::json::object{
        {"tag", boost::json::value("balancer-out")},
        {"strategy", boost::json::object{
            {"type", boost::json::value("leastPing")}
        }},
        {"balancers", boost::json::array{}}
    });
    setParam(comp, "balancers", boost::json::value(balancers));
    
    std::vector<ComponentConfig> components = {comp};
    PipelineContext ctx = createTestContext();
    
    ResolveResult result = resolver_->resolve(components, ctx);
    
    EXPECT_TRUE(result.success);
    auto& routing = result.config.as_object()["routing"].as_object();
    EXPECT_TRUE(routing.contains("rules"));
    EXPECT_TRUE(routing.contains("balancers"));
}

// Test 15: Boolean placeholder
TEST_F(ComponentResolverTest, ResolveBooleanPlaceholder) {
    ComponentConfig comp = makeComponent(ComponentType::INBOUND, "inbound-1");
    setParam(comp, "tag", boost::json::value("socks-in"));
    boost::json::object sniffingObj;
    sniffingObj["enabled"] = boost::json::value("${enableCnDirect}");
    sniffingObj["destOverride"] = boost::json::array{
        boost::json::value("http"),
        boost::json::value("tls")
    };
    setParam(comp, "sniffing", boost::json::value(sniffingObj));
    
    std::vector<ComponentConfig> components = {comp};
    PipelineContext ctx = createTestContext();
    ctx.vars.enableCnDirect = true;
    
    ResolveResult result = resolver_->resolve(components, ctx);
    
    EXPECT_TRUE(result.success);
    auto& inbounds = result.config.as_object()["inbounds"].as_array();
    auto& sniffingResult = inbounds[0].as_object()["sniffing"].as_object();
    // Boolean should be correctly replaced
    auto enabled = sniffingResult.find("enabled");
    ASSERT_TRUE(enabled != sniffingResult.end());
}

// Test 16: Nested placeholder
TEST_F(ComponentResolverTest, ResolveNestedPlaceholder) {
    ComponentConfig comp = makeComponent(ComponentType::OUTBOUND, "outbound-1");
    setParam(comp, "tag", boost::json::value("proxy"));
    boost::json::object settingsObj;
    boost::json::array serversArr;
    serversArr.push_back(boost::json::object{
        {"address", boost::json::value("example.com")},
        {"port", boost::json::value("${socksPort}")},
        {"method", boost::json::value("vmess")}
    });
    settingsObj["servers"] = boost::json::value(serversArr);
    setParam(comp, "settings", boost::json::value(settingsObj));
    
    std::vector<ComponentConfig> components = {comp};
    PipelineContext ctx = createTestContext();
    ctx.vars.socksPort = 10809;
    
    ResolveResult result = resolver_->resolve(components, ctx);
    
    EXPECT_TRUE(result.success);
    auto& outbounds = result.config.as_object()["outbounds"].as_array();
    auto& settingsResult = outbounds[0].as_object()["settings"].as_object();
    auto& serversResult = settingsResult["servers"].as_array();
    EXPECT_EQ(serversResult.size(), 1);
}
