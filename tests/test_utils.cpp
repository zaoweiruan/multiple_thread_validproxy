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
