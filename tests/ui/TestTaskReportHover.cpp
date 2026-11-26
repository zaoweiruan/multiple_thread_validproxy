// tests/ui/TestTaskReportHover.cpp - Task 6 (plan-taskreporthover):
// hovering a finished task row in the task monitor list shows the task report
// popup (title "任务报告 <taskId>"), implemented as a wxPopupTransientWindow
// with wxPU_CONTAINS_CONTROLS containing a read-only multiline wxTextCtrl.
//
// Flow: trigger 移除重复代理 (DEDUP — the fastest queue-visible task; the
// sandbox DB dedups in milliseconds) → wait for the finished row to appear in
// the task list → the taskId is NON-predictable (steady_clock + counter), so
// it is read from the finished row (column 0) and matched against the
// report_<taskId>.txt that Task 5 published into the GUI log dir (exeDir/log)
// → simulate hover: SetCursorPos onto the row's screen centre (wxMSW validates
// WM_MOUSEMOVE against WindowFromPoint) + PostMessage WM_MOUSEMOVE → the
// report popup must appear, located BY TITLE ("任务报告 <taskId>", the
// wxWindow::SetLabel → SetWindowText path), NOT by C++ class name.
//
// Tag: [taskreporthover]  (registered as ctest UI_TASKREPORTHOVER)

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <windows.h>

#include "framework/BottomView.h"
#include "framework/Fixtures.h"
#include "framework/ListView.h"
#include "framework/Menu.h"
#include "framework/Text.h"
#include "framework/UIElement.h"
#include "framework/WindowInfo.h"
#include "UIIds.h"

using namespace uitest;

