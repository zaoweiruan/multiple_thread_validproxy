// tests/ui/framework/Application.h - GUI process lifecycle for UI tests.
#pragma once
#include <windows.h>
#include <string>
#include "framework/ComPtr.h"

namespace uitest {

// Spawns bin/validproxy.exe against the sandbox config, waits until its main
// window is visible, and guarantees termination in the destructor (kill on
// scope exit even when an assertion fails mid-test).
class AppProcess {
public:
    AppProcess() = default;
    ~AppProcess();                       // terminate() if still running
    AppProcess(const AppProcess&) = delete;
    AppProcess& operator=(const AppProcess&) = delete;

    // Starts exePath with --config=configPath (quoted args), suspended=false.
    // Returns false when process creation fails (lastError via GetLastError).
    bool start(const std::wstring& exePath, const std::wstring& configPath);

    // Polls EnumWindows for a visible top-level window owned by this PID
    // within timeoutMs. Stores it; also returns false on timeout.
    bool waitForMainWindow(int timeoutMs, int pollMs = 200);

    DWORD pid() const { return pi_.dwProcessId; }
    HWND mainWindow() const { return hwnd_; }
    bool running() const;
    void terminate();                    // WM_CLOSE, then TerminateProcess fallback

private:
    PROCESS_INFORMATION pi_{};
    HWND hwnd_ = nullptr;
};

// Absolute-path helpers rooted at the repo directory. Paths MUST be absolute:
// the GUI resolves relative config paths against ITS exeDir (bin/), not our
// CWD - a relative path silently becomes bin\test\ui-sandbox\config.json.
class Paths {
public:
    static std::wstring root() {           // UITests.exe lives in <repo>/tests
        wchar_t buf[MAX_PATH] = L"";
        ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::wstring dir(buf);
        const size_t pos = dir.find_last_of(L'\\');
        if (pos == std::wstring::npos) return std::wstring(L".");
        return dir.substr(0, pos) + L"\\..";   // <repo>/tests -> <repo>
    }
    static std::wstring sandboxDir() { return root() + L"\\test\\ui-sandbox"; }
    static std::wstring sandboxDb() { return sandboxDir() + L"\\ui-test.db"; }
    static std::wstring sandboxConfig() { return sandboxDir() + L"\\config.json"; }
    static std::wstring exePath() { return root() + L"\\bin\\validproxy.exe"; }
    static std::wstring artifactsDir() { return root() + L"\\test-results\\ui-artifacts"; }
};

// PID of the GUI instance under test. Set by AppFixture; the artifacts
// listener only touches windows of THIS process, never unrelated windows.
void setUiTargetPid(DWORD pid);
DWORD uiTargetPid();

}
