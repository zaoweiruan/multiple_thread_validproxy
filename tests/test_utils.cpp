#include <gtest/gtest.h>
#include <cstdio>
#include <string>

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

// Regression: numeric-leading valid domain names previously rejected by the
// IPv4 parser's early "octet > 255" short-circuit (see
// docs/bugfix/2026-08-12-Bugfix-IsPublicAddress-NumericDomain-v1.0.md).
TEST(IsPublicAddressTest, NumericDomainWithAlphaTLD) {
    EXPECT_TRUE(utils::isPublicAddress("8103.shomaparvazroyadetonnist-bypassishere.lat"));
    EXPECT_TRUE(utils::isPublicAddress("876.outline-vpn.cloud"));
    EXPECT_TRUE(utils::isPublicAddress("1744156156.tencentapp.cn"));
    EXPECT_TRUE(utils::isPublicAddress("67.7777112.xyz"));
    EXPECT_TRUE(utils::isPublicAddress("230920393.f-sub.com"));
}

TEST(IsPublicAddressTest, LeadingZeroNumericDomain) {
    // "09..." is not the "0." pattern of the Rule 1 heuristic, so it must pass.
    EXPECT_TRUE(utils::isPublicAddress("09303582303.ddns.net"));
}

TEST(IsPublicAddressTest, LongNumericSubdomainFallsBackToDomain) {
    EXPECT_TRUE(utils::isPublicAddress(
        "93343878961078676381579883502615.international-ixp.com"));
    EXPECT_TRUE(utils::isPublicAddress(
        "0101010101010101010101001010101010110010101010101010101010101.poki-pakipon.ir"));
}

TEST(IsPublicAddressTest, LoopbackStillRejected) {
    EXPECT_FALSE(utils::isPublicAddress("127.0.0.1"));
    EXPECT_FALSE(utils::isPublicAddress("127.0.0.53"));
    EXPECT_FALSE(utils::isPublicAddress("127.1.1.127"));
}

TEST(IsPublicAddressTest, PrivateRangesStillRejected) {
    EXPECT_FALSE(utils::isPublicAddress("0.0.0.0"));
    EXPECT_FALSE(utils::isPublicAddress("10.1.2.3"));
    EXPECT_FALSE(utils::isPublicAddress("192.168.1.1"));
    EXPECT_FALSE(utils::isPublicAddress("172.16.0.1"));
    EXPECT_FALSE(utils::isPublicAddress("172.31.255.255"));
    EXPECT_FALSE(utils::isPublicAddress("169.254.1.1"));
}

TEST(IsPublicAddressTest, MulticastAndReservedStillRejected) {
    EXPECT_FALSE(utils::isPublicAddress("224.0.0.1"));
    EXPECT_FALSE(utils::isPublicAddress("240.0.0.1"));
}

TEST(IsPublicAddressTest, PublicIpv4Accepted) {
    EXPECT_TRUE(utils::isPublicAddress("8.8.8.8"));
    EXPECT_TRUE(utils::isPublicAddress("1.2.3.4"));
    EXPECT_TRUE(utils::isPublicAddress("172.32.0.1"));
}

TEST(IsPublicAddressTest, MalformedIpv4Rejected) {
    EXPECT_FALSE(utils::isPublicAddress("256.1.1.1"));
    EXPECT_FALSE(utils::isPublicAddress("1.2.3.4.5"));
    EXPECT_FALSE(utils::isPublicAddress("1.2.3"));
    EXPECT_FALSE(utils::isPublicAddress("1."));
    EXPECT_FALSE(utils::isPublicAddress("."));
    EXPECT_FALSE(utils::isPublicAddress("1..2"));
}

TEST(IsPublicAddressTest, NormalDomainAccepted) {
    EXPECT_TRUE(utils::isPublicAddress("example.com"));
    EXPECT_TRUE(utils::isPublicAddress("sub.domain.example.org"));
}

TEST(IsPublicAddressTest, GarbageDomainHeuristicsRejected) {
    EXPECT_FALSE(utils::isPublicAddress("0.example.com"));
    EXPECT_FALSE(utils::isPublicAddress("a.b.c.d.example.org"));
    EXPECT_FALSE(utils::isPublicAddress("0.0.0.einetwork.news"));
}

TEST(IsPublicAddressTest, IpPrefixedDomainFallsBackToDomain) {
    EXPECT_TRUE(utils::isPublicAddress("127.0.0.1.example.com"));
}

TEST(IsPublicAddressTest, EmptyAndSingleLabel) {
    EXPECT_FALSE(utils::isPublicAddress(""));
    EXPECT_TRUE(utils::isPublicAddress("localhost"));
}

TEST(IsPublicAddressTest, Ipv6LiteralUnchanged) {
    EXPECT_FALSE(utils::isPublicAddress("::1"));
    EXPECT_FALSE(utils::isPublicAddress("::"));
    EXPECT_FALSE(utils::isPublicAddress("fe80::1"));
    EXPECT_TRUE(utils::isPublicAddress("2001:4860:4860::8888"));
}

TEST(IsTestResultValidTest, SuccessWithPositiveLatency) {
    EXPECT_TRUE(utils::isTestResultValid(true, 1));
    EXPECT_TRUE(utils::isTestResultValid(true, 100));
}

TEST(IsTestResultValidTest, SuccessWithZeroLatencyIsInvalid) {
    EXPECT_FALSE(utils::isTestResultValid(true, 0));
}

TEST(IsTestResultValidTest, FailureAlwaysInvalid) {
    EXPECT_FALSE(utils::isTestResultValid(false, -1));
    EXPECT_FALSE(utils::isTestResultValid(false, 0));
    EXPECT_FALSE(utils::isTestResultValid(false, 100));
}

// Bugfix 2026-08-21 (StandaloneMonitor-DataColumns): getCurrentTimestamp()
// returns epoch seconds and must never be fed to durationMsBetween(). The
// formatted variant below is the datetime-compatible counterpart.
TEST(GetCurrentTimestampFormattedTest, MatchesDatetimeFormat) {
    const std::string ts = utils::getCurrentTimestampFormatted();
    EXPECT_EQ(static_cast<int>(ts.size()), 19);
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    EXPECT_EQ(std::sscanf(ts.c_str(), "%d-%d-%d %d:%d:%d",
                          &y, &mo, &d, &h, &mi, &se), 6);
    EXPECT_GE(y, 2026);
    EXPECT_GE(mo, 1);
    EXPECT_LE(mo, 12);
    EXPECT_GE(d, 1);
    EXPECT_LE(d, 31);
}

TEST(GetCurrentTimestampFormattedTest, CompatibleWithDurationMsBetweenParser) {
    // Same string twice: a compatible parser yields exactly zero elapsed ms.
    const std::string now = utils::getCurrentTimestampFormatted();
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    ASSERT_EQ(std::sscanf(now.c_str(), "%d-%d-%d %d:%d:%d",
                          &y, &mo, &d, &h, &mi, &se), 6);
}
