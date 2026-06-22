#include <gtest/gtest.h>
#include "share/VLESSUriBuilder.h"

TEST(VLESSUriBuilderTest, BasicVlessUri) {
    db::models::Profileitem profile;
    profile.configtype = "5";
    profile.address = "example.com";
    profile.port = "443";
    profile.id = "uuid-test";
    profile.security = "auto";
    profile.network = "tcp";
    profile.streamsecurity = "tls";

    share::VLESSUriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_TRUE(uri.find("vless://") == 0);
    EXPECT_TRUE(uri.find("uuid-test@example.com:443") != std::string::npos);
    EXPECT_TRUE(uri.find("encryption=none") != std::string::npos);
}

TEST(VLESSUriBuilderTest, ExactMatch) {
    db::models::Profileitem profile;
    profile.configtype = "5";
    profile.address = "example.com";
    profile.port = "443";
    profile.id = "uuid-test";
    profile.security = "auto";
    profile.network = "tcp";
    profile.streamsecurity = "tls";

    share::VLESSUriBuilder builder;
    std::string uri = builder.build(profile);

    std::string expected = "vless://uuid-test@example.com:443?"
                           "encryption=none&security=tls&"
                           "insecure=0&allowInsecure=0&type=tcp";
    EXPECT_EQ(uri, expected);
}

TEST(VLESSUriBuilderTest, WithRemarks) {
    db::models::Profileitem profile;
    profile.configtype = "5";
    profile.address = "example.com";
    profile.port = "443";
    profile.id = "uuid123";
    profile.streamsecurity = "tls";
    profile.remarks = "My Server";

    share::VLESSUriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_TRUE(uri.find("#") != std::string::npos);
}

TEST(VLESSUriBuilderTest, WithRealityParams) {
    db::models::Profileitem profile;
    profile.configtype = "5";
    profile.address = "reality.example.com";
    profile.port = "443";
    profile.id = "uuid-reality";
    profile.network = "grpc";
    profile.flow = "xtls-rprx-vision";
    profile.sni = "reality.sni.com";
    profile.publickey = "test-pbk";
    profile.shortid = "test-sid";

    share::VLESSUriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_TRUE(uri.find("pbk=test-pbk") != std::string::npos);
    EXPECT_TRUE(uri.find("sid=test-sid") != std::string::npos);
    EXPECT_TRUE(uri.find("flow=xtls-rprx-vision") != std::string::npos);
    EXPECT_TRUE(uri.find("encryption=none") != std::string::npos);
    EXPECT_TRUE(uri.find("sni=reality.sni.com") != std::string::npos);
}

TEST(VLESSUriBuilderTest, MinimalFields) {
    db::models::Profileitem profile;
    profile.configtype = "5";
    profile.address = "example.com";
    profile.port = "80";
    profile.id = "minimal";

    share::VLESSUriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_FALSE(uri.empty());
    EXPECT_TRUE(uri.find("vless://") == 0);
    EXPECT_TRUE(uri.find("minimal@example.com:80") != std::string::npos);
    EXPECT_TRUE(uri.find("encryption=none") != std::string::npos);
}

TEST(VLESSUriBuilderTest, ChineseRemarks) {
    db::models::Profileitem profile;
    profile.configtype = "5";
    profile.address = "example.com";
    profile.port = "443";
    profile.id = "uuid123";
    profile.streamsecurity = "tls";
    profile.remarks = "\xe4\xb8\xad\xe5\x9b\xbd\xe8\x8a\x82\xe7\x82\xb9";

    share::VLESSUriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_TRUE(uri.find("#") != std::string::npos);
    EXPECT_TRUE(uri.find("%E4%B8%AD%E5%9B%BD%E8%8A%82%E7%82%B9") != std::string::npos);
}
