// tests/ui/TestSearch.cpp - Task 8: search box round-trip via ValuePattern.
#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include "framework/Fixtures.h"

using namespace uitest;

TEST_CASE("Search box accepts and echoes text", "[search]") {
    AppFixture fx;
    auto win = fx.mainWindow();
    REQUIRE(win.valid());

    // Live dump (Task 6): "searchCtrl" is a wx container panel; the real
    // text entry is its inner Edit child. ValuePattern lives on the Edit.
    auto wrap = uitest::UiElement::findBy(
        win, { UIA_NamePropertyId, uitest::ids::SearchCtrlName },
        fx.app().pid(), 15000);
    REQUIRE(wrap.valid());                       // locator pinned in Task 6

    auto box = uitest::UiElement::findBy(
        wrap, { UIA_ClassNamePropertyId, L"Edit" },
        fx.app().pid(), 8000);
    REQUIRE(box.valid());
    REQUIRE(box.setText(L"HK"));
    REQUIRE(box.getText() == L"HK");

    // Filtering must not crash or hang the app.
    ::Sleep(500);
    REQUIRE(fx.app().running());
}
