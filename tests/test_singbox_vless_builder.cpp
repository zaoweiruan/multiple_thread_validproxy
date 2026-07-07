#include <gtest/gtest.h>
#include <boost/json.hpp>
#include "config/outbound/SingBoxVLESSOutboundBuilder.h"
#include "Profileitem.h"

using namespace config;
using namespace db::models;

// ============================================================
// Helper: create a VLESS Profileitem with minimal required fields
// ============================================================
static Profileitem makeVLESSProfile(const std::string& address, const std::string& port,
                                    const std::string& uuid, const std::string& network = "tcp",
                                    const std::string& streamSecurity = "",
                                    const std::string& sni = "") {
    Profileitem p;
    p.indexid = "sb-vless-test";
    p.configtype = "5";
    p.address = address;
    p.port = port;
    p.id = uuid;
    p.security = "auto";
    p.network = network;
    p.streamsecurity = streamSecurity;
    p.sni = sni;
    p.allowinsecure = "0";
    p.muxEnabled = 0;
    return p;
}

// ============================================================
// Test 1: Basic VLESS outbound JSON has expected fields and NO ECH
// ============================================================
TEST(SingBoxVLESSBuilderTest, BasicFields) {
    SingBoxVLESSOutboundBuilder builder;
    Profileitem p = makeVLESSProfile("example.com", "443", "my-uuid");

    boost::json::object result = builder.build(p, "proxy");

    EXPECT_STREQ(result.at("type").as_string().c_str(), "vless");
    EXPECT_STREQ(result.at("tag").as_string().c_str(), "proxy");
    EXPECT_STREQ(result.at("server").as_string().c_str(), "example.com");
    EXPECT_EQ(result.at("server_port").as_int64(), 443);
    EXPECT_STREQ(result.at("uuid").as_string().c_str(), "my-uuid");

    // packet_encoding must be xudp for sing-box VLESS
    ASSERT_TRUE(result.contains("packet_encoding"));
    EXPECT_STREQ(result.at("packet_encoding").as_string().c_str(), "xudp");

    // No TLS object when streamsecurity is empty
    EXPECT_FALSE(result.contains("tls")) << "Expected no tls object for plain TCP";

    // No ECH should appear anywhere in the output (no echconfiglist set)
    std::string jsonStr = boost::json::serialize(result);
    EXPECT_EQ(jsonStr.find("ech"), std::string::npos) << "ECH should not appear in sing-box VLESS output";
}

// ============================================================
// Test 2: TLS settings
// ============================================================
TEST(SingBoxVLESSBuilderTest, TlsSettings) {
    SingBoxVLESSOutboundBuilder builder;
    Profileitem p = makeVLESSProfile("example.com", "443", "uuid", "tcp", "tls", "sni.example.com");
    p.allowinsecure = "1";
    p.fingerprint = "chrome";

    boost::json::object result = builder.build(p, "proxy");

    ASSERT_TRUE(result.contains("tls"));
    const auto& tls = result.at("tls").as_object();
    EXPECT_TRUE(tls.at("enabled").as_bool());
    EXPECT_STREQ(tls.at("server_name").as_string().c_str(), "sni.example.com");
    EXPECT_TRUE(tls.at("insecure").as_bool());

    // utls fingerprint with enabled flag
    ASSERT_TRUE(tls.contains("utls"));
    const auto& utls = tls.at("utls").as_object();
    EXPECT_TRUE(utls.at("enabled").as_bool());
    EXPECT_STREQ(utls.at("fingerprint").as_string().c_str(), "chrome");

    // No ECH in tls object (no echconfiglist set)
    EXPECT_FALSE(tls.contains("ech")) << "ECH should not be in TLS settings";
}

