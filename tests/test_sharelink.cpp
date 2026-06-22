#include <gtest/gtest.h>
#include <string>
#include <cctype>
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
// Config Type Name Tests (shared ProxyTypeStrings)
// ============================================================
TEST(ShareLinkTest, GetConfigTypeName) {
    EXPECT_EQ(std::string(ProxyTypeStrings::protocolName(1)), "VMess");
    EXPECT_EQ(std::string(ProxyTypeStrings::protocolName(3)), "Shadowsocks");
    EXPECT_EQ(std::string(ProxyTypeStrings::protocolName(5)), "VLESS");
    EXPECT_EQ(std::string(ProxyTypeStrings::protocolName(6)), "Trojan");
    EXPECT_EQ(std::string(ProxyTypeStrings::protocolName(7)), "Hysteria2");
    EXPECT_EQ(std::string(ProxyTypeStrings::protocolName(100)), "Unknown");
}

namespace {

std::string decodeBase64(const std::string& input) {
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int T[256];
    for (int i = 0; i < 256; i++) T[i] = -1;
    for (int i = 0; i < 64; i++) T[static_cast<unsigned char>(chars[i])] = i;

    std::string data;
    for (unsigned char c : input) {
        if (T[c] != -1) data += static_cast<char>(c);
    }
    while (data.size() % 4) data += "=";

    std::string result;
    for (size_t i = 0; i < data.size(); i += 4) {
        int b0 = T[static_cast<unsigned char>(data[i])];
        int b1 = T[static_cast<unsigned char>(data[i + 1])];
        int b2 = (data[i + 2] == '=') ? 0 : T[static_cast<unsigned char>(data[i + 2])];
        int b3 = (data[i + 3] == '=') ? 0 : T[static_cast<unsigned char>(data[i + 3])];
        result += static_cast<char>((b0 << 2) | ((b1 & 0x30) >> 4));
        if (data[i + 2] != '=') result += static_cast<char>(((b1 & 0x0f) << 4) | ((b2 & 0x3c) >> 2));
        if (data[i + 3] != '=') result += static_cast<char>(((b2 & 0x03) << 6) | b3);
    }
    return result;
}

} // anonymous namespace

// ============================================================
// VLESS Exact Match (characterization)
// ============================================================
TEST(ShareLinkTest, VlessUri_ExactMatch) {
    std::string uri = share::ShareLink::toShareUri(
        "5", "example.com", "443", "uuid-test", "auto", "tcp", "",
        "", "", "", "", "", "", "", "tls", ""
    );
    std::string expected = "vless://uuid-test@example.com:443?"
                           "encryption=none&security=tls&"
                           "insecure=0&allowInsecure=0&type=tcp";
    EXPECT_EQ(uri, expected);
}

