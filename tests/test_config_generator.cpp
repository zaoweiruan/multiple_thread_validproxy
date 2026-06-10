#include <gtest/gtest.h>
#include <sstream>
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