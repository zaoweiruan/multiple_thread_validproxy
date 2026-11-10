// tests/ui/framework/ListView.h - cross-process reads against a native
// SysListView32 (the TaskMonitorPanel task list). Win32 messages only, no
// UIA tree walks, per the TestClear precedent (desktop-wide UIA FindFirst
// stalls on slow WM_GETOBJECT targets).
//
// LVM_GETITEMTEXTW needs a buffer inside the TARGET process (SendMessage
// marshals pointers, not memory), so row text uses VirtualAllocEx +
// ReadProcessMemory — the standard cross-process list-view idiom.
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>

namespace uitest {

// First descendant of `parent` whose window class is `cls` (depth-first,
// message-only windows skipped by the child enumeration itself).
inline HWND findChildByClass(HWND parent, const wchar_t* cls) {
    struct Ctx { const wchar_t* cls; HWND found; };
    Ctx ctx; ctx.cls = cls; ctx.found = nullptr;
    ::EnumChildWindows(parent, [](HWND h, LPARAM lp) -> BOOL {
        Ctx* c = reinterpret_cast<Ctx*>(lp);
        wchar_t buf[128] = { 0 };
        if (::GetClassNameW(h, buf, 128) == 0) return TRUE;
        if (wcscmp(buf, c->cls) == 0) { c->found = h; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

inline int listRowCount(HWND list) {
    if (!list || !::IsWindow(list)) return -1;
    return static_cast<int>(::SendMessageW(list, LVM_GETITEMCOUNT, 0, 0));
}

// Text of one cell; "" on any failure (row scrolled away, process gone...).
inline std::wstring listItemText(HWND list, int row, int subItem) {
    std::wstring out;
    if (!list || !::IsWindow(list)) return out;
    DWORD pid = 0;
    ::GetWindowThreadProcessId(list, &pid);
    if (!pid) return out;
    HANDLE proc = ::OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ |
                                PROCESS_VM_WRITE, FALSE, pid);
    if (!proc) return out;
    const int kChars = 256;
    const size_t textBytes = static_cast<size_t>(kChars) * sizeof(wchar_t);
    // Layout of the remote block: [LVITEMW][text buffer].
    void* remote = ::VirtualAllocEx(proc, nullptr, sizeof(LVITEMW) + textBytes,
                                    MEM_COMMIT, PAGE_READWRITE);
    if (remote) {
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = row;
        it.iSubItem = subItem;
        it.pszText = reinterpret_cast<LPWSTR>(
            static_cast<char*>(remote) + sizeof(LVITEMW));
        it.cchTextMax = kChars;
        SIZE_T written = 0;
        if (::WriteProcessMemory(proc, remote, &it, sizeof(it), &written) &&
            written == sizeof(it)) {
            const LRESULT ok = ::SendMessageW(list, LVM_GETITEMTEXTW, row,
                                              reinterpret_cast<LPARAM>(remote));
            if (ok >= 0) {
                wchar_t local[kChars] = { 0 };
                SIZE_T read = 0;
                if (::ReadProcessMemory(proc, it.pszText, local, textBytes, &read)) {
                    local[kChars - 1] = 0;
                    out.assign(local);
                }
            }
        }
        ::VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    }
    ::CloseHandle(proc);
    return out;
}

// Screen-space centre of a row (for WM_CONTEXTMENU). False when the row is
// not measured (invalid row / empty list).
inline bool listRowCenterScreen(HWND list, int row, POINT* out) {
    if (!list || !out) return false;
    // The RECT parameter is a POINTER: cross-process SendMessage does not
    // marshal it (only a few text messages get that treatment), so the RECT
    // must live inside the TARGET process — same rule as LVM_GETITEMTEXT.
    // Input: RECT.top selects the region (LVIR_BOUNDS = whole row); output:
    // row bounds in list-view client coordinates.
    DWORD pid = 0;
    ::GetWindowThreadProcessId(list, &pid);
    if (!pid) return false;
    HANDLE proc = ::OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ |
                                PROCESS_VM_WRITE, FALSE, pid);
    if (!proc) return false;
    bool ok = false;
    RECT local{};
    void* remote = ::VirtualAllocEx(proc, nullptr, sizeof(RECT), MEM_COMMIT,
                                    PAGE_READWRITE);
    if (remote) {
        local.top = LVIR_BOUNDS;
        SIZE_T written = 0;
        if (::WriteProcessMemory(proc, remote, &local, sizeof(local), &written) &&
            written == sizeof(local)) {
            const LRESULT res = ::SendMessageW(list, LVM_GETITEMRECT,
                                               static_cast<WPARAM>(row),
                                               reinterpret_cast<LPARAM>(remote));
            SIZE_T read = 0;
            if (res > 0 &&
                ::ReadProcessMemory(proc, remote, &local, sizeof(local), &read) &&
                read == sizeof(local) && local.bottom > local.top) {
                out->x = (local.left + local.right) / 2;
                out->y = (local.top + local.bottom) / 2;
                ::ClientToScreen(list, out);
                ok = true;
            }
        }
        ::VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    }
    ::CloseHandle(proc);
    return ok;
}

} // namespace uitest
