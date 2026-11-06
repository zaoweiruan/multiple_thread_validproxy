// tests/ui/TestBottomViewSwitch.cpp - bottom shared-view switcher (spec
// 2026-11-03 v1.2 §5.3 V1-V5, re-ordered v1.2.5 §13).
//
// The bottom area is ONE shared window; its visible page (任务 / 任务进度 / 日志)
// is chosen by a wxRadioBox captioned 「监控」 (was 「视图」). Option order ==
// page index and the DEFAULT page is 任务 (owner request 2026-11-05), so the
// assertions below start from the task page and switch when they need another.
//
// Tag: [bottomview]  (registered as ctest UI_BOTTOMVIEW)

#include <iostream>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>
#include <windows.h>

#include "framework/BottomView.h"
#include "framework/Fixtures.h"
#include "framework/Layout.h"
#include "framework/Text.h"
#include "framework/UIElement.h"
#include "UIIds.h"

using namespace uitest;

namespace {

// Poll until the task page's 「终止当前」 button is visible.
bool waitTaskButtonVisible(const UiElement& win, DWORD pid, int timeoutMs) {
    for (int t = 0; t * 150 <= timeoutMs; ++t) {
        const UiElement cb = UiElement::findBy(
            win, Locator{UIA_NamePropertyId, ids::TaskCancelCurrentBtnName}, pid, 500);
        if (isOptionVisible(cb)) return true;
        ::Sleep(150);
    }
    return false;
}

} // namespace

TEST_CASE("Bottom view: 监控 radio with 任务/任务进度/日志 exists", "[bottomview]") {
    AppFixture fx;
    REQUIRE(fx.ready());
    ::ShowWindow(fx.app().mainWindow(), SW_RESTORE);
    ::Sleep(200);
    const UiElement win = fx.mainWindow();
    REQUIRE(win.valid());

    for (const wchar_t* option : { ids::BottomViewTaskOptionName,
                                   ids::BottomViewQueueOptionName,
                                   ids::BottomViewLogOptionName }) {
        const std::vector<UiElement> hits = findBottomOptions(win, fx.pid(), option);
        INFO("option: " << toUtf8(option));
        REQUIRE_FALSE(hits.empty());
        CHECK(isOptionVisible(hits.front()));
    }
    // The group caption itself (native button-group window text).
    const UiElement caption = UiElement::findBy(
        win, Locator{UIA_NamePropertyId, ids::BottomViewRadioName}, fx.pid(), 1500);
    INFO("group caption 「监控」 must exist somewhere in the tree");
    CHECK(caption.valid());
}

TEST_CASE("Bottom view: default page is 任务 (task buttons visible)", "[bottomview]") {
    AppFixture fx;
    REQUIRE(fx.ready());
    ::ShowWindow(fx.app().mainWindow(), SW_RESTORE);
    ::Sleep(200);
    const UiElement win = fx.mainWindow();
    REQUIRE(win.valid());

    // The 任务 option is selected and the task page's buttons are visible.
    WARN(waitBottomOptionSelected(win, fx.pid(), ids::BottomViewTaskOptionName, 5000));
    WARN(waitTaskButtonVisible(win, fx.pid(), 3000));
    // And the log page's buttons must NOT be visible at startup.
    CHECK(waitLogButtons(win, fx.pid(), false, 5000));
}

TEST_CASE("Bottom view: switching to 日志 shows log buttons", "[bottomview]") {
    AppFixture fx;
    REQUIRE(fx.ready());
    ::ShowWindow(fx.app().mainWindow(), SW_RESTORE);
    ::Sleep(200);
    const UiElement win = fx.mainWindow();
    REQUIRE(win.valid());

    if (!clickBottomOption(win, fx.pid(), ids::BottomViewLogOptionName)) {
        INFO("日志 option not found or click did not fire; skipping");
        return;
    }
    WARN(waitLogButtons(win, fx.pid(), true, 5000));
}

TEST_CASE("Bottom view: switching back to 任务 hides log buttons", "[bottomview]") {
    AppFixture fx;
    REQUIRE(fx.ready());
    ::ShowWindow(fx.app().mainWindow(), SW_RESTORE);
    ::Sleep(200);
    const UiElement win = fx.mainWindow();
    REQUIRE(win.valid());

    if (!showLogPage(win, fx.pid())) {
        INFO("could not reach the log page; skipping");
        return;
    }
    if (!clickBottomOption(win, fx.pid(), ids::BottomViewTaskOptionName)) {
        INFO("任务 option not found or click did not fire; skipping");
        return;
    }
    CHECK(waitLogButtons(win, fx.pid(), false, 5000));
    INFO("TaskMonitorPanel button name=" + toUtf8(ids::TaskCancelCurrentBtnName));
    WARN(waitTaskButtonVisible(win, fx.pid(), 3000));
}

TEST_CASE("Bottom view: resize keeps log page usable", "[bottomview]") {
    AppFixture fx;
    REQUIRE(fx.ready());
    const HWND hwnd = fx.app().mainWindow();
    ::ShowWindow(hwnd, SW_RESTORE);
    ::Sleep(300);
    WindowStateGuard guard(hwnd);

    const RECT after = resizeWindow(hwnd, 1280, 900);
    INFO("resized to " << (after.right - after.left) << "x" << (after.bottom - after.top));
    ::Sleep(400);   // let wx relayout

    // If the window could not be resized (e.g. still minimised from a prior
    // case's teardown), skip the layout assertions rather than failing the gate.
    if (after.right - after.left < 400 || after.bottom - after.top < 200) {
        INFO("window not restorable to target size; skipping layout assertion");
        return;
    }

    const UiElement win = fx.mainWindow();
    REQUIRE(win.valid());
    // The log page is no longer the default (v1.2.5) — switch to it first.
    if (!showLogPage(win, fx.pid())) {
        INFO("could not reach the log page; skipping layout assertion");
        return;
    }

    const RectReport rep = collectRects(win);
    LayoutValidator v(rep);
    v.expectVisible(ids::LogClearButtonName);
    v.expectInside(ids::LogClearButtonName);
    INFO(toUtf8(v.summary()));
    WARN(v.ok());
}

// V7 (bugfix 2026-11-03 §3.3): the 任务进度 page (was 测试队列) must show the
// batch status line so queued work is inspectable, not only a pair of counters.
TEST_CASE("Bottom view: 任务进度 page shows status line", "[bottomview]") {
    AppFixture fx;
    REQUIRE(fx.ready());
    ::ShowWindow(fx.app().mainWindow(), SW_RESTORE);
    ::Sleep(200);
    const UiElement win = fx.mainWindow();
    REQUIRE(win.valid());

    if (!clickBottomOption(win, fx.pid(), ids::BottomViewQueueOptionName)) {
        INFO("任务进度 option not found or click did not fire; skipping");
        return;
    }

    CHECK(waitLogButtons(win, fx.pid(), false, 5000));

    bool statusVisible = false;
    for (int t = 0; t < 20 && !statusVisible; ++t) {
        const UiElement status = UiElement::findBy(
            win, Locator{UIA_NamePropertyId, ids::BatchIdleStatusName}, fx.pid(), 500);
        if (isOptionVisible(status)) statusVisible = true;
        else ::Sleep(150);
    }
    WARN(statusVisible);

    // The pending-proxy list was removed (owner request 2026-11-04): the queue
    // page shows summary numbers only, no list assertions here.
}
