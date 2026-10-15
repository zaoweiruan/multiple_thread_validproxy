// tests/ui/framework/UIElement.h - UIA element wrapper: PID-filtered lookup,
// Invoke/Value pattern actions, and subtree dump.
#pragma once
#include <windows.h>
#include <UIAutomation.h>
#include <string>
#include <vector>
#include "framework/ComPtr.h"

namespace uitest {
struct Locator {
    PROPERTYID property;      // e.g. UIA_NamePropertyId / UIA_ClassNamePropertyId
    std::wstring value;
};

// Screen-pixel bounds of a raw UIA element. Free function rather than a member
// because layout walks iterate over bare IUIAutomationElement* children and must
// not have to wrap each one in a UiElement (which would imply ownership).
bool boundingRectOf(IUIAutomationElement* raw, RECT* out);

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

    // ---- introspection additions (window-type / layout sensing) ----
    int controlType() const;               // UIA_ControlTypePropertyId; 0 when unknown
    bool boundingRect(RECT* out) const;    // screen-pixel bounds; false when unavailable
    UiElement parent() const;              // ControlViewWalker parent; invalid at root

    // Subtree search under `scope`, polled until timeoutMs. When pidFilter != 0,
    // candidates whose ProcessId differs are rejected (multi-instance safety).
    static UiElement findBy(const UiElement& scope, const Locator& loc,
                            DWORD pidFilter, int timeoutMs, int pollMs = 150);

    // Collect every match (not just the first) under `scope`, same polling and
    // pid filtering rules as findBy. Used to enumerate repeated controls such as
    // DataView columns, menu bars and dialog buttons.
    static std::vector<UiElement> findAll(const UiElement& scope, const Locator& loc,
                                          DWORD pidFilter, int timeoutMs,
                                          int pollMs = 150);

    // Direct element for a known HWND — no desktop-wide enumeration, no
    // name/pid matching (the HWND is already unique to the target window).
    static UiElement fromHwnd(HWND hwnd);

    // AddRef'd copy. UiElement is move-only on purpose (an element owns exactly
    // one reference), so any helper that has to hand the same element to more
    // than one caller must clone rather than copy.
    UiElement clone() const;

    // Actions are const because they do not mutate the wrapper - they only
    // drive the underlying COM element. That lets callers hold results in a
    // `const std::vector<UiElement>` without copying the move-only type.
    bool click() const;                    // UIA_InvokePattern; false if unsupported
    bool setText(const wchar_t* text) const;   // UIA_ValuePattern
    std::wstring getText() const;          // UIA_ValuePattern current value
    bool hasPattern(PATTERNID patternId) const;   // pattern capability probe

    std::wstring dumpTree(int maxDepth) const;   // multi-line UTF-16 text

    IUIAutomationElement* raw() const { return element_.get(); }

private:
    ComPtr<IUIAutomationElement> element_;
};
}
