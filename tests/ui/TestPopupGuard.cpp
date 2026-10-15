// tests/ui/TestPopupGuard.cpp - error popup detection, error text extraction and
// dismissal, plus the button-activation path.
//
// Covers: "extract error information from error popups" and part of "can click
// UI page buttons" from the brief.
//
// A clean startup must produce NO error popup. That absence is the actual
// assertion: a wxWidgets assertion box or a fatal config dialog would surface
// here as a popup and fail the test with the extracted message attached.

#include <catch2/catch_test_macros.hpp>

#include "framework/Dialog.h"
#include "framework/Fixtures.h"
#include "framework/Text.h"
#include "framework/WindowInfo.h"
#include "UIIds.h"

using namespace uitest;

TEST_CASE("Popup a clean startup produces no error popup", "[popup][guard]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    const DialogReport rep = waitForErrorDialog(fx.pid(), 2000, 200);
    INFO("popup probe: " << toUtf8(dialogSummary(rep)));
    if (rep.found) {
        INFO("title: " << toUtf8(rep.title));
        for (size_t i = 0; i < rep.texts.size(); ++i) {
            INFO("text[" << i << "]: " << toUtf8(rep.texts[i]));
        }
    }
    CHECK_FALSE(rep.found);
    CHECK_FALSE(looksLikeError(rep));
}

TEST_CASE("Popup log buttons respond to a UIA click", "[popup][button]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    const UiElement win = fx.mainWindow();
    const UiElement clear = UiElement::findBy(
        win, Locator{UIA_NamePropertyId, ids::LogClearButtonName}, fx.pid(), 10000);
    REQUIRE(clear.valid());
    REQUIRE(clear.isEnabled());
    INFO("clear button: " << toUtf8(clear.name()) << " / "
                          << toUtf8(clear.className()));

    const bool clicked = clear.click();
    INFO("clicked: " << clicked);
    CHECK(clicked);
    CHECK(fx.app().running());

    // Clicking must not have opened an error popup as a side effect.
    const DialogReport rep = waitForErrorDialog(fx.pid(), 1500, 200);
    INFO("post-click popup: " << toUtf8(dialogSummary(rep)));
    CHECK_FALSE(looksLikeError(rep));
}

TEST_CASE("Popup severity classification prefers keywords over style bits",
          "[popup][unit]") {
    WindowInfo info;
    info.title = L"validproxy";
    // A wxWidgets message box often uses MB_OK with no icon bit even when it is
    // reporting a real failure, so the text has to win.
    CHECK(classifySeverity(info, L"Error: database is locked") ==
          DialogSeverity::Error);
    CHECK(classifySeverity(info, L"连接失败，请重试") == DialogSeverity::Error);
    CHECK(classifySeverity(info, L"Warning: retrying") == DialogSeverity::Warning);
    CHECK(classifySeverity(info, L"订阅更新完成") == DialogSeverity::None);
    CHECK_FALSE(looksLikeError(DialogReport()));
}

TEST_CASE("Popup error code extraction stays conservative", "[popup][unit]") {
    std::vector<std::wstring> withCode;
    withCode.push_back(L"[D8] xray path invalid - popup guard D8");
    CHECK_FALSE(extractErrorCode(withCode).empty());

    std::vector<std::wstring> withNamed;
    withNamed.push_back(L"E-DB-01: cannot open database");
    CHECK_FALSE(extractErrorCode(withNamed).empty());

    std::vector<std::wstring> plain;
    plain.push_back(L"connection reset by peer");
    CHECK(extractErrorCode(plain).empty());
}

TEST_CASE("Popup dismissAllPopups is safe on a clean window", "[popup][guard]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    const int closed = dismissAllPopups(fx.pid());
    INFO("popups dismissed: " << closed);
    CHECK(closed >= 0);
    CHECK(fx.app().running());

    const std::vector<WindowInfo> windows = describeTopLevelWindows(fx.pid(), true);
    for (size_t i = 0; i < windows.size(); ++i) {
        INFO("still open: " << toUtf8(windowInfoLine(windows[i])));
    }
}