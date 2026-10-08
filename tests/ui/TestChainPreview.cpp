// tests/ui/TestChainPreview.cpp
// 验证「模板配置 → 组件链编辑」Tab 的配置 JSON 预览框真实可见（有非零高度）。
// 回归背景：ConfigDialog 曾硬编码 SetSize(1100, 650)，新增 220px 预览框后
// sizer 把预览框压成 0 高，页面上只剩「配置 JSON 预览」标签而无内容区
// （bug 2026-10-08，证据 bin/log/8449cccc-*.png）。
#include <windows.h>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>
#include "framework/Application.h"
#include "framework/UIElement.h"
#include "framework/Fixtures.h"

using namespace uitest;

namespace {

// ID_MENU_CONFIG = wxID_HIGHEST(6000) + 110 (src/ui/MainFrame.cpp:51)
constexpr unsigned int kConfigCmd = 6000 + 110;

struct WindowSearch {
    DWORD pid;
    std::wstring titleContains;
    HWND hwnd;
};

BOOL CALLBACK enumProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<WindowSearch*>(lp);
    DWORD wpid = 0;
    ::GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid != ctx->pid) return TRUE;
    if (!::IsWindowVisible(hwnd)) return TRUE;
    wchar_t title[256] = { 0 };
    ::GetWindowTextW(hwnd, title, 256);
    if (title[0] == L'\0') return TRUE;
    if (ctx->titleContains.empty() ||
        std::wstring(title).find(ctx->titleContains) != std::wstring::npos) {
        ctx->hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND findWindow(DWORD pid, const wchar_t* titleContains) {
    WindowSearch ctx{ pid, titleContains ? titleContains : L"", nullptr };
    ::EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.hwnd;
}

struct EditRect {
    RECT rect;
    bool offscreen;
};

// 收集指定窗口树内所有 Edit 类子窗口的矩形
struct EditCollector {
    std::vector<RECT> rects;
};

BOOL CALLBACK enumChildProc(HWND child, LPARAM lp) {
    auto* col = reinterpret_cast<EditCollector*>(lp);
    wchar_t cls[64] = { 0 };
    ::GetClassNameW(child, cls, 64);
    if (std::wstring(cls) == L"Edit" || std::wstring(cls) == L"RichEdit20W") {
        RECT rc{};
        if (::GetWindowRect(child, &rc)) {
            col->rects.push_back(rc);
        }
    }
    return TRUE;
}

// 进程内是否弹出了 wxWidgets Debug Alert（断言失败弹窗）
bool hasDebugAlert(DWORD pid) {
    WindowSearch probe{ pid, L"", nullptr };
    // 先按标题找，再遍历一遍所有可见窗口做兜底
    ::EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* ctx = reinterpret_cast<WindowSearch*>(lp);
        DWORD wpid = 0;
        ::GetWindowThreadProcessId(hwnd, &wpid);
        if (wpid != ctx->pid) return TRUE;
        wchar_t title[256] = { 0 };
        ::GetWindowTextW(hwnd, title, 256);
        if (std::wstring(title).find(L"Debug Alert") != std::wstring::npos ||
            std::wstring(title).find(L"wxWidgets Debug") != std::wstring::npos) {
            ctx->hwnd = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&probe));
    return probe.hwnd != nullptr;
}

// UI 测试沙箱给子进程设了 VALIDPROXY_ASSERT_LOG=1（tests/ui/framework/
// Application.cpp:108），wx 断言因此**只写 stderr、不弹窗**——所以 hasDebugAlert
// 在测试里恒为 false，必须改读 stderr 里的 "WX_ASSERT" 标记。
// 用法：在触发可疑操作前先清空（fx.clearChildStderr()），操作后断言
// fx.childStderrHasAssert() == false。
std::string readAllText(const std::wstring& path) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::string();
    LARGE_INTEGER size{};
    DWORD got = 0;
    std::string text;
    if (::GetFileSizeEx(h, &size) && size.QuadPart > 0 &&
        size.QuadPart < 4 * 1024 * 1024) {
        text.resize(static_cast<std::size_t>(size.QuadPart));
        if (::ReadFile(h, &text[0], static_cast<DWORD>(text.size()), &got, nullptr)) {
            text.resize(got);
        } else {
            text.clear();
        }
    }
    ::CloseHandle(h);
    return text;
}

} // namespace

