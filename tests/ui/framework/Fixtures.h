// tests/ui/framework/Fixtures.h - shared test fixtures (header-only).
// Sandbox isolation: ONLY test/ui-sandbox/ is ever touched. Production
// databases (bin/worker/guindb.db, test/guindb.db) are never read or written.
#pragma once
#include <catch2/catch_test_macros.hpp>
#include <exception>
#include <windows.h>
#include "framework/Application.h"
#include "framework/UIAutomation.h"
#include "framework/UIElement.h"
#include "framework/Diagnostics.h"
#include "UIIds.h"

namespace uitest {

// Launches the GUI against the sandbox config and yields its main window.
class AppFixture {
public:
    AppFixture() {
        INFO("starting GUI with sandbox config");
        exceptionsAtStart_ = std::uncaught_exceptions();
        started_ = app_.start(Paths::exePath(), Paths::sandboxConfig());
        if (started_) {
            windowFound_ = app_.waitForMainWindow(kWindowTimeoutMs);
            setUiTargetPid(app_.pid());
        }
    }
    ~AppFixture() {
        // Capture failure artifacts HERE, while the GUI is still alive.
        // The Catch2 listener fires only after this destructor has already
        // terminated the process, so this is the last viable capture point.
        if (started_ && windowFound_
            && std::uncaught_exceptions() > exceptionsAtStart_) {
            const HWND hwnd = app_.mainWindow();
            if (hwnd && ::IsWindow(hwnd)) saveArtifacts(hwnd, L"failure");
        }
    }
    AppFixture(const AppFixture&) = delete;
    AppFixture& operator=(const AppFixture&) = delete;

    bool ready() const { return started_ && windowFound_; }

    // PID-filtered lookup under the desktop so we never bind another
    // instance's controls.
    UiElement mainWindow() const {
        return UiElement::findBy(desktop(),
                                 Locator{ UIA_NamePropertyId, ids::MainWindowName },
                                 app_.pid(), kFindTimeoutMs);
    }
    DWORD pid() const { return app_.pid(); }
    const AppProcess& app() const { return app_; }

private:
    static UiElement desktop() {
        return UiElement(Uia::instance().acquireDesktop());
    }
    static constexpr int kWindowTimeoutMs = 15000;
    static constexpr int kFindTimeoutMs  = 10000;
    int exceptionsAtStart_ = 0;
    AppProcess app_;
    bool started_ = false;
    bool windowFound_ = false;
};

}
