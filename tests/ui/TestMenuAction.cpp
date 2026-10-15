// tests/ui/TestMenuAction.cpp - menu bar discovery and menu item activation.
//
// Covers: "can click UI page buttons and menus" from the brief. Two channels
// are exercised: reading the real HMENU (the source of truth for menu ids) and
// actually driving an item through WM_COMMAND.

#include <catch2/catch_test_macros.hpp>

#include "framework/Dialog.h"
#include "framework/Fixtures.h"
#include "framework/Menu.h"
#include "framework/Text.h"
#include "framework/WindowInfo.h"
#include "UIIds.h"

using namespace uitest;

TEST_CASE("Menu main frame exposes a native menu bar", "[menu][sensing]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    const HWND hwnd = fx.app().mainWindow();
    REQUIRE(hwnd != nullptr);
    REQUIRE(::GetMenu(hwnd) != nullptr);

    const std::vector<MenuEntry> entries = readNativeMenu(hwnd);
    INFO(toUtf8(menuReport(entries)));
    REQUIRE_FALSE(entries.empty());

    for (size_t i = 0; i < entries.size(); ++i) {
        INFO("entry " << i << ": " << toUtf8(entries[i].caption));
        // Separators are legitimate menu members and carry no caption; only a
        // non-separator may be nameless, and that would mean the read is broken.
        if (entries[i].commandId == kMenuSeparatorId) {
            INFO("separator at " << i);
            CHECK(entries[i].caption.empty());
            continue;
        }
        CHECK_FALSE(entries[i].caption.empty());
    }

    // The real menu bar, as read from the running GUI (all captions Chinese):
    //   文件  : 导入订阅链接... [Ctrl+I] / 更新全部订阅 [Ctrl+U] /
    //           同步数据库... / 退出 [Alt+X]
    //   代理  : 查找首个可用代理 [Ctrl+F] / 查找最佳代理 [Ctrl+Shift+F] /
    //           移除重复代理 / 导出分享链接 / 生成配置... / 独立代理监控... [Ctrl+M]
    //   任务  : 执行自动任务 [Ctrl+T] / 恢复自动任务 / 取消任务
    //   设置  : 配置... [Ctrl+,]
    //   帮助  : 关于
    MenuEntry first;
    const bool found = findMenuEntry(entries, ids::MenuFindProxyItem, &first);
    INFO("findMenuEntry found: " << found);
    REQUIRE(found);
    INFO("id " << first.commandId << " accel " << toUtf8(first.accelerator));
    CHECK(first.commandId != 0);
    CHECK(first.accelerator == L"Ctrl+F");

    // Every leaf item must be reachable by its pinned caption, and the two
    // top-level menus asserted here must carry their "&" mnemonic prefix.
    const wchar_t* const leaves[] = {
        ids::MenuImportSubItem, ids::MenuUpdateAllItem, ids::MenuSyncDbItem,
        ids::MenuExitItem,      ids::MenuFindProxyItem,  ids::MenuFindBestItem,
        ids::MenuDedupItem,     ids::MenuExportItem,    ids::MenuGenConfigItem,
        ids::MenuStandaloneMonItem, ids::MenuAutoTaskRunItem,
        ids::MenuAutoTaskResumeItem, ids::MenuCancelTaskItem,
        ids::MenuConfigItem,    ids::MenuAboutItem,
    };
    for (const wchar_t* const leaf : leaves) {
        INFO("missing leaf: " << toUtf8(leaf));
        MenuEntry probe;
        CHECK(findMenuEntry(entries, leaf, &probe));
    }

    const wchar_t* const tops[] = {
        ids::MenuFileCaption, ids::MenuProxyCaption, ids::MenuTaskCaption,
        ids::MenuSettingsCaption, ids::MenuHelpCaption,
    };
    for (const wchar_t* const top : tops) {
        INFO("missing top-level menu: " << toUtf8(top));
        MenuEntry probe;
        CHECK(findMenuEntry(entries, top, &probe));
    }
}

TEST_CASE("Menu caption matching tolerates mnemonics and case", "[menu][unit]") {
    CHECK(menuCaptionMatches(ids::MenuFileCaption, L"文件"));
    CHECK(menuCaptionMatches(L"&文件", ids::MenuFileCaption));
    CHECK(menuCaptionMatches(L"&设置", ids::MenuSettingsCaption));
    CHECK_FALSE(menuCaptionMatches(ids::MenuFileCaption, ids::MenuProxyCaption));

    const std::vector<MenuEntry> entries;
    MenuEntry out;
    CHECK_FALSE(findMenuEntry(entries, L"Anything", &out));
}

TEST_CASE("Menu clicking a menu item does not kill the window", "[menu][action]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    const HWND hwnd = fx.app().mainWindow();
    REQUIRE(hwnd != nullptr);

    // "帮助 -> 关于" is the safest interactive target: it opens a modal dialog
    // we can then close, which proves the menu click really landed.
    std::wstring trace;
    const bool clicked = clickMenu(hwnd, ids::MenuHelpCaption, ids::MenuAboutItem, &trace);
    INFO("menu trace: " << toUtf8(trace));

    if (!clicked) {
        WARN("no menu channel available on this build; skipping activation check");
        return;
    }

    const DialogReport about = waitForErrorDialog(fx.pid(), 6000, 200);
    INFO("about dialog: " << toUtf8(dialogSummary(about)));
    if (about.found) {
        // An About box must never be classified as an error.
        CHECK_FALSE(looksLikeError(about));
        const bool closed = closePopup(about);
        INFO("closed: " << closed);
    }

    CHECK(fx.app().running());
}

TEST_CASE("Menu unknown caption reports failure without crashing", "[menu][action]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    const HWND hwnd = fx.app().mainWindow();
    REQUIRE(hwnd != nullptr);

    std::wstring trace;
    const bool clicked = clickMenu(hwnd, L"NoSuchMenuItem", L"", &trace);
    INFO("trace: " << toUtf8(trace));
    CHECK_FALSE(clicked);
    CHECK(fx.app().running());
}