namespace {

constexpr int kRowWaitMs = 30000;    // dedup run + 1s refresh cadence
constexpr int kPopupWaitMs = 8000;   // report popup appearance

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

// GUI log dir: exeDir + "\log" (main_gui.cpp: logDir = exeDir / "log";
// Logger::getLogDir() resolves to the same). The GUI resolves the exe path
// from its own module, so the test mirrors it from VALIDPROXY_APP_EXE.
std::wstring guiLogDir() {
    const std::wstring exe = Paths::exePath();
    const size_t pos = exe.find_last_of(L'\\');
    const std::wstring dir = (pos == std::wstring::npos) ? exe
                                                         : exe.substr(0, pos);
    return dir + L"\\log";
}

// Read the report content for `taskId` by enumerating report_*.txt in the log
// dir and matching the taskId extracted from the FILENAME (the brief forbids
// hardcoding the non-predictable taskId). "" when no file matches.
std::string readReportForTask(const std::string& taskId,
                              const std::wstring& logDir) {
    const std::string pattern = toUtf8(logDir) + "\\report_*.txt";
    WIN32_FIND_DATAA fd{};
    const HANDLE h = ::FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return std::string();
    std::string out;
    do {
        std::string name(fd.cFileName);
        // name == "report_<taskId>.txt"
        if (name.rfind("report_", 0) == 0 && name.size() > 8 &&
            name.compare(name.size() - 4, 4, ".txt") == 0) {
            const std::string id = name.substr(7, name.size() - 7 - 4);
            if (id == taskId) {
                std::ifstream f((toUtf8(logDir) + "\\" + name).c_str(),
                                std::ios::binary);
                std::ostringstream ss;
                ss << f.rdbuf();
                out = ss.str();
                break;
            }
        }
    } while (::FindNextFileA(h, &fd));
    ::FindClose(h);
    return out;
}

// The report popup of this PID whose title matches exactly (SetLabel → the
// window title "任务报告 <taskId>").
HWND findReportPopup(DWORD pid, const std::wstring& title) {
    for (HWND w : topLevelWindows(pid, true)) {
        wchar_t t[256] = {0};
        ::GetWindowTextW(w, t, 256);
        if (title == t) return w;
    }
    return nullptr;
}

// True when the popup's wxTextCtrl child (read-only multiline, class "Edit"
// on wxMSW) displays text containing `needle` — i.e. the report content is
// actually rendered, not just an empty popup shell. `detail` (optional)
// receives a dump of every child class + text prefix for diagnostics.
bool popupChildShows(HWND popup, const std::string& needle,
                     std::wstring* detail = nullptr) {
    struct Ctx {
        const std::string& needle;
        bool found;
        std::wstring* detail;
    } ctx{needle, false, detail};
    ::EnumChildWindows(popup, [](HWND h, LPARAM lp) -> BOOL {
        Ctx* c = reinterpret_cast<Ctx*>(lp);
        // GetWindowTextLengthW cannot read a child control in another process;
        // use WM_GETTEXTLENGTH (marshaled cross-process by the window manager).
        const int n = static_cast<int>(::SendMessageW(h, WM_GETTEXTLENGTH, 0, 0));
        if (c->detail != nullptr) {
            wchar_t cls[64] = {0};
            ::GetClassNameW(h, cls, 64);
            wchar_t buf[32] = {0};
            std::swprintf(buf, 32, L"0x%p", static_cast<void*>(h));
            *c->detail += std::wstring(cls) + L" hwnd=" + buf +
                          L" len=" + std::to_wstring(n) + L"\n";
        }
        // NOTE: GetWindowTextW does NOT retrieve the text of a child control
        // in ANOTHER process (it only works for the same-process caption), so
        // the child's text is read with SendMessageW(WM_GETTEXT), which the
        // window manager marshals across processes.
        if (n <= 0) return TRUE;   // keep walking
        std::wstring t(static_cast<size_t>(n) + 1, L'\0');
        ::SendMessageW(h, WM_GETTEXT, n + 1,
                       reinterpret_cast<LPARAM>(&t[0]));
        if (toUtf8(t).find(c->needle) != std::string::npos) {
            c->found = true;
            return FALSE;          // stop
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

} // namespace

// ---------------------------------------------------------------------------
TEST_CASE("Hover finished task row shows report popup", "[taskreporthover]") {
    AppFixture fx;
    REQUIRE(fx.ready());
    HWND hMain = fx.app().mainWindow();
    REQUIRE(hMain != nullptr);
    ::ShowWindow(hMain, SW_RESTORE);
    ::Sleep(200);
    const UiElement win = fx.mainWindow();
    REQUIRE(win.valid());

    // The task page is the default page — re-click anyway so the list is shown
    // even if a previous interaction left another page up.
    clickBottomOption(win, fx.pid(), ids::BottomViewTaskOptionName);

    // The task monitor's wxListCtrl is the ONLY SysListView32 in the frame.
    HWND hList = nullptr;
    for (int t = 0; t * 200 < kRowWaitMs && !hList; ++t) {
        hList = findChildByClass(hMain, ids::ListViewClassName);
        if (!hList) ::Sleep(200);
    }
    REQUIRE(hList != nullptr);
    INFO("task list hwnd=" << (void*)hList);

    // ── Trigger DEDUP through the real menu (fastest queue-visible task) ──
    std::wstring how;
    const bool clicked =
        clickMenu(hMain, ids::MenuProxyCaption, ids::MenuDedupItem, &how);
    INFO("dedup menu click path: " << toUtf8(how));
    REQUIRE(clicked);

    // ── Wait for the FINISHED row to appear ──────────────────────────────
    int finishedRow = -1;
    const DWORD start = ::GetTickCount();
    while (static_cast<int>(::GetTickCount() - start) < kRowWaitMs) {
        finishedRow = findLedgerFinishedRow(hList);
        if (finishedRow >= 0) break;
        ::Sleep(250);
    }
    INFO("finished DEDUP row never appeared in the ledger");
    REQUIRE(finishedRow >= 0);
    REQUIRE(listItemText(hList, finishedRow, 4) == ids::TaskStatusCompleted);
    INFO("finished row=" << finishedRow
         << " status=" << toUtf8(listItemText(hList, finishedRow, 4))
         << " type=" << toUtf8(listItemText(hList, finishedRow, 1)));

    // ── taskId from the row (column 0; non-predictable, never hardcoded) ──
    const std::string taskId = toUtf8(listItemText(hList, finishedRow, 0));
    REQUIRE(!taskId.empty());

    // ── The report file must exist in the log dir (Task 5 publish) ───────
    const std::wstring logDir = guiLogDir();
    std::string reportContent;
    const DWORD rs = ::GetTickCount();
    while (static_cast<int>(::GetTickCount() - rs) < kPopupWaitMs) {
        reportContent = readReportForTask(taskId, logDir);
        if (!reportContent.empty()) break;
        ::Sleep(200);
    }
    INFO("taskId=" << taskId << " logDir=" << toUtf8(logDir)
         << " reportLen=" << reportContent.size());
    REQUIRE(!reportContent.empty());
    // Report must carry the metadata header + statistics section (spec R3).
    REQUIRE(reportContent.find("taskId: " + taskId) != std::string::npos);
    REQUIRE(reportContent.find("---- statistics ----") != std::string::npos);

    // ── Ensure the frame is restored+foreground BEFORE the hover. A fresh
    //    sandbox launch minimises the frame ~1s after first show and
    //    onIconize hides it to the tray; the SW_RESTORE at the top of this
    //    test fires too early to beat that. Re-assert here (after the DEDUP
    //    report is published, i.e. after the startup settle) so the hover
    //    really happens on a visible row and the popup can render. ────────
    ::ShowWindow(hMain, SW_RESTORE);
    ::SetForegroundWindow(hMain);
    const DWORD restoreStart = ::GetTickCount();
    while (::IsIconic(hMain) &&
           static_cast<int>(::GetTickCount() - restoreStart) < 4000) {
        ::ShowWindow(hMain, SW_RESTORE);
        ::SetForegroundWindow(hMain);
        ::Sleep(100);
    }
    REQUIRE(::IsIconic(hMain) == FALSE);
    REQUIRE(::IsWindowVisible(hMain) != FALSE);
    ::Sleep(150);  // let the restore + AUI relayout settle

    // ── Simulate hover: SetCursorPos onto the row's screen centre (wxMSW
    //    validates WM_MOUSEMOVE against WindowFromPoint) then post a
    //    client-coordinate WM_MOUSEMOVE to the list. ─────────────────────
    POINT rowPt{};
    REQUIRE(listRowCenterScreen(hList, finishedRow, &rowPt));
    ::SetCursorPos(rowPt.x, rowPt.y);
    ::Sleep(60);
    POINT cl = rowPt;
    ::ScreenToClient(hList, &cl);
    ::PostMessageW(hList, WM_MOUSEMOVE, 0, MAKELPARAM(cl.x, cl.y));

    // ── The popup must appear, located BY TITLE "任务报告 <taskId>" ──────
    // The frame hides to tray on minimize (onIconize→Hide), which hides the
    // popup with it; the poll therefore re-restores + re-hovers whenever the
    // frame drops, so the popup can still be observed even if the app
    // minimises again after the initial restore above.
    const std::wstring popupTitle = toWide(std::string(u8"任务报告 ") + taskId);
    HWND hPopup = nullptr;
    const DWORD pt0 = ::GetTickCount();
    while (static_cast<int>(::GetTickCount() - pt0) < kPopupWaitMs) {
        if (::IsIconic(hMain) || !::IsWindowVisible(hMain)) {
            ::ShowWindow(hMain, SW_RESTORE);
            ::SetForegroundWindow(hMain);
            ::Sleep(120);
            POINT rp{};
            if (listRowCenterScreen(hList, finishedRow, &rp)) {
                ::SetCursorPos(rp.x, rp.y);
                ::Sleep(50);
                POINT rc = rp;
                ::ScreenToClient(hList, &rc);
                ::PostMessageW(hList, WM_MOUSEMOVE, 0,
                               MAKELPARAM(rc.x, rc.y));
            }
            continue;
        }
        hPopup = findReportPopup(fx.pid(), popupTitle);
        if (hPopup) break;
        ::Sleep(50);
    }
    INFO("popup title=" << toUtf8(popupTitle));
    REQUIRE(hPopup != nullptr);
    REQUIRE(::IsWindowVisible(hPopup) != FALSE);

    // ── The popup is anchored near the row (not off-screen at the origin) ─
    RECT pr{};
    REQUIRE(::GetWindowRect(hPopup, &pr) != FALSE);
    INFO("popup rect=(" << pr.left << "," << pr.top << ","
         << pr.right << "," << pr.bottom << ")");
    REQUIRE((pr.left > 0 && pr.top > 0));
    REQUIRE(((pr.right - pr.left) > 0 && (pr.bottom - pr.top) > 0));

    // ── The popup actually renders the report (Task 5 content, not an
    //    empty shell): the wxTextCtrl child must show the metadata header. ─
    std::wstring childDetail;
    const bool shows = popupChildShows(hPopup, "taskId: " + taskId, &childDetail);
    INFO("popup children:\n" << toUtf8(childDetail));
    REQUIRE(shows);

    // ── task-report-hover Task 6 (bugfix): explicit click-away dismissal ──
    // The popup suppresses wx's native transient dismissal (WM_ACTIVATE →
    // DismissOnDeactivate) by design (wxPU_CONTAINS_CONTROLS ⇒
    // MSWDismissUnfocusedPopup never dismisses), so closing an open report
    // popup relies on the explicit paths: list leave / non-finished row /
    // missing report / (new) click-away. Regression: a left-down that lands
    // OUTSIDE the popup rect must dismiss it. Post WM_LBUTTONDOWN to the
    // TaskMonitorPanel HWND (the panel owns the wxEVT_LEFT_DOWN binding) at
    // client origin — the popup anchors below the hovered row, so the panel
    // client origin is outside the popup rect and must trigger dismissal.
    const HWND hPanel = reinterpret_cast<HWND>(::GetWindowLongPtr(hList, GWLP_HWNDPARENT));
    REQUIRE(hPanel != nullptr);
    INFO("task monitor panel hwnd=" << (void*)hPanel);
    ::PostMessageW(hPanel, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(0, 0));

    // The panel is on the UI thread and the posted message is marshalled
    // back to it, so the dismissal should land within a few event-loop turns.
    bool popupDismissed = false;
    const DWORD hideDeadline = ::GetTickCount() + 5000;
    while (::GetTickCount() < hideDeadline) {
        if (!::IsWindowVisible(hPopup)) { popupDismissed = true; break; }
        ::Sleep(50);
    }
    INFO("report popup dismissed by outside click: " << popupDismissed);
    REQUIRE(popupDismissed);

    // ── The dismissal must be STICKY for the hovered row ─────────────────
    // Hiding the popup makes Windows synthesise a WM_MOUSEMOVE over the row
    // under the cursor; without the "suppress re-hover" guard the popup
    // re-opens a few ms after the click (the original intermittent failure:
    // the poll above saw it visible again). The cursor is still on the row
    // here, so the popup must remain hidden.
    bool stayedHidden = true;
    const DWORD stableDeadline = ::GetTickCount() + 600;
    while (::GetTickCount() < stableDeadline) {
        if (::IsWindowVisible(hPopup)) { stayedHidden = false; break; }
        ::Sleep(50);
    }
    INFO("report popup stayed hidden after click-away: " << stayedHidden);
    REQUIRE(stayedHidden);
}