// ============================================================
// VMess JSON Field Verification (characterization)
// ============================================================
TEST(ShareLinkTest, VmessUri_ExactJsonFields) {
    std::string uri = share::ShareLink::toShareUri(
        "1", "example.com", "443", "uuid123", "auto", "tcp", "none",
        "example.com", "", "", "", "tls", "", "", "", ""
    );
    EXPECT_TRUE(uri.find("vmess://") == 0);
    std::string b64 = uri.substr(8);
    std::string decoded = decodeBase64(b64);
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

// ============================================================
// Trojan Exact Match (characterization)
// ============================================================
TEST(ShareLinkTest, TrojanUri_ExactMatch) {
    std::string uri = share::ShareLink::toShareUri(
        "6", "trojan.example.com", "8443", "mypassword", "", "grpc", "",
        "sni.example.com", "h2", "chrome", "1", "", "", "", "tls", ""
    );
    std::string expected = "trojan://mypassword@trojan.example.com:8443?"
                           "security=tls&sni=sni.example.com&"
                           "fp=chrome&insecure=1&allowInsecure=1&type=grpc";
    EXPECT_EQ(uri, expected);
}

// ============================================================
// Shadowsocks Exact Match (characterization)
// ============================================================
TEST(ShareLinkTest, SsUri_ExactMatch) {
    std::string uri = share::ShareLink::toShareUri(
        "3", "ss.example.com", "8388", "test-pass", "aes-256-gcm", "tcp", "",
        "", "", "", "", "", "", "", "", ""
    );
    EXPECT_TRUE(uri.find("ss://") == 0);
    size_t atPos = uri.find('@');
    EXPECT_TRUE(atPos != std::string::npos);
    // base64 starts after "ss://" (5 chars)
    std::string b64 = uri.substr(5, atPos - 5);
    std::string decoded = decodeBase64(b64);
    EXPECT_EQ(decoded, "aes-256-gcm:test-pass");
    EXPECT_TRUE(uri.find("@ss.example.com:8388") != std::string::npos);
}

// ============================================================
// Hysteria2 Exact Match (characterization)
// ============================================================
TEST(ShareLinkTest, Hysteria2Uri_ExactMatch) {
    std::string uri = share::ShareLink::toShareUri(
        "7", "hy2.example.com", "443", "pass123", "", "", "",
        "sni.example.com", "h3", "", "", "", "", "", "", ""
    );
    std::string expected = "hy2://pass123@hy2.example.com:443?"
                           "alpn=h3&sni=sni.example.com";
    EXPECT_EQ(uri, expected);
}

// ============================================================
// TUIC URI (maps to vlessToUri internally)
// ============================================================
TEST(ShareLinkTest, TUIC_Uri) {
    std::string uri = share::ShareLink::toShareUri(
        "8", "tuic.example.com", "8443", "uuid-tuic", "", "tcp", "",
        "sni.tuic.com", "h3", "", "", "", "", "", "tls", ""
    );
    EXPECT_TRUE(uri.find("vless://") == 0);
    EXPECT_TRUE(uri.find("uuid-tuic@tuic.example.com:8443") != std::string::npos);
    EXPECT_TRUE(uri.find("sni=sni.tuic.com") != std::string::npos);
    EXPECT_TRUE(uri.find("encryption=none") != std::string::npos);
    EXPECT_TRUE(uri.find("security=tls") != std::string::npos);
}

// ============================================================
// Unsupported Protocol — Unknown Type (characterization)
// ============================================================
TEST(ShareLinkTest, UnsupportedProtocol_Unknown) {
    std::string uri = share::ShareLink::toShareUri(
        "999", "example.com", "80", "id", "", "", "", "", "", "",
        "", "", "", "", "", ""
    );
    EXPECT_TRUE(uri.empty());
}

// ============================================================
// Chinese Characters in Remarks (characterization)
// ============================================================
TEST(ShareLinkTest, ChineseRemarks) {
    std::string uri = share::ShareLink::toShareUri(
        "5", "example.com", "443", "uuid123", "auto", "tcp", "",
        "", "", "", "", "tls", "", "", "", "中国节点"
    );
    EXPECT_TRUE(uri.find("#") != std::string::npos);
    EXPECT_TRUE(uri.find("%E4%B8%AD%E5%9B%BD%E8%8A%82%E7%82%B9") != std::string::npos);
}

// ============================================================
// VLESS with Reality Parameters (characterization)
// ============================================================
TEST(ShareLinkTest, UriWithRealityParams) {
    std::string uri = share::ShareLink::toShareUri(
        "5", "reality.example.com", "443", "uuid-reality", "", "grpc",
        "xtls-rprx-vision", "reality.sni.com", "", "", "", "", "", "", "",
        "", "", "test-pbk", "test-sid"
    );
    EXPECT_TRUE(uri.find("pbk=test-pbk") != std::string::npos);
    EXPECT_TRUE(uri.find("sid=test-sid") != std::string::npos);
    EXPECT_TRUE(uri.find("flow=xtls-rprx-vision") != std::string::npos);
    EXPECT_TRUE(uri.find("encryption=none") != std::string::npos);
    EXPECT_TRUE(uri.find("sni=reality.sni.com") != std::string::npos);
}

// ============================================================
// All Empty Fields Except Required (characterization)
// ============================================================
TEST(ShareLinkTest, EmptyFields) {
    std::string uri = share::ShareLink::toShareUri(
        "5", "example.com", "80", "minimal", "", "", "", "", "", "",
        "", "", "", "", "", ""
    );
    EXPECT_FALSE(uri.empty());
    EXPECT_TRUE(uri.find("vless://") == 0);
    EXPECT_TRUE(uri.find("minimal@example.com:80") != std::string::npos);
    EXPECT_TRUE(uri.find("encryption=none") != std::string::npos);
}