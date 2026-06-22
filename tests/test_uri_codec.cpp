#include <gtest/gtest.h>
#include <string>
#include "share/UriCodec.h"

TEST(UriCodecTest, Base64EncodeHello) {
    std::string result = share::UriCodec::base64Encode("hello");
    EXPECT_EQ(result, "aGVsbG8=");
}

TEST(UriCodecTest, Base64EncodeEmpty) {
    EXPECT_TRUE(share::UriCodec::base64Encode("").empty());
}

TEST(UriCodecTest, Base64DecodeHello) {
    std::string result = share::UriCodec::base64Decode("aGVsbG8=");
    EXPECT_EQ(result, "he@lo");
}

TEST(UriCodecTest, Base64DecodeShortString) {
    std::string result = share::UriCodec::base64Decode("YQ==");
    EXPECT_EQ(result, "a");
}

TEST(UriCodecTest, Base64DecodeTwoChars) {
    std::string result = share::UriCodec::base64Decode("YWI=");
    EXPECT_EQ(result, "ab");
}

TEST(UriCodecTest, UrlEncodeStandardSpaces) {
    std::string result = share::UriCodec::urlEncodeStandard("a b");
    EXPECT_EQ(result, "a%20b");
}

TEST(UriCodecTest, UrlEncodeStandardSpecial) {
    std::string result = share::UriCodec::urlEncodeStandard("a=b");
    EXPECT_EQ(result, "a%3Db");
}

TEST(UriCodecTest, UrlEncodeRemarksEncodesEquals) {
    std::string result = share::UriCodec::urlEncodeRemarksOrPath("a=b");
    EXPECT_EQ(result, "a%3Db");
}

TEST(UriCodecTest, UrlEncodeRemarksEncodesNonAscii) {
    std::string result = share::UriCodec::urlEncodeRemarksOrPath("\xe4\xb8\xad");
    EXPECT_TRUE(result.find('%') != std::string::npos);
}

TEST(UriCodecTest, JsonEncodeEscapesQuotes) {
    std::string result = share::UriCodec::jsonEncode("say \"hello\"");
    EXPECT_EQ(result, "say \\\"hello\\\"");
}

TEST(UriCodecTest, JsonEncodeEscapesBackslash) {
    std::string result = share::UriCodec::jsonEncode("a\\b");
    EXPECT_EQ(result, "a\\\\b");
}

TEST(UriCodecTest, BuildQueryStringBasic) {
    std::map<std::string, std::string> params;
    params["key1"] = "value1";
    params["key2"] = "value2";
    std::string result = share::UriCodec::buildQueryString(params);
    EXPECT_TRUE(result.find("key1=value1") != std::string::npos ||
                result.find("key1=value1&key2=value2") != std::string::npos ||
                result.find("key2=value2&key1=value1") != std::string::npos);
}

TEST(UriCodecTest, BuildQueryStringEmptyParams) {
    std::map<std::string, std::string> params;
    params["key"] = "";
    std::string result = share::UriCodec::buildQueryString(params);
    EXPECT_TRUE(result.empty());
}
