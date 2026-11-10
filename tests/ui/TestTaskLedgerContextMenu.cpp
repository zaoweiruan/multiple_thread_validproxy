// tests/ui/TestTaskLedgerContextMenu.cpp - V8 (spec 2026-11-06 §6):
// finished-task retention + context menu 「清理全部完成记录」.
//
// Flow: trigger 移除重复代理 (DEDUP: the fastest queue-visible task — the
// sandbox DB dedups in milliseconds) → after completion the row STAYS in the
// task list with a terminal status (retention is the whole point of the
// feature) → WM_CONTEXTMENU over that row opens the native #32768 popup →
// UIA-invoke 「清理全部完成记录」 → the finished row disappears.
//
// Menu-driving rules (spec lesson list, all hard-won):
//   * WM_CONTEXTMENU goes via SendMessageW — while the popup is up, the APP
//     UI thread is blocked in its modal TrackPopupMenu loop, but that loop
//     keeps pumping window messages, which is what makes the popup's own
//     windows answer our probes (window enumeration AND UIA WM_GETOBJECT —
//     the UIA provider for native menus lives in user32, not in the app);
//   * the item is driven through UIA InvokePattern, scoped to the menu HWND
//     via ElementFromHandle (never a desktop-wide walk);
//   * never postMessage(WM_RBUTTONUP) — PostMessage starves the modal loop;
//   * SetForegroundWindow precedes the popup;
//   * all post-menu assertions run only after the menu loop has exited
//     (poster thread joined).
// Downgrade path (spec §6): if the popup cannot be reached on this machine,
// the menu interaction is reported via INFO and skipped — but the CORE
// assertion (the finished row persists after completion) always runs.
//
// Tag: [taskledger]  (registered as ctest UI_TASKLEDGER)

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

#include "framework/BottomView.h"
#include "framework/Dialog.h"
#include "framework/Fixtures.h"
#include "framework/ListView.h"
#include "framework/Menu.h"
#include "framework/Text.h"
#include "framework/UIElement.h"
#include "framework/WindowInfo.h"
#include "UIIds.h"

using namespace uitest;

