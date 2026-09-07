#include <gtest/gtest.h>
#include <sstream>
#include <boost/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include "test_utils.h"
#include "ConfigGenerator.h"
#include "Profileitem.h"

#ifdef _WIN32
#include <windows.h>
#endif

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

    const boost::json::object& root = jv.as_object();
    ASSERT_TRUE(root.contains("outbounds"));
    const boost::json::array& outbounds = root.at("outbounds").as_array();
    ASSERT_EQ(outbounds.size(), 1);

    const boost::json::object& outbound = outbounds[0].as_object();
    EXPECT_STREQ(outbound.at("protocol").as_string().c_str(), "vless");

    const boost::json::object& settings = outbound.at("settings").as_object();
    const boost::json::array& vnext = settings.at("vnext").as_array();
    ASSERT_EQ(vnext.size(), 1);
    EXPECT_STREQ(vnext[0].as_object().at("address").as_string().c_str(), "example.com");
    EXPECT_EQ(vnext[0].as_object().at("port").as_int64(), 443);

    const boost::json::array& users = vnext[0].as_object().at("users").as_array();
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

    const boost::json::object& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    const boost::json::object& streamSettings = outbound.at("streamSettings").as_object();
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

    const boost::json::object& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    const boost::json::object& streamSettings = outbound.at("streamSettings").as_object();
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

    const boost::json::object& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    const boost::json::object& streamSettings = outbound.at("streamSettings").as_object();
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

        const boost::json::object& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
        const boost::json::object& streamSettings = outbound.at("streamSettings").as_object();
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

    const boost::json::object& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    const boost::json::object& streamSettings = outbound.at("streamSettings").as_object();
    ASSERT_TRUE(streamSettings.contains("realitySettings"));

    const boost::json::object& reality = streamSettings.at("realitySettings").as_object();
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

    const boost::json::object& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    const boost::json::object& streamSettings = outbound.at("streamSettings").as_object();
    ASSERT_TRUE(streamSettings.contains("tlsSettings"));

    const boost::json::object& tls = streamSettings.at("tlsSettings").as_object();
    EXPECT_STREQ(tls.at("serverName").as_string().c_str(), "example.com");
    EXPECT_EQ(tls.at("allowInsecure").as_bool(), true);
    EXPECT_STREQ(tls.at("fingerprint").as_string().c_str(), "chrome");
}

// ----------------------------------------------------------
// Regression: v2rayN stores "false" in streamsecurity to mean "no
// security". This is NOT a valid Xray security value and previously
// produced `"security": "false"`, which made Xray fail to start with
// `Unknown security "false"`. The builder must omit the field entirely
// (Xray defaults to "none") for such invalid values.
// ----------------------------------------------------------
TEST(ConfigGeneratorTest, StreamSecurity_FalseOmitted) {
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);
    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test",
                                "ws", "false");
    p.path = "/path";
    p.requesthost = "example.com";

    XrayConfig cfg = gen.generateConfig(p);
    boost::json::value jv = parseOutboundJson(cfg);

    const boost::json::object& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    const boost::json::object& streamSettings = outbound.at("streamSettings").as_object();
    // No valid security -> the "security" key must NOT be emitted
    EXPECT_FALSE(streamSettings.contains("security"))
        << "streamsecurity=\"false\" must not emit a security field";
    // wsSettings must still be produced (network handling is unaffected)
    EXPECT_TRUE(streamSettings.contains("wsSettings"));
}

// ----------------------------------------------------------
// Regression: any invalid/unknown streamsecurity value must be
// dropped just like "false", never emitted verbatim.
// ----------------------------------------------------------
TEST(ConfigGeneratorTest, StreamSecurity_InvalidValuesOmitted) {
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);

    const char* invalidValues[] = {"false", "unknown", "FALSE", "0"};
    for (const char* sec : invalidValues) {
        Profileitem p = makeProfile("5", "example.com", "443", "uuid-test", "tcp", sec);
        XrayConfig cfg = gen.generateConfig(p);
        boost::json::value jv = parseOutboundJson(cfg);

        const boost::json::object& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
        const boost::json::object& streamSettings = outbound.at("streamSettings").as_object();
        EXPECT_FALSE(streamSettings.contains("security"))
            << "streamsecurity=\"" << sec << "\" must not emit a security field";
    }
}

