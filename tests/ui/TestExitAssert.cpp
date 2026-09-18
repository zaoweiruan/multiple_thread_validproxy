#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <string>
#include "framework/Fixtures.h"
#include "framework/Wait.h"

using namespace uitest;

// Read the child process's redirected stderr capture file (assert lines from
// UIApp::OnAssertFailure are fwprintf'd there when VALIDPROXY_ASSERT_LOG=1,
// which AppProcess sets for every spawned instance).
static bool readFile(const std::wstring& path, std::string& out) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[4096];
    DWORD n = 0;
    for (;;) {
        if (!::ReadFile(h, buf, sizeof(buf), &n, nullptr) || n == 0) break;
        out.append(buf, buf + n);
    }
    ::CloseHandle(h);
    return true;
}

TEST_CASE("Exit via WM_CLOSE does not fire the RemoveEventHandler assert", "[exitassert]") {
    AppFixture fx;
    REQUIRE(fx.ready());

    HWND hwnd = fx.app().mainWindow();
    REQUIRE(hwnd != nullptr);

    // WM_CLOSE goes through the same onClose -> Destroy -> ~MainFrame path as
    // the tray exit menu item; the assert fires in ~wxFrame base teardown
    // AFTER ~MainFrame's body — path-independent.
    ::PostMessageW(hwnd, WM_CLOSE, 0, 0);

    // 90s cap: startup does a large DB-backed panel load on the UI thread,
    // which can occasionally stall the message pump for 30+s under load (cold
    // disk / antivirus). The WM_CLOSE then waits in the queue: the test
    // verifies the exit path is assert-clean, not that teardown is fast.
    bool exited = waitFor(90000, 200, [&fx] { return !fx.app().running(); });
    REQUIRE(exited);

    std::string captured;
    REQUIRE(readFile(fx.app().stderrPath(), captured));

    // The wxWidgets Debug assert "RemoveEventHandler: where has the event
    // handler gone?" must never fire during the exit path.
    REQUIRE(captured.find("where has the event handler gone") == std::string::npos);
}
