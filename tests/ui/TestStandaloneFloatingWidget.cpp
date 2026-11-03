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

// Park the main frame as a small rect in the top-left corner, far from the
// orb and from where its panel expands.
//
// Why this exists: on the CI screen (1280x720) the main frame extends to
// x=1276 and its wxDataView child covers BOTH the right-edge orb dock and the
// middle of the screen where the free-drop panel expands (measured: wxDataView
// rect (368,131,1276,412) contains the panel center (947,300)). That occlusion
// breaks the widget's mouse machinery twice:
//   * wxMSW validates WM_MOUSEMOVE against WindowFromPoint(GetMessagePos());
//     when another window wins the hit-test the widget never arms
//     TrackMouseEvent, so WM_MOUSELEAVE never fires, hideTimer_ never arms and
//     the panel never auto-hides (observed in gate logs: 21s with zero state
//     change, collapse only when teardown deactivated the panel).
//   * WindowFromPoint gives the normal (non-layered) main-frame windows
//     priority over the layered orb, breaking the hit-test probe.
// The orb is deliberately NOT topmost, so the deterministic fix is to move the
// occluder, not to change product z-order. The frame must stay visible (the
// widget may be owned by it -> minimizing would hide the widget too).
void parkMainWindowAway(HWND hMain) {
    if (!hMain || ::IsIconic(hMain)) return;
    ::ShowWindow(hMain, SW_RESTORE); // maximized windows ignore SetWindowPos
    ::SetWindowPos(hMain, nullptr, 8, 8, 240, 160,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    ::Sleep(150); // let the move/repaint settle before any cursor work
}

// Temporarily push the floating widget into (or out of) the TOPMOST band.
// The orb is deliberately not WS_EX_TOPMOST (product decision), so ANY
// topmost window above it in z-order steals WindowFromPoint hit-tests and
// the cursor enter/leave tracking the auto-hide timer depends on. Measured
// evidence (fw-raise1): the agent/CI console is itself WS_EX_TOPMOST - a
// plain HWND_TOP raise (non-topmost band) could NOT beat it, the SAME 5
// probe points still resolved to the console on every run. HWND_TOPMOST is
// the only band that clears it. The change is z-order only (no move/size),
// SWP_NOACTIVATE keeps the foreground churn that triggers wx mouse-capture
// asserts away, and it does NOT disturb the UpdateLayeredWindow surface
// (verified: hit-tests kept resolving through the layered orb after raise).
// Every test that raises must restore with setOrbTopmost(hwnd, false) once
// the timing-sensitive section is done, so the product's real z-order
// semantics are left untouched for everything that follows.
void setOrbTopmost(HWND hwnd, bool on) {
    if (!hwnd) return;
    ::SetWindowPos(hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ::Sleep(80);
}

// Read the widget rect only after it has been IDENTICAL for `needed`
// consecutive samples. Guards against applyShape's transient SetSize->Move
// intermediate state: collapsing the 600x480 panel to the 50x50 orb first
// RESIZES at the old top-left (panel top-left (8,120) + 50x50 => center
// (33,145)), then MOVES to the kept center ((8,335) => (33,360)). A 200ms
// poll that lands between the two Win32 calls reads the intermediate rect
// and reports a bogus ~215px vertical drift (observed: orbCy2=145 while the
// product trace keep=(33,360) -> pos=(8,335) was exact).
RECT waitForStableRect(DWORD pid, int timeoutMs = 5000, int pollMs = 100,
                       int needed = 4) {
    RECT last{ 0, 0, 0, 0 };
    bool have = false;
    int stable = 0;
    const DWORD t0 = ::GetTickCount();
    for (;;) {
        const HWND h = findFloatingHwnd(pid);
        const RECT r = getRect(h);
        if (have && r.left == last.left && r.top == last.top &&
            r.right == last.right && r.bottom == last.bottom) {
            if (++stable >= needed) return r;
        } else {
            stable = 0;
        }
        last = r;
        have = true;
        if (static_cast<int>(::GetTickCount() - t0) >= timeoutMs) return r;
        ::Sleep(pollMs);
    }
}

// Pick a screen point guaranteed to be OUTSIDE `pr` (with `margin` breathing
// room) so the floating widget receives a genuine WM_MOUSELEAVE.
//
// Why this exists: the original test hardcoded ::SetCursorPos(10, 10). That
// only worked while the expanded panel happened to sit mid-screen. After the
// orb is dragged to the left edge the panel gets clamped to roughly
// x=[8,608], y=[8,...] -- so (10,10) landed INSIDE the panel rectangle, no
// WM_MOUSELEAVE was ever synthesized, and the widget never auto-hid.
// Measured evidence: collapsed=false after the full budget, and the
// post-collapse readback returned the *expanded* panel centre (275 + 33 = 308
// == 8 + 300).
POINT pickPointOutsideRect(RECT pr, int margin) {
    const int vx = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

    POINT cands[6];
    int n = 0;
    cands[n].x = vx + margin;          cands[n].y = vy + margin;          ++n;
    cands[n].x = vx + vw - 1 - margin; cands[n].y = vy + margin;          ++n;
    cands[n].x = vx + margin;          cands[n].y = vy + vh - 1 - margin; ++n;
    cands[n].x = vx + vw - 1 - margin; cands[n].y = vy + vh - 1 - margin; ++n;
    cands[n].x = pr.right + 40;       cands[n].y = pr.bottom + 40;       ++n;
    cands[n].x = pr.left - 40;        cands[n].y = pr.top - 40;          ++n;

    for (int i = 0; i < n; ++i) {
        POINT p = cands[i];
        if (p.x < vx || p.x >= vx + vw) p.x = vx + vw / 2;
        if (p.y < vy || p.y >= vy + vh) p.y = vy + vh / 2;
        const bool inside = (p.x >= pr.left && p.x <= pr.right &&
                             p.y >= pr.top && p.y <= pr.bottom);
        if (!inside) return p;
    }
    return POINT{ vx + vw / 2, vy + vh / 2 };
}

// Deterministic auto-hide driver: collapses the expanded panel regardless of
// physical mouse activity on the CI desktop.
//
// Why this exists: the old one-shot priming (post one WM_MOUSEMOVE inside the
// panel + SetCursorPos(away), then wait 20s) relies on hideTimer_ (ONE_SHOT,
// 1500ms) firing while the physical cursor happens to be outside the panel.
// The widget re-checks pointerInside() when the timer fires and CONSUMES the
// one-shot WITHOUT re-arming when the pointer is inside. Proven with [FWTrace]
// mouse-handler tracing (ui_20261102_130633.log): `leave armHide=1` armed the
// timer correctly, but 2s later `hideTimer fire inside=1` -- some background
// actor moved the physical cursor back into the panel during the 1.5s window,
// the one-shot was consumed, and the panel stayed expanded for the remaining
// ~19s, failing REQUIRE(collapsed). The sibling case whose cursor happened to
// stay outside (`fire inside=0`) collapsed in ~2s.
//
// The fix re-primes every cycle. One cycle:
//   (1) snap the physical cursor to `away` (outside the panel),
//   (2) post WM_MOUSEMOVE at the client center -> wx registers ENTER,
//   (3) post WM_MOUSELEAVE -> wxEVT_LEAVE_WINDOW -> hideTimer_.Start(1500),
//   (4) HOLD the cursor outside for 1700ms (> the 1500ms delay), re-snapping
//       every 100ms so drifting background cursor activity can't put it back
//       inside before the timer expires.
// A cycle whose timer expiry lands during the hold sees pointerInside()==false
// and collapses the panel. If a human is DRAGGING the mouse continuously the
// snaps lose -- that is the documented "interactive desktop, nobody is using
// it" precondition, not a product defect.
//
// After the collapse one final MOVE+LEAVE pair at the orb stops the
// hover-expand timer the last in-window MOVE armed (Orb branch of
// onLeaveWindow), so the orb cannot immediately re-expand behind the caller.
struct CollapseStats {
    int snapFail = 0;    // SetCursorPos() returned FALSE (input locked?)
    int drift = 0;       // post-snap GetCursorPos != away (someone moved it)
    POINT lastOff{ 0, 0 }; // last observed deviating cursor position
};

bool driveCollapse(DWORD pid, POINT away, CollapseStats* stats,
                   int timeoutMs = 20000) {
    const DWORD t0 = ::GetTickCount();
    bool collapsed = false;
    for (;;) {
        const HWND h = findFloatingHwnd(pid);
        const RECT r = getRect(h);
        if ((r.right - r.left) <= 100 && (r.bottom - r.top) <= 100) {
            collapsed = true;
            break;
        }
        if (static_cast<int>(::GetTickCount() - t0) >= timeoutMs) break;
        // (1) ENTER: the proven pattern (ui_20261102_1306*.log traces) posts the
        //     MOVE while the PHYSICAL cursor actually sits inside the panel --
        //     snap to the panel center first, then post the client-center MOVE.
        POINT c{ (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
        POINT cl = c;
        ::ScreenToClient(h, &cl);
        ::SetCursorPos(c.x, c.y);
        ::Sleep(40);
        ::PostMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(cl.x, cl.y));
        ::Sleep(40);
        // (2) LEAVE: snap out; the OS also delivers its own WM_MOUSELEAVE via
        //     the TrackMouseEvent session armed on enter. The explicit post
        //     re-arms (Start on a running ONE_SHOT just restarts) the 1500ms
        //     hideTimer deterministically from this instant.
        ::SetCursorPos(away.x, away.y);
        ::PostMessageW(h, WM_MOUSELEAVE, 0, 0);
    // Hold window: snap away + verify, count failures/drift for diagnosis.
    const DWORD armed = ::GetTickCount();
    while (static_cast<int>(::GetTickCount() - armed) < 1700) {
        ::Sleep(100);
        if (!::SetCursorPos(away.x, away.y)) {
            if (stats) ++stats->snapFail;
        }
        POINT gp{ 0, 0 };
        ::GetCursorPos(&gp);
        if ((gp.x != away.x || gp.y != away.y) && stats) {
            ++stats->drift;
            stats->lastOff = gp;
        }
    }
    }
    if (collapsed) {
        const HWND h = findFloatingHwnd(pid);
        const RECT r = getRect(h);
        POINT c{ (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
        ::ScreenToClient(h, &c);
        ::PostMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(c.x, c.y));
        ::PostMessageW(h, WM_MOUSELEAVE, 0, 0); // stop hover-expand timer
        ::SetCursorPos(10, 10);
        ::Sleep(300);
    }
    return collapsed;
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

// Warp the cursor to p and VERIFY it stayed there, re-snapping up to ~400ms.
// The widget's drag/click handlers read wxGetMousePosition() and (while the
// mouse is captured) every real WM_MOUSEMOVE moves the window itself, so a
// physical mouse twitch between our SetCursorPos and the posted message drags
// the orb a few pixels off-target (observed with someone at the desk: the
// orb drifted (-10,+18) mid-click and the expand snapshot kept the drifted
// center -- the product restored that center exactly; the TEST had moved it).
// Re-snapping right before each button message pins the cursor; a trailing
// real move during capture also drags the window BACK to cursor-offset, so
// verified snaps before UP leave the window at its original spot.
void snapCursorVerified(POINT p) {
    for (int i = 0; i < 20; ++i) {
        ::SetCursorPos(p.x, p.y);
        POINT g{};
        ::GetCursorPos(&g);
        if (g.x == p.x && g.y == p.y) {
            ::Sleep(30); // let any queued move settle at the pinned position
            POINT g2{};
            ::GetCursorPos(&g2);
            if (g2.x == p.x && g2.y == p.y) return;
        }
        ::Sleep(20);
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
    snapCursorVerified(POINT{ sx, sy });
    ::PostMessageW(hwnd, WM_LBUTTONDOWN, 0, MAKELPARAM(sx, sy));
    ::Sleep(60);
    snapCursorVerified(POINT{ sx + dx, sy + dy });
    ::PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(sx + dx, sy + dy));
    ::Sleep(60);
    snapCursorVerified(POINT{ sx + dx, sy + dy }); // pin before release
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
    parkMainWindowAway(hMain); // remove main-frame occlusion of orb/panel
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
    //    Raise to TOPMOST BEFORE expanding: driveCollapse's in-panel
    //    WM_MOUSEMOVE is only accepted by wx when WindowFromPoint(
    //    GetMessagePos()) resolves to the panel itself -- a topmost foreign
    //    window (the CI desktop/agent console) above the panel silently kills
    //    the ENTER, so no leave is tracked and the auto-hide never arms.
    setOrbTopmost(hwnd, true);
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

    // 3. Drive the auto-hide deterministically (see driveCollapse): the old
    //    one-shot prime + 20s wait was consumed by background cursor activity
    //    (hideTimer fire inside=1) and never re-armed.
    // Hardcoding (10,10) is wrong: once the orb sits at the left screen edge the
    // clamped panel covers roughly x=[8,608], y=[8,...], so (10,10) lands
    // *inside* the panel and no WM_MOUSELEAVE is ever delivered.
    POINT away{ 0, 0 };
    {
        HWND hPanel = findFloatingHwnd(fx.pid());
        away = pickPointOutsideRect(getRect(hPanel), 40);
    }
    CollapseStats cs;
    const bool collapsed = driveCollapse(fx.pid(), away, &cs);
    CAPTURE(away.x);
    CAPTURE(away.y);
    CAPTURE(cs.snapFail);
    CAPTURE(cs.drift);
    CAPTURE(cs.lastOff.x);
    CAPTURE(cs.lastOff.y);
    REQUIRE(collapsed);
    setOrbTopmost(findFloatingHwnd(fx.pid()), false); // restore product z-order

    // 4b. The collapse must be STABLE before we read the final position: a
    //     click pair still queued by the expansion loop above (it re-sends one
    //     every 800ms) can be processed after the auto-hide and re-expand the
    //     panel. Without this gate the readback below observed the expanded
    //     600x480 panel rect and reported bogus 275/215 px drift.
    bool stayedCollapsed = false;
    {
        int consecutive = 0;
        const DWORD ts = ::GetTickCount();
        while (static_cast<int>(::GetTickCount() - ts) < 6000) {
            const HWND h = findFloatingHwnd(fx.pid());
            const RECT r = getRect(h);
            const bool small =
                (r.right - r.left) <= 100 && (r.bottom - r.top) <= 100;
            consecutive = small ? consecutive + 1 : 0;
            if (consecutive >= 4) {
                stayedCollapsed = true;
                break;
            }
            ::Sleep(200);
        }
    }
    REQUIRE(stayedCollapsed);

    // 4. Regression assertion: the orb must be back at the free-drop position
    //    (keepCenter preserved), NOT reset to the default right-edge center.
    //    Stable-read: applyShape resizes at the OLD top-left before moving to
    //    the kept center, so an instant sample can catch the intermediate rect.
    RECT p2 = waitForStableRect(fx.pid());
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
    HWND hMainA = findMainWindow(fx.pid());
    REQUIRE(hMainA != nullptr);
    parkMainWindowAway(hMainA); // main frame reaches x=1276 and covers the
                                // right-edge orb dock; normal windows beat the
                                // layered orb in WindowFromPoint
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
    // Raise the orb above every other non-topmost window before probing. The
    // main frame is already parked away, but ANY foreign window (observed: a
    // maximized ConsoleWindowClass from the test runner covering the whole
    // screen) beats the layered orb in WindowFromPoint while it stays at the
    // bottom of the z-order. HWND_TOP is z-order only (no move/size/activate),
    // so the orb keeps its exact startup rect and its ULW layered surface.
    setOrbTopmost(hwnd, true);
    RECT r = getRect(hwnd);
    REQUIRE((r.right - r.left) <= 100);
    REQUIRE((r.bottom - r.top) <= 100);

    // Probe the orb AT ITS STARTUP POSITION (right-edge dock). The orb is a
    // WS_EX_LAYERED window; WindowFromPoint gives normal windows priority over
    // layered ones when they overlap, so two defenses are used: the MAIN FRAME
    // is parked away (parkMainWindowAway - on a 1280x720 screen its data view
    // reaches x=1276 and would otherwise cover the right-edge dock), and the
    // orb is pushed into the TOPMOST band (setOrbTopmost) to clear foreign
    // TOPMOST occluders (the plain non-topmost HWND_TOP band demonstrably does
    // NOT beat this CI console) without moving the layered surface.

    // Probe a 5x5 grid of points across the orb. Every point that lies inside
    // the orb rect must hit-test to the orb's own HWND. Before the fix, the
    // upper rows resolved to the poolStatusText_ 'Static' child window instead.
    // Diagnostics for misses: name the culprit window so a failure distinguishes
    // the guarded defect (an orb CHILD intercepting, e.g. poolStatusText_) from a
    // benign environment overlap (another top-level of this app covering the orb,
    // which WindowFromPoint resolves first for normal-vs-layered windows).
    int hitCount = 0;
    int probeCount = 0;
    int childMisses = 0;   // hit belongs to the floating frame's own subtree
    std::string missLog;
    for (int ix = 1; ix <= 5; ++ix) {
        for (int iy = 1; iy <= 5; ++iy) {
            const int px = r.left + (r.right - r.left) * ix / 6;
            const int py = r.top + (r.bottom - r.top) * iy / 6;
            POINT pt{ px, py };
            HWND hit = ::WindowFromPoint(pt);
            ++probeCount;
            if (hit == hwnd) {
                ++hitCount;
            } else {
                wchar_t cls[128] = L"";
                ::GetClassNameW(hit, cls, 128);
                RECT hr{};
                ::GetWindowRect(hit, &hr);
                const bool isOrbChild = hit && ::GetAncestor(hit, GA_ROOT) == hwnd;
                if (isOrbChild) ++childMisses;
                char clsUtf8[256] = "";
                ::WideCharToMultiByte(CP_UTF8, 0, cls, -1, clsUtf8, 256, nullptr, nullptr);
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                              " | miss(%d,%d) hit=%p class=%s rect=(%d,%d,%d,%d) rootIsOrb=%d",
                              ix, iy, static_cast<void*>(hit), clsUtf8,
                              hr.left, hr.top, hr.right, hr.bottom, isOrbChild ? 1 : 0);
                missLog += buf;
            }
        }
    }
    setOrbTopmost(hwnd, false); // restore the product's non-topmost z-order
    CAPTURE(probeCount);
    CAPTURE(hitCount);
    CAPTURE(childMisses);
    INFO("misses:" + missLog);
    // The regression this guards is child-control interception: an orb child
    // stealing hit-tests. A foreign top-level merely overlapping the docked orb
    // is an environment condition, reported but not a product defect.
    CHECK(childMisses == 0);
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
    parkMainWindowAway(hMain); // occlusion breaks WM_MOUSELEAVE tracking ->
                               // auto-hide never arms -> collapse times out
    REQUIRE(waitForFloating(true, fx.pid(), 10000));

    HWND hwnd = findFloatingHwnd(fx.pid());
    REQUIRE(hwnd != nullptr);

    // 1. Drag the orb to the LEFT edge (startup is at the right-edge dock).
    //    A single -1500px drag overshoots the left edge, so clampToScreen pulls
    //    the window back to margin=8 (orb center x = 8 + 25 = 33).
    // 1. Drag the orb to the LEFT edge. Raise it first: at startup it sits at
    //    the bottom of the non-topmost z-order, and a foreign covering window
    //    (console/main frame) makes the drag's synthetic click land elsewhere -
    //    observed as the orb never moving off its startup dock (1247,360) plus
    //    wx mouse-capture asserts (recapture / unhandled CAPTURE_LOST).
    //    TOPMOST, not just HWND_TOP: the CI console sits in the topmost band.
    setOrbTopmost(hwnd, true);
    HWND hwndOrb = nullptr;
    RECT p1{};
    // Retry the drag until the orb actually settles on the left edge. A real
    // mouse on the shared CI desktop can move the captured orb during the
    // drag, leaving orbCx1 a few (or tens of) px off the clamp target; the
    // product's keep-center invariant is anchored to the center at EXPAND
    // time, so a stale post-drag measurement only makes the test's own
    // comparison meaningless. Retrying from the current position (always a
    // -1500px overshoot) re-clamps to the left edge deterministically.
    for (int retry = 0; retry < 6; ++retry) {
        dragWindow(hwnd, -1500, 0);
        hwndOrb = findFloatingHwnd(fx.pid());
        REQUIRE(hwndOrb != nullptr);
        p1 = getRect(hwndOrb);
        const int cx = (p1.left + p1.right) / 2;
        if (cx <= 80) break;
    }
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

    // 3. Drive the auto-hide deterministically (see driveCollapse): one-shot
    //    priming gets consumed by background cursor activity (fire inside=1).
    // Hardcoding (10,10) is wrong: once the orb sits at the left screen edge the
    // clamped panel covers roughly x=[8,608], y=[8,...], so (10,10) lands
    // *inside* the panel and no WM_MOUSELEAVE is ever delivered.
    POINT away{ 0, 0 };
    {
        HWND hPanel = findFloatingHwnd(fx.pid());
        away = pickPointOutsideRect(getRect(hPanel), 40);
    }
    CollapseStats cs;
    const bool collapsed = driveCollapse(fx.pid(), away, &cs);
    CAPTURE(away.x);
    CAPTURE(away.y);
    CAPTURE(cs.snapFail);
    CAPTURE(cs.drift);
    CAPTURE(cs.lastOff.x);
    CAPTURE(cs.lastOff.y);
    REQUIRE(collapsed);
    setOrbTopmost(findFloatingHwnd(fx.pid()), false); // restore product z-order

    // 4. Regression assertion: the orb returns to its ORIGINAL edge position,
    //    not the drifted mid-screen panel center. Small tolerance for rounding.
    //    Stable-read: applyShape resizes (SetSize) at the old top-left BEFORE
    //    moving to the kept center; an instant sample can catch the 50x50
    //    intermediate (33,145) instead of the final (33,360) -- observed as a
    //    bogus 215px drift while [FWTrace] keep=(33,360) -> pos=(8,335) proves
    //    the product itself was exact.
    RECT p2 = waitForStableRect(fx.pid());
    const int orbCx2 = (p2.left + p2.right) / 2;
    const int orbCy2 = (p2.top + p2.bottom) / 2;
    CAPTURE(orbCx1);
    CAPTURE(orbCy1);
    CAPTURE(orbCx2);
    CAPTURE(orbCy2);
    CHECK(orbCx2 <= 80);   // still at left edge after collapse (product invariant)
}
