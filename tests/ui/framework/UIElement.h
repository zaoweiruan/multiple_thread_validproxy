// tests/ui/framework/UIElement.h - UIA element wrapper: PID-filtered lookup,
// Invoke/Value pattern actions, and subtree dump.
#pragma once
#include <windows.h>
#include <UIAutomation.h>
#include <string>
#include "framework/ComPtr.h"

namespace uitest {
struct Locator {
    PROPERTYID property;      // e.g. UIA_NamePropertyId / UIA_ClassNamePropertyId
    std::wstring value;
};

class UiElement {
public:
    UiElement() = default;
    // Takes ownership of an AddRef'd element pointer.
    explicit UiElement(IUIAutomationElement* owned) : element_(owned) {}
    UiElement(UiElement&& o) noexcept : element_(o.element_.release()) {}
    UiElement& operator=(UiElement&& o) noexcept {
        if (this != &o) { element_.reset(o.element_.release()); }
        return *this;
    }
    UiElement(const UiElement&) = delete;
    UiElement& operator=(const UiElement&) = delete;

    bool valid() const { return element_.get() != nullptr; }

    std::wstring name() const;
    std::wstring className() const;
    std::wstring automationId() const;
    bool isEnabled() const;
    bool isOffscreen() const;
    DWORD processId() const;

    // Subtree search under `scope`, polled until timeoutMs. When pidFilter != 0,
    // candidates whose ProcessId differs are rejected (multi-instance safety).
    static UiElement findBy(const UiElement& scope, const Locator& loc,
                            DWORD pidFilter, int timeoutMs, int pollMs = 150);

    // Direct element for a known HWND — no desktop-wide enumeration, no
    // name/pid matching (the HWND is already unique to the target window).
    static UiElement fromHwnd(HWND hwnd);

    bool click();                          // UIA_InvokePattern; false if unsupported
    bool setText(const wchar_t* text);     // UIA_ValuePattern
    std::wstring getText();                // UIA_ValuePattern current value

    std::wstring dumpTree(int maxDepth) const;   // multi-line UTF-16 text

    IUIAutomationElement* raw() const { return element_.get(); }

private:
    ComPtr<IUIAutomationElement> element_;
};
}
