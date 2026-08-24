// tests/ui/TestDiscovery.cpp - Task 6: sandbox readiness + live control-tree
// discovery. The [dumptree] output is used to pin UIIds.h (re-flow gate).
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include "framework/Application.h"
#include "framework/Fixtures.h"
#include "UIIds.h"

using namespace uitest;

// Catch2 INFO streams are narrow; convert UTF-16 to UTF-8.
static std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                        static_cast<int>(w.size()),
                                        nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                          &s[0], n, nullptr, nullptr);
    return s;
}

TEST_CASE("sandbox environment is prepared", "[discovery]") {
    REQUIRE(std::filesystem::exists(Paths::sandboxDb()));
    REQUIRE(std::filesystem::exists(Paths::sandboxConfig()));
    REQUIRE(std::filesystem::exists(Paths::exePath()));
}

TEST_CASE("dump real main window control tree", "[dumptree]") {
    AppFixture fx;
    if (!fx.ready()) {
        FAIL("GUI failed to start or main window not found; check sandbox config");
    }

    UiElement win = fx.mainWindow();
    if (!win.valid()) {
        // Diagnostic: list top-level windows as UIA sees them for this PID.
        UiElement desk(Uia::instance().acquireDesktop());
        UNSCOPED_INFO(narrow(L"--- desktop top-level (PID filter: "
                             + std::to_wstring(fx.pid()) + L") ---"));
        UNSCOPED_INFO(narrow(desk.dumpTree(1)));
        FAIL("main window not found by UIA name");
    }

    INFO("MainWindow class=" << narrow(win.className()));
    INFO("PID=" << fx.pid());

    // Print the tree to console; capture this with -s (successful output).
    const std::string tree = narrow(win.dumpTree(12));
    UNSCOPED_INFO("=== CONTROL TREE BEGIN ===");
    UNSCOPED_INFO(tree);
    UNSCOPED_INFO("=== CONTROL TREE END ===");

    // Structural sanity only - identifiers get pinned after human review.
    CHECK_FALSE(win.name().empty());
}
