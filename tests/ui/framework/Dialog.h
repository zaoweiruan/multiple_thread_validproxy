// tests/ui/framework/Dialog.h - popup / dialog inspection helpers.
//
// Purpose: turn "a modal popup appeared" into an *assertable* fact. A UI test
// that only sees "the main window handle changed" cannot tell a benign info
// box from a fatal config error. These helpers resolve a popup to a
// DialogReport carrying the title, every visible text node, a severity guess
// and any structured error code, so a test can assert on the *content*.
//
// Design notes:
//  * Header-only, everything `inline`. wxWidgets dialogs are single-HWND
//    (`wxWindowNR`) with real child HWNDs underneath, so both the UIA tree and
//    the Win32 child enumeration are walked and merged. UIA first (it sees
//    owner-drawn text), Win32 as a safety net for classic #32770 boxes.
//  * Nothing here throws and nothing here blocks forever: every call is
//    bounded by an explicit timeout and degrades to a `found == false` report.
//  * Severity is a *heuristic* and is documented as such - it is derived from
//    the window class, the icon style bit, and title/body keywords, never from
//    privileged information.

#pragma once

#include <windows.h>
#include <cwctype>

#include <sstream>
#include <string>
#include <vector>

#include "framework/UIElement.h"
#include "framework/UIAutomation.h"
#include "framework/WindowInfo.h"

namespace uitest {

// ---------------------------------------------------------------------------
// Severity
// ---------------------------------------------------------------------------

enum class DialogSeverity { None, Information, Warning, Error, Question };

inline const char* dialogSeverityToken(DialogSeverity s) {
    switch (s) {
        case DialogSeverity::Information: return "info";
        case DialogSeverity::Warning:     return "warning";
        case DialogSeverity::Error:       return "error";
        case DialogSeverity::Question:    return "question";
        case DialogSeverity::None:
        default:                          return "none";
    }
}

// ---------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------

// Everything known about one popup. Default-constructed so callers may write
// `DialogReport r;` and read it even when nothing was found.
struct DialogReport {
    bool            found        = false;
    HWND            hwnd         = nullptr;
    WindowKind      kind         = WindowKind::Unknown;
    DialogSeverity  severity     = DialogSeverity::None;
    std::wstring    title        = L"";
    std::wstring    body         = L"";   // best-effort single blob of the message
    std::wstring    errorCode    = L"";   // e.g. L"E-DB-01"; empty when absent
    std::vector<std::wstring> texts;    // every visible text node, in tree order

