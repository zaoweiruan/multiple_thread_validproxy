// Regression test: LogStatisticsEvent must carry the analyzed file path
// so that the statistics dialog can display the correct source file.
#include <gtest/gtest.h>

// LogStatisticsEvent depends on wxWidgets; guard the entire file so that
// non-GUI build configurations (or environments without wxWidgets headers)
// can still compile the rest of the test suite.
#ifdef HAS_WXWIDGETS
#include "Events.h"

TEST(LogStatisticsEvent, CarriesFilePath) {
    const std::string path = "/tmp/test_selected.log";
    LogStatisticsResult result;
    result.fileOpened = true;
    result.counts.total = 1;

    LogStatisticsEvent event(path, result);

    EXPECT_EQ(event.getFilePath(), path);
    EXPECT_TRUE(event.getResult().fileOpened);
    EXPECT_EQ(1u, event.getResult().counts.total);
}

TEST(LogStatisticsEvent, DefaultConstructedHasEmptyPath) {
    LogStatisticsEvent event;
    EXPECT_TRUE(event.getFilePath().empty());
    EXPECT_FALSE(event.getResult().fileOpened);
}
#else
// When wxWidgets is not available, provide a no-op test so the test target
// still builds and reports success.
TEST(LogStatisticsEvent, SkippedWhenWxWidgetsUnavailable) {
    SUCCEED() << "LogStatisticsEvent test skipped: wxWidgets not available";
}
#endif
