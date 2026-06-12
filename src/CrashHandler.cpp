#include "CrashHandler.h"

#include <windows.h>
#include <DbgHelp.h>
#include <ctime>
#include <cstdio>
#include <string>
#include <filesystem>

static LPTOP_LEVEL_EXCEPTION_FILTER s_previousFilter = nullptr;
static char s_dumpDir[MAX_PATH] = {0};

static void initDumpDirectory() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    std::filesystem::path dumpDir = exeDir / "temp";
    if (!std::filesystem::exists(dumpDir)) {
        std::filesystem::create_directory(dumpDir);
    }
    std::string dir = dumpDir.string();
    strncpy(s_dumpDir, dir.c_str(), MAX_PATH - 1);
    s_dumpDir[MAX_PATH - 1] = '\0';
}

static LONG WINAPI exceptionHandler(EXCEPTION_POINTERS* exceptionInfo) {
    char timestamp[32];
    std::time_t now = std::time(nullptr);
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", std::localtime(&now));

    char dumpPath[MAX_PATH];
    _snprintf(dumpPath, MAX_PATH, "%s\\crash_%s.dmp", s_dumpDir, timestamp);

    HANDLE hFile = CreateFileA(dumpPath, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = exceptionInfo;
        mei.ClientPointers = FALSE;

        MiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            hFile,
            MiniDumpWithDataSegs,
            exceptionInfo ? &mei : nullptr,
            nullptr,
            nullptr
        );
        CloseHandle(hFile);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

bool crash::installHandler() {
    initDumpDirectory();
    s_previousFilter = SetUnhandledExceptionFilter(exceptionHandler);
    return true;
}
