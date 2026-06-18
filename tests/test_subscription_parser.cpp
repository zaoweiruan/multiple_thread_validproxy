#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "update/SubscriptionParser.h"

// ============================================================
// SubscriptionParser Tests
// ============================================================

TEST(SubscriptionParserTest, ParseVlessBasic) {
    update::SubscriptionParser parser;
    
    std::string content = "vless://uuid123@example.com:443?type=tcp";
    
    auto profiles = parser.parse(content, "test-sub");
    
    ASSERT_GE(profiles.size(), 1);
    EXPECT_EQ(profiles[0].configtype, "5");
    EXPECT_EQ(profiles[0].address, "example.com");
    EXPECT_EQ(profiles[0].port, "443");
    EXPECT_EQ(profiles[0].id, "uuid123");
    EXPECT_EQ(profiles[0].subid, "test-sub");
}

TEST(SubscriptionParserTest, ParseVlessWithRemarks) {
    update::SubscriptionParser parser;
    
    std::string content = "vless://uuid123@example.com:443?type=tcp#My%20Server";
    
    auto profiles = parser.parse(content, "test-sub");
    
    ASSERT_GE(profiles.size(), 1);
    EXPECT_EQ(profiles[0].configtype, "5");
    EXPECT_EQ(profiles[0].remarks, "My Server");
}

TEST(SubscriptionParserTest, ParseShadowSocksBasic) {
    update::SubscriptionParser parser;
    
    // SS format: ss://base64(method:password)@host:port
    // aes-256-gcm:password123 -> YXNlcy0yNTYtZ2NtOnBhc3N3b3JkMTIz
    std::string content = "ss://YmFzZTY0LWtleTpwYXNzd29yZA@example.com:8388";  // Simplified test - just check configtype
    
    auto profiles = parser.parse(content, "test-sub");
    
    ASSERT_GE(profiles.size(), 1);
    EXPECT_EQ(profiles[0].configtype, "3");
    EXPECT_EQ(profiles[0].address, "example.com");
    EXPECT_EQ(profiles[0].port, "8388");
}

TEST(SubscriptionParserTest, ParseTrojanBasic) {
    update::SubscriptionParser parser;
    
    std::string content = "trojan://password123@example.com:443";
    
    auto profiles = parser.parse(content, "test-sub");
    
    ASSERT_GE(profiles.size(), 1);
    EXPECT_EQ(profiles[0].configtype, "6");
    EXPECT_EQ(profiles[0].address, "example.com");
    EXPECT_EQ(profiles[0].port, "443");
    EXPECT_EQ(profiles[0].id, "password123");
}

TEST(SubscriptionParserTest, ParseHysteria2Basic) {
    update::SubscriptionParser parser;
    
    std::string content = "hysteria2://password123@example.com:443";
    
    auto profiles = parser.parse(content, "test-sub");
    
    ASSERT_GE(profiles.size(), 1);
    EXPECT_EQ(profiles[0].configtype, "7");
    EXPECT_EQ(profiles[0].address, "example.com");
    EXPECT_EQ(profiles[0].port, "443");
}

TEST(SubscriptionParserTest, ParseHy2Alias) {
    update::SubscriptionParser parser;
    
    std::string content = "hy2://password123@example.com:443";
    
    auto profiles = parser.parse(content, "test-sub");
    
    ASSERT_GE(profiles.size(), 1);
    EXPECT_EQ(profiles[0].configtype, "7");
}

TEST(SubscriptionParserTest, ParseMixedProtocols) {
    update::SubscriptionParser parser;
    
    std::string content = 
        "vless://uuid1@example.com:443?type=tcp\r\n"
        "trojan://pass@example.com:443\r\n"
        "hy2://pass@example.com:443";
    
    auto profiles = parser.parse(content, "test-sub");
    
    ASSERT_GE(profiles.size(), 3);
    EXPECT_EQ(profiles[0].configtype, "5");  // vless
    EXPECT_EQ(profiles[1].configtype, "6");  // trojan
    EXPECT_EQ(profiles[2].configtype, "7");  // hysteria2
}

TEST(SubscriptionParserTest, ParseInvalidContentReturnsEmpty) {
    update::SubscriptionParser parser;
    
    std::string content = "invalid-content-without-protocol";
    
    auto profiles = parser.parse(content, "test-sub");
    
    EXPECT_EQ(profiles.size(), 0);
}

TEST(SubscriptionParserTest, ParseEmptyContentReturnsEmpty) {
    update::SubscriptionParser parser;
    
    std::string content = "";
    
    auto profiles = parser.parse(content, "test-sub");
    
    EXPECT_EQ(profiles.size(), 0);
}