// tests/ui/TestClear.cpp - Task 8: log-clear button invoke via UIA.
#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include "framework/Fixtures.h"

using namespace uitest;

namespace {

// Pure Win32 descendant search for the clear button: resolve the button HWND
// directly (EnumChildWindows + button text) and wrap it with UIA
// ElementFromHandle. Desktop-wide UIA FindFirst stalls intermittently when any
// window on the system is slow to answer WM_GETOBJECT, so we avoid tree walks.
HWND findChildByText(HWND parent, const wchar_t* text) {
    struct Ctx { const wchar_t* text; HWND found; } ctx{ text, nullptr };
    ::EnumChildWindows(parent, [](HWND h, LPARAM lp) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lp);
        wchar_t buf[256] = { 0 };
        if (::GetWindowTextW(h, buf, 256) == 0) return TRUE;
        if (wcscmp(buf, c->text) == 0) { c->found = h; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

UiElement findClearButton(HWND hMain, int timeoutMs) {
    const DWORD start = ::GetTickCount();
    while (static_cast<int>(::GetTickCount() - start) < timeoutMs) {
        HWND hBtn = findChildByText(hMain, ids::LogClearButtonName);
        if (hBtn) {
            UiElement el = UiElement::fromHwnd(hBtn);
            if (el.valid()) return el;
        }
        ::Sleep(100);
    }
    return UiElement();
}

} // namespace

TEST_CASE("Log panel clear button invokes", "[clear]") {
    AppFixture fx;
    auto win = fx.mainWindow();
    REQUIRE(win.valid());

    HWND hMain = fx.app().mainWindow();
    REQUIRE(hMain != nullptr);

    UiElement btn = findClearButton(hMain, 8000);
    REQUIRE(btn.valid());
    REQUIRE(btn.isEnabled());
    REQUIRE(btn.click());                        // InvokePattern on real Win32 BUTTON
    ::Sleep(300);
    REQUIRE(fx.app().running());                 // click handled, app alive
}
