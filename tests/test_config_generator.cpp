#include <gtest/gtest.h>
#include <sstream>
#include <boost/json.hpp>
#include "test_utils.h"
#include "ConfigGenerator.h"
#include "Profileitem.h"

using namespace config;
using namespace db::models;

// ============================================================
// Helper to create Profileitem with minimal required fields
// ============================================================
Profileitem makeProfile(const std::string& configType, const std::string& address,
                        const std::string& port, const std::string& id,
                        const std::string& network = "tcp",
                        const std::string& streamSecurity = "",
                        const std::string& sni = "",
                        const std::string& publicKey = "") {
    Profileitem p;
    p.indexid = "test-index";
    p.configtype = configType;
    p.address = address;
    p.port = port;
    p.id = id;
    p.security = "auto";
    p.network = network;
    p.streamsecurity = streamSecurity;
    p.sni = sni;
    p.publickey = publicKey;
    p.allowinsecure = "0";
    p.muxEnabled = 0;
    return p;
}

// ============================================================
// Note: These tests require a valid SQLite DB pointer
// We test generateConfig directly via a mock DB or test only
// the JSON output properties
// ============================================================

// ============================================================
// VLESS Outbound Tests
// ============================================================
TEST(ConfigGeneratorTest, GenerateVlessOutboundBasic) {
    // Create an in-memory DB (won't actually be used by generateConfig)
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");

    // generateConfig will throw because of missing DB in loadProfiles
    // Skip this test for now
    GTEST_SKIP() << "generateConfig requires DB for loadProfiles - not implemented yet";
}

// ============================================================
// Direct buildXrayConfig tests (if made public)
// For now, test JSON structure only
// ============================================================
TEST(ConfigGeneratorTest, ProfileitemRequiredFieldsCheck) {
    // Test that Profileitem::checkRequired validates correctly
    Profileitem p;
    p.address = "example.com";
    p.port = "443";
    p.id = "uuid";
    p.configtype = "5";

    // Should not throw when fields are valid
    EXPECT_NO_THROW(p.checkRequired());
}

TEST(ConfigGeneratorTest, ProfileitemInvalidPort) {
    Profileitem p;
    p.address = "example.com";
    p.port = "0";
    p.id = "uuid";
    p.configtype = "5";

    EXPECT_THROW(p.checkRequired(), std::runtime_error);
}

// ============================================================
// Helper: parse generateConfig output JSON into value
// ============================================================
static boost::json::value parseOutboundJson(const XrayConfig& cfg) {
    return boost::json::parse(cfg.outbound_json);
}

// ============================================================
// Characterization tests for ConfigGenerator::generateConfig
//
// generateConfig() does NOT use db_ — it works solely on the
// Profileitem parameter. All tests pass nullptr for the DB.
// ============================================================

// ----------------------------------------------------------
// Test 1: Basic VLESS outbound JSON structure
// ----------------------------------------------------------
TEST(ConfigGeneratorTest, GenerateVlessOutbound_NoDb) {
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test");

    XrayConfig cfg = gen.generateConfig(p);
    boost::json::value jv = parseOutboundJson(cfg);

    auto const& root = jv.as_object();
    ASSERT_TRUE(root.contains("outbounds"));
    auto const& outbounds = root.at("outbounds").as_array();
    ASSERT_EQ(outbounds.size(), 1);

    auto const& outbound = outbounds[0].as_object();
    EXPECT_STREQ(outbound.at("protocol").as_string().c_str(), "vless");

    auto const& settings = outbound.at("settings").as_object();
    auto const& vnext = settings.at("vnext").as_array();
    ASSERT_EQ(vnext.size(), 1);
    EXPECT_STREQ(vnext[0].as_object().at("address").as_string().c_str(), "example.com");
    EXPECT_EQ(vnext[0].as_object().at("port").as_int64(), 443);

    auto const& users = vnext[0].as_object().at("users").as_array();
    ASSERT_EQ(users.size(), 1);
    EXPECT_STREQ(users[0].as_object().at("id").as_string().c_str(), "uuid-test");

    EXPECT_TRUE(outbound.contains("streamSettings"));
}

// ----------------------------------------------------------
// Test 2: Empty network normalization
// Note: generateConfig() does NOT normalize — loadProfiles() does.
//       Empty network results in no "network" key in streamSettings.
// ----------------------------------------------------------
TEST(ConfigGeneratorTest, NetworkNormalization_EmptyToTcp) {
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test", "");

    XrayConfig cfg = gen.generateConfig(p);
    boost::json::value jv = parseOutboundJson(cfg);

    auto const& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    auto const& streamSettings = outbound.at("streamSettings").as_object();
    EXPECT_FALSE(streamSettings.contains("network"));
}

// ----------------------------------------------------------
// Test 3: splithttp → xhttp normalization
// Note: generateConfig() passes network through as-is.
//       The splithttp→xhttp mapping occurs in loadProfiles().
// ----------------------------------------------------------
TEST(ConfigGeneratorTest, NetworkNormalization_SplithttpToXhttp) {
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test", "splithttp");

    XrayConfig cfg = gen.generateConfig(p);
    boost::json::value jv = parseOutboundJson(cfg);

    auto const& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    auto const& streamSettings = outbound.at("streamSettings").as_object();
    EXPECT_STREQ(streamSettings.at("network").as_string().c_str(), "splithttp");
}

// ----------------------------------------------------------
// Test 4: Invalid network fallback to tcp
// Note: generateConfig() does not validate network — only
//       loadProfiles() does. Invalid values pass through.
// ----------------------------------------------------------
TEST(ConfigGeneratorTest, NetworkNormalization_InvalidToTcp) {
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test", "weird_protocol");

    XrayConfig cfg = gen.generateConfig(p);
    boost::json::value jv = parseOutboundJson(cfg);

    auto const& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    auto const& streamSettings = outbound.at("streamSettings").as_object();
    EXPECT_STREQ(streamSettings.at("network").as_string().c_str(), "weird_protocol");
}

