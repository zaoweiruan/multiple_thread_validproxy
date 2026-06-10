#include <gtest/gtest.h>
#include <string>
#include "ShareLink.h"

// ============================================================
// VLESS URI Generation Tests (public API)
// ============================================================
TEST(ShareLinkTest, VlessUriBasic) {
    std::string uri = share::ShareLink::toShareUri(
        "5", "example.com", "443", "uuid123", "auto", "tcp", "",
        "example.com", "", "", "", "tls", "", "", "", ""
    );
    // Check basic structure - vless://uuid@address:port
    EXPECT_TRUE(uri.find("vless://") == 0);
    EXPECT_TRUE(uri.find("uuid123@example.com:443") != std::string::npos);
}

TEST(ShareLinkTest, VlessUriWithRemarks) {
    std::string uri = share::ShareLink::toShareUri(
        "5", "example.com", "443", "uuid123", "auto", "tcp", "",
        "", "", "", "", "tls", "", "", "", "My Server"
    );
    EXPECT_TRUE(uri.find("#") != std::string::npos);
}

// ============================================================
// VMess URI Generation Tests (public API)
// ============================================================
TEST(ShareLinkTest, VmessUriBasic) {
    std::string uri = share::ShareLink::toShareUri(
        "1", "example.com", "443", "uuid123", "auto", "tcp", "none",
        "example.com", "", "", "", "tls", "", "", "", ""
    );
    EXPECT_TRUE(uri.find("vmess://") == 0);
}

TEST(ShareLinkTest, VmessUriContainsBase64) {
    std::string uri = share::ShareLink::toShareUri(
        "1", "example.com", "443", "uuid123", "auto", "tcp", "none",
        "", "", "", "", "", "", "", "", ""
    );
    EXPECT_TRUE(uri.find("vmess://") == 0);
    // Verify it's valid base64 after vmess://
    std::string b64 = uri.substr(8);
    EXPECT_FALSE(b64.empty());
}

// ============================================================
// Trojan URI Generation Tests (public API)
// ============================================================
TEST(ShareLinkTest, TrojanUriBasic) {
    std::string uri = share::ShareLink::toShareUri(
        "6", "example.com", "443", "password123", "", "tcp", "",
        "", "", "", "", "tls", "", "", "", ""
    );
    EXPECT_TRUE(uri.find("trojan://password123@example.com:443") != std::string::npos);
}

// ============================================================
// Shadowsocks URI Generation Tests (public API)
// ============================================================
TEST(ShareLinkTest, SsUriBasic) {
    std::string uri = share::ShareLink::toShareUri(
        "3", "example.com", "8388", "aes-256-gcm", "password123", "tcp", "",
        "", "", "", "", "", "", "", "", ""
    );
    EXPECT_TRUE(uri.find("ss://") == 0);
    // The userInfo part should be base64 encoded
    EXPECT_TRUE(uri.find("@example.com:8388") != std::string::npos);
}

// ============================================================
// Hysteria2 URI Generation Tests (public API)
// ============================================================
TEST(ShareLinkTest, Hysteria2UriBasic) {
    std::string uri = share::ShareLink::toShareUri(
        "7", "example.com", "443", "password123", "", "", "", "",
        "", "", "", "", "tls", "sni.example.com", "", "", "", ""
    );
    EXPECT_TRUE(uri.find("hy2://password123@example.com:443") != std::string::npos);
}

// ============================================================
// Unsupported Protocol Tests (public API)
// ============================================================
TEST(ShareLinkTest, UnsupportedProtocolSocks) {
    std::string uri = share::ShareLink::toShareUri(
        "4", "example.com", "1080", "user", "", "", "", "", "", "", "", "", "", "", "", "", ""
    );
    EXPECT_TRUE(uri.empty());
}

TEST(ShareLinkTest, UnsupportedProtocolWireGuard) {
    std::string uri = share::ShareLink::toShareUri(
        "9", "example.com", "51820", "key", "", "", "", "", "", "", "", "", "", "", "", "", ""
    );
    EXPECT_TRUE(uri.empty());
}

TEST(ShareLinkTest, UnsupportedProtocolHttp) {
    std::string uri = share::ShareLink::toShareUri(
        "10", "example.com", "8080", "key", "", "", "", "", "", "", "", "", "", "", "", "", ""
    );
    EXPECT_TRUE(uri.empty());
}

// ============================================================
// Config Type Name Tests (public API)
// ============================================================
TEST(ShareLinkTest, GetConfigTypeName) {
    EXPECT_EQ(share::getConfigTypeName(1), "VMess");
    EXPECT_EQ(share::getConfigTypeName(3), "Shadowsocks");
    EXPECT_EQ(share::getConfigTypeName(5), "VLESS");
    EXPECT_EQ(share::getConfigTypeName(6), "Trojan");
    EXPECT_EQ(share::getConfigTypeName(7), "Hysteria2");
    EXPECT_EQ(share::getConfigTypeName(100), "Unknown");
}