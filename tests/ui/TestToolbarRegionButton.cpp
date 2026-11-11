// tests/ui/TestToolbarRegionButton.cpp - V11 (spec 2026-11-11
// Spec-ToolbarRegionParseButton): toolbar 「解析地区」 button wiring.
//
// The button replaces the retired 「最佳代理」 toolbar button in the same
// slot. wxAuiToolBar is self-drawn (single wxWindowNR node, no per-button
// UIA Name — see spec 2026-10-15 §3.2), so the button is driven by its
// command id through WM_COMMAND, exactly like TestChainPreview drives
// ID_TOOL_CONFIG (6205).
//
// Positive: WM_COMMAND 6212 (ID_TOOL_REGION = wxID_HIGHEST+212) enqueues
// RESOLVE_REGION → the task ledger gains a row whose type cell reads
// 「批量解析地区 (RESOLVE_REGION)」 and settles into a terminal status.
// Negative: the retired 「最佳代理」 id 6202 is inert — no FIND_BEST row,
// app still alive.
//
// Tag: [toolbarregion]  (registered as ctest UI_TOOLBARREGION)

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <windows.h>

#include "framework/BottomView.h"
#include "framework/Fixtures.h"
#include "framework/ListView.h"
#include "framework/Text.h"
#include "framework/UIElement.h"
#include "UIIds.h"

using namespace uitest;

namespace {

// ID_TOOL_REGION = wxID_HIGHEST(6000) + 212 (src/ui/MainFrame.cpp enum).
constexpr unsigned int kRegionCmd = 6000 + 212;
// Retired ID_TOOL_FIND = wxID_HIGHEST + 202 — the removed 「最佳代理」
// button's command id; must be inert now that the id is unbound.
constexpr unsigned int kFindBestCmd = 6000 + 202;

constexpr int kRowWaitMs = 90000;    // RESOLVE_REGION: network per proxy
constexpr int kInertWaitMs = 2500;   // negative test settle budget

bool isTerminalStatus(const std::wstring& s) {
    return s == ids::TaskStatusCompleted || s == ids::TaskStatusFailed ||
           s == ids::TaskStatusCancelled;
}

// Row whose type cell (column 1) contains `typeSubstr`; -1 when none.
int findLedgerRowByType(HWND list, const std::wstring& typeSubstr) {
    const int rows = listRowCount(list);
    for (int r = 0; r < rows; ++r) {
        if (listItemText(list, r, 1).find(typeSubstr) != std::wstring::npos) {
            return r;
        }
    }
    return -1;
}

} // namespace

TEST_CASE("Toolbar: 解析地区 button enqueues RESOLVE_REGION", "[toolbarregion]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    HWND hMain = fx.app().mainWindow();
    REQUIRE(hMain != nullptr);
    ::ShowWindow(hMain, SW_RESTORE);
    ::Sleep(200);
    const UiElement win = fx.mainWindow();
    REQUIRE(win.valid());

    clickBottomOption(win, fx.pid(), ids::BottomViewTaskOptionName);

    // The task monitor's wxListCtrl is the ONLY SysListView32 in the frame.
    HWND hList = nullptr;
    for (int t = 0; t * 200 < 30000 && !hList; ++t) {
        hList = findChildByClass(hMain, ids::ListViewClassName);
        if (!hList) ::Sleep(200);
    }
    REQUIRE(hList != nullptr);

    REQUIRE(findLedgerRowByType(hList, L"RESOLVE_REGION") < 0);

    // Toolbar button by command id (same mechanism as TestChainPreview).
    ::SendMessageTimeoutW(hMain, WM_COMMAND, MAKEWPARAM(kRegionCmd, 0), 0,
                          SMTO_NORMAL, 10000, nullptr);

    int row = -1;
    const DWORD start = ::GetTickCount();
    while (static_cast<int>(::GetTickCount() - start) < kRowWaitMs) {
        row = findLedgerRowByType(hList, L"RESOLVE_REGION");
        if (row >= 0) break;
        ::Sleep(250);
    }
    INFO("RESOLVE_REGION ledger row never appeared");
    REQUIRE(row >= 0);

    // The row must settle into a terminal status (COMPLETED on the sandbox
    // DB — zero targets returns instantly; blocked DNS fails fast).
    while (!isTerminalStatus(listItemText(hList, row, 4))) {
        if (static_cast<int>(::GetTickCount() - start) >= kRowWaitMs) break;
        ::Sleep(250);
    }
    INFO("row=" << row
         << " type=" << toUtf8(listItemText(hList, row, 1))
         << " status=" << toUtf8(listItemText(hList, row, 4)));
    REQUIRE(isTerminalStatus(listItemText(hList, row, 4)));

    REQUIRE(fx.app().running());
}

TEST_CASE("Toolbar: retired 最佳代理 id 6202 is inert", "[toolbarregion]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    HWND hMain = fx.app().mainWindow();
    REQUIRE(hMain != nullptr);
    ::ShowWindow(hMain, SW_RESTORE);
    ::Sleep(200);
    const UiElement win = fx.mainWindow();
    REQUIRE(win.valid());

    clickBottomOption(win, fx.pid(), ids::BottomViewTaskOptionName);

    HWND hList = nullptr;
    for (int t = 0; t * 200 < 30000 && !hList; ++t) {
        hList = findChildByClass(hMain, ids::ListViewClassName);
        if (!hList) ::Sleep(200);
    }
    REQUIRE(hList != nullptr);

    REQUIRE(findLedgerRowByType(hList, L"FIND_BEST") < 0);

    ::SendMessageTimeoutW(hMain, WM_COMMAND, MAKEWPARAM(kFindBestCmd, 0), 0,
                          SMTO_NORMAL, 10000, nullptr);
    ::Sleep(kInertWaitMs);

    REQUIRE(findLedgerRowByType(hList, L"FIND_BEST") < 0);
    REQUIRE(fx.app().running());
}
