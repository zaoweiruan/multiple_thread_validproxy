#include <gtest/gtest.h>

#include <cstddef>
#include <string>

#include <boost/system/error_code.hpp>
#include <boost/json.hpp>

#include "OutboundPayload.h"

namespace {

const char* const kFullConfig = R"json({
  "log": {"loglevel": "warning"},
  "inbounds": [
    {"tag": "socks", "port": 10808, "protocol": "socks", "settings": {"udp": true}}
  ],
  "outbounds": [
    {"tag": "proxy", "protocol": "hysteria", "settings": {"version": 2, "address": "example.com", "port": 443}},
    {"tag": "direct", "protocol": "freedom", "settings": {}},
    {"tag": "block", "protocol": "blackhole", "settings": {}}
  ],
  "routing": {"rules": [{"type": "field", "inboundTag": ["socks"], "outboundTag": "proxy"}]}
})json";

const char* const kSingleOutbound = R"json({
  "outbounds": [
    {"tag": "proxy", "protocol": "vless", "settings": {}}
  ]
})json";

} // namespace

TEST(OutboundPayloadTest, FullConfigIsReducedToFirstOutbound) {
    std::string normalized;
    ASSERT_TRUE(outbound_payload::normalizeForAdo(kFullConfig, normalized));

    boost::system::error_code ec;
    boost::json::value parsed = boost::json::parse(normalized, ec);
    ASSERT_FALSE(ec);
    ASSERT_TRUE(parsed.is_object());

    boost::json::object const& root = parsed.get_object();
    EXPECT_EQ(root.size(), static_cast<std::size_t>(1));

    boost::json::value const* outbounds = root.if_contains("outbounds");
    ASSERT_NE(outbounds, nullptr);
    ASSERT_TRUE(outbounds->is_array());

    boost::json::array const& list = outbounds->get_array();
    ASSERT_EQ(list.size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(list.front().is_object());

    boost::json::object const& first = list.front().get_object();
    EXPECT_EQ(std::string(first.at("tag").as_string()), std::string("proxy"));
    EXPECT_EQ(std::string(first.at("protocol").as_string()), std::string("hysteria"));

    // 锁定现场签名：direct/block 不得出现在 ado 载荷中。
    EXPECT_EQ(normalized.find("direct"), std::string::npos);
    EXPECT_EQ(normalized.find("blackhole"), std::string::npos);
}

TEST(OutboundPayloadTest, SingleOutboundPayloadIsIdempotent) {
    std::string firstPass;
    std::string secondPass;
    ASSERT_TRUE(outbound_payload::normalizeForAdo(kSingleOutbound, firstPass));
    ASSERT_TRUE(outbound_payload::normalizeForAdo(firstPass, secondPass));
    EXPECT_EQ(firstPass, secondPass);
    EXPECT_EQ(outbound_payload::outboundCount(firstPass), static_cast<std::size_t>(1));
}

TEST(OutboundPayloadTest, EmptyOutboundsReturnsFalse) {
    std::string normalized = "sentinel";
    EXPECT_FALSE(outbound_payload::normalizeForAdo("{\"outbounds\":[]}", normalized));
    EXPECT_EQ(normalized, std::string("sentinel"));
}

TEST(OutboundPayloadTest, NonObjectRootReturnsFalse) {
    std::string normalized = "sentinel";
    EXPECT_FALSE(outbound_payload::normalizeForAdo("[{\"tag\":\"proxy\"}]", normalized));
    EXPECT_EQ(normalized, std::string("sentinel"));
}

TEST(OutboundPayloadTest, InvalidJsonReturnsFalse) {
    std::string normalized = "sentinel";
    EXPECT_FALSE(outbound_payload::normalizeForAdo("{not json", normalized));
    EXPECT_EQ(normalized, std::string("sentinel"));
}

TEST(OutboundPayloadTest, OutboundsNotArrayReturnsFalse) {
    std::string normalized = "sentinel";
    EXPECT_FALSE(outbound_payload::normalizeForAdo("{\"outbounds\":{}}", normalized));
    EXPECT_EQ(normalized, std::string("sentinel"));
}

TEST(OutboundPayloadTest, FirstElementNotObjectReturnsFalse) {
    std::string normalized = "sentinel";
    EXPECT_FALSE(outbound_payload::normalizeForAdo("{\"outbounds\":[1]}", normalized));
    EXPECT_EQ(normalized, std::string("sentinel"));
}

// 锁定 D3：裸 outbound 对象不被自动包裹（当前无调用方使用该形状）。
TEST(OutboundPayloadTest, BareOutboundObjectReturnsFalse) {
    std::string normalized = "sentinel";
    EXPECT_FALSE(outbound_payload::normalizeForAdo(
        "{\"tag\":\"proxy\",\"protocol\":\"freedom\"}", normalized));
    EXPECT_EQ(normalized, std::string("sentinel"));
}

TEST(OutboundPayloadTest, EmptyPayloadReturnsFalse) {
    std::string normalized = "sentinel";
    EXPECT_FALSE(outbound_payload::normalizeForAdo("", normalized));
    EXPECT_EQ(normalized, std::string("sentinel"));
}

TEST(OutboundPayloadTest, OutboundCountReportsAllOutbounds) {
    EXPECT_EQ(outbound_payload::outboundCount(kFullConfig), static_cast<std::size_t>(3));
    EXPECT_EQ(outbound_payload::outboundCount(kSingleOutbound), static_cast<std::size_t>(1));
    EXPECT_EQ(outbound_payload::outboundCount("{not json"), static_cast<std::size_t>(0));
    EXPECT_EQ(outbound_payload::outboundCount("{\"outbounds\":[]}"), static_cast<std::size_t>(0));
}
