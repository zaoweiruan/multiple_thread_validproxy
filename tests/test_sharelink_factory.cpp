#include <gtest/gtest.h>
#include "share/ShareLinkFactory.h"

TEST(ShareLinkFactoryTest, RouteToVLESS) {
    db::models::Profileitem profile;
    profile.configtype = "5";
    profile.address = "example.com";
    profile.port = "443";
    profile.id = "uuid123";
    profile.streamsecurity = "tls";

    share::ShareLinkFactory factory;
    std::string uri = factory.toShareUri(profile);

    EXPECT_TRUE(uri.find("vless://") == 0);
}

TEST(ShareLinkFactoryTest, RouteToVMess) {
    db::models::Profileitem profile;
    profile.configtype = "1";
    profile.address = "example.com";
    profile.port = "443";
    profile.id = "uuid123";
    profile.security = "auto";
    profile.streamsecurity = "tls";

    share::ShareLinkFactory factory;
    std::string uri = factory.toShareUri(profile);

    EXPECT_TRUE(uri.find("vmess://") == 0);
}

TEST(ShareLinkFactoryTest, RouteToTrojan) {
    db::models::Profileitem profile;
    profile.configtype = "6";
    profile.address = "example.com";
    profile.port = "443";
    profile.id = "password123";
    profile.streamsecurity = "tls";

    share::ShareLinkFactory factory;
    std::string uri = factory.toShareUri(profile);

    EXPECT_TRUE(uri.find("trojan://") == 0);
}

TEST(ShareLinkFactoryTest, RouteToSS) {
    db::models::Profileitem profile;
    profile.configtype = "3";
    profile.address = "example.com";
    profile.port = "8388";
    profile.id = "password123";
    profile.security = "aes-256-gcm";

    share::ShareLinkFactory factory;
    std::string uri = factory.toShareUri(profile);

    EXPECT_TRUE(uri.find("ss://") == 0);
}

TEST(ShareLinkFactoryTest, RouteToHysteria2) {
    db::models::Profileitem profile;
    profile.configtype = "7";
    profile.address = "example.com";
    profile.port = "443";
    profile.id = "pass123";

    share::ShareLinkFactory factory;
    std::string uri = factory.toShareUri(profile);

    EXPECT_TRUE(uri.find("hy2://") == 0);
}

TEST(ShareLinkFactoryTest, RouteToTUIC) {
    db::models::Profileitem profile;
    profile.configtype = "8";
    profile.address = "tuic.example.com";
    profile.port = "8443";
    profile.id = "uuid-tuic";
    profile.streamsecurity = "tls";
    profile.sni = "sni.tuic.com";

    share::ShareLinkFactory factory;
    std::string uri = factory.toShareUri(profile);

    EXPECT_TRUE(uri.find("vless://") == 0);
}

TEST(ShareLinkFactoryTest, UnsupportedSocks) {
    db::models::Profileitem profile;
    profile.configtype = "4";
    profile.address = "example.com";
    profile.port = "1080";
    profile.id = "user";

    share::ShareLinkFactory factory;
    std::string uri = factory.toShareUri(profile);

    EXPECT_TRUE(uri.empty());
}

TEST(ShareLinkFactoryTest, UnsupportedWireGuard) {
    db::models::Profileitem profile;
    profile.configtype = "9";

    share::ShareLinkFactory factory;
    std::string uri = factory.toShareUri(profile);

    EXPECT_TRUE(uri.empty());
}

TEST(ShareLinkFactoryTest, UnsupportedHttp) {
    db::models::Profileitem profile;
    profile.configtype = "10";

    share::ShareLinkFactory factory;
    std::string uri = factory.toShareUri(profile);

    EXPECT_TRUE(uri.empty());
}

TEST(ShareLinkFactoryTest, UnknownType) {
    db::models::Profileitem profile;
    profile.configtype = "999";
    profile.address = "example.com";
    profile.port = "80";
    profile.id = "id";

    share::ShareLinkFactory factory;
    std::string uri = factory.toShareUri(profile);

    EXPECT_TRUE(uri.empty());
}
