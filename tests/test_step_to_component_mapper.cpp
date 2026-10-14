#include <gtest/gtest.h>
#include "config/StepToComponentMapper.h"
#include "config/PipelineConfig.h"
#include "config/ComponentBundle.h"
#include <boost/json.hpp>
#include <set>

using namespace config;

namespace {

StepConfig makeStep(const std::string& type) {
    StepConfig step;
    step.type = type;
    return step;
}

void setParam(StepConfig& step, const std::string& key, const std::string& value) {
    step.params[key] = boost::json::value(value);
}

void setParam(StepConfig& step, const std::string& key, int value) {
    step.params[key] = boost::json::value(static_cast<std::int64_t>(value));
}

} // namespace

// ============================================================
// StepToComponentMapper Tests
// ============================================================

class StepToComponentMapperTest : public testing::Test {
protected:
    StepToComponentMapper mapper;
};

// 映射表: 10 种步骤类型齐全
TEST_F(StepToComponentMapperTest, MappingTableHasAllTenTypes) {
    const std::vector<StepToComponentMapping>& mappings = StepToComponentMapper::mappings();
    ASSERT_EQ(mappings.size(), 10u);

    std::set<std::string> types;
    for (const StepToComponentMapping& m : mappings) {
        types.insert(m.stepType);
    }
    EXPECT_NE(types.find("replace_variables"), types.end());
    EXPECT_NE(types.find("add_outbound"), types.end());
    EXPECT_NE(types.find("merge_routing"), types.end());
    EXPECT_NE(types.find("add_inbound"), types.end());
    EXPECT_NE(types.find("add_observatory"), types.end());
    EXPECT_NE(types.find("add_balancer"), types.end());
    EXPECT_NE(types.find("add_stats"), types.end());
    EXPECT_NE(types.find("add_policy"), types.end());
    EXPECT_NE(types.find("add_api"), types.end());
    EXPECT_NE(types.find("modify_field"), types.end());
}

// add_outbound -> OUTBOUND（CONVERTED），params 透传
TEST_F(StepToComponentMapperTest, ConvertAddOutbound) {
    StepConfig step = makeStep("add_outbound");
    setParam(step, "tag", "proxy");
    setParam(step, "protocol", "vmess");

    ComponentConfig out;
    ConvertOutcome outcome = mapper.convert(step, out);

    EXPECT_EQ(outcome, ConvertOutcome::CONVERTED);
    EXPECT_EQ(out.type, ComponentType::OUTBOUND);
    EXPECT_EQ(boost::json::value_to<std::string>(out.params.at("protocol")), "vmess");
}

// merge_routing -> ROUTING（CONVERTED），inbound/outbound/balancer 透传
TEST_F(StepToComponentMapperTest, ConvertMergeRouting) {
    StepConfig step = makeStep("merge_routing");
    setParam(step, "inboundTag", "socks");
    setParam(step, "outboundTag", "proxy");
    setParam(step, "balancerTag", "balancer-out");

    ComponentConfig out;
    ConvertOutcome outcome = mapper.convert(step, out);

    EXPECT_EQ(outcome, ConvertOutcome::CONVERTED);
    EXPECT_EQ(out.type, ComponentType::ROUTING);
    EXPECT_EQ(boost::json::value_to<std::string>(out.params.at("inboundTag")), "socks");
    EXPECT_EQ(boost::json::value_to<std::string>(out.params.at("outboundTag")), "proxy");
}

// add_inbound -> INBOUND（CONVERTED），port 数值透传
TEST_F(StepToComponentMapperTest, ConvertAddInbound) {
    StepConfig step = makeStep("add_inbound");
    setParam(step, "tag", "socks");
    setParam(step, "protocol", "socks");
    setParam(step, "port", 10808);

    ComponentConfig out;
    ConvertOutcome outcome = mapper.convert(step, out);

    EXPECT_EQ(outcome, ConvertOutcome::CONVERTED);
    EXPECT_EQ(out.type, ComponentType::INBOUND);
    EXPECT_EQ(out.params.at("port").as_int64(), 10808);
}

// add_observatory -> OBSERVATORY（CONVERTED）
TEST_F(StepToComponentMapperTest, ConvertAddObservatory) {
    StepConfig step = makeStep("add_observatory");
    setParam(step, "subjectSelector", "proxy");
    setParam(step, "probeURL", "https://www.google.com/generate_204");

    ComponentConfig out;
    ConvertOutcome outcome = mapper.convert(step, out);

    EXPECT_EQ(outcome, ConvertOutcome::CONVERTED);
    EXPECT_EQ(out.type, ComponentType::OBSERVATORY);
}

// add_balancer -> ROUTING（CONVERTED），balancer 包装进 balancers 数组
TEST_F(StepToComponentMapperTest, ConvertAddBalancer) {
    StepConfig step = makeStep("add_balancer");
    setParam(step, "tag", "balancer-out");
    setParam(step, "selector", "proxy");
    setParam(step, "strategy", "leastPing");

    ComponentConfig out;
    ConvertOutcome outcome = mapper.convert(step, out);

    EXPECT_EQ(outcome, ConvertOutcome::CONVERTED);
    EXPECT_EQ(out.type, ComponentType::ROUTING);
    ASSERT_TRUE(out.params.find("balancers") != out.params.end());
    ASSERT_TRUE(out.params.at("balancers").is_array());
    ASSERT_EQ(out.params.at("balancers").as_array().size(), 1u);
    const boost::json::object& balancer = out.params.at("balancers").as_array()[0].as_object();
    EXPECT_EQ(boost::json::value_to<std::string>(balancer.at("tag")), "balancer-out");
    EXPECT_EQ(boost::json::value_to<std::string>(balancer.at("selector")), "proxy");
}

