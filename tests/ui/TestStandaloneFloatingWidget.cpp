// tests/ui/TestStandaloneFloatingWidget.cpp
//
// GUI regression test for the floating standalone-monitor widget's free-drop
// position preservation. Bug: dragging the orb to a free position, then hiding
// and re-showing it (Ctrl+M / menu toggle) used to reset it back to the default
// docked edge. Fix: Show() only positions on the FIRST appearance (placed_
// guard) and onLeftUp does free docking (stays put, clamped to screen).
//
// The widget is created+shown at startup when proxy_process_monitor.enabled is
// true (MainFrame::setActive(true)). We drive the real toggle command handler
// (onMenuStandaloneMonitor -> toggleActive -> Show) via WM_COMMAND so we don't
// depend on keyboard-focus / async modifier state in a headless test session.

#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <UIAutomation.h>
#include <string>
#include "framework/Application.h"
#include "framework/UIAutomation.h"
#include "framework/UIElement.h"
#include "framework/Fixtures.h"
#include "UIIds.h"

using namespace uitest;

namespace {

// WM_COMMAND id of the floating-widget toggle, mirrored from
// MainFrame.cpp: ID_MENU_STANDALONE_MON = wxID_HIGHEST(6000) + 114.
static constexpr UINT kToggleFloatingCmd = 6000 + 114; // 6114

RECT getRect(HWND hwnd) {
    RECT r = { 0, 0, 0, 0 };
    if (hwnd) ::GetWindowRect(hwnd, &r);
    return r;
}

// Find the floating widget HWND of a SPECIFIC app instance (PID-filtered), so a
// leftover validproxy.exe from another test case can never be mistaken for ours.
HWND findFloatingHwnd(DWORD pid) {
    struct Ctx { DWORD pid; HWND hwnd; } ctx{ pid, nullptr };
    ::EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* ctx = reinterpret_cast<Ctx*>(lp);
        DWORD wpid = 0;
        ::GetWindowThreadProcessId(hwnd, &wpid);
        if (wpid == ctx->pid) {
            wchar_t title[256] = { 0 };
            ::GetWindowTextW(hwnd, title, 256);
            if (std::wcscmp(title, ids::FloatingWidgetName) == 0) {
                ctx->hwnd = hwnd;
                return FALSE; // stop enumeration
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.hwnd;
}

bool floatingVisible(DWORD pid) {
    HWND h = findFloatingHwnd(pid);
    return h != nullptr && ::IsWindowVisible(h);
}

// Find the REAL main frame (MainFrame) of a specific app instance.
//
// We cannot use AppProcess::mainWindow() here: it returns the first visible
// top-level window of the PID that has a non-empty title, and the floating widget
// (StandaloneFloatingWidget) is exactly that -- a visible top-level wxFrame with a
// stable title -- so mainWindow() can resolve to the WIDGET instead of MainFrame.
// Sending the toggle WM_COMMAND to the widget is a silent no-op. We therefore
// exclude the floating widget's known title and return the application's main frame
// (titled e.g. "validproxy - Proxy Manager").
HWND findMainWindow(DWORD pid) {
    struct Ctx { DWORD pid; HWND hwnd; } ctx{ pid, nullptr };
    ::EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* ctx = reinterpret_cast<Ctx*>(lp);
        DWORD wpid = 0;
        ::GetWindowThreadProcessId(hwnd, &wpid);
        if (wpid != ctx->pid) return TRUE;
        if (!::IsWindowVisible(hwnd)) return TRUE;
        wchar_t title[256] = { 0 };
        ::GetWindowTextW(hwnd, title, 256);
        // Exclude the floating widget (known stable title) and any untitled window.
        if (std::wcscmp(title, ids::FloatingWidgetName) == 0) return TRUE;
        if (title[0] == L'\0') return TRUE;
        ctx->hwnd = hwnd;
        return FALSE; // stop on the first qualifying (main) window
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.hwnd;
}

// Polls until the floating widget's visibility matches `want` (or timeout).
bool waitForFloating(bool want, DWORD pid, int timeoutMs = 5000, int pollMs = 100) {
    const DWORD start = ::GetTickCount();
    for (;;) {
        if (floatingVisible(pid) == want) return true;
        if (static_cast<int>(::GetTickCount() - start) >= timeoutMs) return false;
        ::Sleep(pollMs);
    }
}

// Toggle the floating widget through the real menu command handler.
//
// Driven SYNCHRONOUSLY with SendMessageTimeout (flags=0 == SMTO_NORMAL): it blocks
// until the target thread actually processes the WM_COMMAND, so the toggle is fully
// applied before we return. This is deliberate:
//   * PostMessage depends on the app pumping its message loop. Under the load of
//     leftover xray/worker subprocesses the loop can be slow enough that the
//     toggle never lands in time, so the test sees "still visible" and fails
//     (observed: consistent failures in this environment).
//   * Because the send is synchronous and we call it exactly once per toggle, there
//     is never an over-toggle race (a re-posted message could double-flip the state
//     back). A sent message is processed exactly once.
// We then poll briefly in case the visibility update lags a touch.
void toggleFloating(HWND hMain, DWORD pid) {
    if (!hMain) return;
    const bool wasVisible = floatingVisible(pid);
    ::SendMessageTimeoutW(hMain, WM_COMMAND,
                          MAKEWPARAM(kToggleFloatingCmd, 0), 0,
                          0 /*SMTO_NORMAL: block until processed*/, 10000, nullptr);
    // Poll briefly in case the visibility update lags a touch.
    const DWORD start = ::GetTickCount();
    while (static_cast<int>(::GetTickCount() - start) < 3000) {
        ::Sleep(50);
        if (floatingVisible(pid) != wasVisible) {
            return; // flipped -> done
        }
    }
}

// Drag the widget by (dx, dy) screen pixels via real cursor moves + posted
// button/move messages. The widget's drag handlers read wxGetMousePosition(),
// so we move the real cursor and post the events straight to its HWND.
void dragWindow(HWND hwnd, int dx, int dy) {
    if (!hwnd) return;
    RECT r = getRect(hwnd);
    const int sx = (r.left + r.right) / 2;
    const int sy = (r.top + r.bottom) / 2;
    ::SetCursorPos(sx, sy);
    ::Sleep(60);
    ::PostMessageW(hwnd, WM_LBUTTONDOWN, 0, MAKELPARAM(sx, sy));
    ::Sleep(60);
    ::SetCursorPos(sx + dx, sy + dy);
    ::Sleep(60);
    ::PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(sx + dx, sy + dy));
    ::Sleep(60);
    ::PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(sx + dx, sy + dy));
    ::ReleaseCapture(); // safety: drop any OS-level mouse capture left behind
    // Move the real cursor far from the widget so the hover-expand timer can't fire
    // and switch it into Panel mode (which would change its size/position between
    // the p2 and p3 measurements and break the position-preservation check).
    ::SetCursorPos(10, 10);
    ::Sleep(250);
}

// Perform a real double-click on the widget via posted mouse messages.
//
// wxMSW turns the second WM_LBUTTONDOWN into a WM_LBUTTONDBLCLK (which wx maps to
// wxEVT_LEFT_DCLICK -> StandaloneFloatingWidget::onLeftDClick). We post DOWN,
// DBLCLK and UP in sequence so both the single-click code path (onLeftDown) and the
// double-click handler run, exactly as a real user double-click would dispatch.
void dblClickWindow(HWND hwnd) {
    if (!hwnd) return;
    RECT r = getRect(hwnd);
    const int sx = (r.left + r.right) / 2;
    const int sy = (r.top + r.bottom) / 2;
    ::PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(sx, sy));
    ::Sleep(30);
    ::PostMessageW(hwnd, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(sx, sy));
    ::Sleep(30);
    ::PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(sx, sy));
    ::Sleep(60);
}

} // namespace