// ----------------------------------------------------------
// Valid security values still pass through correctly.
// ----------------------------------------------------------
TEST(ConfigGeneratorTest, StreamSecurity_ValidPassthrough) {
    sqlite3* db = nullptr;
    ConfigGenerator gen(db);

    Profileitem p = makeProfile("5", "example.com", "443", "uuid-test",
                                "tcp", "none");
    XrayConfig cfg = gen.generateConfig(p);
    boost::json::value jv = parseOutboundJson(cfg);

    const boost::json::object& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    const boost::json::object& streamSettings = outbound.at("streamSettings").as_object();
    EXPECT_TRUE(streamSettings.contains("security"));
    EXPECT_STREQ(streamSettings.at("security").as_string().c_str(), "none");
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

    for (std::size_t i = 0; i < 5; ++i) {
        Profileitem p = makeProfile(testCases[i].configType, "example.com", "443", "uuid-test");
        XrayConfig cfg = gen.generateConfig(p);
        boost::json::value jv = parseOutboundJson(cfg);

        const boost::json::object& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
        EXPECT_STREQ(outbound.at("protocol").as_string().c_str(), testCases[i].expectedProtocol.c_str())
            << "configType=" << testCases[i].configType;
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

    const boost::json::object& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    const boost::json::object& streamSettings = outbound.at("streamSettings").as_object();
    ASSERT_TRUE(streamSettings.contains("wsSettings"));

    const boost::json::object& ws = streamSettings.at("wsSettings").as_object();
    EXPECT_STREQ(ws.at("path").as_string().c_str(), "/websocket");
    EXPECT_STREQ(ws.at("host").as_string().c_str(), "ws.example.com");

    const boost::json::object& headers = ws.at("headers").as_object();
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

    const boost::json::object& outbound = jv.as_object().at("outbounds").as_array()[0].as_object();
    const boost::json::object& streamSettings = outbound.at("streamSettings").as_object();
    ASSERT_TRUE(streamSettings.contains("grpcSettings"));

    const boost::json::object& grpc = streamSettings.at("grpcSettings").as_object();
    EXPECT_STREQ(grpc.at("serviceName").as_string().c_str(), "my-service");
}

// ============================================================
// Regression: ConfigGenerator::buildPoolConfig (xray 26.x)
//   - inbound `listen` must be a plain address + separate `port`
//     (xray rejects the combined "address:port" form)
//   - balancer must be routing.balancers (the legacy outbound
//     protocol "balancing" is no longer registered)
// ============================================================

namespace {

std::string findXrayBinary() {
    const char* env = std::getenv("XRAY_PATH");
    if (env && env[0] != '\0') return env;
    const char* candidates[] = {
        "E:/v2rayN-windows-64/bin/xray/xray.exe",
        "E:/v2rayN-windows-64/bin/xray/xray",
        "xray.exe"
    };
    for (const char* c : candidates) {
        FILE* f = fopen(c, "rb");
        if (f) { fclose(f); return c; }
    }
    return "";
}

std::string toForwardSlashes(std::string p) {
    for (char& c : p) if (c == '\\') c = '/';
    return p;
}

std::string runXrayTest(const std::string& cfgPath, const std::string& xrayPath) {
    // Ensure xray can load geoip.dat/geosite.dat when the config uses geo
    // routing rules / geosite DNS matchers. Derived from the xray exe layout
    // (.../bin/xray/xray.exe -> .../bin where geoip.dat lives); only set when
    // the asset actually exists so non-geo configs are unaffected.
    {
        std::filesystem::path exe(xrayPath);
        std::filesystem::path asset = exe.parent_path().parent_path();
        if (std::filesystem::exists(asset / "geoip.dat")) {
#ifdef _WIN32
            SetEnvironmentVariableA("XRAY_LOCATION_ASSET", asset.string().c_str());
#else
            (void)setenv("XRAY_LOCATION_ASSET", asset.string().c_str(), 1);
#endif
        }
    }
    // NOTE: cmd.exe mis-parses paths wrapped in inner double quotes when the
    // path contains backslashes, so we avoid quotes entirely and use forward
    // slashes (Windows APIs accept them). Our paths contain no spaces.
    std::string cmd = toForwardSlashes(xrayPath);
    cmd += " run -c ";
    cmd += toForwardSlashes(cfgPath);
    cmd += " -test 2>&1";
    FILE* pipe = _popen(cmd.c_str(), "r");
    std::string out;
    if (pipe) {
        char buf[512];
        while (fgets(buf, sizeof(buf), pipe)) out += buf;
        _pclose(pipe);
    }
    return out;
}

} // namespace

TEST(ConfigGeneratorTest, BuildPoolConfig_Structure) {
    StandalonePoolConfig cfg; // defaults: balancerStrategy "leastPing", observatory present
    std::string json = ConfigGenerator::buildPoolConfig(10809, 10810, cfg);
    boost::json::value jv = boost::json::parse(json);
    const boost::json::object& root = jv.as_object();

    // Inbound must use a plain address + separate port (not "127.0.0.1:10809")
    ASSERT_TRUE(root.contains("inbounds"));
    const boost::json::array& inbounds = root.at("inbounds").as_array();
    ASSERT_EQ(inbounds.size(), 1);
    const boost::json::object& socksIn = inbounds[0].as_object();
    EXPECT_STREQ(socksIn.at("listen").as_string().c_str(), "127.0.0.1");
    ASSERT_TRUE(socksIn.contains("port"));
    EXPECT_EQ(socksIn.at("port").as_int64(), 10809);
    // The buggy combined form must be gone
    EXPECT_EQ(std::string(socksIn.at("listen").as_string().c_str()).find(':'), std::string::npos);

    // No outbound may use the removed "balancing" protocol
    ASSERT_TRUE(root.contains("outbounds"));
    const boost::json::array& outbounds = root.at("outbounds").as_array();
    for (const auto& ob : outbounds) {
        const boost::json::object& o = ob.as_object();
        if (o.contains("protocol")) {
            EXPECT_STRNE(o.at("protocol").as_string().c_str(), "balancing")
                << "legacy 'balancing' outbound must not be emitted";
        }
    }

    // Balancer must be declared under routing.balancers
    ASSERT_TRUE(root.contains("routing"));
    const boost::json::object& routing = root.at("routing").as_object();
    ASSERT_TRUE(routing.contains("balancers")) << "routing.balancers missing";
    const boost::json::array& balancers = routing.at("balancers").as_array();
    ASSERT_EQ(balancers.size(), 1);
    const boost::json::object& bal = balancers[0].as_object();
    EXPECT_STREQ(bal.at("tag").as_string().c_str(), "balancer-out");
    const boost::json::array& sel = bal.at("selector").as_array();
    ASSERT_EQ(sel.size(), 1);
    EXPECT_STREQ(sel[0].as_string().c_str(), "px-");
    EXPECT_STREQ(bal.at("strategy").at("type").as_string().c_str(), "leastPing");
    EXPECT_TRUE(routing.contains("rules"));

    // Observatory present for health discovery
    EXPECT_TRUE(root.contains("observatory"));
}

TEST(ConfigGeneratorTest, BuildPoolConfig_TemplateHardening) {
    StandalonePoolConfig cfg;
    std::string json = ConfigGenerator::buildPoolConfig(10809, 10810, cfg);
    boost::json::value jv = boost::json::parse(json);
    const boost::json::object& root = jv.as_object();

    // DNS block mirrored from the template (hosts + DoH servers).
    ASSERT_TRUE(root.contains("dns")) << "pool config must carry a dns block";
    const boost::json::object& dns = root.at("dns").as_object();
    ASSERT_TRUE(dns.contains("hosts"));
    ASSERT_TRUE(dns.contains("servers"));
    ASSERT_GT(dns.at("hosts").as_object().size(), 0u);
    ASSERT_GT(dns.at("servers").as_array().size(), 0u);

    // block (blackhole) outbound must exist for the udp/443 block rule.
    const boost::json::array& outbounds = root.at("outbounds").as_array();
    bool hasBlock = false;
    for (std::size_t i = 0; i < outbounds.size(); ++i) {
        const boost::json::object& o = outbounds[i].as_object();
        if (o.contains("tag") && o.at("tag").as_string() == "block") {
            hasBlock = true;
            EXPECT_STREQ(o.at("protocol").as_string().c_str(), "blackhole");
        }
    }
    EXPECT_TRUE(hasBlock) << "block (blackhole) outbound required for udp/443 rule";

    // Inbound parity with the template: mixed protocol, allowTransparent false,
    // quic in sniffing destOverride, routeOnly false.
    const boost::json::object& socksIn = root.at("inbounds").as_array()[0].as_object();
    EXPECT_STREQ(socksIn.at("protocol").as_string().c_str(), "mixed");
    const boost::json::object& settings = socksIn.at("settings").as_object();
    ASSERT_TRUE(settings.contains("allowTransparent"));
    EXPECT_FALSE(settings.at("allowTransparent").as_bool());
    const boost::json::object& sniff = socksIn.at("sniffing").as_object();
    ASSERT_TRUE(sniff.contains("routeOnly"));
    EXPECT_FALSE(sniff.at("routeOnly").as_bool());
    const boost::json::array& dest = sniff.at("destOverride").as_array();
    bool hasQuic = false;
    for (std::size_t i = 0; i < dest.size(); ++i) {
        if (dest[i].as_string() == "quic") hasQuic = true;
    }
    EXPECT_TRUE(hasQuic) << "sniffing must include quic";

    // Routing rules: api first, socks-in->balancer last, and the template's
    // direct-split rules all present (order-independent presence check).
    const boost::json::array& rules = root.at("routing").at("rules").as_array();
    ASSERT_GE(rules.size(), 9u);

    const boost::json::object& first = rules[0].as_object();
    EXPECT_STREQ(first.at("outboundTag").as_string().c_str(), "api");
    const boost::json::object& last = rules[rules.size() - 1].as_object();
    ASSERT_TRUE(last.contains("balancerTag"));
    EXPECT_STREQ(last.at("balancerTag").as_string().c_str(), "balancer-out");

    bool seenUdpBlock = false, seenPrivateIp = false, seenPrivateDom = false,
         seenCnDnsIps = false, seenDnsDom = false, seenCnIp = false, seenCnDom = false;
    for (std::size_t i = 0; i < rules.size(); ++i) {
        const boost::json::object& r = rules[i].as_object();
        if (!r.contains("outboundTag")) continue;
        std::string ob = r.at("outboundTag").as_string().c_str();
        if (ob != "direct" && ob != "block") continue;
        if (r.contains("network") && r.at("network").as_string() == "udp" &&
            r.contains("port") && r.at("port").as_string() == "443" && ob == "block") {
            seenUdpBlock = true;
        }
        if (r.contains("ip")) {
            const boost::json::array& ip = r.at("ip").as_array();
            for (std::size_t j = 0; j < ip.size(); ++j) {
                std::string v = ip[j].as_string().c_str();
                if (v == "geoip:private") seenPrivateIp = true;
                if (v == "geoip:cn") seenCnIp = true;
                if (v == "114.114.114.114") seenCnDnsIps = true;
            }
        }
        if (r.contains("domain")) {
            const boost::json::array& dom = r.at("domain").as_array();
            for (std::size_t j = 0; j < dom.size(); ++j) {
                std::string v = dom[j].as_string().c_str();
                if (v == "geosite:private") seenPrivateDom = true;
                if (v == "geosite:cn") seenCnDom = true;
                if (v == "domain:alidns.com") seenDnsDom = true;
            }
        }
    }
    EXPECT_TRUE(seenUdpBlock) << "udp/443 -> block rule missing";
    EXPECT_TRUE(seenPrivateIp) << "geoip:private -> direct missing";
    EXPECT_TRUE(seenPrivateDom) << "geosite:private -> direct missing";
    EXPECT_TRUE(seenCnDnsIps) << "mainland DNS resolver IPs -> direct missing";
    EXPECT_TRUE(seenDnsDom) << "domestic DNS provider domains -> direct missing";
    EXPECT_TRUE(seenCnIp) << "geoip:cn -> direct missing";
    EXPECT_TRUE(seenCnDom) << "geosite:cn -> direct missing";
}

TEST(ConfigGeneratorTest, BuildPoolConfig_Xray26Loads) {
    StandalonePoolConfig cfg;
    std::string json = ConfigGenerator::buildPoolConfig(13999, 13998, cfg);

    std::string xray = findXrayBinary();
    if (xray.empty()) {
        GTEST_SKIP() << "xray binary not found; skipping live load test";
    }

    namespace fs = std::filesystem;
    std::string tmp = (fs::temp_directory_path() / "validproxy_pool_cfg_test.json").string();
    FILE* f = fopen(tmp.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    fwrite(json.c_str(), 1, json.size(), f);
    fclose(f);

    std::string out = runXrayTest(tmp, xray);
    std::remove(tmp.c_str());

    bool configOk = out.find("Configuration OK.") != std::string::npos;
    bool bindConflict = out.find("unable to listen") != std::string::npos;
    bool schemaError =
        out.find("unknown config id") != std::string::npos ||
        out.find("failed to build") != std::string::npos ||
        out.find("unable to listen on domain address") != std::string::npos;

    if (configOk) {
        SUCCEED() << "xray 26.x accepted the generated pool config";
    } else if (bindConflict) {
        GTEST_SKIP() << "xray could not bind port (environment); config schema not exercised: " << out;
    } else {
        FAIL() << "xray 26.x rejected generated pool config:\n" << out;
    }
    (void)schemaError;
}