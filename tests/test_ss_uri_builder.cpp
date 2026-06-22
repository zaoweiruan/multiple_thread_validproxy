#include <gtest/gtest.h>
#include "share/SSUriBuilder.h"
#include "share/UriCodec.h"

TEST(SSUriBuilderTest, BasicSsUri) {
    db::models::Profileitem profile;
    profile.configtype = "3";
    profile.address = "example.com";
    profile.port = "8388";
    profile.id = "password123";
    profile.security = "aes-256-gcm";

    share::SSUriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_TRUE(uri.find("ss://") == 0);
    EXPECT_TRUE(uri.find("@example.com:8388") != std::string::npos);
}

TEST(SSUriBuilderTest, Base64UserInfo) {
    db::models::Profileitem profile;
    profile.configtype = "3";
    profile.address = "ss.example.com";
    profile.port = "8388";
    profile.id = "test-pass";
    profile.security = "aes-256-gcm";

    share::SSUriBuilder builder;
    std::string uri = builder.build(profile);

    size_t atPos = uri.find('@');
    EXPECT_TRUE(atPos != std::string::npos);
    std::string b64 = uri.substr(5, atPos - 5);
    std::string decoded = share::UriCodec::base64Decode(b64);
    EXPECT_EQ(decoded, "aes-256-gcm:test-pass");
}

TEST(SSUriBuilderTest, PluginWsWithTls) {
    db::models::Profileitem profile;
    profile.configtype = "3";
    profile.address = "ws.example.com";
    profile.port = "443";
    profile.id = "pass";
    profile.security = "chacha20-ietf-poly1305";
    profile.network = "ws";
    profile.path = "/";
    profile.sni = "ws.example.com";
    profile.streamsecurity = "tls";

    share::SSUriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_TRUE(uri.find("plugin=v2ray-plugin") != std::string::npos);
    EXPECT_TRUE(uri.find("%3Btls") != std::string::npos);
    EXPECT_TRUE(uri.find("%3Bhost%3Dws.example.com") != std::string::npos);
}
