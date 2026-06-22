#include <gtest/gtest.h>
#include "share/Ipv6Formatter.h"

TEST(Ipv6FormatterTest, FormatIpv4Unchanged) {
    std::string result = share::Ipv6Formatter::formatAddress("192.168.1.1");
    EXPECT_EQ(result, "192.168.1.1");
}

TEST(Ipv6FormatterTest, FormatIpv6Loopback) {
    std::string result = share::Ipv6Formatter::formatAddress("::1");
    EXPECT_EQ(result, "[::1]");
}

TEST(Ipv6FormatterTest, FormatIpv6Full) {
    std::string result = share::Ipv6Formatter::formatAddress("2001:db8::1");
    EXPECT_EQ(result, "[2001:db8::1]");
}

TEST(Ipv6FormatterTest, FormatIpv6AlreadyBracketed) {
    std::string result = share::Ipv6Formatter::formatAddress("[::1]");
    EXPECT_EQ(result, "[::1]");
}

TEST(Ipv6FormatterTest, FormatEmptyString) {
    std::string result = share::Ipv6Formatter::formatAddress("");
    EXPECT_TRUE(result.empty());
}

TEST(Ipv6FormatterTest, IsValidIpv6WithBrackets) {
    EXPECT_TRUE(share::Ipv6Formatter::isValidIpv6("[::1]"));
}

TEST(Ipv6FormatterTest, IsValidIpv6WithoutBrackets) {
    EXPECT_FALSE(share::Ipv6Formatter::isValidIpv6("192.168.1.1"));
    EXPECT_FALSE(share::Ipv6Formatter::isValidIpv6("::1"));
}
