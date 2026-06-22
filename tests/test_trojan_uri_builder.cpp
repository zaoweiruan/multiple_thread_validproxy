#include <gtest/gtest.h>
#include "share/TrojanUriBuilder.h"

TEST(TrojanUriBuilderTest, BasicTrojanUri) {
    db::models::Profileitem profile;
    profile.configtype = "6";
    profile.address = "example.com";
    profile.port = "443";
    profile.id = "password123";
    profile.streamsecurity = "tls";

    share::TrojanUriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_TRUE(uri.find("trojan://password123@example.com:443") != std::string::npos);
}

TEST(TrojanUriBuilderTest, ExactMatch) {
    db::models::Profileitem profile;
    profile.configtype = "6";
    profile.address = "trojan.example.com";
    profile.port = "8443";
    profile.id = "mypassword";
    profile.network = "grpc";
    profile.sni = "sni.example.com";
    profile.alpn = "h2";
    profile.fingerprint = "chrome";
    profile.allowinsecure = "1";
    profile.streamsecurity = "tls";

    share::TrojanUriBuilder builder;
    std::string uri = builder.build(profile);

    std::string expected = "trojan://mypassword@trojan.example.com:8443?"
                           "security=tls&sni=sni.example.com&"
                           "fp=chrome&insecure=1&allowInsecure=1&type=grpc";
    EXPECT_EQ(uri, expected);
}

TEST(TrojanUriBuilderTest, WithPathAndHost) {
    db::models::Profileitem profile;
    profile.configtype = "6";
    profile.address = "trojan.example.com";
    profile.port = "443";
    profile.id = "pass";
    profile.streamsecurity = "tls";
    profile.requesthost = "host.example.com";
    profile.path = "/mypath";

    share::TrojanUriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_TRUE(uri.find("host=host.example.com") != std::string::npos);
    EXPECT_TRUE(uri.find("path=%2Fmypath") != std::string::npos);
}
