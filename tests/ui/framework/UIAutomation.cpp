// tests/ui/framework/UIAutomation.cpp
#include "framework/UIAutomation.h"
namespace uitest {
Uia& Uia::instance() { static Uia inst; return inst; }
bool Uia::init() {
    if (automation_.get()) return true;
    HRESULT hr = ::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IUIAutomation, reinterpret_cast<void**>(&automation_));
    if (FAILED(hr)) return false;
    hr = automation_->GetRootElement(&desktop_);
    return SUCCEEDED(hr);
}
void Uia::shutdown() {
    desktop_.reset();
    automation_.reset();
}
}
