// tests/ui/framework/UIElement.cpp
#include "framework/UIElement.h"
#include "framework/UIAutomation.h"
#include "framework/Wait.h"
#include <sstream>

namespace uitest {
std::wstring UiElement::name() const {
    if (!element_.get()) return std::wstring();
    BSTR b = nullptr;
    if (FAILED(element_->get_CurrentName(&b))) return std::wstring();
    std::wstring s(b ? b : L"");
    ::SysFreeString(b);
    return s;
}
std::wstring UiElement::className() const {
    if (!element_.get()) return std::wstring();
    BSTR b = nullptr;
    if (FAILED(element_->get_CurrentClassName(&b))) return std::wstring();
    std::wstring s(b ? b : L"");
    ::SysFreeString(b);
    return s;
}
std::wstring UiElement::automationId() const {
    if (!element_.get()) return std::wstring();
    BSTR b = nullptr;
    if (FAILED(element_->get_CurrentAutomationId(&b))) return std::wstring();
    std::wstring s(b ? b : L"");
    ::SysFreeString(b);
    return s;
}
bool UiElement::isEnabled() const {
    BOOL v = FALSE;
    return element_.get() && SUCCEEDED(element_->get_CurrentIsEnabled(&v)) && v;
}
bool UiElement::isOffscreen() const {
    BOOL v = TRUE;
    return !element_.get() || FAILED(element_->get_CurrentIsOffscreen(&v)) || v;
}
DWORD UiElement::processId() const {
    int v = 0;                       // MinGW header declares int*, not DWORD*
    if (element_.get()) element_->get_CurrentProcessId(&v);
    return static_cast<DWORD>(v);
}

// Leak-safe variant of the plan's findBy: the candidate pointer is kept in a
// local variable across polls; ownership transfers to UiElement only on the
// success path, every rejected/failed branch Releases it (plan §六 Task 3 note).
UiElement UiElement::findBy(const UiElement& scope, const Locator& loc,
                            DWORD pidFilter, int timeoutMs, int pollMs) {
    IUIAutomation* ua = Uia::instance().com();
    if (!ua || !scope.raw()) return UiElement();

    VARIANT v;
    ::VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = ::SysAllocString(loc.value.c_str());
    ComPtr<IUIAutomationCondition> cond;
    HRESULT hr = ua->CreatePropertyCondition(loc.property, v, &cond);
    ::VariantClear(&v);
    if (FAILED(hr)) return UiElement();

    IUIAutomationElement* resultRaw = nullptr;
    const bool found = waitFor(timeoutMs, pollMs, [&]() -> bool {
        HRESULT hrf = scope.raw()->FindFirst(TreeScope_Subtree, cond.get(), &resultRaw);
        if (FAILED(hrf)) {
            if (resultRaw) { resultRaw->Release(); resultRaw = nullptr; }
            return false;
        }
        if (!resultRaw) return false;
        if (pidFilter != 0) {
            int pidRaw = 0;          // MinGW header declares int*, not DWORD*
            resultRaw->get_CurrentProcessId(&pidRaw);
            if (static_cast<DWORD>(pidRaw) != pidFilter) {
                resultRaw->Release();
                resultRaw = nullptr;
                return false;
            }
        }
        return true;
    });
    if (!found) {
        if (resultRaw) { resultRaw->Release(); resultRaw = nullptr; }
        return UiElement();
    }
    return UiElement(resultRaw);   // transfer ownership
}

bool UiElement::click() {
    if (!element_.get()) return false;
    IUnknown* unk = nullptr;         // MinGW header: IUnknown** out-param, not VARIANT
    HRESULT hr = element_->GetCurrentPattern(UIA_InvokePatternId, &unk);
    bool ok = false;
    if (SUCCEEDED(hr) && unk) {
        IUIAutomationInvokePattern* inv = nullptr;
        if (SUCCEEDED(unk->QueryInterface(IID_IUIAutomationInvokePattern,
                                          reinterpret_cast<void**>(&inv))) && inv) {
            ok = SUCCEEDED(inv->Invoke());
            inv->Release();
        }
        unk->Release();
    }
    return ok;
}

bool UiElement::setText(const wchar_t* text) {
    if (!element_.get()) return false;
    IUnknown* unk = nullptr;         // MinGW header: IUnknown** out-param, not VARIANT
    HRESULT hr = element_->GetCurrentPattern(UIA_ValuePatternId, &unk);
    bool ok = false;
    if (SUCCEEDED(hr) && unk) {
        IUIAutomationValuePattern* val = nullptr;
        if (SUCCEEDED(unk->QueryInterface(IID_IUIAutomationValuePattern,
                                          reinterpret_cast<void**>(&val))) && val) {
            ok = SUCCEEDED(val->SetValue(const_cast<BSTR>(text)));
            val->Release();
        }
        unk->Release();
    }
    return ok;
}

std::wstring UiElement::getText() {
    if (!element_.get()) return std::wstring();
    VARIANT v;
    ::VariantInit(&v);
    if (FAILED(element_->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &v)))
        return std::wstring();
    std::wstring s = (v.vt == VT_BSTR && v.bstrVal) ? std::wstring(v.bstrVal) : std::wstring();
    ::VariantClear(&v);
    return s;
}

static void appendLine(std::wostringstream& out, IUIAutomationElement* el, int depth) {
    BSTR n = nullptr, c = nullptr;
    CONTROLTYPEID ct = UIA_ControlTypePropertyId;   // MinGW constant name
    int pidRaw = 0;                  // MinGW header declares int*, not DWORD*
    BOOL off = TRUE;
    el->get_CurrentName(&n);
    el->get_CurrentClassName(&c);
    el->get_CurrentControlType(&ct);
    el->get_CurrentProcessId(&pidRaw);
    const DWORD pid = static_cast<DWORD>(pidRaw);
    el->get_CurrentIsOffscreen(&off);
    for (int i = 0; i < depth; ++i) out << L"  ";
    out << L"- type=" << ct << L" name=\"" << (n ? n : L"") << L"\" class=\""
        << (c ? c : L"") << L"\" pid=" << pid << (off ? L" [offscreen]" : L"") << L"\n";
    ::SysFreeString(n);
    ::SysFreeString(c);
}
static void walk(IUIAutomation* ua, IUIAutomationElement* el,
                 std::wostringstream& out, int depth, int maxDepth) {
    appendLine(out, el, depth);
    if (depth >= maxDepth) return;
    IUIAutomationTreeWalker* walker = nullptr;
    if (FAILED(ua->get_ControlViewWalker(&walker)) || !walker) return;
    IUIAutomationElement* child = nullptr;
    if (SUCCEEDED(walker->GetFirstChildElement(el, &child)) && child) {
        while (child) {
            walk(ua, child, out, depth + 1, maxDepth);
            IUIAutomationElement* next = nullptr;
            if (FAILED(walker->GetNextSiblingElement(child, &next)) || !next) {
                child->Release();
                child = nullptr;
                break;
            }
            child->Release();
            child = next;
        }
    }
    walker->Release();
}

std::wstring UiElement::dumpTree(int maxDepth) const {
    std::wostringstream out;
    IUIAutomation* ua = Uia::instance().com();
    if (ua && element_.get()) walk(ua, element_.get(), out, 0, maxDepth);
    return out.str();
}
}