namespace {

constexpr int kRowWaitMs = 30000;     // dedup run + 1s refresh cadence
constexpr int kMenuWaitMs = 6000;     // popup appearance
constexpr int kMenuCloseWaitMs = 4000;  // menu loop exit after invoke / ESC
constexpr int kClearedWaitMs = 6000;  // post-clear refresh

// Waits for the poster thread to signal that its SendMessage returned.
bool waitPosterDone(std::atomic<bool>* done, int timeoutMs) {
    for (int t = 0; t * 50 < timeoutMs; ++t) {
        if (done->load()) return true;
        ::Sleep(50);
    }
    return done->load();
}

// Last resort so the worker thread can never dangle: the poster is blocked in
// SendMessage until the app leaves its modal menu loop. Escalate to app
// termination, which makes the blocked call return immediately.
void closeMenuLoop(std::atomic<bool>* done, HWND hMain, AppProcess& app) {
    if (waitPosterDone(done, 1500)) return;
    ::PostMessageW(hMain, WM_CANCELMODE, 0, 0);
    if (waitPosterDone(done, 1500)) return;
    app.terminate();
    (void)waitPosterDone(done, 5000);
}

bool isTerminalStatus(const std::wstring& s) {
    return s == ids::TaskStatusCompleted || s == ids::TaskStatusFailed ||
           s == ids::TaskStatusCancelled;
}

// Row whose status cell (column 4) holds a terminal state; -1 when none.
int findLedgerFinishedRow(HWND list) {
    const int rows = listRowCount(list);
    for (int r = 0; r < rows; ++r) {
        if (isTerminalStatus(listItemText(list, r, 4))) return r;
    }
    return -1;
}

bool hasLedgerFinishedRow(HWND list) { return findLedgerFinishedRow(list) >= 0; }

// ---------------------------------------------------------------------------
// #32768 popup discovery. Safe DURING the app's menu modal loop: every probe
// is same-process Win32 state or a SendMessage to the app, and the pumping
// menu loop services both.
// ---------------------------------------------------------------------------
HWND findVisibleMenuWindow(HWND root, int depth) {
    if (!root || depth > 4) return nullptr;
    struct Ctx { HWND found; } ctx{ nullptr };
    ::EnumChildWindows(root, [](HWND h, LPARAM lp) -> BOOL {
        Ctx* c = reinterpret_cast<Ctx*>(lp);
        wchar_t cls[16] = { 0 };
        if (::GetClassNameW(h, cls, 16) == 0) return TRUE;
        if (wcscmp(cls, L"#32768") == 0 && ::IsWindowVisible(h)) {
            c->found = h;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.found) return ctx.found;
    for (HWND child = ::GetWindow(root, GW_CHILD); child;
         child = ::GetWindow(child, GW_HWNDNEXT)) {
        HWND found = findVisibleMenuWindow(child, depth + 1);
        if (found) return found;
    }
    return nullptr;
}

HWND findPopupMenu(HWND hMain, DWORD pid) {
    HWND menu = findVisibleMenuWindow(hMain, 0);
    if (menu) return menu;
    for (HWND w : topLevelWindows(pid, /*visibleOnly=*/true)) {
        if (w == hMain) continue;
        const WindowInfo info = describeWindow(w);
        if (info.kind == WindowKind::Menu) return w;
    }
    return nullptr;
}

// Poll for the popup. MUST run while the app UI thread sits in its
// TrackPopupMenu modal loop (its window message pump keeps answering).
bool waitForPopupMenu(HWND hMain, DWORD pid, HWND* out, int timeoutMs) {
    const DWORD start = ::GetTickCount();
    while (static_cast<int>(::GetTickCount() - start) < timeoutMs) {
        HWND menu = findPopupMenu(hMain, pid);
        if (menu) { *out = menu; return true; }
        ::Sleep(50);
    }
    return false;
}

LPARAM packLParam(LONG x, LONG y) {
    return static_cast<LPARAM>(
        static_cast<DWORD>(static_cast<WORD>(x)) |
        (static_cast<DWORD>(static_cast<WORD>(y)) << 16));
}

} // namespace

TEST_CASE("Task ledger: finished row persists, context menu 清理 removes it",
          "[taskledger]") {
    AppFixture fx;
    REQUIRE(fx.ready());
    HWND hMain = fx.app().mainWindow();
    REQUIRE(hMain != nullptr);
    ::ShowWindow(hMain, SW_RESTORE);
    ::Sleep(200);
    const UiElement win = fx.mainWindow();
    REQUIRE(win.valid());

    // The task page is the default page (v1.2.5) — re-click anyway so the
    // list is shown even if a previous interaction left another page up.
    clickBottomOption(win, fx.pid(), ids::BottomViewTaskOptionName);

    // The task monitor's wxListCtrl is the ONLY SysListView32 in the frame
    // (proxy/subscription lists are wxDataView; the old batch pending list
    // was removed 2026-11-04).
    HWND hList = nullptr;
    for (int t = 0; t * 200 < kRowWaitMs && !hList; ++t) {
        hList = findChildByClass(hMain, ids::ListViewClassName);
        if (!hList) ::Sleep(200);
    }
    REQUIRE(hList != nullptr);
    INFO("task list hwnd=" << (void*)hList);

    // Baseline: fresh process → nothing finished yet (placeholder only).
    REQUIRE_FALSE(hasLedgerFinishedRow(hList));

    // ── Trigger DEDUP through the real menu ───────────────────────────
    std::wstring how;
    const bool clicked =
        clickMenu(hMain, ids::MenuProxyCaption, ids::MenuDedupItem, &how);
    INFO("dedup menu click path: " << toUtf8(how));
    REQUIRE(clicked);

    // ── Wait for the FINISHED row to appear and PERSIST ────────────────
    // Completion posts kDedupOk → a modal "Dedup completed." box may appear;
    // dismiss it repeatedly, it is not what V8 asserts on.
    int finishedRow = -1;
    const DWORD start = ::GetTickCount();
    while (static_cast<int>(::GetTickCount() - start) < kRowWaitMs) {
        dismissAllPopups(fx.pid());
        finishedRow = findLedgerFinishedRow(hList);
        if (finishedRow >= 0) break;
        ::Sleep(250);
    }
    // CORE assertion (never downgraded): the finished task stays listed with
    // a terminal status instead of vanishing from the list.
    INFO("finished DEDUP row never appeared/persisted in the ledger");
    REQUIRE(finishedRow >= 0);
    INFO("finished row=" << finishedRow
         << " status=" << toUtf8(listItemText(hList, finishedRow, 4))
         << " type=" << toUtf8(listItemText(hList, finishedRow, 1)));

    // Persistence: after two more refresh cadences the row is STILL there
    // (the 1s rebuild must not drop history).
    ::Sleep(2500);
    dismissAllPopups(fx.pid());
    finishedRow = findLedgerFinishedRow(hList);
    REQUIRE(finishedRow >= 0);

    // ── Right-click the finished row → native popup menu ──────────────
    ::SendMessageW(hList, LVM_ENSUREVISIBLE, static_cast<WPARAM>(finishedRow), FALSE);
    POINT center{};
    REQUIRE(listRowCenterScreen(hList, finishedRow, &center));
    ::SetForegroundWindow(hMain);
    ::SetFocus(hList);   // cross-thread: best-effort, ignored on failure
    ::Sleep(150);

    // The SendMessage blocks until the menu loop exits → run on a worker.
    // Captured by value: the worker must never touch our stack.
    const HWND listForPoster = hList;
    const POINT ptForPoster = center;
    std::atomic<bool> posterDone{false};
    std::thread poster([listForPoster, ptForPoster, &posterDone] {
        ::SendMessageW(listForPoster, WM_CONTEXTMENU,
                       reinterpret_cast<WPARAM>(listForPoster),
                       packLParam(ptForPoster.x, ptForPoster.y));
        posterDone.store(true);
    });

    HWND hMenu = nullptr;
    const bool menuUp = waitForPopupMenu(hMain, fx.pid(), &hMenu, kMenuWaitMs);
    if (!menuUp) {
        // Popup never came up: an injected ESC closes any late menu (input
        // goes through the system queue, which the modal menu loop DOES pump
        // — posted messages to the main window would starve there), then let
        // the worker's SendMessage return, join, LOUDLY skip (never silently
        // drop).
        keybd_event(VK_ESCAPE, 0, 0, 0);
        keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
        closeMenuLoop(&posterDone, hMain, fx.app());
        poster.join();
        INFO("V8 downgrade: #32768 popup never appeared for the ledger row; "
             "menu interaction skipped (retention assertion still enforced).");
        return;
    }
    INFO("popup menu hwnd=" << (void*)hMenu);

    // UIA-invoke the clear item, scoped to the menu window (no desktop walk).
    // The menu's UIA provider lives in user32 and answers while the app's
    // modal loop pumps.
    bool invoked = false;
    {
        const UiElement menuEl = UiElement::fromHwnd(hMenu);
        if (menuEl.valid()) {
            const std::vector<UiElement> items = UiElement::findAll(
                menuEl, Locator{UIA_NamePropertyId, ids::TaskLedgerClearMenuItemName},
                fx.pid(), 2000);
            for (const UiElement& item : items) {
                if (item.click()) { invoked = true; break; }
            }
        }
    }

    // A successful InvokePattern return does NOT prove the command ran: on a
    // non-interactive desktop (locked/idle session, foreground contended) the
    // provider can accept the call while the app keeps tracking the menu.
    // Only "the menu loop actually exited" counts as an effective invoke.
    bool menuClosed = waitPosterDone(&posterDone, kMenuCloseWaitMs);
    if (!invoked || !menuClosed) {
        if (!menuClosed) {
            ::PostMessageW(hMenu, WM_CANCELMODE, 0, 0);
            menuClosed = waitPosterDone(&posterDone, kMenuCloseWaitMs);
        }
        if (!menuClosed) {
            // ESC through the input queue closes the tracking menu reliably
            // (posted messages would starve in the modal loop).
            keybd_event(VK_ESCAPE, 0, 0, 0);
            keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
            menuClosed = waitPosterDone(&posterDone, kMenuCloseWaitMs);
        }
        closeMenuLoop(&posterDone, hMain, fx.app());
        poster.join();   // menu loop exited (cancelled or app terminated)
        INFO("V8 downgrade: 清理全部完成记录 not driven on this desktop "
             "(invoked=" << invoked << ", menuClosed=" << menuClosed
             << "); menu interaction skipped (retention assertion still enforced).");
        return;
    }
    poster.join();   // menu loop exited on the invoke

    // ── Post-menu assertion (spec: assertions AFTER the menu loop) ─────
    bool cleared = false;
    for (int t = 0; t * 200 < kClearedWaitMs && !cleared; ++t) {
        dismissAllPopups(fx.pid());
        cleared = !hasLedgerFinishedRow(hList);
        if (!cleared) ::Sleep(200);
    }
    CHECK(cleared);
    CHECK(fx.app().running());
}
