// tests/ui/TestMainWindow.cpp - Task 7: first real UI smoke test.
#include <catch2/catch_test_macros.hpp>
#include "framework/Fixtures.h"

using namespace uitest;

TEST_CASE("Application starts and shows main window", "[mainwindow]") {
    AppFixture fx;
    auto win = fx.mainWindow();
    REQUIRE(win.valid());                                        // window appeared
    REQUIRE(win.processId() == fx.app().pid());                  // ours, not another instance
    REQUIRE(win.isEnabled());
    REQUIRE_FALSE(win.isOffscreen());
    REQUIRE(fx.app().running());
}
