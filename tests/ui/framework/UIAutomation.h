// tests/ui/framework/UIAutomation.h
#pragma once
#include <windows.h>
#include <UIAutomation.h>
#include "framework/ComPtr.h"
namespace uitest {
class Uia {
public:
    static Uia& instance();
    bool init();                                   // idempotent
    // Explicit teardown: MUST be called before CoUninitialize, otherwise the
    // static-singleton destructor releases COM pointers after the apartment
    // is gone (access violation at process exit).
    void shutdown();
    IUIAutomation* com() const { return automation_.get(); }
    // Non-owning pointer; use acquireDesktop() when wrapping into UiElement.
    IUIAutomationElement* desktop() const { return desktop_.get(); }
    // Returns a NEW reference (caller owns; safe for UiElement ctor).
    IUIAutomationElement* acquireDesktop() const { return desktop_.acquire(); }
private:
    Uia() = default;
    ComPtr<IUIAutomation> automation_;
    ComPtr<IUIAutomationElement> desktop_;
};
}
