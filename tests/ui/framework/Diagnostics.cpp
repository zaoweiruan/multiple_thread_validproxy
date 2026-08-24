// tests/ui/framework/Diagnostics.cpp
#include "framework/Diagnostics.h"
#include "framework/Screenshot.h"
#include "framework/UIAutomation.h"
#include "framework/UIElement.h"
#include "framework/Application.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>

namespace uitest {

void ensureArtifactsDir() {
    std::error_code ec;
    std::filesystem::create_directories(Paths::artifactsDir(), ec);
}

static std::wstring timestampTag() {
    const std::time_t t = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    wchar_t buf[32];
    struct tm tmv{};
    ::localtime_s(&tmv, &t);
    wcsftime(buf, 32, L"%Y%m%d-%H%M%S", &tmv);
    return std::wstring(buf);
}

// MinGW libstdc++ fstream only accepts narrow (UTF-8) paths on Windows.
static std::string narrowPath(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                        static_cast<int>(w.size()),
                                        nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                          &s[0], n, nullptr, nullptr);
    return s;
}
// Same conversion for content: wofstream silently fails on non-ASCII wide
// text under the default locale, so artifacts are written as UTF-8 bytes.
static std::string narrow(const std::wstring& w) {
    return narrowPath(w);   // identical CP_UTF8 conversion
}

std::wstring saveArtifacts(HWND hwnd, const std::wstring& tag) {
    ensureArtifactsDir();
    const std::wstring base = Paths::artifactsDir() + L"\\" + tag + L"_" + timestampTag();

    saveWindowPng(hwnd, base + L".png");

    // Bind the exact window by handle - never walk unrelated desktop trees.
    IUIAutomationElement* el = nullptr;
    IUIAutomation* ua = Uia::instance().com();
    if (ua && SUCCEEDED(ua->ElementFromHandle(hwnd, &el)) && el) {
        UiElement win(el);
        // Write as UTF-8 through a NARROW stream: libstdc++'s wofstream
        // silently fails (badbit) on non-ASCII wide chars under the default
        // locale, producing empty files.
        std::ofstream out(narrowPath(base + L".txt"), std::ios::binary);
        if (out) {
            const std::string utf8 = narrow(win.dumpTree(12));
            out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        }
    }
    return base;
}

}
