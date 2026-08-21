#include "ProcessInspector.h"

#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

namespace proc {

namespace {

// UTF-8 → UTF-16 (config file names are ASCII; used only for matching).
std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) {
        return L"";
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) {
        return L"";
    }
    std::wstring w(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], len);
    return w;
}

// Minimal PROCESS_BASIC_INFORMATION (avoids winternl.h conflicts).
struct ProcessBasicInfo {
    PVOID Reserved1;
    PVOID PebBaseAddress;
    PVOID Reserved2[2];
    ULONG_PTR UniqueProcessId;
    PVOID Reserved3;
};

using NtQueryInformationProcessFn = LONG(WINAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

NtQueryInformationProcessFn loadNtQuery() {
    static NtQueryInformationProcessFn fn = []() {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll) {
            return static_cast<NtQueryInformationProcessFn>(nullptr);
        }
        return reinterpret_cast<NtQueryInformationProcessFn>(
            GetProcAddress(ntdll, "NtQueryInformationProcess"));
    }();
    return fn;
}

// Reads another process's command line through its PEB. Returns false (and
// leaves out empty) when the target is protected or the PEB cannot be read.
// x64 offsets: PEB.ProcessParameters = 0x20, CommandLine = 0x70.
// x86 offsets: PEB.ProcessParameters = 0x10, CommandLine = 0x40.
bool readCommandLine(HANDLE hProcess, std::wstring& out) {
    NtQueryInformationProcessFn pNtQuery = loadNtQuery();
    if (!pNtQuery) {
        return false;
    }

    ProcessBasicInfo pbi{};
    LONG status = pNtQuery(hProcess, 0 /*ProcessBasicInformation*/, &pbi,
                           sizeof(pbi), nullptr);
    if (status != 0 || !pbi.PebBaseAddress) {
        return false;
    }

    // Detect the target process architecture to pick the correct offsets.
    bool targetIs32 = false;
    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    IsWow64Process2Fn pIsWow64Process2 = reinterpret_cast<IsWow64Process2Fn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
    if (pIsWow64Process2) {
        USHORT procMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        if (pIsWow64Process2(hProcess, &procMachine, &nativeMachine)) {
            // procMachine != UNKNOWN means the target is a WOW64 (32-bit) process.
            targetIs32 = (procMachine != IMAGE_FILE_MACHINE_UNKNOWN);
        } else {
            SYSTEM_INFO si{};
            GetNativeSystemInfo(&si);
            targetIs32 = (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL);
        }
    } else {
        SYSTEM_INFO si{};
        GetNativeSystemInfo(&si);
        targetIs32 = (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL);
    }

    const size_t pebParamsOffset = targetIs32 ? 0x10 : 0x20;
    const size_t paramsCmdOffset = targetIs32 ? 0x40 : 0x70;

    BYTE* paramsAddr = nullptr;
    SIZE_T read = 0;
    if (!ReadProcessMemory(hProcess,
                           reinterpret_cast<BYTE*>(pbi.PebBaseAddress) + pebParamsOffset,
                           &paramsAddr, sizeof(paramsAddr), &read) ||
        read != sizeof(paramsAddr) || !paramsAddr) {
        return false;
    }

    // RTL_USER_PROCESS_PARAMETERS.CommandLine is a UNICODE_STRING.
    struct LocalUnicodeString {
        USHORT Length;
        USHORT MaximumLength;
        PVOID Buffer;
    };
    LocalUnicodeString cmd{};
    if (!ReadProcessMemory(hProcess,
                           reinterpret_cast<BYTE*>(paramsAddr) + paramsCmdOffset,
                           &cmd, sizeof(cmd), &read) ||
        read != sizeof(cmd) || !cmd.Buffer || cmd.Length == 0) {
        return false;
    }

    const size_t byteLen = cmd.Length;
    std::wstring buf(byteLen / sizeof(wchar_t), L'\0');
    if (!ReadProcessMemory(hProcess, cmd.Buffer, &buf[0], byteLen, &read) ||
        read != byteLen) {
        return false;
    }
    out = buf;
    return true;
}

} // namespace

ProcessInspector::EnumeratorFn ProcessInspector::g_testEnumerator_ = nullptr;

void ProcessInspector::setEnumeratorForTesting(EnumeratorFn fn) {
    g_testEnumerator_ = std::move(fn);
}

bool ProcessInspector::isProcessRunningWithConfig(const std::string& configFileName) {
    // Only the two standalone backends can host a standalone config.
    const std::vector<std::string> exes = {"xray.exe", "sing-box.exe"};
    bool anyNameMatch = false;
    bool anyReadFailed = false;
    bool matched = false;

    for (const std::string& exe : exes) {
        const std::vector<ProcessInfo> procs = enumerateByName(exe);
        for (const ProcessInfo& p : procs) {
            anyNameMatch = true;
            if (p.commandLine.empty()) {
                // Could not read the command line → cannot prove it is a
                // different config, so treat it as a potential duplicate.
                anyReadFailed = true;
                continue;
            }
            if (matchesConfig(p.commandLine, true, false, configFileName)) {
                matched = true;
            }
        }
    }

    // Conservative fallback: if a matching exe is running but its command
    // line could not be read, block the duplicate start (WARN elsewhere).
    return matched || (anyNameMatch && anyReadFailed);
}

std::vector<ProcessInfo> ProcessInspector::enumerateByName(const std::string& exeName) {
    if (g_testEnumerator_) {
        return g_testEnumerator_(exeName);
    }

    std::vector<ProcessInfo> result;
    const std::wstring target = utf8ToWide(exeName);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return result;
    }

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, target.c_str()) != 0) {
            continue;
        }
        ProcessInfo info;
        info.pid = pe.th32ProcessID;
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                               FALSE, pe.th32ProcessID);
        if (h) {
            std::wstring cmdLine;
            if (readCommandLine(h, cmdLine)) {
                info.commandLine = cmdLine;
            }
            info.creationTime = processCreationTime(h);
            CloseHandle(h);
        }
        result.push_back(std::move(info));
    }

    CloseHandle(snap);
    return result;
}

