// tests/ui/TestClear.cpp - Task 8: log-clear button invoke via UIA.
#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include "framework/Fixtures.h"

using namespace uitest;

TEST_CASE("Log panel clear button invokes", "[clear]") {
    AppFixture fx;
    auto win = fx.mainWindow();
    REQUIRE(win.valid());

    auto btn = uitest::UiElement::findBy(
        win, { UIA_NamePropertyId, uitest::ids::LogClearButtonName },
        fx.app().pid(), 5000);
    REQUIRE(btn.valid());
    REQUIRE(btn.isEnabled());
    REQUIRE(btn.click());                        // InvokePattern on real Win32 BUTTON
    ::Sleep(300);
    REQUIRE(fx.app().running());                 // click handled, app alive
}
