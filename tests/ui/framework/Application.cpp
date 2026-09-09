// tests/ui/framework/Application.cpp
#include "framework/Application.h"
#include "framework/Wait.h"
#include <vector>
#include <unordered_map>
#include <tlhelp32.h>

namespace uitest {

// Forcefully terminate every descendant of `rootPid` (not the root itself). The
// GUI app spawns worker/xray subprocesses; if we only kill the main process the
// children keep running, accumulate across tests, and saturate the machine --
// which makes every subsequent GUI test flaky. Windows reparents orphaned
// children to a system process, so we resolve the full transitive descendant set
// via the parent map before killing.
static void killProcessTree(DWORD rootPid) {
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    std::unordered_map<DWORD, DWORD> parentOf; // pid -> parent pid
    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    if (::Process32First(snap, &pe)) {
        do { parentOf[pe.th32ProcessID] = pe.th32ParentProcessID; }
        while (::Process32Next(snap, &pe));
    }
    ::CloseHandle(snap);

    std::vector<DWORD> descendants;
    for (const auto& kv : parentOf) {
        DWORD cur = kv.first;
        bool isDesc = false;
        // Windows parent chains occasionally form cycles (e.g. after service
        // host restarts or PID reuse). Guard the walk so a cycle cannot make
        // teardown spin forever; real chains are shallow (well under 64).
        for (std::size_t hops = 0; cur != 0 && hops < 64; ++hops) {
            if (cur == rootPid) { isDesc = true; break; }
            auto it = parentOf.find(cur);
            if (it == parentOf.end()) break;
            cur = it->second;
        }
        if (isDesc && kv.first != rootPid) descendants.push_back(kv.first);
    }
    for (DWORD pid : descendants) {
        HANDLE h = ::OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (h) { ::TerminateProcess(h, static_cast<UINT>(-1)); ::CloseHandle(h); }
    }
}

static DWORD g_targetPid = 0;
void setUiTargetPid(DWORD pid) { g_targetPid = pid; }
DWORD uiTargetPid() { return g_targetPid; }

AppProcess::~AppProcess() {
    terminate();
    if (stderrHandle_) { ::CloseHandle(stderrHandle_); stderrHandle_ = nullptr; }
    if (pi_.hProcess) { CloseHandle(pi_.hProcess); pi_.hProcess = nullptr; }
    if (pi_.hThread)  { CloseHandle(pi_.hThread);  pi_.hThread = nullptr; }
}

namespace {

// Open a capture file under the UI artifacts directory that will receive the
// child process's stdout/stderr. The handle is made inheritable so the spawned
// GUI process writes here — this is how we recover wxWidgets assert text that
// would otherwise only surface inside a blocking modal dialog.
HANDLE openStderrCapture(std::wstring& outPath) {
    std::wstring dir = Paths::artifactsDir();
    ::CreateDirectoryW(dir.c_str(), nullptr);   // ignore if already exists
    outPath = dir + L"\\uitest-stderr-" +
              std::to_wstring(::GetCurrentProcessId()) + L"-" +
              std::to_wstring(::GetTickCount64()) + L".log";
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE h = ::CreateFileW(outPath.c_str(),
                             FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return (h == INVALID_HANDLE_VALUE) ? nullptr : h;
}

} // namespace

bool AppProcess::start(const std::wstring& exePath, const std::wstring& configPath) {
    // GUI parses ONLY the space-separated form "--config <path>" (main_gui.cpp);
    // the "--config=path" form is silently ignored and falls back to bin/config.
    std::wstring cmd = L"\"" + exePath + L"\" --config \"" + configPath + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    // (3) Redirect child stdout/stderr to a capture file so assert/log text is
    //     recoverable by the test harness instead of being lost to a modal box.
    stderrHandle_ = openStderrCapture(stderrPath_);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (stderrHandle_) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = stderrHandle_;
        si.hStdError  = stderrHandle_;
        si.hStdInput  = ::GetStdHandle(STD_INPUT_HANDLE);
    }

    // (1) Tell the app to convert asserts into log-only (no modal dialog) so a
    //     stray assert cannot block UI-test initialization. Inherited by child.
    ::SetEnvironmentVariableW(L"VALIDPROXY_ASSERT_LOG", L"1");

    // (1b) Disable dangling-standalone adoption in the sandbox app: adoption
    //     of a production standalone xray (visible system-wide) makes the
    //     app's main thread stall inside the event-driven refreshResults()
    //     sync DB read and hangs all behavior/UIA tests. Production never
    //     sets this variable. Inherited by child.
    ::SetEnvironmentVariableW(L"VALIDPROXY_NO_ADOPT", L"1");

    BOOL ok = ::CreateProcessW(nullptr,          // application name from cmdline
                                buf.data(),       // mutable command line
                                nullptr, nullptr,
                                stderrHandle_ ? TRUE : FALSE,
                                CREATE_UNICODE_ENVIRONMENT,
                                nullptr, nullptr, &si, &pi_);
    if (!ok && stderrHandle_) {
        ::CloseHandle(stderrHandle_);
        stderrHandle_ = nullptr;
    }
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

        // Kill spawned children (xray / worker subprocesses) so they don't linger
        // and saturate the machine across tests.
        killProcessTree(pi_.dwProcessId);

        if (!waitFor(3000, 150, [&]() -> bool { return !running(); })) {
            ::TerminateProcess(pi_.hProcess, static_cast<UINT>(-1));
            ::WaitForSingleObject(pi_.hProcess, 5000);
        }
    }
    hwnd_ = nullptr;
}

}
