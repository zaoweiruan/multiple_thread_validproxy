// tests/ui/framework/WindowInfo.h - Top-level window discovery, descriptive
// metadata reads, and window-kind classification.
//
// Purpose: answer "what kind of window is this?" from Win32 style bits plus the
// wxWidgets/MSW class-name taxonomy, so tests can branch on window semantics
// instead of hard-coding class strings at every call site.
//
// MinGW notes: header-only; every accessor is `inline`. BSTR results are freed
// with SysFreeString. Process ids are read through an `int` lvalue because the
// MinGW UIA headers declare CurrentProcessId as `int*`, not `DWORD*`.

#pragma once
#include <windows.h>
#include <sstream>
#include <string>
#include <vector>

namespace uitest {

enum class WindowKind {
    Unknown = 0,
    Frame,          // top-level application window (WS_OVERLAPPEDWINDOW)
    Dialog,         // modal/top-level popup with a caption and a message child
    MessageBox,     // #32770 / wxWidgets modal message box
    FloatingWidget, // WS_EX_TOOLWINDOW tool window (the standalone proxy widget)
    Menu,           // menu window class (#32768)
    Tooltip,        // tooltip window class
    Splitter,       // wxSplitterWindow sash region owner
    Control         // WS_CHILD owned control
};

// Stable lowercase snake-case token for a kind, safe to embed in artifact text.
inline const char* windowKindToken(WindowKind kind) {
    switch (kind) {
    case WindowKind::Frame:          return "frame";
    case WindowKind::Dialog:         return "dialog";
    case WindowKind::MessageBox:     return "message_box";
    case WindowKind::FloatingWidget: return "floating_widget";
    case WindowKind::Menu:           return "menu";
    case WindowKind::Tooltip:        return "tooltip";
    case WindowKind::Splitter:       return "splitter";
    case WindowKind::Control:        return "control";
    default:                         return "unknown";
    }
}

// Everything a test usually needs to know about one window.
struct WindowInfo {
    HWND hwnd;
    DWORD pid;
    WindowKind kind;
    std::wstring title;      // GetWindowTextW
    std::wstring className;  // GetClassNameW
    RECT rect;               // screen pixels
    bool visible;
    bool enabled;
    bool hasCaption;
    bool isPopup;
    bool isChild;
    bool isToolWindow;
    LONG exStyle;

    WindowInfo()
        : hwnd(nullptr), pid(0), kind(WindowKind::Unknown), rect{0, 0, 0, 0},
          visible(false), enabled(false), hasCaption(false), isPopup(false),
          isChild(false), isToolWindow(false), exStyle(0) {}

