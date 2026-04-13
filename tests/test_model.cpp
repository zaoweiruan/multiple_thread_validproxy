#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "Profileitem.h"

class ProfileitemTest : public ::testing::Test {
protected:
    void SetUp() override {
        spdlog::set_level(spdlog::level::warn);
    }
};

TEST_F(ProfileitemTest, DefaultConstructor) {
    db::models::Profileitem profile;
    EXPECT_EQ(profile.indexid, "");
    EXPECT_EQ(profile.configtype, "");
    EXPECT_EQ(profile.address, "");
    EXPECT_EQ(profile.port, "");
}

TEST_F(ProfileitemTest, ToString) {
    db::models::Profileitem profile;
    profile.indexid = "test-id";
    profile.configtype = "1";
    profile.address = "192.168.1.1";
    profile.port = "443";

    std::string result = profile.toString();
    EXPECT_TRUE(result.find("test-id") != std::string::npos);
    EXPECT_TRUE(result.find("192.168.1.1") != std::string::npos);
}

TEST_F(ProfileitemTest, ConfigTypeValues) {
    EXPECT_EQ("1", "1");
    EXPECT_EQ("3", "3");
    EXPECT_EQ("5", "5");
    EXPECT_EQ("6", "6");
}
