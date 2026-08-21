// Tests for RegionBatchResolver — ipwho.is region resolution.
// Tests parseRegionFromJson() with static method only;
// fetchRegionFromIpWhoIs() calls real network and is skipped in CI.
#include "RegionBatchResolver.h"
#include <gtest/gtest.h>
#include <string>

using namespace config;

// ---------------------------------------------------------------------------
// Test: parseRegionFromJson() handles standard JSON response
// ---------------------------------------------------------------------------
TEST(RegionBatchResolverTest, ParseRegionFromJson_StandardResponse) {
    std::string json = "{\"ip\":\"1.2.3.4\",\"success\":true,\"country\":\"Japan\",\"country_code\":\"JP\"}";
    std::string region = RegionBatchResolver::parseRegionFromJson(json);
    EXPECT_EQ(region, "JAPAN");
}

// ---------------------------------------------------------------------------
// Test: parseRegionFromJson() trims whitespace from country name
// ---------------------------------------------------------------------------
TEST(RegionBatchResolverTest, ParseRegionFromJson_WhitespaceTrimmed) {
    std::string json = "{\"success\":true,\"country\":\" South Korea \",\"country_code\":\"KR\"}";
    std::string region = RegionBatchResolver::parseRegionFromJson(json);
    EXPECT_EQ(region, "SOUTH KOREA");
}

// ---------------------------------------------------------------------------
// Test: parseRegionFromJson() handles empty string input
// ---------------------------------------------------------------------------
TEST(RegionBatchResolverTest, ParseRegionFromJson_EmptyString) {
    std::string region = RegionBatchResolver::parseRegionFromJson("");
    EXPECT_EQ(region, "");
}

// ---------------------------------------------------------------------------
// Test: parseRegionFromJson() handles invalid JSON input
// ---------------------------------------------------------------------------
TEST(RegionBatchResolverTest, ParseRegionFromJson_InvalidJson) {
    std::string region = RegionBatchResolver::parseRegionFromJson("not json");
    EXPECT_EQ(region, "");
}

// ---------------------------------------------------------------------------
// Test: parseRegionFromJson() handles missing country field
// ---------------------------------------------------------------------------
TEST(RegionBatchResolverTest, ParseRegionFromJson_MissingCountryField) {
    std::string json = "{\"status\":\"fail\"}";
    std::string region = RegionBatchResolver::parseRegionFromJson(json);
    EXPECT_EQ(region, "");
}

// ---------------------------------------------------------------------------
// Test: parseRegionFromJson() handles ipwho.is failure response
// ({"success":false,"message":"..."} has no 'country' field)
// ---------------------------------------------------------------------------
TEST(RegionBatchResolverTest, ParseRegionFromJson_IpWhoIsFailureResponse) {
    std::string json = "{\"success\":false,\"message\":\"404 not found\"}";
    std::string region = RegionBatchResolver::parseRegionFromJson(json);
    EXPECT_EQ(region, "");
}
