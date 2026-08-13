// fake_xray_helper.cpp
// Test fixture executable standing in for xray.exe during XrayInstance
// unit tests. It ignores the command-line arguments appended by
// XrayInstance ("run -c <config>") and instead behaves based on its own
// file name:
//   - name contains "startup" : writes PANIC to stderr, exits with code 7
//     (simulates xray dying immediately during the start() poll window)
//   - otherwise              : writes PANIC to stderr, sleeps ~8s, exits
//     with code 42 (simulates a runtime death after start() succeeded,
//     since XrayInstance's start() poll window is 5s)
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

    if (startupMode) {
        return 7;
    }

    Sleep(8000);
    return 42;
}