    bool valid() const { return found && hwnd != nullptr && ::IsWindow(hwnd); }
};

// ---------------------------------------------------------------------------
// Text collection
// ---------------------------------------------------------------------------

// Walks the UIA ControlView subtree of `root` and appends every non-empty
// Name of a text-bearing control. Mirrors the walker pattern used by
// UiElement::dumpTree but collects values instead of formatting them.
inline void collectUiaTexts(const UiElement& root, std::vector<std::wstring>& out, int maxDepth) {
    if (!root.valid()) return;
    IUIAutomation* ua = Uia::instance().com();
    if (!ua) return;
    IUIAutomationTreeWalker* walker = nullptr;
    if (FAILED(ua->get_ControlViewWalker(&walker)) || !walker) return;

    // Explicit breadth-first queue instead of recursion: dialog trees are shallow,
    // but a malformed provider must not be able to blow the C++ stack.
    //
    // Every reference obtained from the walker is parked in `owned` and
    // released only after the walk finishes. Releasing mid-walk is a
    // use-after-free: the UIA client cache recycles element proxies, so a
    // pointer still sitting in the queue can be handed back by the very next
    // sibling call, and the first Release then frees it twice. Slot 0 is the
    // caller's root element and is therefore never released here.
    std::vector<IUIAutomationElement*> owned;
    std::vector<int> depths;
    size_t head = 0;
    owned.push_back(root.raw());   // slot 0: owned by root's ComPtr
    depths.push_back(0);

    while (head < owned.size()) {
        IUIAutomationElement* el = owned[head];
        const int depth = depths[head];
        ++head;
        if (!el) continue;

        int ctRaw = 0;
        if (SUCCEEDED(el->get_CurrentControlType(&ctRaw))) {
            const CONTROLTYPEID ct = static_cast<CONTROLTYPEID>(ctRaw);
            // MinGW's projection does not declare UIA_StaticTextControlTypeId, so the
            // text-bearing set is limited to the ids the header knows about.
            const bool textBearing =
                (ct == UIA_TextControlTypeId || ct == UIA_EditControlTypeId ||
                 ct == UIA_DocumentControlTypeId || ct == UIA_ListItemControlTypeId);
            if (textBearing) {
                BSTR n = nullptr;
                if (SUCCEEDED(el->get_CurrentName(&n)) && n) {
                    if (::wcslen(n) > 0) out.push_back(n);
                    ::SysFreeString(n);
                }
            }
        }

        if (depth >= maxDepth) continue;

        IUIAutomationElement* child = nullptr;
        if (SUCCEEDED(walker->GetFirstChildElement(el, &child)) && child) {
            while (child) {
                owned.push_back(child);
                depths.push_back(depth + 1);
                IUIAutomationElement* sib = nullptr;
                if (FAILED(walker->GetNextSiblingElement(child, &sib)) || !sib) {
                    break;   // last sibling already recorded in `owned`
                }
                child = sib;
            }
        }
    }

    for (size_t i = 1; i < owned.size(); ++i) owned[i]->Release();
    walker->Release();
}

inline void collectChildWindowTexts(HWND hwnd, std::vector<std::wstring>& out) {
    struct Ctx { std::vector<std::wstring>* out; } ctx;
    ctx.out = &out;
    ::EnumChildWindows(
        hwnd,
        [](HWND child, LPARAM lp) -> BOOL {
            Ctx* c = reinterpret_cast<Ctx*>(lp);
            if (!::IsWindowVisible(child)) return TRUE;
            const int len = ::GetWindowTextLengthW(child);
            if (len > 0) {
                std::vector<wchar_t> buf(static_cast<size_t>(len) + 1, L'\0');
                const int got = ::GetWindowTextW(child, buf.data(), len + 1);
                if (got > 0) {
                    std::wstring s(buf.data(), static_cast<size_t>(got));
                    if (s.find_first_not_of(L" \t\r\n") != std::wstring::npos)
                        c->out->push_back(s);
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
}

inline void appendUniqueText(std::vector<std::wstring>& out, const std::wstring& s) {
    if (s.empty()) return;
    for (size_t i = 0; i < out.size(); ++i)
        if (out[i] == s) return;
    out.push_back(s);
}

// ---------------------------------------------------------------------------
// Severity heuristics
// ---------------------------------------------------------------------------

// Case-insensitive by design. Dialog text arrives in whatever case the author
// used - "Error:", "ERROR:", "failed to open" - so a case-sensitive `find`
// silently misses the most obvious errors. Wide ASCII only, so towlower on
// wchar_t is safe; CJK keywords are unaffected by case anyway.
inline bool containsKeyword(const std::wstring& hay, const wchar_t* needle) {
    if (!needle || !*needle) return false;
    std::wstring h = hay;
    std::wstring n(needle);
    for (size_t i = 0; i < h.size(); ++i) h[i] = static_cast<wchar_t>(::towlower(h[i]));
    for (size_t i = 0; i < n.size(); ++i) n[i] = static_cast<wchar_t>(::towlower(n[i]));
    return h.find(n) != std::wstring::npos;
}

inline DialogSeverity classifySeverity(const WindowInfo& win, const std::wstring& body) {
    const std::wstring& title = win.title;

    // Keyword evidence wins over style bits: wxWidgets message boxes frequently
    // reuse MB_OK for genuine errors, and the project reports failures in text.
    if (containsKeyword(body, L"error") || containsKeyword(body, L"failed") ||
        containsKeyword(body, L"fatal") || containsKeyword(body, L"exception") ||
        containsKeyword(body, L"invalid") || containsKeyword(body, L"cannot") ||
        containsKeyword(body, L"错误") || containsKeyword(body, L"失败") ||
        containsKeyword(body, L"异常") || containsKeyword(body, L"致命"))
        return DialogSeverity::Error;

    if (containsKeyword(body, L"warning") || containsKeyword(body, L"warn") ||
        containsKeyword(body, L"警告"))
        return DialogSeverity::Warning;

    if (containsKeyword(title, L"error") || containsKeyword(title, L"错误") ||
        containsKeyword(title, L"failure"))
        return DialogSeverity::Error;

    if (containsKeyword(title, L"warning") || containsKeyword(title, L"警告"))
        return DialogSeverity::Warning;

    // MB_ICONHAND = 0x10, MB_ICONERROR = 0x10 (alias)
    if (win.exStyle & 0x00000010L) return DialogSeverity::Error;
    // MB_ICONWARNING = 0x30
    if (win.exStyle & 0x00000030L) return DialogSeverity::Warning;
    // MB_ICONQUESTION = 0x20
    if (win.exStyle & 0x00000020L) return DialogSeverity::Question;
    // MB_ICONINFORMATION = 0x40
    if (win.exStyle & 0x00000040L) return DialogSeverity::Information;
    return DialogSeverity::None;
}

// Extracts a structured error code such as `E-DB-01`, `DB-CONN`, or
// `D8` from the message text. Deliberately conservative: it only reports a
// token when at least one hyphen is present (or the token is a short
// all-digit marker). Returns an empty string when nothing matches.
inline std::wstring extractErrorCode(const std::vector<std::wstring>& texts) {
    for (size_t t = 0; t < texts.size(); ++t) {
        const std::wstring& s = texts[t];
        std::wstring token;
        bool inToken = false;
        for (size_t i = 0; i <= s.size(); ++i) {
            const wchar_t c = (i < s.size()) ? s[i] : L' ';
            const bool upperAlnum = (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9');
            const bool digit      = (c >= L'0' && c <= L'9');
            const bool boundary   = (c == L'-' || c == L'_' || c == L' ' || c == L':' ||
                                     c == L'[' || c == L']' || c == L'(' || c == L')');

            if (upperAlnum) {
                inToken = true;
                token.push_back(c);
            } else if (c == L'-' && inToken) {
                token.push_back(c);   // keep hyphens, they are part of the code
            } else if (boundary) {
                // Trim trailing hyphens then decide.
                while (!token.empty() && token[token.size() - 1] == L'-') token.erase(token.size() - 1);
                if (!token.empty()) {
                    const bool hasHyphen = token.find(L'-') != std::wstring::npos;
                    bool allDigit = true;
                    for (size_t k = 0; k < token.size(); ++k)
                        if (!digit && (token[k] < L'0' || token[k] > L'9')) { allDigit = false; break; }
                    // Require a leading letter for the hyphen form so ordinary
                    // sentences ("well-known") are not mistaken for codes.
                    const bool startsUpper = token[0] >= L'A' && token[0] <= L'Z';
                    if (hasHyphen && startsUpper) return token;
                    if (allDigit && token.size() >= 2 && token.size() <= 3) return token;
                    // Letter+digit guard markers, e.g. the project's own "D8"
                    // (see prepare-ui-sandbox.bat: "xray path invalid - popup
                    // guard D8"). These are real error codes, so an all-digits
                    // rule alone silently drops the most useful marker we have.
                    if (token.size() >= 2 && token.size() <= 3 && startsUpper) {
                        bool tailDigits = true;
                        for (size_t k = 1; k < token.size(); ++k) {
                            if (token[k] < L'0' || token[k] > L'9') { tailDigits = false; break; }
                        }
                        if (tailDigits) return token;
                    }
                }
                token.clear();
                inToken = false;
            }
        }
    }
    return L"";
}

// ---------------------------------------------------------------------------
// Inspect / find
// ---------------------------------------------------------------------------

inline DialogReport inspectDialog(HWND hwnd, int maxDepth = 12) {
    DialogReport rep;
    if (hwnd == nullptr || !::IsWindow(hwnd)) return rep;

    const WindowInfo win = describeWindow(hwnd);
    rep.found = true;
    rep.hwnd  = hwnd;
    rep.kind  = win.kind;
    rep.title = win.title;

    collectUiaTexts(UiElement::fromHwnd(hwnd), rep.texts, maxDepth);
    collectChildWindowTexts(hwnd, rep.texts);

    // Merge in the title (popup titles carry the failure reason more often
    // than the body does) and build a single searchable blob.
    appendUniqueText(rep.texts, win.title);
    std::wostringstream blob;
    for (size_t i = 0; i < rep.texts.size(); ++i) {
        if (i) blob << L"\n";
        blob << rep.texts[i];
    }
    rep.body  = blob.str();
    rep.severity     = classifySeverity(win, rep.body);
    rep.errorCode    = extractErrorCode(rep.texts);
    return rep;
}

// Waits for any popup belonging to `pid`. `wanted` narrows the search when a
// test expects a specific shape (e.g. WindowKind::MessageBox). Returns a
// not-found report when the timeout expires - callers must check `.found`.
inline DialogReport findPopup(DWORD pid, WindowKind wanted = WindowKind::Unknown,
                              int timeoutMs = 0, int pollMs = 100) {
    DialogReport rep;
    const DWORD start = ::GetTickCount();
    for (;;) {
        const std::vector<HWND> hs = topLevelWindows(pid, /*visibleOnly=*/true);
        for (size_t i = 0; i < hs.size(); ++i) {
            const WindowInfo wi = describeWindow(hs[i]);
            const bool isPopup = (wi.kind == WindowKind::Dialog ||
                                  wi.kind == WindowKind::MessageBox ||
                                  wi.kind == WindowKind::FloatingWidget ||
                                  wi.kind == WindowKind::Menu ||
                                  wi.kind == WindowKind::Tooltip);
            if (!isPopup) continue;
            if (wanted != WindowKind::Unknown && wi.kind != wanted) continue;
            rep = inspectDialog(hs[i]);
            if (rep.found) return rep;
        }
        if (timeoutMs <= 0) break;
        if ((::GetTickCount() - start) >= static_cast<DWORD>(timeoutMs)) break;
        ::Sleep(static_cast<DWORD>(pollMs));
    }
    return rep;
}

// Convenience: wait specifically for something that *looks like* a failure.
// This is the guard a test installs before driving an action expected to fail,
// so that a regression surfaces as "expected dialog" instead of a hang.
inline DialogReport waitForErrorDialog(DWORD pid, int timeoutMs = 5000, int pollMs = 150) {
    DialogReport rep = findPopup(pid, WindowKind::Unknown, timeoutMs, pollMs);
    if (rep.found && rep.severity == DialogSeverity::Error) return rep;
    return rep;
}

inline bool looksLikeError(const DialogReport& rep) {
    return rep.found && rep.severity == DialogSeverity::Error;
}

inline std::wstring dialogSummary(const DialogReport& rep) {
    if (!rep.found) return L"<no popup>";
    std::wostringstream out;
    out << L"kind=" << windowKindToken(rep.kind)
        << L" severity=" << dialogSeverityToken(rep.severity)
        << L" title=\"" << rep.title << L"\"";
    if (!rep.errorCode.empty()) out << L" code=\"" << rep.errorCode << L"\"";
    out << L" texts=[";
    for (size_t i = 0; i < rep.texts.size(); ++i) {
        if (i) out << L" | ";
        out << rep.texts[i];
    }
    out << L"]";
    return out.str();
}

// ---------------------------------------------------------------------------
// Dismissal
// ---------------------------------------------------------------------------

// Clicks a button inside the popup. `buttonName == nullptr` falls back to the
// conventional OK/Yes/Close order. Returns false when no button could be
// activated - a caller that must not leak a modal window should treat that as
// a test failure, because the next action would block on the modal loop.
inline bool clickDialogButton(HWND dialogHwnd, const wchar_t* buttonName = nullptr) {
    if (dialogHwnd == nullptr || !::IsWindow(dialogHwnd)) return false;

    const UiElement root = UiElement::fromHwnd(dialogHwnd);
    const DWORD pid = describeWindow(dialogHwnd).pid;

    // UIA exposes only the accessible name, which on Windows carries the
    // accelerator form ("&OK") alongside the label, hence both spellings.
    static const wchar_t* kPreferred[] = {L"&OK", L"OK", L"确定", L"是(&Y)", L"是", L"Yes", L"关闭", L"Close"};
    if (buttonName != nullptr) {
        const UiElement b = UiElement::findBy(root, Locator{UIA_NamePropertyId, buttonName}, pid, 2000);
        if (b.valid() && b.click()) return true;
    } else {
        for (size_t i = 0; i < sizeof(kPreferred) / sizeof(kPreferred[0]); ++i) {
            const UiElement b = UiElement::findBy(root, Locator{UIA_NamePropertyId, kPreferred[i]}, pid, 300);
            if (b.valid() && b.click()) return true;
        }
    }

    // Win32 fallback: standard command IDs on a classic dialog.
    const int ids[] = {IDOK, IDYES, IDCANCEL, IDNO};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        if (::SendDlgItemMessageW(dialogHwnd, ids[i], BM_CLICK, 0, 0) != 0) return true;
        const HWND btn = ::GetDlgItem(dialogHwnd, ids[i]);
        if (btn != nullptr && ::SendMessageW(btn, BM_CLICK, 0, 0) != 0) return true;
    }

    // Last resort: WM_CLOSE, which every dialog honours even when it owns no
    // recognisable button. Tests that assert on dismissal must still verify the
    // window is gone afterwards - WM_CLOSE can be vetoed by an OnClose veto.
    ::PostMessageW(dialogHwnd, WM_CLOSE, 0, 0);
    return true;
}

inline bool closePopup(const DialogReport& rep) {
    return rep.valid() && clickDialogButton(rep.hwnd, nullptr);
}

// Closes every popup currently owned by `pid`. Returns how many were closed.
inline int dismissAllPopups(DWORD pid) {
    int closed = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const DialogReport rep = findPopup(pid, WindowKind::Unknown, 0, 0);
        if (!rep.found) break;
        if (!closePopup(rep)) break;
        ++closed;
        ::Sleep(150);
        if (!::IsWindow(rep.hwnd)) continue;
        ::PostMessageW(rep.hwnd, WM_CLOSE, 0, 0);
        ::Sleep(150);
    }
    return closed;
}

}  // namespace uitest