    int width() const { return rect.right - rect.left; }
    int height() const { return rect.bottom - rect.top; }
    bool valid() const { return hwnd != nullptr && ::IsWindow(hwnd) != FALSE; }
};// Classify a live HWND purely from style bits and class name.
// Classification order matters: a tool window also carries WS_POPUP, so the
// tool-window check must precede the generic popup check.
inline WindowKind classifyWindow(HWND hwnd, const std::wstring& className) {
    if (!hwnd || !::IsWindow(hwnd)) return WindowKind::Unknown;

    const LONG style = ::GetWindowLongW(hwnd, GWL_STYLE);
    const LONG exStyle = ::GetWindowLongW(hwnd, GWL_EXSTYLE);

    if (style & WS_CHILD) return WindowKind::Control;

    if (className == L"#32768") return WindowKind::Menu;
    if (className == L"tooltips_class32") return WindowKind::Tooltip;

    const bool hasCaption = (style & WS_CAPTION) != 0;
    if (className == L"#32770") return WindowKind::MessageBox;

    // wxWidgets MSW floating helper windows are WS_EX_TOOLWINDOW popups.
    if (exStyle & WS_EX_TOOLWINDOW) return WindowKind::FloatingWidget;

    if (style & WS_POPUP) {
        return hasCaption ? WindowKind::Dialog : WindowKind::Unknown;
    }
    if (hasCaption || (style & WS_OVERLAPPED)) return WindowKind::Frame;
    return WindowKind::Unknown;
}

// Fill a WindowInfo from a live HWND. Never throws; fields degrade to defaults.
inline WindowInfo describeWindow(HWND hwnd) {
    WindowInfo info;
    info.hwnd = hwnd;
    if (!hwnd || !::IsWindow(hwnd)) return info;

    // GetWindowThreadProcessId RETURNS the owning thread id and writes the
    // process id through the out-param. Passing nullptr and using the return
    // value silently yields a thread id, which lives in the same numeric range
    // as a pid - so the bug hides until a test compares it against the pid it
    // spawned, and then the mismatch looks like "the wrong process owns this
    // window" instead of "we read the wrong field".
    DWORD ownerPid = 0;
    ::GetWindowThreadProcessId(hwnd, &ownerPid);
    info.pid = ownerPid;

    const int titleLen = ::GetWindowTextLengthW(hwnd);
    if (titleLen > 0) {
        std::vector<wchar_t> buffer(static_cast<size_t>(titleLen) + 1, L'\0');
        const int copied = ::GetWindowTextW(hwnd, buffer.data(), titleLen + 1);
        if (copied > 0) info.title.assign(buffer.data(), static_cast<size_t>(copied));
    }

    wchar_t classBuffer[256];
    const int classLen = ::GetClassNameW(hwnd, classBuffer, 256);
    if (classLen > 0) info.className.assign(classBuffer, static_cast<size_t>(classLen));

    const LONG style = ::GetWindowLongW(hwnd, GWL_STYLE);
    info.exStyle = ::GetWindowLongW(hwnd, GWL_EXSTYLE);
    info.hasCaption = (style & WS_CAPTION) != 0;
    info.isPopup = (style & WS_POPUP) != 0;
    info.isChild = (style & WS_CHILD) != 0;
    info.isToolWindow = (info.exStyle & WS_EX_TOOLWINDOW) != 0;
    info.visible = ::IsWindowVisible(hwnd) != FALSE;
    info.enabled = ::IsWindowEnabled(hwnd) != FALSE;
    info.kind = classifyWindow(hwnd, info.className);

    if (::GetWindowRect(hwnd, &info.rect) == FALSE) {
        info.rect.left = info.rect.top = info.rect.right = info.rect.bottom = 0;
    }
    return info;
}

struct EnumWindowsCtx {
    DWORD pidFilter;      // 0 disables the filter
    bool visibleOnly;
    std::vector<HWND> handles;
};

inline BOOL CALLBACK collectTopLevelProc(HWND hwnd, LPARAM param) {
    EnumWindowsCtx* ctx = reinterpret_cast<EnumWindowsCtx*>(param);
    if (!ctx) return FALSE;
    if (ctx->visibleOnly && ::IsWindowVisible(hwnd) == FALSE) return TRUE;

    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (ctx->pidFilter != 0 && pid != ctx->pidFilter) return TRUE;

    ctx->handles.push_back(hwnd);
    return TRUE;
}

// Enumerate top-level windows, optionally filtered to one process and to
// visible windows only. Caller owns nothing: raw HWNDs are copied out.
inline std::vector<HWND> topLevelWindows(DWORD pidFilter, bool visibleOnly = true) {
    EnumWindowsCtx ctx;
    ctx.pidFilter = pidFilter;
    ctx.visibleOnly = visibleOnly;
    ctx.handles.clear();
    ::EnumWindows(&collectTopLevelProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.handles;
}

// Describe every top-level window of a process, in Z-order (bottom first).
inline std::vector<WindowInfo> describeTopLevelWindows(DWORD pidFilter,
                                                       bool visibleOnly = true) {
    std::vector<WindowInfo> out;
    const std::vector<HWND> handles = topLevelWindows(pidFilter, visibleOnly);
    out.reserve(handles.size());
    for (size_t i = 0; i < handles.size(); ++i) out.push_back(describeWindow(handles[i]));
    return out;
}

// First window of a process whose kind matches `wanted`. Returns an invalid
// WindowInfo when nothing matches, so callers can branch on `.valid()`.
inline WindowInfo findWindowOfKind(DWORD pidFilter, WindowKind wanted,
                                   int timeoutMs = 0, int pollMs = 100) {
    const DWORD deadlineStart = ::GetTickCount();
    for (;;) {
        const std::vector<WindowInfo> all = describeTopLevelWindows(pidFilter, true);
        for (size_t i = 0; i < all.size(); ++i) {
            if (all[i].kind == wanted) return all[i];
        }
        if (timeoutMs <= 0) break;
        if (static_cast<int>(::GetTickCount() - deadlineStart) >= timeoutMs) break;
        ::Sleep(static_cast<DWORD>(pollMs));
    }
    return WindowInfo();
}

// Human-readable one-line summary, used in INFO logs and artifact reports.
inline std::wstring windowInfoLine(const WindowInfo& info) {
    std::wostringstream out;
    out << L"kind=" << windowKindToken(info.kind)
        << L" class=\"" << info.className << L"\""
        << L" title=\"" << info.title << L"\""
        << L" pid=" << info.pid
        << L" rect=(" << info.rect.left << L"," << info.rect.top << L","
        << info.rect.right << L"," << info.rect.bottom << L")"
        << L" visible=" << (info.visible ? 1 : 0)
        << L" enabled=" << (info.enabled ? 1 : 0)
        << L" toolwindow=" << (info.isToolWindow ? 1 : 0);
    return out.str();
}

}  // namespace uitest