// add_stats -> STATS（CONVERTED）
TEST_F(StepToComponentMapperTest, ConvertAddStats) {
    StepConfig step = makeStep("add_stats");

    ComponentConfig out;
    ConvertOutcome outcome = mapper.convert(step, out);

    EXPECT_EQ(outcome, ConvertOutcome::CONVERTED);
    EXPECT_EQ(out.type, ComponentType::STATS);
}

// add_policy -> POLICY（CONVERTED）
TEST_F(StepToComponentMapperTest, ConvertAddPolicy) {
    StepConfig step = makeStep("add_policy");
    setParam(step, "levels", "system");

    ComponentConfig out;
    ConvertOutcome outcome = mapper.convert(step, out);

    EXPECT_EQ(outcome, ConvertOutcome::CONVERTED);
    EXPECT_EQ(out.type, ComponentType::POLICY);
}

// add_api -> API（CONVERTED）
TEST_F(StepToComponentMapperTest, ConvertAddApi) {
    StepConfig step = makeStep("add_api");
    setParam(step, "tag", "api");
    setParam(step, "listen", "127.0.0.1:8080");

    ComponentConfig out;
    ConvertOutcome outcome = mapper.convert(step, out);

    EXPECT_EQ(outcome, ConvertOutcome::CONVERTED);
    EXPECT_EQ(out.type, ComponentType::API);
    EXPECT_EQ(boost::json::value_to<std::string>(out.params.at("listen")), "127.0.0.1:8080");
}

// replace_variables -> IGNORED（不产组件，全局替换）
TEST_F(StepToComponentMapperTest, ConvertReplaceVariablesIgnored) {
    StepConfig step = makeStep("replace_variables");

    ComponentConfig out;
    ConvertOutcome outcome = mapper.convert(step, out);

    EXPECT_EQ(outcome, ConvertOutcome::IGNORED);
}

// modify_field -> UNSUPPORTED
TEST_F(StepToComponentMapperTest, ConvertModifyFieldUnsupported) {
    StepConfig step = makeStep("modify_field");
    setParam(step, "path", "routing.domainStrategy");
    setParam(step, "value", "IPIfNonMatch");

    ComponentConfig out;
    ConvertOutcome outcome = mapper.convert(step, out);

    EXPECT_EQ(outcome, ConvertOutcome::UNSUPPORTED);
}

// 未登记步骤类型 -> UNSUPPORTED
TEST_F(StepToComponentMapperTest, ConvertUnknownStepUnsupported) {
    StepConfig step = makeStep("unknown_step");

    ComponentConfig out;
    ConvertOutcome outcome = mapper.convert(step, out);

    EXPECT_EQ(outcome, ConvertOutcome::UNSUPPORTED);
}

// convertAll: 顺序保持 + unsupported 收集
TEST_F(StepToComponentMapperTest, ConvertAllKeepsOrderAndCollectsUnsupported) {
    std::vector<StepConfig> steps;
    steps.push_back(makeStep("add_inbound"));
    steps.push_back(makeStep("replace_variables"));
    steps.push_back(makeStep("add_outbound"));
    steps.push_back(makeStep("modify_field"));

    std::vector<std::string> unsupported;
    std::vector<ComponentConfig> components = mapper.convertAll(steps, unsupported);

    ASSERT_EQ(components.size(), 2u);
    EXPECT_EQ(components[0].type, ComponentType::INBOUND);
    EXPECT_EQ(components[1].type, ComponentType::OUTBOUND);
    ASSERT_EQ(unsupported.size(), 1u);
    EXPECT_EQ(unsupported[0], "modify_field");
}

// convertAll: 空输入返回空
TEST_F(StepToComponentMapperTest, ConvertAllEmptyReturnsEmpty) {
    std::vector<StepConfig> steps;
    std::vector<std::string> unsupported;
    std::vector<ComponentConfig> components = mapper.convertAll(steps, unsupported);

    EXPECT_TRUE(components.empty());
    EXPECT_TRUE(unsupported.empty());
}

// canConvertAll: 全部可转 -> true
TEST_F(StepToComponentMapperTest, CanConvertAllConvertible) {
    std::vector<StepConfig> steps;
    steps.push_back(makeStep("add_inbound"));
    steps.push_back(makeStep("add_outbound"));

    EXPECT_TRUE(mapper.canConvertAll(steps));
}

// canConvertAll: 含 modify_field -> false
TEST_F(StepToComponentMapperTest, CanConvertAllWithUnsupported) {
    std::vector<StepConfig> steps;
    steps.push_back(makeStep("add_inbound"));
    steps.push_back(makeStep("modify_field"));

    EXPECT_FALSE(mapper.canConvertAll(steps));
}

// canConvertAll: 空输入 -> true
TEST_F(StepToComponentMapperTest, CanConvertAllEmpty) {
    std::vector<StepConfig> steps;
    EXPECT_TRUE(mapper.canConvertAll(steps));
}

// isSupported: 各类型
TEST_F(StepToComponentMapperTest, IsSupported) {
    EXPECT_TRUE(mapper.isSupported("add_outbound"));
    EXPECT_TRUE(mapper.isSupported("replace_variables"));
    EXPECT_FALSE(mapper.isSupported("modify_field"));
    EXPECT_FALSE(mapper.isSupported("not_registered"));
}