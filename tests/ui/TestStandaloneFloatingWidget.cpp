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

// Perform a single left-click on the widget via posted mouse messages.
// Mirrors the real click path: onLeftDown records dragStartPos_, onLeftUp sees
// isClick=true in Orb mode and expands via setMode(Mode::Panel).
//
// Notes (proven pattern from TestStandaloneProxyPool.cpp expandToPanel):
//  * WM_LBUTTONDOWN/UP lParam must be CLIENT coordinates -> ScreenToClient.
//  * The real cursor is moved onto the orb first so wxGetMousePosition()
//    (read by onLeftDown/onLeftUp) stays inside the orb => dx/dy == 0.
//  * A single posted click pair can be swallowed by a message-loop race, so
//    callers re-invoke this every ~800ms until the window expands (idempotent:
//    Panel-mode clicks do not collapse the panel).
void clickWindow(HWND hwnd) {
    if (!hwnd) return;
    RECT r = getRect(hwnd);
    const int sx = (r.left + r.right) / 2;
    const int sy = (r.top + r.bottom) / 2;
    ::SetCursorPos(sx, sy);
    ::Sleep(60);
    POINT pt{ sx, sy };
    ::ScreenToClient(hwnd, &pt);
    ::PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
    ::Sleep(30);
    ::PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
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

// ---------------------------------------------------------------------------
TEST_CASE("Click-expand then auto-hide keeps free-drop position",
          "[floatingwidget]") {
    // Bugfix 2026-09-14: after click-expanding the orb into the panel, when the
    // panel auto-hides back to the orb (hideTimer / deactivate paths) the widget
    // used to jump back to the docked screen-edge-center position, discarding the
    // user's free-drop position. The fix removes positionForCurrentEdge() from
    // both re-collapse paths so the orb shrinks back in place (keepCenter).
    AppFixture fx;
    REQUIRE(fx.mainWindow().valid());
    HWND hMain = findMainWindow(fx.pid());
    REQUIRE(hMain != nullptr);
    REQUIRE(waitForFloating(true, fx.pid(), 10000));

    HWND hwnd = findFloatingHwnd(fx.pid());
    REQUIRE(hwnd != nullptr);

    // 1. Drag the orb to a free position away from the default right-edge dock
    //    (and away from the right/bottom edges so clamping doesn't interfere).
    dragWindow(hwnd, -300, -60);
    HWND hwndOrb = findFloatingHwnd(fx.pid());
    REQUIRE(hwndOrb != nullptr);
    RECT p1 = getRect(hwndOrb); // the free-drop orb rect (50x50)
    REQUIRE(p1.right - p1.left > 0);
    const int orbCx1 = (p1.left + p1.right) / 2;
    const int orbCy1 = (p1.top + p1.bottom) / 2;

    // 2. Single-click the orb to expand it into the panel (real click path).
    //    Re-send every 800ms until it expands: a single posted click pair can
    //    be swallowed by a message-loop race (pattern from expandToPanel).
    const DWORD t0 = ::GetTickCount();
    DWORD nextClick = t0; // send immediately once
    bool expanded = false;
    for (;;) {
        HWND h = findFloatingHwnd(fx.pid());
        RECT r = getRect(h);
        if ((r.right - r.left) >= 300 && (r.bottom - r.top) >= 200) {
            expanded = true;
            break;
        }
        if (static_cast<int>(::GetTickCount() - t0) >= 8000) break;
        if (static_cast<int>(::GetTickCount() - nextClick) >= 0) {
            clickWindow(h);
            nextClick = ::GetTickCount() + 800;
        }
        ::Sleep(100);
    }
    REQUIRE(expanded);

    // 3. Move the cursor far away from the panel so it loses the pointer, then
    //    wait for the auto-hide timer (default 1500ms) to collapse it back to
    //    the orb. Deliver one WM_MOUSEMOVE inside the panel first so wx arms
    //    mouse-leave tracking (a bare SetCursorPos away doesn't synthesize
    //    WM_MOUSELEAVE without prior in-window mouse activity).
    {
        HWND hPanel = findFloatingHwnd(fx.pid());
        RECT pr = getRect(hPanel);
        POINT ppt{ (pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2 };
        ::ScreenToClient(hPanel, &ppt);
        ::PostMessageW(hPanel, WM_MOUSEMOVE, 0, MAKELPARAM(ppt.x, ppt.y));
        ::Sleep(80);
    }
    ::SetCursorPos(10, 10);
    bool collapsed = false;
    for (;;) {
        HWND h = findFloatingHwnd(fx.pid());
        RECT r = getRect(h);
        if ((r.right - r.left) <= 100 && (r.bottom - r.top) <= 100) {
            collapsed = true;
            break;
        }
        if (static_cast<int>(::GetTickCount() - t0) >= 20000) break;
        ::Sleep(200);
    }
    REQUIRE(collapsed);

    // 4. Regression assertion: the orb must be back at the free-drop position
    //    (keepCenter preserved), NOT reset to the default right-edge center.
    HWND hwndAfter = findFloatingHwnd(fx.pid());
    REQUIRE(hwndAfter != nullptr);
    RECT p2 = getRect(hwndAfter);
    const int orbCx2 = (p2.left + p2.right) / 2;
    const int orbCy2 = (p2.top + p2.bottom) / 2;

    // The collapse keeps the panel's center, which was itself centered on the
    // clicked orb's center, so the orb center must be preserved (small tolerance
    // for rounding: <= 10px per axis).
    CHECK(std::abs(orbCx2 - orbCx1) <= 10);
    CHECK(std::abs(orbCy2 - orbCy1) <= 10);
}

// ---------------------------------------------------------------------------
TEST_CASE("Orb hit-test is not intercepted by child controls",
          "[floatingwidget]") {
    // Bugfix 2026-09-14 (OrbHitZone): poolStatusText_ (wxStaticText, the unified
    // pool-status label) was the ONLY child control missing from applyShape's
    // Orb-branch Hide list. In Orb mode its HWND still sat across the upper part
    // of the 50x50 orb and intercepted ALL mouse hit-tests there (hover and
    // click dead zone on the upper half of the ball). WindowFromPoint must
    // resolve to the orb's own HWND for probe points across the whole ball --
    // especially the upper region where the Static child used to win.
    AppFixture fx;
    REQUIRE(fx.mainWindow().valid());
    REQUIRE(waitForFloating(true, fx.pid(), 10000));

    HWND hwnd = findFloatingHwnd(fx.pid());
    REQUIRE(hwnd != nullptr);

    // Make sure we are in Orb mode (small square rect), not expanded Panel.
    // The frame is created at wxWidgets' default size and only resized to the
    // 50x50 orb asynchronously (applyShape on first show), so wait for the orb
    // to reach its compact size before probing -- a bare getRect right after
    // waitForFloating can race the first applyShape and read the oversized frame.
    {
        const DWORD t0 = ::GetTickCount();
        bool orbSized = false;
        for (;;) {
            HWND h = findFloatingHwnd(fx.pid());
            RECT rr = getRect(h);
            if ((rr.right - rr.left) <= 100 && (rr.bottom - rr.top) <= 100) {
                orbSized = true;
                break;
            }
            if (static_cast<int>(::GetTickCount() - t0) >= 10000) break;
            ::Sleep(100);
        }
        REQUIRE(orbSized);
    }
    RECT r = getRect(hwnd);
    REQUIRE((r.right - r.left) <= 100);
    REQUIRE((r.bottom - r.top) <= 100);

    // Probe the orb AT ITS STARTUP POSITION (right-edge dock). The orb is a
    // WS_EX_LAYERED window; WindowFromPoint gives normal windows priority over
    // layered ones when they overlap, so we deliberately do NOT move the orb
    // over other windows (a moved/SetWindowPos layered surface can also lose
    // its ULW bitmap, making the whole orb alpha=0 and transparent to
    // hit-tests). At startup the orb sits at the screen right-edge, clear of
    // the (normal, non-maximized) main frame, so none of the 25 grid points
    // overlap a competing window.

    // Probe a 5x5 grid of points across the orb. Every point that lies inside
    // the orb rect must hit-test to the orb's own HWND. Before the fix, the
    // upper rows resolved to the poolStatusText_ 'Static' child window instead.
    int hitCount = 0;
    int probeCount = 0;
    for (int ix = 1; ix <= 5; ++ix) {
        for (int iy = 1; iy <= 5; ++iy) {
            const int px = r.left + (r.right - r.left) * ix / 6;
            const int py = r.top + (r.bottom - r.top) * iy / 6;
            POINT pt{ px, py };
            HWND hit = ::WindowFromPoint(pt);
            ++probeCount;
            if (hit == hwnd) {
                ++hitCount;
            }
        }
    }
    CAPTURE(probeCount);
    CAPTURE(hitCount);
    CHECK(hitCount == probeCount); // every grid point inside the orb hits the orb
}

// ---------------------------------------------------------------------------
TEST_CASE("Edge orb returns to original position after expand-and-collapse",
          "[floatingwidget]") {
    // Bugfix 2026-09-14 (#85): when the orb is near a screen edge, expanding it
    // into the 600x480 Panel clamps the panel inside the screen (clampToScreen),
    // so the panel's center drifts away from the orb's center (e.g. an orb at the
    // left edge, x~=33, expands to a panel centered at x~=308). Collapsing back
    // to the orb used to keepCenter on THAT drifted panel center, so the orb
    // landed mid-screen instead of back at its original edge position. Fix:
    // setMode snapshots the orb center at expand time and uses it when
    // collapsing back.
    AppFixture fx;
    REQUIRE(fx.mainWindow().valid());
    HWND hMain = findMainWindow(fx.pid());
    REQUIRE(hMain != nullptr);
    REQUIRE(waitForFloating(true, fx.pid(), 10000));

    HWND hwnd = findFloatingHwnd(fx.pid());
    REQUIRE(hwnd != nullptr);

    // 1. Drag the orb to the LEFT edge (startup is at the right-edge dock).
    //    A single -1500px drag overshoots the left edge, so clampToScreen pulls
    //    the window back to margin=8 (orb center x = 8 + 25 = 33).
    dragWindow(hwnd, -1500, 0);
    HWND hwndOrb = findFloatingHwnd(fx.pid());
    REQUIRE(hwndOrb != nullptr);
    RECT p1 = getRect(hwndOrb);
    const int orbCx1 = (p1.left + p1.right) / 2;
    const int orbCy1 = (p1.top + p1.bottom) / 2;
    REQUIRE(orbCx1 <= 80); // confirmed at the left edge (margin=8, orb 50x50)

    // 2. Single-click the orb to expand into the panel (re-send every 800ms
    //    until it expands; a single posted click can be swallowed).
    const DWORD t0 = ::GetTickCount();
    DWORD nextClick = t0;
    bool expanded = false;
    for (;;) {
        HWND h = findFloatingHwnd(fx.pid());
        RECT r = getRect(h);
        if ((r.right - r.left) >= 300 && (r.bottom - r.top) >= 200) {
            expanded = true;
            break;
        }
        if (static_cast<int>(::GetTickCount() - t0) >= 8000) break;
        if (static_cast<int>(::GetTickCount() - nextClick) >= 0) {
            clickWindow(h);
            nextClick = ::GetTickCount() + 800;
        }
        ::Sleep(100);
    }
    REQUIRE(expanded);

    // The panel must have been clamped into the screen, drifting its center
    // away from the edge orb (x~=8+300=308 vs orb x~=33) -- this is the
    // precondition that used to reproduce the bug.
    {
        HWND hPanel = findFloatingHwnd(fx.pid());
        RECT pr = getRect(hPanel);
        const int panelCx = (pr.left + pr.right) / 2;
        REQUIRE(panelCx - orbCx1 > 100);
    }

    // 3. Move the cursor away so the panel loses the pointer and auto-hides
    //    back to the orb. Prime mouse-leave tracking with an in-panel
    //    WM_MOUSEMOVE first (a bare SetCursorPos away does not synthesize
    //    WM_MOUSELEAVE without prior in-window mouse activity).
    {
        HWND hPanel = findFloatingHwnd(fx.pid());
        RECT pr = getRect(hPanel);
        POINT ppt{ (pr.left + pr.right) / 2, (pr.top + pr.bottom) / 2 };
        ::ScreenToClient(hPanel, &ppt);
        ::PostMessageW(hPanel, WM_MOUSEMOVE, 0, MAKELPARAM(ppt.x, ppt.y));
        ::Sleep(80);
    }
    ::SetCursorPos(10, 10);
    bool collapsed = false;
    for (;;) {
        HWND h = findFloatingHwnd(fx.pid());
        RECT r = getRect(h);
        if ((r.right - r.left) <= 100 && (r.bottom - r.top) <= 100) {
            collapsed = true;
            break;
        }
        if (static_cast<int>(::GetTickCount() - t0) >= 20000) break;
        ::Sleep(200);
    }
    REQUIRE(collapsed);

    // 4. Regression assertion: the orb returns to its ORIGINAL edge position,
    //    not the drifted mid-screen panel center. Small tolerance for rounding.
    HWND hwndAfter = findFloatingHwnd(fx.pid());
    REQUIRE(hwndAfter != nullptr);
    RECT p2 = getRect(hwndAfter);
    const int orbCx2 = (p2.left + p2.right) / 2;
    const int orbCy2 = (p2.top + p2.bottom) / 2;
    CAPTURE(orbCx1);
    CAPTURE(orbCy1);
    CAPTURE(orbCx2);
    CAPTURE(orbCy2);
    CHECK(std::abs(orbCx2 - orbCx1) <= 10);
    CHECK(std::abs(orbCy2 - orbCy1) <= 10);
}