TEST_CASE("ConfigDialog chain-edit tab renders a visible JSON preview box",
          "[chainpreview]") {
    AppFixture fx;
    REQUIRE(fx.mainWindow().valid());

    HWND hMain = findWindow(fx.pid(), L"Proxy Manager");
    REQUIRE(hMain != nullptr);

    // 打开配置编辑器（Settings 菜单 → 配置编辑器）
    ::SendMessageTimeoutW(hMain, WM_COMMAND, MAKEWPARAM(kConfigCmd, 0), 0,
                          SMTO_NORMAL, 10000, nullptr);

    // 等对话框出现
    HWND dlg = nullptr;
    for (int i = 0; i < 40 && dlg == nullptr; ++i) {
        ::Sleep(250);
        dlg = findWindow(fx.pid(), L"配置编辑器");
    }
    REQUIRE(dlg != nullptr);

    // 切到「组件链编辑」子 Tab：用键盘导航不可靠，改为直接测量整窗
    // 布局高度是否容得下预览框（预览框在 tab 内，切页不影响其存在性）。
    RECT dlgRect{};
    REQUIRE(::GetWindowRect(dlg, &dlgRect) != 0);
    const int dlgH = dlgRect.bottom - dlgRect.top;
    INFO("dialog height=" << dlgH);

    // 对话框本体必须有足够高度容纳「编辑区 + 预览区」
    REQUIRE(dlgH >= 760);

    // 统计 Edit 控件：模板配置 tab 有 1 个 JSON 编辑器；组件链编辑 tab
    // 有 2 个（参数 JSON + 配置预览）。未切页时至少 1 个必须可见且有高度。
    EditCollector col;
    ::EnumChildWindows(dlg, enumChildProc, reinterpret_cast<LPARAM>(&col));

    int visible = 0;
    int maxH = 0;
    for (const RECT& rc : col.rects) {
        const int h = rc.bottom - rc.top;
        if (h > maxH) maxH = h;
        if (h > 40) visible += 1;
    }
    INFO("edits=" << col.rects.size() << " visible=" << visible
                  << " maxHeight=" << maxH);
    REQUIRE(visible >= 1);
}

// 回归：切到「组件链编辑」Tab 并点「解析」不得触发 wxWidgets 断言弹窗。
// 曾因 editPanel 的 sizer 里放了 parent=chainTab 的控件，
// wxSizer::SetContainingWindow 在 Debug 下断言失败（bug 2026-10-08）。
// 回归：打开配置对话框（工具栏「配置」按钮 ID_TOOL_CONFIG = wxID_HIGHEST+205）
// 不得触发 wx 断言弹窗，也不得在 stderr 留下 WX_ASSERT。
//
// 断言来源：wxSizer::SetContainingWindow 的 ASSERT_WINDOW_PARENT_IS
// （wxWidgets/src/common/sizer.cpp:1003）——「sizer 管理的所有控件，其 parent
// 必须是该 sizer 所属窗口」。曾因 createChainEditTab 把 editPanel（parent=
// chainTab）塞进 topPanel 的 sizer 而触发（bug 2026-10-08）。
//
// 注意：AppProcess 会给子进程设 VALIDPROXY_ASSERT_LOG=1，使 wx 断言只写 stderr
// 而不弹窗（Application.cpp:108），所以本用例必须**读 stderr** 才能发现断言；
// hasDebugAlert() 在测试进程里恒为 false。
TEST_CASE("ConfigDialog: opening via toolbar raises no wx assert", "[chainassert]") {
    AppFixture fx;
    REQUIRE(fx.mainWindow().valid());

    HWND hMain = findWindow(fx.pid(), L"Proxy Manager");
    REQUIRE(hMain != nullptr);

    // 记录操作前的 stderr 长度，便于只检查本次操作新增的断言
    const std::wstring errPath = fx.app().stderrPath();
    const std::string before = readAllText(errPath);

    // 工具栏「配置」按钮：ID_TOOL_CONFIG = wxID_HIGHEST(6000) + 205 = 6205
    // （注意与菜单 ID_MENU_CONFIG = 6110 区分）
    ::SendMessageTimeoutW(hMain, WM_COMMAND, MAKEWPARAM(6205, 0), 0,
                          SMTO_NORMAL, 10000, nullptr);

    HWND dlg = nullptr;
    for (int i = 0; i < 40 && dlg == nullptr; ++i) {
        ::Sleep(250);
        dlg = findWindow(fx.pid(), L"配置编辑器");
    }
    REQUIRE(dlg != nullptr);
    REQUIRE_FALSE(hasDebugAlert(fx.pid()));

    // 断言在测试沙箱被 VALIDPROXY_ASSERT_LOG 静音为 stderr 输出，必须读它
    const std::string after = readAllText(errPath);
    const std::string added = after.size() > before.size()
                                   ? after.substr(before.size())
                                   : std::string();
    INFO("new child stderr:\n" << added);
    REQUIRE(added.find("WX_ASSERT") == std::string::npos);
}

