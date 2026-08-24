// tests/ui/framework/Application.cpp
#include "framework/Application.h"
#include "framework/Wait.h"
#include <vector>

namespace uitest {

static DWORD g_targetPid = 0;
void setUiTargetPid(DWORD pid) { g_targetPid = pid; }
DWORD uiTargetPid() { return g_targetPid; }

AppProcess::~AppProcess() {
    terminate();
    if (pi_.hProcess) { CloseHandle(pi_.hProcess); pi_.hProcess = nullptr; }
    if (pi_.hThread)  { CloseHandle(pi_.hThread);  pi_.hThread = nullptr; }
}

bool AppProcess::start(const std::wstring& exePath, const std::wstring& configPath) {
    // GUI parses ONLY the space-separated form "--config <path>" (main_gui.cpp);
    // the "--config=path" form is silently ignored and falls back to bin/config.
    std::wstring cmd = L"\"" + exePath + L"\" --config \"" + configPath + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    BOOL ok = ::CreateProcessW(nullptr,          // application name from cmdline
                               buf.data(),       // mutable command line
                               nullptr, nullptr, FALSE,
                               CREATE_UNICODE_ENVIRONMENT,
                               nullptr, nullptr, &si, &pi_);
    return ok != FALSE;
}

struct EnumCtx { DWORD pid; HWND hwnd; };
static BOOL CALLBACK enumProc(HWND hwnd, LPARAM lp) {
    EnumCtx* ctx = reinterpret_cast<EnumCtx*>(lp);
    DWORD pid = 0;
    ::GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ctx->pid && ::IsWindowVisible(hwnd)) {
        // Top-level app window: has a title or is an app window per GW_OWNER.
        wchar_t title[256] = L"";
        ::GetWindowTextW(hwnd, title, 256);
        if (title[0] != L'\0' || ::GetWindow(hwnd, GW_OWNER) == nullptr) {
            ctx->hwnd = hwnd;
            return FALSE;                    // stop enumeration
        }
    }
    return TRUE;
}

bool AppProcess::waitForMainWindow(int timeoutMs, int pollMs) {
    const bool found = waitFor(timeoutMs, pollMs, [&]() -> bool {
        EnumCtx ctx{ pi_.dwProcessId, nullptr };
        ::EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.hwnd) { hwnd_ = ctx.hwnd; return true; }
        return false;
    });
    return found;
}

bool AppProcess::running() const {
    if (!pi_.hProcess) return false;
    DWORD code = 0;
    if (!::GetExitCodeProcess(pi_.hProcess, &code)) return false;
    return code == STILL_ACTIVE;
}

void AppProcess::terminate() {
    if (pi_.hProcess && running()) {
        // Graceful first: WM_CLOSE to every window of this PID.
        struct Ctx { DWORD pid; } ctx{ pi_.dwProcessId };
        ::EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
            Ctx* c = reinterpret_cast<Ctx*>(lp);
            DWORD pid = 0;
            ::GetWindowThreadProcessId(hwnd, &pid);
            if (pid == c->pid) ::PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));

        if (!waitFor(3000, 150, [&]() -> bool { return !running(); })) {
            ::TerminateProcess(pi_.hProcess, static_cast<UINT>(-1));
            ::WaitForSingleObject(pi_.hProcess, 5000);
        }
    }
    hwnd_ = nullptr;
}

}
