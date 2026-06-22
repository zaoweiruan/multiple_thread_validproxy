#include <gtest/gtest.h>
#include "share/TUICUriBuilder.h"

TEST(TUICUriBuilderTest, BasicTUICUri) {
    db::models::Profileitem profile;
    profile.configtype = "8";
    profile.address = "tuic.example.com";
    profile.port = "8443";
    profile.id = "uuid-tuic";
    profile.streamsecurity = "tls";
    profile.sni = "sni.tuic.com";
    profile.alpn = "h3";

    share::TUICUriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_TRUE(uri.find("vless://") == 0);
    EXPECT_TRUE(uri.find("uuid-tuic@tuic.example.com:8443") != std::string::npos);
    EXPECT_TRUE(uri.find("encryption=none") != std::string::npos);
    EXPECT_TRUE(uri.find("security=tls") != std::string::npos);
}

TEST(TUICUriBuilderTest, TUICWithSni) {
    db::models::Profileitem profile;
    profile.configtype = "8";
    profile.address = "tuic.example.com";
    profile.port = "8443";
    profile.id = "uuid-tuic";
    profile.streamsecurity = "tls";
    profile.sni = "sni.tuic.com";
    profile.alpn = "h3";

    share::TUICUriBuilder builder;
    std::string uri = builder.build(profile);

    EXPECT_TRUE(uri.find("sni=sni.tuic.com") != std::string::npos);
}