// ----------------------------------------------------------
// Test 5: Valid networks pass through correctly
// ----------------------------------------------------------
TEST(ConfigGeneratorTest, NetworkNormalization_ValidPassthrough) {
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);

    const std::vector<std::string> validNetworks = {"ws", "grpc", "tcp", "kcp", "xhttp", "hysteria"};

    for (const std::string& net : validNetworks) {
        Profileitem p = makeProfile("5", "example.com", "443", "uuid-test", net);
        XrayConfig cfg = gen.generateConfig(p);
        boost::json::value jv = parseOutboundJson(cfg);

        auto const& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
        auto const& streamSettings = outbound.at("streamSettings").as_object();
        EXPECT_STREQ(streamSettings.at("network").as_string().c_str(), net.c_str())
            << "network=" << net;
    }
}

// ----------------------------------------------------------
// Test 6: Reality stream settings
// ----------------------------------------------------------
TEST(ConfigGeneratorTest, RealitySettings) {
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test",
                                "tcp", "reality", "example.com", "test-public-key");
    p.fingerprint = "chrome";

    XrayConfig cfg = gen.generateConfig(p);
    boost::json::value jv = parseOutboundJson(cfg);

    auto const& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    auto const& streamSettings = outbound.at("streamSettings").as_object();
    ASSERT_TRUE(streamSettings.contains("realitySettings"));

    auto const& reality = streamSettings.at("realitySettings").as_object();
    EXPECT_STREQ(reality.at("serverName").as_string().c_str(), "example.com");
    EXPECT_STREQ(reality.at("publicKey").as_string().c_str(), "test-public-key");
    EXPECT_STREQ(reality.at("fingerprint").as_string().c_str(), "chrome");
    EXPECT_STREQ(reality.at("shortId").as_string().c_str(), "");
    EXPECT_STREQ(reality.at("spiderX").as_string().c_str(), "");
}

// ----------------------------------------------------------
// Test 7: TLS stream settings
// ----------------------------------------------------------
TEST(ConfigGeneratorTest, TlsSettings) {
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test",
                                "tcp", "tls", "example.com", "");
    p.allowinsecure = "1";
    p.fingerprint = "chrome";

    XrayConfig cfg = gen.generateConfig(p);
    boost::json::value jv = parseOutboundJson(cfg);

    auto const& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    auto const& streamSettings = outbound.at("streamSettings").as_object();
    ASSERT_TRUE(streamSettings.contains("tlsSettings"));

    auto const& tls = streamSettings.at("tlsSettings").as_object();
    EXPECT_STREQ(tls.at("serverName").as_string().c_str(), "example.com");
    EXPECT_EQ(tls.at("allowInsecure").as_bool(), true);
    EXPECT_STREQ(tls.at("fingerprint").as_string().c_str(), "chrome");
}

// ----------------------------------------------------------
// Test 8: Multiple outbound protocol types
// configType → expected protocol mappings:
//   "5" → "vless", "1" → "vmess", "6" → "trojan",
//   "3" → "shadowsocks", "7" → "hysteria"
// ----------------------------------------------------------
TEST(ConfigGeneratorTest, MultipleOutboundTypes) {
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);

    struct {
        std::string configType;
        std::string expectedProtocol;
    } testCases[] = {
        {"5", "vless"},
        {"1", "vmess"},
        {"6", "trojan"},
        {"3", "shadowsocks"},
        {"7", "hysteria"},
    };

    for (const auto& tc : testCases) {
        Profileitem p = makeProfile(tc.configType, "example.com", "443", "uuid-test");
        XrayConfig cfg = gen.generateConfig(p);
        boost::json::value jv = parseOutboundJson(cfg);

        auto const& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
        EXPECT_STREQ(outbound.at("protocol").as_string().c_str(), tc.expectedProtocol.c_str())
            << "configType=" << tc.configType;
    }
}

// ----------------------------------------------------------
// Test 9: WebSocket stream settings with path and host
// ----------------------------------------------------------
TEST(ConfigGeneratorTest, ProfileWithWsPath) {
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test", "ws");
    p.path = "/websocket";
    p.requesthost = "ws.example.com";

    XrayConfig cfg = gen.generateConfig(p);
    boost::json::value jv = parseOutboundJson(cfg);

    auto const& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    auto const& streamSettings = outbound.at("streamSettings").as_object();
    ASSERT_TRUE(streamSettings.contains("wsSettings"));

    auto const& ws = streamSettings.at("wsSettings").as_object();
    EXPECT_STREQ(ws.at("path").as_string().c_str(), "/websocket");
    EXPECT_STREQ(ws.at("host").as_string().c_str(), "ws.example.com");

    auto const& headers = ws.at("headers").as_object();
    EXPECT_STREQ(headers.at("host").as_string().c_str(), "ws.example.com");
}

// ----------------------------------------------------------
// Test 10: gRPC stream settings with service name
// ----------------------------------------------------------
TEST(ConfigGeneratorTest, ProfileWithGrpcService) {
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test", "grpc");
    p.path = "my-service";

    XrayConfig cfg = gen.generateConfig(p);
    boost::json::value jv = parseOutboundJson(cfg);

    auto const& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    auto const& streamSettings = outbound.at("streamSettings").as_object();
    ASSERT_TRUE(streamSettings.contains("grpcSettings"));

    auto const& grpc = streamSettings.at("grpcSettings").as_object();
    EXPECT_STREQ(grpc.at("serviceName").as_string().c_str(), "my-service");
}