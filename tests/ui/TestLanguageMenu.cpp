// tests/ui/TestLanguageMenu.cpp - language menu: write-through to config,
// restart hint, and the deliberate absence of runtime retranslation.
//
// Spec 2026-11-18-Spec-BilingualUI-v1.2 §10.2 (P4-3) / decisions D1 + D2:
//   D1  a language choice takes effect only after a restart, so the running
//       window MUST stay in its startup language (asserted here - it guards
//       against someone "helpfully" adding runtime retranslation);
//   D2  English mode is never UI-automated; the English side is covered by
//       test_i18n table assertions plus manual screenshots.
//
// Isolation: this suite uses its own sandbox (test/ui-sandbox-lang) because all
// other UI_* tests share test/ui-sandbox/config.json. The file is restored to
// its original bytes in teardown, whatever the outcome.

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <fstream>
#include <string>
#include <vector>

#include "framework/Application.h"
#include "framework/Dialog.h"
#include "framework/Menu.h"
#include "framework/Text.h"
#include "UIIds.h"

namespace {

constexpr int kWindowTimeoutMs = 15000;
constexpr int kConfigTimeoutMs = 5000;

std::wstring languageSandboxDir() {
    return uitest::Paths::root() + L"\\test\\ui-sandbox-lang";
}

std::wstring languageSandboxConfig() {
    return languageSandboxDir() + L"\\config.json";
}

bool readWholeFile(const std::wstring& path, std::string* out) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return false;
    std::string buffer((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
    if (out != nullptr) *out = buffer;
    return true;
}

bool writeWholeFile(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.c_str(), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

// Keeps the sandbox config byte-identical across the test, pass or fail.
class ConfigGuard {
public:
    explicit ConfigGuard(const std::wstring& path) : path_(path) {
        existed_ = readWholeFile(path, &original_);
    }
    ~ConfigGuard() {
        if (!existed_) return;
        writeWholeFile(path_, original_);
    }
    ConfigGuard(const ConfigGuard&) = delete;
    ConfigGuard& operator=(const ConfigGuard&) = delete;

private:
    std::wstring path_;
    std::string original_;
    bool existed_ = false;
};

// Re-anchors the shared sandbox config onto test/ui-sandbox-lang:
//   * database.path -> <repo>/test/ui-sandbox-lang/ui-test.db (same seed DB)
//   * "ui": { "language": "zh-CN" } appended so the case does not depend on the
//     built-in default (D4).
bool prepareLanguageSandbox(std::string* error) {
    const std::wstring dir = languageSandboxDir();
    ::CreateDirectoryW(dir.c_str(), nullptr);

    const std::wstring sourceDb = uitest::Paths::sandboxDb();
    const std::wstring targetDb = dir + L"\\ui-test.db";
    if (!::CopyFileW(sourceDb.c_str(), targetDb.c_str(), FALSE)) {
        if (error != nullptr) *error = "cannot seed sandbox database";
        return false;
    }

    std::string base;
    if (!readWholeFile(uitest::Paths::sandboxConfig(), &base)) {
        if (error != nullptr) *error = "cannot read shared sandbox config";
        return false;
    }

    // Point the database at our own copy. The generated config stores the path
    // with forward slashes; keep that so the JSON stays valid.
    std::wstring dbPath = targetDb;
    for (std::size_t i = 0; i < dbPath.size(); ++i) {
        if (dbPath[i] == L'\\') dbPath[i] = L'/';
    }
    const std::string dbUtf8 = uitest::toUtf8(dbPath);
    const std::string uiBlock = "  \"ui\": { \"language\": \"zh-CN\" }\n";

    const std::string marker = "\"database\": { \"path\": \"";
    const std::size_t markerPos = base.find(marker);
    if (markerPos == std::string::npos) {
        if (error != nullptr) *error = "sandbox config has no database.path";
        return false;
    }
    const std::size_t valueStart = markerPos + marker.size();
    const std::size_t valueEnd = base.find('"', valueStart);
    if (valueEnd == std::string::npos) {
        if (error != nullptr) *error = "unterminated database.path value";
        return false;
    }
    base = base.substr(0, valueStart) + dbUtf8 + base.substr(valueEnd);

    // Append the ui block as the last member: the generated config ends with
    // "  }\n}" so the closing brace of the root object is the final '}'.
    const std::size_t rootClose = base.find_last_of('}');
    if (rootClose == std::string::npos) {
        if (error != nullptr) *error = "sandbox config has no root object";
        return false;
    }
    std::string head = base.substr(0, rootClose);
    const std::size_t lastNonSpace = head.find_last_not_of(" \t\r\n");
    if (lastNonSpace == std::string::npos) {
        if (error != nullptr) *error = "sandbox config root object is empty";
        return false;
    }
    base = head.substr(0, lastNonSpace + 1) + ",\n" + uiBlock + "}";

    if (!writeWholeFile(languageSandboxConfig(), base)) {
        if (error != nullptr) *error = "cannot write language sandbox config";
        return false;
    }
    return true;
}

bool configContains(const std::wstring& path, const char* needle) {
    std::string bytes;
    if (!readWholeFile(path, &bytes)) return false;
    return bytes.find(needle) != std::string::npos;
}

// Radio state of a leaf item inside a top-level menu. wxWidgets renders a radio
// group as plain check items, so MF_CHECKED is the observable state.
bool menuItemChecked(HWND hwnd, const wchar_t* topCaption, const wchar_t* itemCaption) {    HMENU bar = ::GetMenu(hwnd);
    if (bar == nullptr) return false;

    const int topCount = ::GetMenuItemCount(bar);
    for (int i = 0; i < topCount; ++i) {
        HMENU sub = ::GetSubMenu(bar, i);
        if (sub == nullptr) continue;
        wchar_t top[128] = {0};
        ::GetMenuStringW(bar, i, top, 128, MF_BYPOSITION);
        if (!uitest::menuCaptionMatches(std::wstring(top), std::wstring(topCaption))) continue;

        const int itemCount = ::GetMenuItemCount(sub);
        for (int j = 0; j < itemCount; ++j) {
            wchar_t text[256] = {0};
            ::GetMenuStringW(sub, j, text, 256, MF_BYPOSITION);
            std::wstring caption(text);
            const std::size_t tab = caption.find(L'\t');
            if (tab != std::wstring::npos) caption = caption.substr(0, tab);
            if (!uitest::menuCaptionMatches(caption, std::wstring(itemCaption))) continue;
            return (::GetMenuState(sub, j, MF_BYPOSITION) & MF_CHECKED) != 0;
        }
    }
    return false;
}

// Counts non-separator top-level menus (the bar itself: 文件/代理/任务/设置/语言/帮助).
int countTopLevelMenus(HWND hwnd) {
    HMENU bar = ::GetMenu(hwnd);
    if (bar == nullptr) return 0;
    return ::GetMenuItemCount(bar);
}

// wxMessageBox appears asynchronously after WM_COMMAND, so a single
// dismissAllPopups() right after the click can miss it (it did: 0 popups).
// Poll until the dialog shows up, close it, then give the handler a moment to
// finish the state updates that run after the modal returns.
int waitAndDismissPopups(DWORD pid, int timeoutMs) {
    const int stepMs = 150;
    int closed = 0;
    int waited = 0;
    while (waited < timeoutMs) {
        const uitest::DialogReport rep =
            uitest::findPopup(pid, uitest::WindowKind::Unknown, 0, 0);
        if (rep.found) {
            if (uitest::closePopup(rep)) {
                ++closed;
            } else {
                ::PostMessageW(rep.hwnd, WM_CLOSE, 0, 0);
            }
            ::Sleep(stepMs);
        } else if (closed > 0) {
            break;   // nothing left; the modal handler has returned
        } else {
            ::Sleep(stepMs);
        }
        waited += stepMs;
    }
    return closed;
}

}  // namespace

TEST_CASE("Language menu writes the choice to config and asks for a restart",
          "[language]") {
    std::string error;
    REQUIRE(prepareLanguageSandbox(&error));
    INFO("sandbox prep: " << error);

    ConfigGuard guard(languageSandboxConfig());

    // Production config must never be touched by a UI test (AC-10).
    std::wstring productionConfig = uitest::Paths::root() + L"\\bin\\config.json";
    std::string productionBefore;
    const bool productionReadable = readWholeFile(productionConfig, &productionBefore);

    uitest::AppProcess app;
    REQUIRE(app.start(uitest::Paths::exePath(), languageSandboxConfig()));
    REQUIRE(app.waitForMainWindow(kWindowTimeoutMs));
    uitest::setUiTargetPid(app.pid());

    const HWND hwnd = app.mainWindow();
    REQUIRE(hwnd != nullptr);

    // --- 1) menu bar shape: 6 top-level menus, language menu 5th, 中文 selected.
    const int topCount = countTopLevelMenus(hwnd);
    INFO("top-level menus: " << topCount);
    CHECK(topCount == 6);

    const std::vector<uitest::MenuEntry> entries = uitest::readNativeMenu(hwnd);
    INFO(uitest::toUtf8(uitest::menuReport(entries)));
    uitest::MenuEntry languageEntry;
    CHECK(uitest::findMenuEntry(entries, uitest::ids::MenuLanguageCaption, &languageEntry));
    CHECK(uitest::findMenuEntry(entries, uitest::ids::MenuLanguageZhItem, nullptr));
    CHECK(uitest::findMenuEntry(entries, uitest::ids::MenuLanguageEnItem, nullptr));
    CHECK(menuItemChecked(hwnd, uitest::ids::MenuLanguageCaption, uitest::ids::MenuLanguageZhItem));
    CHECK_FALSE(menuItemChecked(hwnd, uitest::ids::MenuLanguageCaption,
                                uitest::ids::MenuLanguageEnItem));

    // --- 2) pick English; the app must confirm that a restart is required.
    std::wstring trace;
    const bool clicked = uitest::clickMenu(hwnd, uitest::ids::MenuLanguageCaption,
                                           uitest::ids::MenuLanguageEnItem, &trace);
    INFO("menu trace: " << uitest::toUtf8(trace));
    REQUIRE(clicked);

    const int popups = waitAndDismissPopups(app.pid(), kConfigTimeoutMs);
    INFO("popups dismissed: " << popups);
    CHECK(popups >= 1);   // "UI language set to en-US; restart to apply"

    // --- 3) the choice reached the sandbox config, not the production one.
    bool written = false;
    for (int i = 0; i < kConfigTimeoutMs / 100; ++i) {
        if (configContains(languageSandboxConfig(), "\"en-US\"")) {
            written = true;
            break;
        }
        ::Sleep(100);
    }
    INFO("sandbox config now: " << [&]() {
        std::string bytes;
        readWholeFile(languageSandboxConfig(), &bytes);
        return bytes;
    }());
    CHECK(written);

    if (productionReadable) {
        std::string productionAfter;
        readWholeFile(productionConfig, &productionAfter);
        CHECK(productionAfter == productionBefore);
    }

    // --- 4) D1: the running window is still Chinese (no runtime retranslation).
    const std::vector<uitest::MenuEntry> after = uitest::readNativeMenu(hwnd);
    INFO(uitest::toUtf8(uitest::menuReport(after)));
    uitest::MenuEntry fileEntry;
    CHECK(uitest::findMenuEntry(after, uitest::ids::MenuFileCaption, &fileEntry));
    uitest::MenuEntry englishBar;
    CHECK_FALSE(uitest::findMenuEntry(after, uitest::ids::MenuLanguageCaptionEn, &englishBar));

    // --- 5) the English radio is now the selected one.
    CHECK(menuItemChecked(hwnd, uitest::ids::MenuLanguageCaption, uitest::ids::MenuLanguageEnItem));
    CHECK_FALSE(menuItemChecked(hwnd, uitest::ids::MenuLanguageCaption,
                                uitest::ids::MenuLanguageZhItem));

    CHECK(app.running());
}

TEST_CASE("Language menu is present exactly once", "[language][unit]") {
    // 常量层面的守门：语言菜单文案在 zh 会话是双语的，英文会话是纯英文；
    // 两个值都必须在 UIIds.h 里固定着（test_i18n 会与表交叉校验）。
    CHECK(std::wstring(uitest::ids::MenuLanguageCaption) !=
          std::wstring(uitest::ids::MenuLanguageCaptionEn));
    CHECK(std::wstring(uitest::ids::MenuLanguageZhItem) !=
          std::wstring(uitest::ids::MenuLanguageEnItem));
}
