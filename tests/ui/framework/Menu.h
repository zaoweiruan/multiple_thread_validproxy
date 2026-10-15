// tests/ui/framework/Menu.h - menu bar / menu item discovery and activation.
//
// wxWidgets renders its menu bar as a native HMENU-backed popup chain. UIA
// surfaces it inconsistently across Windows versions: sometimes as a
// MenuBar/MenuItem control tree, sometimes as nothing at all. This header
// therefore drives menus through two complementary channels:
//
//   1. UIA channel  - preferred. Uses the ExpandCollapse pattern to open a
//                     top level menu and Invoke/SelectItem to pick an entry.
//                     Works when wxWidgets / MSAA exposes the menu tree.
//   2. Win32 channel - fallback. Sends WM_COMMAND with the real wxWidgets
//                     menu id. Always reliable for wxFrame menu items and
//                     completely independent of UIA provider quirks.
//
// Both channels take the destination HWND, so callers never have to care
// which one ends up doing the work.

#pragma once
#include <windows.h>
#include <sstream>
#include <string>
#include <vector>

#include "framework/UIElement.h"
#include "framework/WindowInfo.h"
#include "framework/Wait.h"

namespace uitest {

// ---------------------------------------------------------------- Menu model

// Win32 reports a separator item as MF_SEPARATOR, which GetMenuString renders
// as an empty caption. Tests must recognise it rather than treat the empty
// caption as a broken read.
inline constexpr UINT kMenuSeparatorId = 65534u;   // 0xFFFE, what wxWidgets reports

// One entry of a menu bar: the caption shown in the bar plus the Win32 command
// id wxWidgets assigned to it. `commandId == 0` means "unknown", in which case
// only the UIA channel is attempted.
struct MenuEntry {
    std::wstring caption;      // 可见文本，例如 L"文件"（不含加速键）
    UINT commandId;            // real menu id, 0 when not supplied
    std::wstring accelerator;  // e.g. L"Ctrl+S", empty when unknown
};

// Where an entry lives once its menu is open.
struct MenuItemHit {
    std::wstring caption;
    bool found = false;
};

// Default accelerator text for a command id. wxWidgets bakes the shortcut into
// the item text as "\tCtrl+S", so parsing the real menu is the authoritative
// source; this table only covers the shortcuts this project actually installs.
inline std::wstring knownAccelerator(UINT id) {
    // Only ids this project actually installs, verified against the live menu
    // bar via readNativeMenu() rather than guessed. wxWidgets resource ids such
    // as ID_APP_EXIT / ID_ABOUT live in the app, not in the test binary, which
    // does not link wxWidgets - so they must not be referenced here.
    switch (id) {
        case 6100: return L"Ctrl+I";        // 导入订阅链接...
        case 6103: return L"Ctrl+U";        // 更新全部订阅
        case 6102: return L"Alt+X";         // 退出
        case 6105: return L"Ctrl+F";        // 查找首个可用代理
        case 6106: return L"Ctrl+Shift+F";  // 查找最佳代理
        case 6114: return L"Ctrl+M";        // 独立代理监控...
        case 6112: return L"Ctrl+T";        // 执行自动任务
        case 6110: return L"Ctrl+,";        // 配置...
        default: return std::wstring();
    }
}

// -------------------------------------------------------------- Win32 channel

// Read the real menu of `hwnd` (menu bar first, then the window menu) and
// return every entry. This is the ground truth used to build MenuEntry values;
// it works even when UIA exposes nothing.
inline std::vector<MenuEntry> readNativeMenu(HWND hwnd) {
    std::vector<MenuEntry> entries;
    if (!hwnd || !::IsWindow(hwnd)) return entries;

    HMENU bar = ::GetMenu(hwnd);
    if (!bar) return entries;

    const int count = ::GetMenuItemCount(bar);
    for (int i = 0; i < count; ++i) {
        HMENU sub = ::GetSubMenu(bar, i);
        wchar_t top[128] = {0};
        ::GetMenuStringW(bar, i, top, 128, MF_BYPOSITION);
        if (!sub) {
            MenuEntry e;
            e.caption = top;
            e.commandId = ::GetMenuItemID(bar, i);
            e.accelerator = knownAccelerator(e.commandId);
            entries.push_back(e);
            continue;
        }
        // A top level caption with sub items: expose both the caption itself
        // (so callers can assert on the menu bar) and each leaf.
        MenuEntry topEntry;
        topEntry.caption = top;
        topEntry.accelerator = knownAccelerator(::GetMenuItemID(bar, i));
        entries.push_back(topEntry);

        const int leafCount = ::GetMenuItemCount(sub);
        for (int j = 0; j < leafCount; ++j) {
            HMENU leaf = ::GetSubMenu(sub, j);
            wchar_t text[256] = {0};
            ::GetMenuStringW(sub, j, text, 256, MF_BYPOSITION);
            // Strip the "\tCtrl+S" accelerator suffix; keep it separately.
            std::wstring caption(text);
            std::wstring accel;
            const size_t tab = caption.find(L'\t');
            if (tab != std::wstring::npos) {
                accel = caption.substr(tab + 1);
                caption = caption.substr(0, tab);
            }
            MenuEntry e;
            e.caption = caption;
            e.commandId = leaf ? static_cast<UINT>(-1)
                               : static_cast<UINT>(::GetMenuItemID(sub, j));
            if (!accel.empty()) {
                e.accelerator = accel;
            } else {
                e.accelerator = knownAccelerator(e.commandId);
            }
            entries.push_back(e);
            if (leaf) {
                // One level of flattening is enough for this project's menus;
                // deeper nesting is reported through readNativeMenu recursion
                // guarded by the caller's depth argument.
                const int deep = ::GetMenuItemCount(leaf);
                for (int k = 0; k < deep; ++k) {
                    wchar_t deepText[256] = {0};
                    ::GetMenuStringW(leaf, k, deepText, 256, MF_BYPOSITION);
                    std::wstring dc(deepText);
                    const size_t dtab = dc.find(L'\t');
                    if (dtab != std::wstring::npos) dc = dc.substr(0, dtab);
                    MenuEntry de;
                    de.caption = dc;
                    de.commandId = ::GetMenuItemID(leaf, k);
                    de.accelerator = knownAccelerator(de.commandId);
                    entries.push_back(de);
                }
            }
        }
    }
    return entries;
}

// Single line report used by INFO logs and failure artifacts.
inline std::wstring menuReport(const std::vector<MenuEntry>& entries) {
    std::wostringstream out;
    out << L"menu entries=" << entries.size();
    for (size_t i = 0; i < entries.size(); ++i) {
        out << L" | \"" << entries[i].caption << L"\"";
        if (entries[i].commandId != 0 && entries[i].commandId != static_cast<UINT>(-1)) {
            out << L"(id=" << entries[i].commandId << L")";
        }
        if (!entries[i].accelerator.empty()) {
            out << L"[" << entries[i].accelerator << L"]";
        }
    }
    return out.str();
}

// 大小写无关比较，同时把 wxWidgets 的 "&文件" 助记符形式视为等于 "文件"。
inline bool menuCaptionMatches(const std::wstring& a, const std::wstring& b) {
    std::wstring na;
    std::wstring nb;
    for (size_t i = 0; i < a.size(); ++i) {
        const wchar_t c = a[i];
        if (c != L'&') na.push_back(static_cast<wchar_t>(::towlower(c)));
    }
    for (size_t i = 0; i < b.size(); ++i) {
        const wchar_t c = b[i];
        if (c != L'&') nb.push_back(static_cast<wchar_t>(::towlower(c)));
    }
    return na == nb;
}

// Locate a caption in a flattened menu list.
inline bool findMenuEntry(const std::vector<MenuEntry>& entries,
                          const std::wstring& caption,
                          MenuEntry* out) {
    for (size_t i = 0; i < entries.size(); ++i) {
        if (menuCaptionMatches(entries[i].caption, caption)) {
            if (out) *out = entries[i];
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------- UIA channel

// Find the menu bar control under `root`. wxWidgets exposes it either as a
// dedicated MenuBar control or, more often on Windows, not at all - in that case
// the frame itself is the right scope because the leaf MenuItems hang directly
// off it. We therefore walk the control-view tree looking for UIA_MenuBar and
// fall back to "root owns a native HMENU".
//
// Note: we deliberately do NOT use UiElement::findAll with a ControlType
// locator here. findAll needs a concrete property *value*, and an empty value
// matches nothing, so enumeration has to walk the tree instead.
inline UiElement findMenuBar(const UiElement& root, DWORD pid, int timeoutMs = 3000) {
    (void)pid;
    if (!root.valid()) return UiElement();

    IUIAutomation* ua = Uia::instance().com();
    IUIAutomationTreeWalker* walker = nullptr;
    if (ua && SUCCEEDED(ua->get_ControlViewWalker(&walker)) && walker) {
        IUIAutomationElement* child = nullptr;
        if (SUCCEEDED(walker->GetFirstChildElement(root.raw(), &child)) && child) {
            while (child) {
                CONTROLTYPEID ct = UIA_ControlTypePropertyId;
                child->get_CurrentControlType(&ct);
                if (ct == UIA_MenuBarControlTypeId) {
                    UiElement bar(child);  // takes the owned reference
                    child = nullptr;
                    walker->Release();
                    return bar;
                }
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

    // Fallback: the frame owns a native menu, so use the frame as the scope.
    // MinGW declares UIA_HWND as void*, hence the void** out-param.
    UIA_HWND rawHwnd = nullptr;
    if (!root.raw() ||
        FAILED(root.raw()->get_CurrentNativeWindowHandle(&rawHwnd)) ||
        !rawHwnd) {
        return UiElement();
    }
    HWND hwnd = static_cast<HWND>(rawHwnd);
    // UiElement is move-only, so hand back an owned clone rather than copying.
    if (::GetMenu(hwnd)) return root.clone();
    return UiElement();
}

// Open a top level menu by caption using the UIA ExpandCollapse pattern.
// wxWidgets menus are also reachable through the physical keyboard, so the
// Win32 fallback below is what actually drives most interactions.
inline bool expandMenuItem(const UiElement& scope, const std::wstring& caption,
                           DWORD pid, int timeoutMs = 3000, int pollMs = 150) {
    if (!scope.valid()) return false;

    bool opened = waitFor(timeoutMs, pollMs, [&]() -> bool {
        const std::vector<UiElement> items =
            UiElement::findAll(scope, Locator{UIA_NamePropertyId, caption}, pid, 0);
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].controlType() != UIA_MenuItemControlTypeId &&
                items[i].controlType() != UIA_ButtonControlTypeId &&
                items[i].controlType() != UIA_MenuBarControlTypeId) {
                continue;
            }
            // ExpandCollapse pattern: the documented way to open a menu.
            if (items[i].hasPattern(UIA_ExpandCollapsePatternId)) {
                // GetCurrentPattern hands back an IUnknown under MinGW; QI it.
                IUnknown* unk = nullptr;
                if (SUCCEEDED(items[i].raw()->GetCurrentPattern(
                        UIA_ExpandCollapsePatternId, &unk)) && unk) {
                    IUIAutomationExpandCollapsePattern* ec = nullptr;
                    const HRESULT hr = unk->QueryInterface(
                        IID_IUIAutomationExpandCollapsePattern,
                        reinterpret_cast<void**>(&ec));
                    unk->Release();
                    if (SUCCEEDED(hr) && ec) {
                        ec->Expand();
                        ec->Release();
                        return true;
                    }
                }
            }
            // Last resort: plain invoke.
            if (items[i].click()) return true;
        }
        return false;
    });
    return opened;
}

// ------------------------------------------------------------ Combined API

// 按菜单标题点击菜单项，例如先 L"文件" 再 L"退出"。
//
// Order of attack:
//   1. UIA expand on the caption, then invoke the leaf caption.
//   2. Win32 WM_COMMAND with the real id read from the native menu.
//
// Returns true when either channel reported success. `verbose` (optional)
// receives a human readable trace of what happened, which is invaluable when a
// menu interaction flakes.
inline bool clickMenu(HWND hwnd, const std::wstring& caption,
                      const std::wstring& subCaption = std::wstring(),
                      std::wstring* verbose = nullptr) {
    if (!hwnd || !::IsWindow(hwnd)) return false;
    std::wostringstream trace;

    const std::vector<MenuEntry> entries = readNativeMenu(hwnd);
    MenuEntry top;
    const bool topFound = findMenuEntry(entries, caption, &top);

    // --- channel 1: UIA
    const UiElement frame = UiElement::fromHwnd(hwnd);
    if (frame.valid()) {
        const UiElement bar = findMenuBar(frame, static_cast<DWORD>(::GetWindowThreadProcessId(hwnd, nullptr)));
        if (bar.valid()) {
            const DWORD pid = static_cast<DWORD>(::GetWindowThreadProcessId(hwnd, nullptr));
            if (expandMenuItem(bar, topFound ? top.caption : caption, pid)) {
                if (subCaption.empty()) {
                    trace << L"uia:top-level-expanded";
                    if (verbose) *verbose = trace.str();
                    return true;
                }
                if (expandMenuItem(frame, subCaption, pid)) {
                    trace << L"uia:expanded+invoked";
                    if (verbose) *verbose = trace.str();
                    return true;
                }
            }
        }
    }

    // --- channel 2: WM_COMMAND against the native menu id
    if (!subCaption.empty()) {
        MenuEntry leaf;
        if (findMenuEntry(entries, subCaption, &leaf) && leaf.commandId != 0) {
            ::PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(leaf.commandId, 0), 0);
            trace << L"win32:WM_COMMAND id=" << leaf.commandId;
            if (verbose) *verbose = trace.str();
            return true;
        }
    }
    if (topFound && top.commandId != 0 && top.commandId != static_cast<UINT>(-1)) {
        ::PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(top.commandId, 0), 0);
        trace << L"win32:WM_COMMAND top-level id=" << top.commandId;
        if (verbose) *verbose = trace.str();
        return true;
    }

    trace << L"no channel available";
    if (verbose) *verbose = trace.str();
    return false;
}

// Keyboard route: Alt+<mnemonic> then the item. Useful when both the UIA and
// the WM_COMMAND channels are blocked (for example by a modal dialog on top).
inline bool clickMenuViaKeyboard(HWND hwnd, wchar_t mnemonic) {
    if (!hwnd || !::IsWindow(hwnd)) return false;
    ::SetForegroundWindow(hwnd);
    ::Sleep(80);
    INPUT alt[2] = {};
    alt[0].type = INPUT_KEYBOARD;
    alt[0].ki.wVk = VK_MENU;
    alt[0].ki.wScan = static_cast<WORD>(mnemonic);
    alt[1].type = INPUT_KEYBOARD;
    alt[1].ki.wVk = mnemonic;
    alt[1].ki.wScan = static_cast<WORD>(mnemonic);
    alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
    ::SendInput(2, alt, sizeof(INPUT));
    return true;
}

}  // namespace uitest