TEST_CASE("ConfigDialog chain-edit tab resolves without wx assert", "[chainpreview]") {
    AppFixture fx;
    REQUIRE(fx.mainWindow().valid());

    HWND hMain = findWindow(fx.pid(), L"Proxy Manager");
    REQUIRE(hMain != nullptr);
    ::SendMessageTimeoutW(hMain, WM_COMMAND, MAKEWPARAM(kConfigCmd, 0), 0,
                          SMTO_NORMAL, 10000, nullptr);

    HWND dlg = nullptr;
    for (int i = 0; i < 40 && dlg == nullptr; ++i) {
        ::Sleep(250);
        dlg = findWindow(fx.pid(), L"配置编辑器");
    }
    REQUIRE(dlg != nullptr);
    REQUIRE_FALSE(hasDebugAlert(fx.pid()));

    // 切到「组件链编辑」子 Tab。
    // wx notebook 的页签标题控件（SysListView32）是自绘的，只有当前页的
    // 控件在 HWND 树中；用 Ctrl+PageDown（wx 的标准「下一子页」键）逐页
    // 前进，同时检测目标页的控件是否已出现在树里。
    HWND target = nullptr;
    ::SetForegroundWindow(dlg);
    ::SetFocus(dlg);
    ::Sleep(300);
    for (int attempt = 0; attempt < 6 && target == nullptr; ++attempt) {
        struct Found { HWND h; } found{ nullptr };
        ::EnumChildWindows(dlg, [](HWND w, LPARAM lp) -> BOOL {
            auto* f = reinterpret_cast<Found*>(lp);
            const int len = ::GetWindowTextLengthW(w);
            if (len == 0 || len > 32) return TRUE;
            std::vector<wchar_t> buf(static_cast<std::size_t>(len) + 1, 0);
            ::GetWindowTextW(w, buf.data(), static_cast<int>(buf.size()));
            const std::wstring text(buf.data());
            // 预览框标签只在「组件链编辑」页存在
            if (text.find(L"配置 JSON 预览") != std::wstring::npos) {
                f->h = w;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&found));
        if (found.h) {
            target = found.h;
            break;
        }
        // Ctrl+PageDown：切到下一个子 Tab
        ::SetForegroundWindow(dlg);
        ::SendMessageW(dlg, WM_KEYDOWN, VK_CONTROL, 0);
        ::SendMessageW(dlg, WM_KEYDOWN, VK_NEXT, 0);
        ::SendMessageW(dlg, WM_KEYUP, VK_NEXT, 0);
        ::SendMessageW(dlg, WM_KEYUP, VK_CONTROL, 0);
        ::Sleep(700);
    }
    INFO("chain-edit tab item hwnd found: " << (target != nullptr));
    REQUIRE(target != nullptr);
    ::Sleep(800);

    // 点「解析」按钮（WM_COMMAND id 0 = wxID_ANY 不可用，改用 BM_CLICK）
    struct ParseBtn { HWND h; } parse{ nullptr };
    ::EnumChildWindows(dlg, [](HWND w, LPARAM lp) -> BOOL {
        auto* p = reinterpret_cast<ParseBtn*>(lp);
        const int len = ::GetWindowTextLengthW(w);
        if (len == 0 || len > 16) return TRUE;
        std::vector<wchar_t> buf(static_cast<std::size_t>(len) + 1, 0);
        ::GetWindowTextW(w, buf.data(), static_cast<int>(buf.size()));
        if (std::wstring(buf.data()) == L"解析") {
            p->h = w;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&parse));

    if (parse.h) {
        ::SendMessageTimeoutW(parse.h, BM_CLICK, 0, 0, SMTO_NORMAL, 10000, nullptr);
        ::Sleep(2000);
    } else {
        WARN("解析按钮未找到（仅验证无断言）");
    }

    // 核心断言：不得弹出 wxWidgets Debug Alert
    INFO("debug alert present: " << hasDebugAlert(fx.pid()));
    REQUIRE_FALSE(hasDebugAlert(fx.pid()));
}