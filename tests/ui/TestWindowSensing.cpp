// tests/ui/TestWindowSensing.cpp - window information reading and window-type
// sensing for the main frame and the floating widget.
//
// Covers: "can read window info" and "can sense window type" from the brief.

#include <catch2/catch_test_macros.hpp>

#include "framework/Fixtures.h"
#include "framework/Text.h"
#include "framework/WindowInfo.h"
#include "UIIds.h"

using namespace uitest;

TEST_CASE("WindowSensing main frame is sensed as a frame", "[window][sensing]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    const WindowInfo info = describeWindow(fx.app().mainWindow());
    INFO(toUtf8(windowInfoLine(info)));

    REQUIRE(info.valid());
    CHECK(info.kind == WindowKind::Frame);
    CHECK(info.pid == fx.pid());
    CHECK(info.visible);
    CHECK(info.hasCaption);
    CHECK_FALSE(info.isPopup);
    CHECK_FALSE(info.isChild);
    CHECK(info.title == ids::MainWindowName);
    CHECK(info.className == ids::MainWindowClass);
    CHECK(info.width() > 0);
    CHECK(info.height() > 0);
    CHECK(info.enabled);
}

TEST_CASE("WindowSensing enumerates every top level window of the app",
          "[window][sensing]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    const std::vector<WindowInfo> windows = describeTopLevelWindows(fx.pid(), true);
    INFO("top level count: " << windows.size());
    for (size_t i = 0; i < windows.size(); ++i) {
        INFO("[" << i << "] " << toUtf8(windowInfoLine(windows[i])));
    }

    REQUIRE_FALSE(windows.empty());
    bool sawMain = false;
    for (size_t i = 0; i < windows.size(); ++i) {
        CHECK(windows[i].pid == fx.pid());
        CHECK(windows[i].visible);
        if (windows[i].kind == WindowKind::Frame &&
            windows[i].title == ids::MainWindowName) {
            sawMain = true;
        }
    }
    CHECK(sawMain);
}

TEST_CASE("WindowSensing kind lookup rejects a kind that is absent",
          "[window][sensing]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    // Nothing should claim to be a message box on a clean startup.
    const WindowInfo box = findWindowOfKind(fx.pid(), WindowKind::MessageBox, 500, 100);
    INFO(toUtf8(windowInfoLine(box)));
    CHECK_FALSE(box.valid());

    const WindowInfo frame = findWindowOfKind(fx.pid(), WindowKind::Frame, 3000, 100);
    REQUIRE(frame.valid());
    CHECK(frame.title == ids::MainWindowName);
}

TEST_CASE("WindowSensing float widget is sensed as a tool window",
          "[window][sensing]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    const WindowInfo fw =
        findWindowOfKind(fx.pid(), WindowKind::FloatingWidget, 10000, 150);
    INFO(toUtf8(windowInfoLine(fw)));
    if (!fw.valid()) {
        WARN("floating widget not present; the widget is opt-in in this build");
        return;
    }
    CHECK(fw.title == ids::FloatingWidgetName);
    CHECK(fw.isToolWindow);
    CHECK(fw.isPopup);
}

TEST_CASE("WindowSensing text helpers round-trip", "[window][unit]") {
    const std::wstring source = L"清空日志窗口";
    const std::string narrow = toUtf8(source);
    CHECK_FALSE(narrow.empty());
    CHECK(toWide(narrow) == source);
}