// ---------------------------------------------------------------------------
TEST_CASE("Floating widget is shown at startup and toggles via Ctrl+M handler",
          "[floatingwidget]") {
    AppFixture fx;
    REQUIRE(fx.mainWindow().valid());
    HWND hMain = findMainWindow(fx.pid());
    REQUIRE(hMain != nullptr);

    // proxy_process_monitor.enabled=true in the sandbox config -> shown at launch.
    // The orb is created shortly after the main window appears, so wait for it.
    REQUIRE(waitForFloating(true, fx.pid(), 10000));

    // Toggle OFF -> hidden.
    toggleFloating(hMain, fx.pid());
    REQUIRE(waitForFloating(false, fx.pid()));

    // Toggle ON -> visible again.
    toggleFloating(hMain, fx.pid());
    REQUIRE(waitForFloating(true, fx.pid()));
}

// ---------------------------------------------------------------------------
TEST_CASE("Floating widget keeps free-drop position after re-show",
          "[floatingwidget]") {
    AppFixture fx;
    REQUIRE(fx.mainWindow().valid());
    HWND hMain = findMainWindow(fx.pid());
    REQUIRE(hMain != nullptr);
    REQUIRE(waitForFloating(true, fx.pid(), 10000));

    HWND hwnd = findFloatingHwnd(fx.pid());
    REQUIRE(hwnd != nullptr);

    // Initial (default-edge) position.
    RECT p1 = getRect(hwnd);
    REQUIRE(p1.right > p1.left);
    REQUIRE(p1.bottom > p1.top);

    // Drag the orb to a free position, away from the right edge to avoid clamping.
    dragWindow(hwnd, -200, -100);
    RECT p2 = getRect(hwnd);

    // The window must have actually moved to a new free position.
    const bool moved = (p2.left != p1.left) || (p2.top != p1.top);
    REQUIRE(moved);

    // Hide and re-show (the path that previously reset the position).
    toggleFloating(hMain, fx.pid());
    REQUIRE(waitForFloating(false, fx.pid()));
    toggleFloating(hMain, fx.pid());
    REQUIRE(waitForFloating(true, fx.pid()));

    HWND hwnd2 = findFloatingHwnd(fx.pid());
    REQUIRE(hwnd2 != nullptr);
    RECT p3 = getRect(hwnd2);

    // Regression assertion: re-show must preserve the free-drop position, not
    // reset back to the default edge (p1).
    CHECK(p3.left == p2.left);
    CHECK(p3.top == p2.top);
}

// ---------------------------------------------------------------------------
TEST_CASE("Double-click floating widget toggles main frame maximize/restore",
          "[floatingwidget]") {
    AppFixture fx;
    REQUIRE(fx.mainWindow().valid());
    HWND hMain = findMainWindow(fx.pid());
    REQUIRE(hMain != nullptr);
    REQUIRE(waitForFloating(true, fx.pid(), 10000));

    HWND hwnd = findFloatingHwnd(fx.pid());
    REQUIRE(hwnd != nullptr);

    // Record the main frame's maximized state before the first double-click.
    const bool wasMaxed = ::IsZoomed(hMain) != FALSE;

    // Double-click the floating widget -> main frame should flip its
    // maximize/restore state (onLeftDClick toggles via Maximize()).
    dblClickWindow(hwnd);
    ::Sleep(200); // give the toggle time to take effect
    const bool afterFirst = ::IsZoomed(hMain) != FALSE;

    // Double-click again -> toggle back to the original state.
    dblClickWindow(hwnd);
    ::Sleep(200);
    const bool afterSecond = ::IsZoomed(hMain) != FALSE;

    CHECK(afterFirst != wasMaxed);   // first dclick flipped the state
    CHECK(afterSecond == wasMaxed);  // second dclick restored it
}
