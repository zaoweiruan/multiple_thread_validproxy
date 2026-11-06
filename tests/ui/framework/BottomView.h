// tests/ui/framework/BottomView.h - drive the bottom shared-view radio
// (「监控」: 任务 / 任务进度 / 日志; spec 2026-11-03 v1.2, re-ordered v1.2.5,
//
// Since v1.2.5 the DEFAULT page is 任务 (owner request 2026-11-05), so any
// test whose anchors live on the log page must switch to it first via
// showLogPage() instead of assuming the buttons are visible at startup.
//
// wxMSW renders the wxRadioBox as a native group box (Name == 「监控」) with
// one radio Button per option; scoping lookups to that group avoids the menu
// bar's 任务 entry (same Name) without relying on geometry.
#pragma once
#include <vector>
#include <windows.h>

#include "framework/Layout.h"
#include "framework/UIElement.h"
#include "UIIds.h"

namespace uitest {

inline bool isOptionVisible(const UiElement& el) {
    if (!el.valid() || el.isOffscreen()) return false;
    RECT r{};
    if (!el.boundingRect(&r)) return false;
    return rectWidth(r) > 0 && rectHeight(r) > 0;
}

// All pid-owned 监控 options with this Name. Group-scoped first (immune to
// window resizing moving the radio box), geometry fallback second (bottom
// half of the window) for trees that do not expose the group caption.
inline std::vector<UiElement> findBottomOptions(const UiElement& win, DWORD pid,
                                                const wchar_t* name) {
    std::vector<UiElement> out;
    const UiElement group = UiElement::findBy(
        win, Locator{UIA_NamePropertyId, ids::BottomViewRadioName}, pid, 3000);
    if (group.valid()) {
        out = UiElement::findAll(group, Locator{UIA_NamePropertyId, name}, pid, 1500);
        if (!out.empty()) return out;
    }
    RECT wr{};
    if (!win.boundingRect(&wr)) return out;
    const std::vector<UiElement> all = UiElement::findAll(
        win, Locator{UIA_NamePropertyId, name}, pid, 1500);
    const long midY = wr.top + (wr.bottom - wr.top) / 2;
    for (const UiElement& el : all) {
        RECT r{};
        if (!el.boundingRect(&r)) continue;
        const long cy = (r.top + r.bottom) / 2;
        if (cy > midY) out.push_back(el.clone());
    }
    return out;
}

// Click a 监控 option by Name. False when the option is missing or Invoke is
// unsupported (callers skip rather than fail the gate on such environments).
inline bool clickBottomOption(const UiElement& win, DWORD pid, const wchar_t* name) {
    const std::vector<UiElement> hits = findBottomOptions(win, pid, name);
    if (hits.empty()) return false;
    return hits.front().click();
}

// Poll until the option reports selected (radio state flips on the UI thread
// after a programmatic or invoked switch).
inline bool waitBottomOptionSelected(const UiElement& win, DWORD pid,
                                     const wchar_t* name, int timeoutMs) {
    for (int waited = 0; waited <= timeoutMs; waited += 200) {
        const std::vector<UiElement> hits = findBottomOptions(win, pid, name);
        for (const UiElement& el : hits) {
            if (el.isSelected()) return true;
        }
        ::Sleep(200);
    }
    return false;
}

// Poll until every named log button reports visible (or timeout, ms).
inline bool waitLogButtons(const UiElement& win, DWORD pid,
                           bool wantVisible, int timeoutMs) {
    const wchar_t* names[] = { ids::LogClearButtonName, ids::LogStatsButtonName,
                               ids::LogOpenButtonName };
    for (int waited = 0; waited <= timeoutMs; waited += 200) {
        bool all = true;
        for (const wchar_t* n : names) {
            const UiElement el = UiElement::findBy(
                win, Locator{UIA_NamePropertyId, n}, pid, 300);
            const bool vis = isOptionVisible(el);
            if (vis != wantVisible) all = false;
        }
        if (all) return true;
        ::Sleep(200);
    }
    return false;
}

// Switch to the log page and wait until its buttons are visible. Tests that
// anchor on log controls call this before asserting (default page is 任务).
inline bool showLogPage(const UiElement& win, DWORD pid, int timeoutMs = 5000) {
    if (!clickBottomOption(win, pid, ids::BottomViewLogOptionName)) return false;
    return waitLogButtons(win, pid, true, timeoutMs);
}

} // namespace uitest
