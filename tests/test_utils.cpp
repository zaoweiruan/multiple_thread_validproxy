#include <gtest/gtest.h>
#include "Utils.h"

TEST(JoinUrlTest, BothWithoutSlash) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/path", "https://sub.com/v2?t=1"),
              "https://acc.com/path/https://sub.com/v2?t=1");
}

TEST(JoinUrlTest, BaseHasTrailingSlash) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/path/", "https://sub.com"),
              "https://acc.com/path/https://sub.com");
}

TEST(JoinUrlTest, SuffixHasLeadingSlash) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/path", "/https://sub.com"),
              "https://acc.com/path/https://sub.com");
}

TEST(JoinUrlTest, BothHaveSlash) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/path/", "/https://sub.com"),
              "https://acc.com/path/https://sub.com");
}

TEST(JoinUrlTest, EmptyBaseReturnsSuffix) {
    EXPECT_EQ(utils::joinUrl("", "https://sub.com"), "https://sub.com");
}

TEST(JoinUrlTest, EmptySuffixReturnsBase) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/path", ""), "https://acc.com/path");
}

TEST(JoinUrlTest, BothEmptyReturnsEmpty) {
    EXPECT_EQ(utils::joinUrl("", ""), "");
}

TEST(JoinUrlTest, BaseAlreadyEndsWithSlash) {
    EXPECT_EQ(utils::joinUrl("https://acc.com/", "sub?t=1"),
              "https://acc.com/sub?t=1");
}

TEST(UrlValidationTest, ValidHttpUrl) {
    EXPECT_TRUE(utils::isValidUrlFormat("http://example.com"));
}

TEST(UrlValidationTest, ValidHttpsUrl) {
    EXPECT_TRUE(utils::isValidUrlFormat("https://example.com"));
}

TEST(UrlValidationTest, ValidUrlWithPath) {
    EXPECT_TRUE(utils::isValidUrlFormat("https://example.com/path/to/file"));
}

TEST(UrlValidationTest, ValidUrlWithSubdomain) {
    EXPECT_TRUE(utils::isValidUrlFormat("https://sub.example.com"));
}

TEST(UrlValidationTest, InvalidNoScheme) {
    EXPECT_FALSE(utils::isValidUrlFormat("example.com"));
}

TEST(UrlValidationTest, InvalidFtpScheme) {
    EXPECT_FALSE(utils::isValidUrlFormat("ftp://example.com"));
}

TEST(UrlValidationTest, InvalidNoDomain) {
    EXPECT_FALSE(utils::isValidUrlFormat("http://"));
}

TEST(UrlValidationTest, InvalidEmpty) {
    EXPECT_FALSE(utils::isValidUrlFormat(""));
}

TEST(UrlValidationTest, InvalidDotOnly) {
    EXPECT_FALSE(utils::isValidUrlFormat("http://."));
}

TEST(UrlValidationTest, InvalidLocalhostNoDot) {
    EXPECT_FALSE(utils::isValidUrlFormat("http://localhost"));
}

// Port availability tests — requires Winsock
#include <winsock2.h>
#include <ws2tcpip.h>

TEST(PortCheckTest, IsPortAvailableReturnsFalseWhenPortOccupied) {
    WSADATA wsaData;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsaData), 0);

    // Create a TCP socket and bind to a known port
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(sock, INVALID_SOCKET);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(19876);

    // Bind + listen to occupy the port
    int bindResult = bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ASSERT_EQ(bindResult, 0) << "Cannot bind test port 19876";
    ASSERT_EQ(listen(sock, 1), 0);

    // Now the port should NOT be available
    EXPECT_FALSE(utils::isPortAvailable(19876));

    closesocket(sock);
    WSACleanup();
}

TEST(PortCheckTest, IsPortAvailableReturnsTrueWhenPortFree) {
    // After the occupied port is released, isPortAvailable should return true
    // Use a specific port that should be free
    EXPECT_TRUE(utils::isPortAvailable(19976));
}

TEST(PortCheckTest, FindAvailablePortReturnsNextFreePort) {
    // Create a listening socket to block a specific port
    WSADATA wsaData;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsaData), 0);

    SOCKET testSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(testSock, INVALID_SOCKET);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(19877);

    ASSERT_EQ(bind(testSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(listen(testSock, 1), 0);

    // findAvailablePort should skip port 19877 and return 19878 (or higher)
    int found = utils::findAvailablePort(19877, 10);
    EXPECT_GT(found, 19877);
    EXPECT_LE(found, 19887);

    closesocket(testSock);
    WSACleanup();
}

TEST(PrintableAsciiTest, EmptyStringReturnsTrue) {
    EXPECT_TRUE(utils::isPrintableAscii(""));
}

TEST(PrintableAsciiTest, PrintableAsciiOnlyReturnsTrue) {
    EXPECT_TRUE(utils::isPrintableAscii("aA1 _-~"));
    EXPECT_TRUE(utils::isPrintableAscii("0123456789"));
    EXPECT_TRUE(utils::isPrintableAscii("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"));
}

TEST(PrintableAsciiTest, ControlCharactersReturnFalse) {
    std::string withNul = "abc";
    withNul.push_back('\0');
    withNul.push_back('d');
    EXPECT_FALSE(utils::isPrintableAscii(withNul));

    std::string withUsAscii31 = "abc";
    withUsAscii31.push_back(static_cast<char>(0x1F));
    EXPECT_FALSE(utils::isPrintableAscii(withUsAscii31));

    std::string withDel = "abc";
    withDel.push_back(static_cast<char>(0x7F));
    EXPECT_FALSE(utils::isPrintableAscii(withDel));
}

TEST(PrintableAsciiTest, Utf8MultibyteReturnsFalse) {
    EXPECT_FALSE(utils::isPrintableAscii("中文"));
}

TEST(PrintableAsciiTest, MixedContentReturnsFalse) {
    EXPECT_FALSE(utils::isPrintableAscii("abc\xE4\xB8\xAD"));
}