bool ProcessInspector::matchesConfig(const std::wstring& commandLine,
                                     bool nameMatched, bool readFailed,
                                     const std::string& configFileName) {
    if (readFailed) {
        return nameMatched;  // conservative
    }
    const std::wstring needle = utf8ToWide(configFileName);
    return commandLine.find(needle) != std::wstring::npos;
}

std::string ProcessInspector::extractConfigFileName(const std::wstring& commandLine) {
    // Standalone config files are ASCII and always start with "standalone_".
    const std::wstring marker = L"standalone_";
    const size_t pos = commandLine.find(marker);
    if (pos == std::wstring::npos) {
        return std::string();
    }
    const std::wstring ext = L".json";
    const size_t extPos = commandLine.find(ext, pos);
    if (extPos == std::wstring::npos) {
        return std::string();
    }
    // The token between marker and ".json" must be a plain file name: no path
    // separator and no quotes may appear inside it.
    const std::wstring token = commandLine.substr(pos, extPos + ext.size() - pos);
    if (token.find(L'\\') != std::wstring::npos ||
        token.find(L'/') != std::wstring::npos ||
        token.find(L'"') != std::wstring::npos) {
        return std::string();
    }
    // UTF-16 → UTF-8 (the caller only uses the ASCII file name).
    std::string out;
    int len = WideCharToMultiByte(CP_UTF8, 0, token.c_str(), -1, nullptr, 0,
                                  nullptr, nullptr);
    if (len <= 0) {
        return std::string();
    }
    out.resize(static_cast<size_t>(len - 1));
    WideCharToMultiByte(CP_UTF8, 0, token.c_str(), -1, &out[0], len,
                        nullptr, nullptr);
    return out;
}

std::string ProcessInspector::extractConfigFullPath(const std::wstring& commandLine) {
    // Same marker/suffix logic as extractConfigFileName, but here we extend the
    // extracted token leftward to include any directory portion of the path.
    const std::wstring marker = L"standalone_";
    const size_t pos = commandLine.find(marker);
    if (pos == std::wstring::npos) {
        return std::string();
    }
    const std::wstring ext = L".json";
    const size_t extPos = commandLine.find(ext, pos);
    if (extPos == std::wstring::npos) {
        return std::string();
    }
    const size_t tokenEnd = extPos + ext.size();  // exclusive end

    // Extend left from pos to include the directory portion. Stop at the first
    // character that is not a valid path character (space, quote, '=', etc.).
    // Valid path chars: alphanumerics and \ / : . - _  (drive letters use ':').
    auto isPathChar = [](wchar_t c) -> bool {
        return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
               (c >= L'0' && c <= L'9') ||
               c == L'\\' || c == L'/' || c == L':' ||
               c == L'.' || c == L'-' || c == L'_';
    };
    size_t start = pos;
    while (start > 0 && isPathChar(commandLine[start - 1])) {
        --start;
    }
    const std::wstring fullToken = commandLine.substr(start, tokenEnd - start);

    // Require a directory component; otherwise this is a bare file name that the
    // caller's directory probe already handles.
    if (fullToken.find(L'\\') == std::wstring::npos &&
        fullToken.find(L'/') == std::wstring::npos) {
        return std::string();
    }

    // UTF-16 -> UTF-8 (mirrors extractConfigFileName).
    std::string out;
    const int len = WideCharToMultiByte(CP_UTF8, 0, fullToken.c_str(), -1,
                                        nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return std::string();
    }
    out.resize(static_cast<size_t>(len - 1));
    WideCharToMultiByte(CP_UTF8, 0, fullToken.c_str(), -1, &out[0], len,
                        nullptr, nullptr);
    return out;
}

std::string ProcessInspector::nowTimestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

std::string ProcessInspector::fileTimeToString(const FILETIME& ft) {
    // FILETIME counts 100ns ticks since 1601-01-01 UTC.
    ULARGE_INTEGER ui;
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    const long long ticksSince1601 =
        static_cast<long long>(ui.QuadPart);
    const long long unixSeconds =
        ticksSince1601 / 10000000LL - 11644473600LL;  // 1601 -> 1970 offset
    if (unixSeconds < 0) {
        return std::string();
    }
    std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}

std::string ProcessInspector::processCreationTime(HANDLE processHandle) {
    if (!processHandle) {
        return std::string();
    }
    FILETIME create{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(processHandle, &create, &exit, &kernel, &user)) {
        return std::string();
    }
    return fileTimeToString(create);
}

std::string ProcessInspector::processCreationTime(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) {
        return std::string();
    }
    const std::string result = processCreationTime(h);
    CloseHandle(h);
    return result;
}

long long ProcessInspector::durationMsBetween(const std::string& start,
                                              const std::string& end) {
    auto parseTs = [](const std::string& s) -> long long {
        std::tm tm{};
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
        if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) {
            return -1;
        }
        tm.tm_year = y - 1900;
        tm.tm_mon = mo - 1;
        tm.tm_mday = d;
        tm.tm_hour = h;
        tm.tm_min = mi;
        tm.tm_sec = se;
        tm.tm_isdst = -1;
        std::time_t t = std::mktime(&tm);
        return t < 0 ? -1 : static_cast<long long>(t) * 1000LL;
    };
    const long long a = parseTs(start);
    const long long b = parseTs(end);
    if (a < 0 || b < 0) {
        return 0;
    }
    return b - a;
}

} // namespace proc