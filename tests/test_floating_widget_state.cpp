// tests/test_floating_widget_state.cpp - TDD unit tests for the pure
// floating-widget policy (no wx dependency).
#include <gtest/gtest.h>

#include "FloatingWidgetPolicy.h"

TEST(FloatingWidgetStateTest, ClampIntervalMsLower) {
    EXPECT_EQ(FloatingWidgetPolicy::clampIntervalMs(0), 5000);
    EXPECT_EQ(FloatingWidgetPolicy::clampIntervalMs(1000), 5000);
    EXPECT_EQ(FloatingWidgetPolicy::clampIntervalMs(4999), 5000);
    EXPECT_EQ(FloatingWidgetPolicy::clampIntervalMs(5000), 5000);
}

TEST(FloatingWidgetStateTest, ClampIntervalMsWithin) {
    EXPECT_EQ(FloatingWidgetPolicy::clampIntervalMs(15000), 15000);
    EXPECT_EQ(FloatingWidgetPolicy::clampIntervalMs(30000), 30000);
    EXPECT_EQ(FloatingWidgetPolicy::clampIntervalMs(120000), 120000);
    EXPECT_EQ(FloatingWidgetPolicy::clampIntervalMs(300000), 300000);
}

TEST(FloatingWidgetStateTest, ClampIntervalMsUpper) {
    EXPECT_EQ(FloatingWidgetPolicy::clampIntervalMs(300001), 300000);
    EXPECT_EQ(FloatingWidgetPolicy::clampIntervalMs(999999), 300000);
}

TEST(FloatingWidgetStateTest, ShouldShowCollapsedTruthTable) {
    EXPECT_FALSE(FloatingWidgetPolicy::shouldShowCollapsed(false, false));
    EXPECT_FALSE(FloatingWidgetPolicy::shouldShowCollapsed(false, true));
    EXPECT_FALSE(FloatingWidgetPolicy::shouldShowCollapsed(true, false));
    EXPECT_TRUE(FloatingWidgetPolicy::shouldShowCollapsed(true, true));
}

TEST(FloatingWidgetStateTest, ShouldExpandTruthTable) {
    EXPECT_FALSE(FloatingWidgetPolicy::shouldExpand(false, false));
    EXPECT_FALSE(FloatingWidgetPolicy::shouldExpand(false, true));
    EXPECT_FALSE(FloatingWidgetPolicy::shouldExpand(true, false));
    EXPECT_TRUE(FloatingWidgetPolicy::shouldExpand(true, true));
}

TEST(FloatingWidgetStateTest, FormatDurationBelowOneHour) {
    EXPECT_EQ(FloatingWidgetPolicy::formatDuration(0), "00:00");
    EXPECT_EQ(FloatingWidgetPolicy::formatDuration(5000), "00:05");
    EXPECT_EQ(FloatingWidgetPolicy::formatDuration(65000), "01:05");
    EXPECT_EQ(FloatingWidgetPolicy::formatDuration(3599000), "59:59");
}

TEST(FloatingWidgetStateTest, FormatDurationAtLeastOneHour) {
    EXPECT_EQ(FloatingWidgetPolicy::formatDuration(3600000), "1:00:00");
    EXPECT_EQ(FloatingWidgetPolicy::formatDuration(3661000), "1:01:01");
    EXPECT_EQ(FloatingWidgetPolicy::formatDuration(7325000), "2:02:05");
}

// ----- v1.2 circular-widget policy -----

TEST(FloatingWidgetStateTest, ClampRadius) {
    EXPECT_EQ(FloatingWidgetPolicy::clampRadius(0), 24);
    EXPECT_EQ(FloatingWidgetPolicy::clampRadius(36), 36);
    EXPECT_EQ(FloatingWidgetPolicy::clampRadius(200), 80);
    EXPECT_EQ(FloatingWidgetPolicy::clampRadius(50), 50);
}

// v1.3: orb 默认半径由 36 缩小 30% -> 25（直径 72 -> ~50px），且须在有效区间内。
TEST(FloatingWidgetStateTest, DefaultRadiusShrunkBy30Percent) {
    EXPECT_EQ(FloatingWidgetPolicy::CircleDefaults::kDefaultRadius, 25);
    EXPECT_EQ(FloatingWidgetPolicy::clampRadius(
                  FloatingWidgetPolicy::CircleDefaults::kDefaultRadius),
              FloatingWidgetPolicy::CircleDefaults::kDefaultRadius);
}

TEST(FloatingWidgetStateTest, ClampToScreenKeepsOnScreen) {
    FloatingWidgetPolicy::ScreenAnchor a;
    a.screenW = 1920; a.screenH = 1080; a.margin = 8;
    int x = -50, y = -50, w = 100, h = 100;
    FloatingWidgetPolicy::clampToScreen(x, y, w, h, a);
    EXPECT_EQ(x, 8);
    EXPECT_EQ(y, 8);

    int x2 = 1900, y2 = 2000, w2 = 100, h2 = 100;
    FloatingWidgetPolicy::clampToScreen(x2, y2, w2, h2, a);
    EXPECT_EQ(x2, 1920 - 8 - 100);
    EXPECT_EQ(y2, 1080 - 8 - 100);
}

TEST(FloatingWidgetStateTest, NearestEdgePicksClosest) {
    EXPECT_EQ(static_cast<int>(FloatingWidgetPolicy::nearestEdge(10, 540, 1920, 1080)),
              static_cast<int>(FloatingWidgetPolicy::DockEdge::Left));
    EXPECT_EQ(static_cast<int>(FloatingWidgetPolicy::nearestEdge(1910, 540, 1920, 1080)),
              static_cast<int>(FloatingWidgetPolicy::DockEdge::Right));
    EXPECT_EQ(static_cast<int>(FloatingWidgetPolicy::nearestEdge(960, 5, 1920, 1080)),
              static_cast<int>(FloatingWidgetPolicy::DockEdge::Top));
    EXPECT_EQ(static_cast<int>(FloatingWidgetPolicy::nearestEdge(960, 1075, 1920, 1080)),
              static_cast<int>(FloatingWidgetPolicy::DockEdge::Bottom));
}

TEST(FloatingWidgetStateTest, DockPositionRightEdge) {
    FloatingWidgetPolicy::ScreenAnchor a;
    a.screenW = 1920; a.screenH = 1080; a.margin = 8;
    int x = 0, y = 0;
    FloatingWidgetPolicy::dockPosition(FloatingWidgetPolicy::DockEdge::Right, 72, 72, a, x, y);
    EXPECT_EQ(x, 1920 - 72 - 8);
    EXPECT_EQ(y, (1080 - 72) / 2);
}

TEST(FloatingWidgetStateTest, HideDelaySliderRoundTrip) {
    EXPECT_EQ(FloatingWidgetPolicy::clampHideDelayMs(0), 300);
    EXPECT_EQ(FloatingWidgetPolicy::clampHideDelayMs(99999), 4000);
    EXPECT_EQ(FloatingWidgetPolicy::sliderToHideDelay(0), 300);
    EXPECT_EQ(FloatingWidgetPolicy::sliderToHideDelay(1000), 4000);
    const int mid = FloatingWidgetPolicy::sliderToHideDelay(500);
    EXPECT_EQ(FloatingWidgetPolicy::hideDelayToSlider(mid), 500);
    EXPECT_EQ(FloatingWidgetPolicy::dragThresholdPx(), 4);
}
