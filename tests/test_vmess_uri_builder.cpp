#include <gtest/gtest.h>
#include "share/VMessUriBuilder.h"
#include "share/UriCodec.h"

TEST(VMessUriBuilderTest, BasicVmessUri) {
    db::models::Profileitem profile;
    profile.configtype = "1";
    profile.address = "example.com";
    profile.port = "443";
    profile.id = "uuid123";
    profile.security = "auto";
    profile.network = "tcp";
    profile.streamsecurity = "tls";
    profile.sni = "example.com";

    share::VMessUriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_TRUE(uri.find("vmess://") == 0);
}

TEST(VMessUriBuilderTest, ContainsBase64) {
    db::models::Profileitem profile;
    profile.configtype = "1";
    profile.address = "example.com";
    profile.port = "443";
    profile.id = "uuid123";
    profile.security = "auto";

    share::VMessUriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_TRUE(uri.find("vmess://") == 0);
    std::string b64 = uri.substr(8);
    EXPECT_FALSE(b64.empty());
}

TEST(VMessUriBuilderTest, JsonFieldsPresent) {
    db::models::Profileitem profile;
    profile.configtype = "1";
    profile.address = "example.com";
    profile.port = "443";
    profile.id = "uuid123";
    profile.security = "auto";
    profile.network = "tcp";
    profile.streamsecurity = "tls";
    profile.sni = "example.com";
    profile.remarks = "test";

    share::VMessUriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_TRUE(uri.find("vmess://") == 0);
    std::string b64 = uri.substr(8);
    std::string decoded = share::UriCodec::base64Decode(b64);
    EXPECT_TRUE(decoded.find("\"v\":") != std::string::npos);
    EXPECT_TRUE(decoded.find("\"ps\":") != std::string::npos);
    EXPECT_TRUE(decoded.find("\"add\":") != std::string::npos);
    EXPECT_TRUE(decoded.find("\"port\":") != std::string::npos);
    EXPECT_TRUE(decoded.find("\"id\":") != std::string::npos);
    EXPECT_TRUE(decoded.find("\"aid\":") != std::string::npos);
    EXPECT_TRUE(decoded.find("\"net\":") != std::string::npos);
    EXPECT_TRUE(decoded.find("\"type\":") != std::string::npos);
    EXPECT_TRUE(decoded.find("\"host\":") != std::string::npos);
    EXPECT_TRUE(decoded.find("\"path\":") != std::string::npos);
    EXPECT_TRUE(decoded.find("\"tls\":") != std::string::npos);
    EXPECT_TRUE(decoded.find("\"sni\":") != std::string::npos);
}