// ============================================================
// Test 3: REALITY settings
// ============================================================
TEST(SingBoxVLESSBuilderTest, RealitySettings) {
    SingBoxVLESSOutboundBuilder builder;
    Profileitem p = makeVLESSProfile("example.com", "443", "uuid", "tcp", "reality", "reality.sni.com");
    p.publickey = "test-public-key";
    p.shortid = "abc123";
    p.fingerprint = "chrome";

    boost::json::object result = builder.build(p, "proxy");

    ASSERT_TRUE(result.contains("tls"));
    const auto& tls = result.at("tls").as_object();
    EXPECT_TRUE(tls.at("enabled").as_bool());
    EXPECT_STREQ(tls.at("server_name").as_string().c_str(), "reality.sni.com");

    ASSERT_TRUE(tls.contains("reality"));
    const auto& reality = tls.at("reality").as_object();
    EXPECT_TRUE(reality.at("enabled").as_bool());
    EXPECT_STREQ(reality.at("public_key").as_string().c_str(), "test-public-key");
    EXPECT_STREQ(reality.at("short_id").as_string().c_str(), "abc123");

    // utls fingerprint (for sing-box REALITY, fingerprint goes in tls.utls)
    ASSERT_TRUE(tls.contains("utls"));
    const auto& utls = tls.at("utls").as_object();
    EXPECT_TRUE(utls.at("enabled").as_bool());
    EXPECT_STREQ(utls.at("fingerprint").as_string().c_str(), "chrome");

    // No ECH
    EXPECT_FALSE(tls.contains("ech")) << "ECH should not be in REALITY TLS settings";
}

// ============================================================
// Test 4: WebSocket transport
// ============================================================
TEST(SingBoxVLESSBuilderTest, WebSocketTransport) {
    SingBoxVLESSOutboundBuilder builder;
    Profileitem p = makeVLESSProfile("example.com", "443", "uuid", "ws");
    p.path = "/websocket";
    p.requesthost = "ws.example.com";

    boost::json::object result = builder.build(p, "proxy");

    ASSERT_TRUE(result.contains("transport"));
    const auto& transport = result.at("transport").as_object();
    EXPECT_STREQ(transport.at("type").as_string().c_str(), "ws");
    EXPECT_STREQ(transport.at("path").as_string().c_str(), "/websocket");

    ASSERT_TRUE(transport.contains("headers"));
    EXPECT_STREQ(transport.at("headers").as_object().at("Host").as_string().c_str(), "ws.example.com");
}

// ============================================================
// Test 5: gRPC transport
// ============================================================
TEST(SingBoxVLESSBuilderTest, GrpcTransport) {
    SingBoxVLESSOutboundBuilder builder;
    Profileitem p = makeVLESSProfile("example.com", "443", "uuid", "grpc");
    p.path = "my-service";

    boost::json::object result = builder.build(p, "proxy");

    ASSERT_TRUE(result.contains("transport"));
    const auto& transport = result.at("transport").as_object();
    EXPECT_STREQ(transport.at("type").as_string().c_str(), "grpc");
    EXPECT_STREQ(transport.at("service").as_string().c_str(), "my-service");
}

// ============================================================
// Test 6: XTLS Vision flow
// ============================================================
TEST(SingBoxVLESSBuilderTest, XtlsVisionFlow) {
    SingBoxVLESSOutboundBuilder builder;
    Profileitem p = makeVLESSProfile("example.com", "443", "uuid");
    p.flow = "xtls-rprx-vision";

    boost::json::object result = builder.build(p, "proxy");

    ASSERT_TRUE(result.contains("flow"));
    EXPECT_STREQ(result.at("flow").as_string().c_str(), "xtls-rprx-vision");
}

// Test 7: DNS URL format echconfiglist → ech.enabled: true + config: [] + query_server_name
// ============================================================
TEST(SingBoxVLESSBuilderTest, EchDnsUrlFormat) {
    SingBoxVLESSOutboundBuilder builder;
    Profileitem p = makeVLESSProfile("example.com", "443", "uuid", "tcp", "tls", "sni.example.com");
    // DNS URL format contains a space → ech.enabled: true + config: [] + query_server_name
    p.echconfiglist = "cloudflare-ech.com+https://dns.alidns.com/dns-query";
    p.echforcequery = "1";

    boost::json::object result = builder.build(p, "proxy");

    ASSERT_TRUE(result.contains("tls"));
    const auto& tls = result.at("tls").as_object();
    ASSERT_TRUE(tls.contains("ech")) << "ECH must be emitted when echconfiglist is non-empty";
    const auto& ech = tls.at("ech").as_object();
    EXPECT_TRUE(ech.at("enabled").as_bool());
    // DNS URL format generates: config: [] (empty array) + query_server_name
    ASSERT_TRUE(ech.contains("config")) << "DNS URL format should have ech.config array (empty)";
    EXPECT_EQ(ech.at("config").as_array().size(), 0u) << "DNS URL format ech.config should be empty array";
    EXPECT_TRUE(ech.contains("query_server_name")) << "DNS URL format should have query_server_name";
    EXPECT_STREQ(ech.at("query_server_name").as_string().c_str(), "cloudflare-ech.com")
        << "query_server_name should be the domain part before '+'";
}

