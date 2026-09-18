// fake_xray_helper.cpp
// Test fixture executable standing in for xray.exe during XrayInstance /
// XrayManager unit tests. It ignores the command-line arguments appended by
// XrayInstance ("run -c <config>") and instead behaves based on its own
// file name:
//   - name contains "startup"   : writes PANIC to stderr, exits with code 7
//     (simulates xray dying immediately during the start() spawn-grace window)
//   - name contains "latecrash" : writes PANIC to stderr, sleeps ~6s, exits
//     with code 7 (simulates a startup FLASH-CRASH / hang that SURVIVES the
//     short spawn-grace poll AND the old 5s liveness poll, then dies (or never
//     opens its API port) — this is the exact case that must NOT block for the
//     full readiness timeout)
//   - otherwise                 : writes PANIC to stderr, sleeps ~8s, exits
//     with code 42 (simulates a runtime death after start() succeeded)
#include <windows.h>

#include <cstdio>
#include <cstring>

int main() {
    fprintf(stderr, "PANIC\n");
    fflush(stderr);

    char selfPath[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, selfPath, MAX_PATH);
    const bool startupMode =
        (len > 0 && strstr(selfPath, "startup") != nullptr);
    const bool lateCrashMode =
        (len > 0 && strstr(selfPath, "latecrash") != nullptr);

    if (startupMode) {
        return 7;
    }
    if (lateCrashMode) {
        Sleep(6000);
        return 7;
    }

    Sleep(8000);
    return 42;
}
