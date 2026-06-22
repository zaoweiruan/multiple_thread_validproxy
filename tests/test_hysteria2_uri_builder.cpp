#include <gtest/gtest.h>
#include "share/Hysteria2UriBuilder.h"

TEST(Hysteria2UriBuilderTest, BasicHysteria2Uri) {
    db::models::Profileitem profile;
    profile.configtype = "7";
    profile.address = "example.com";
    profile.port = "443";
    profile.id = "password123";

    share::Hysteria2UriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_TRUE(uri.find("hy2://password123@example.com:443") != std::string::npos);
}

TEST(Hysteria2UriBuilderTest, ExactMatch) {
    db::models::Profileitem profile;
    profile.configtype = "7";
    profile.address = "hy2.example.com";
    profile.port = "443";
    profile.id = "pass123";
    profile.sni = "sni.example.com";
    profile.alpn = "h3";

    share::Hysteria2UriBuilder builder;
    std::string uri = builder.build(profile);

    std::string expected = "hy2://pass123@hy2.example.com:443?"
                           "alpn=h3&sni=sni.example.com";
    EXPECT_EQ(uri, expected);
}

TEST(Hysteria2UriBuilderTest, WithAllParams) {
    db::models::Profileitem profile;
    profile.configtype = "7";
    profile.address = "hy2.example.com";
    profile.port = "443";
    profile.id = "pass";
    profile.sni = "sni.example.com";
    profile.alpn = "h3";
    profile.fingerprint = "chrome";
    profile.path = "/download";
    profile.remarks = "my server";

    share::Hysteria2UriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_TRUE(uri.find("fp=chrome") != std::string::npos);
    EXPECT_TRUE(uri.find("path=%2Fdownload") != std::string::npos);
    EXPECT_TRUE(uri.find("#my%20server") != std::string::npos);
}