// ============================================================
// Test 8: Base64 format echconfiglist → ech.enabled: true + ech.config array
// ============================================================
TEST(SingBoxVLESSBuilderTest, EchBase64Format) {
    SingBoxVLESSOutboundBuilder builder;
    Profileitem p = makeVLESSProfile("example.com", "443", "uuid", "tcp", "tls", "sni.example.com");
    // Base64 format: no spaces, valid base64 chars
    p.echconfiglist = "APBb5QAAAAAAAhA+MDMuZHVubHktZG5zLmNvbQo=";

    boost::json::object result = builder.build(p, "proxy");

    ASSERT_TRUE(result.contains("tls"));
    const auto& tls = result.at("tls").as_object();
    ASSERT_TRUE(tls.contains("ech")) << "ECH must be emitted when echconfiglist is non-empty";
    const auto& ech = tls.at("ech").as_object();
    EXPECT_TRUE(ech.at("enabled").as_bool());
    ASSERT_TRUE(ech.contains("config")) << "Base64 format should have ech.config array";
    const auto& configArr = ech.at("config").as_array();
    ASSERT_EQ(configArr.size(), 1);
    // The hex-decoded value should be non-empty
    std::string hexVal = configArr[0].as_string().c_str();
    EXPECT_GT(hexVal.length(), 0u) << "ech.config should contain hex-decoded Base64 data";
}

// ============================================================
// Test 10: ECH fields are omitted when echconfiglist is empty
// ============================================================
TEST(SingBoxVLESSBuilderTest, EchFieldsNotSet) {
    SingBoxVLESSOutboundBuilder builder;
    Profileitem p = makeVLESSProfile("example.com", "443", "uuid", "tcp", "tls", "sni.example.com");
    // echconfiglist is empty — no ECH in output
    p.echconfiglist = "";
    p.echforcequery = "";

    boost::json::object result = builder.build(p, "proxy");

    ASSERT_TRUE(result.contains("tls"));
    const auto& tls = result.at("tls").as_object();
    EXPECT_FALSE(tls.contains("ech")) << "ECH must NOT be emitted when echconfiglist is empty";
    EXPECT_STREQ(tls.at("server_name").as_string().c_str(), "sni.example.com");
}

// ============================================================
// Test 9: H2 transport
// ============================================================
TEST(SingBoxVLESSBuilderTest, H2Transport) {
    SingBoxVLESSOutboundBuilder builder;
    Profileitem p = makeVLESSProfile("example.com", "443", "uuid", "h2");

    boost::json::object result = builder.build(p, "proxy");

    ASSERT_TRUE(result.contains("transport"));
    EXPECT_STREQ(result.at("transport").as_object().at("type").as_string().c_str(), "http");
}

// ============================================================
// Test 11: HTTP/2 (alias) transport
// ============================================================
TEST(SingBoxVLESSBuilderTest, Http2Transport) {
    SingBoxVLESSOutboundBuilder builder;
    Profileitem p = makeVLESSProfile("example.com", "443", "uuid", "http2");

    boost::json::object result = builder.build(p, "proxy");

    ASSERT_TRUE(result.contains("transport"));
    EXPECT_STREQ(result.at("transport").as_object().at("type").as_string().c_str(), "http");
}

// ============================================================
// Test 12: ALPN settings
// ============================================================
TEST(SingBoxVLESSBuilderTest, AlpnSettings) {
    SingBoxVLESSOutboundBuilder builder;
    Profileitem p = makeVLESSProfile("example.com", "443", "uuid", "tcp", "tls", "sni.example.com");
    p.alpn = "h2,http/1.1";

    boost::json::object result = builder.build(p, "proxy");

    ASSERT_TRUE(result.contains("tls"));
    const auto& tls = result.at("tls").as_object();
    ASSERT_TRUE(tls.contains("alpn"));
    const auto& alpn = tls.at("alpn").as_array();
    ASSERT_EQ(alpn.size(), 2);
    EXPECT_STREQ(alpn[0].as_string().c_str(), "h2");
    EXPECT_STREQ(alpn[1].as_string().c_str(), "http/1.1");
}
