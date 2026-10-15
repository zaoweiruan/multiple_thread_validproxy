// tests/ui/TestLayoutSensing.cpp - page layout measurement, layout validation
// and automatic window resizing.
//
// Covers: "analyse the page layout" and "page layout auto adjustment" from the
// brief. Layout assertions are made numerically against UIA bounding rectangles
// so a failure prints exact pixels instead of forcing a human to eyeball a PNG.

#include <catch2/catch_test_macros.hpp>

#include "framework/Fixtures.h"
#include "framework/Layout.h"
#include "framework/Text.h"
#include "UIIds.h"

using namespace uitest;

TEST_CASE("Layout collects measurable controls inside the main window",
          "[layout][sensing]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    // Depth/budget come from the framework defaults: the walk prunes grid
// containers and honours a node budget, so it finishes in seconds instead of
// walking every rendered cell.
const RectReport rep = collectRects(fx.mainWindow());
    INFO(toUtf8(rectReportText(rep)));

    REQUIRE(rep.hwnd != nullptr);
    CHECK(rep.windowWidth() > 0);
    CHECK(rep.windowHeight() > 0);
    REQUIRE_FALSE(rep.entries.empty());

    // Every measured control must carry geometry and a usable name.
    for (size_t i = 0; i < rep.entries.size(); ++i) {
        INFO("entry " << i << ": " << toUtf8(rep.entries[i].label()));
        CHECK(rep.entries[i].measured);
        CHECK_FALSE(rep.entries[i].name.empty());
        CHECK(rectWidth(rep.entries[i].rect) > 0);
        CHECK(rectHeight(rep.entries[i].rect) > 0);
    }
}

TEST_CASE("Layout of the main window is inside the window bounds",
          "[layout][validation]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    // Depth/budget come from the framework defaults: the walk prunes grid
// containers and honours a node budget, so it finishes in seconds instead of
// walking every rendered cell.
const RectReport rep = collectRects(fx.mainWindow());
    INFO(toUtf8(rectReportText(rep)));

    // The search box and the log panel buttons are the stable anchors of this
    // layout; MainFrame always creates them.
    LayoutValidator validator(rep);
    validator.expectVisible(ids::SearchCtrlName);
    validator.expectInside(ids::SearchCtrlName);
    validator.expectVisible(ids::LogClearButtonName);
    validator.expectVisible(ids::LogStatsButtonName);
    validator.expectVisible(ids::LogOpenButtonName);

    INFO(toUtf8(validator.summary()));
    CHECK(validator.ok());
}

TEST_CASE("Layout search box and log controls do not overlap",
          "[layout][validation]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    // Depth/budget come from the framework defaults: the walk prunes grid
// containers and honours a node budget, so it finishes in seconds instead of
// walking every rendered cell.
const RectReport rep = collectRects(fx.mainWindow());
    const RectEntry* search = findRect(rep, ids::SearchCtrlName);
    REQUIRE(search != nullptr);
    INFO("search: " << toUtf8(search->label()));
    INFO("window " << rep.windowWidth() << "x" << rep.windowHeight());

    // The search box sits at the top of the window and the log panel at the
    // bottom, so they must not intersect at any window size.
    const RectEntry* clear = findRect(rep, ids::LogClearButtonName);
    REQUIRE(clear != nullptr);
    INFO("clear: " << toUtf8(clear->label()));

    const long overlap = rectOverlapArea(search->rect, clear->rect);
    INFO("overlap area " << overlap);
    CHECK(overlap == 0);
}

TEST_CASE("Layout resizing the window grows the client area and can be restored",
          "[layout][resize]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    const HWND hwnd = fx.app().mainWindow();
    REQUIRE(hwnd != nullptr);

    WindowStateGuard guard(hwnd);
    REQUIRE(guard.captured);
    const int originalWidth = rectWidth(guard.original);
    const int originalHeight = rectHeight(guard.original);
    INFO("original " << originalWidth << "x" << originalHeight);

    const RECT grown = resizeWindow(hwnd, originalWidth + 240,
                                    originalHeight + 180);
    INFO("grown " << rectWidth(grown) << "x" << rectHeight(grown));
    CHECK(rectWidth(grown) > originalWidth);
    CHECK(rectHeight(grown) > originalHeight);

    // Controls must survive the relayout: still present, still inside bounds.
    // Depth/budget come from the framework defaults: the walk prunes grid
// containers and honours a node budget, so it finishes in seconds instead of
// walking every rendered cell.
const RectReport rep = collectRects(fx.mainWindow());
    LayoutValidator validator(rep);
    validator.expectVisible(ids::SearchCtrlName);
    validator.expectInside(ids::SearchCtrlName);
    validator.expectVisible(ids::LogOpenButtonName);
    INFO(toUtf8(validator.summary()));
    CHECK(validator.ok());

    // Shrinking must work too - that is the other auto-adjust direction.
    const RECT shrunk = ensureMinSize(hwnd, originalWidth, originalHeight, 3);
    INFO("shrunk " << rectWidth(shrunk) << "x" << rectHeight(shrunk));
    CHECK(rectWidth(shrunk) >= originalWidth);
    CHECK(rectHeight(shrunk) >= originalHeight);
}

TEST_CASE("Layout rect helpers behave", "[layout][unit]") {
    RECT a = {0, 0, 100, 100};
    RECT b = {50, 50, 150, 150};
    // NB: do not name this `far` - that is a legacy macro in the MinGW headers.
    RECT distant = {200, 200, 300, 300};

    CHECK(rectWidth(a) == 100);
    CHECK(rectHeight(a) == 100);
    CHECK(rectArea(a) == 10000);
    CHECK_FALSE(rectIsEmpty(a));

    CHECK(rectOverlapArea(a, b) == 50L * 50L);
    CHECK(rectOverlapArea(a, distant) == 0);

    // a and b merely cross, so neither contains the other.
    CHECK_FALSE(rectInside(a, b));
    CHECK_FALSE(rectInside(b, a));
    // Containment only holds when the inner rect really is inside.
    RECT nested = {10, 10, 40, 40};
    CHECK(rectInside(nested, a));
    CHECK_FALSE(rectInside(a, nested));

    const RectOverflow ov = rectOverflow(b, a, 2);
    CHECK(ov.any());
    CHECK(ov.right == 50);
    CHECK(ov.bottom == 50);

    const RECT dup = a;
    const RectOverflow none = rectOverflow(dup, a, 2);
    CHECK_FALSE(none.any());
}

TEST_CASE("Layout validator reports violations instead of crashing",
          "[layout][unit]") {
    RectReport rep;
    rep.windowRect = RECT{0, 0, 800, 600};

    RectEntry inside;
    inside.name = L"insideCtl";
    inside.measured = true;
    inside.rect = RECT{10, 10, 100, 50};
    rep.entries.push_back(inside);

    RectEntry outsideCtl;
    outsideCtl.name = L"outsideCtl";
    outsideCtl.measured = true;
    outsideCtl.rect = RECT{700, 500, 900, 700};   // pokes past the window
    rep.entries.push_back(outsideCtl);

    LayoutValidator validator(rep);
    validator.expectInside(L"insideCtl");
    validator.expectInside(L"outsideCtl");
    validator.expectVisible(L"doesNotExist");

    CHECK_FALSE(validator.ok());
    CHECK(validator.violations().size() == 2);
    INFO(toUtf8(validator.summary()));
    for (size_t i = 0; i < validator.violations().size(); ++i) {
        INFO(toUtf8(validator.violations()[i].line()));
    }
}