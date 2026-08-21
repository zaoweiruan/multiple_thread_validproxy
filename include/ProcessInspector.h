#ifndef PROC_PROCESS_INSPECTOR_H
#define PROC_PROCESS_INSPECTOR_H

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

namespace proc {

// One running process of interest (xray.exe / sing-box.exe), with its
// command line when it could be read (empty on PEB read failure) and its
// system creation time (empty when it could not be read).
struct ProcessInfo {
    DWORD pid = 0;
    std::wstring commandLine;
    std::string creationTime;   // "yyyy-MM-dd HH:mm:ss", empty on failure
};

// Enumerates running processes and matches command lines / executable names.
// Used by R1 (duplicate-start guard) and R6 (auto takeover) of the
// standalone-proxy lifecycle (docs/specs/2026-08-13-Spec-ProxyScoring-History-v1.0.md).
class ProcessInspector {
public:
    // Enumerator hook (test injection point). When set, enumerateByName
    // delegates to this callable instead of walking the real process table.
    using EnumeratorFn = std::function<std::vector<ProcessInfo>(const std::string& exeName)>;
    static void setEnumeratorForTesting(EnumeratorFn fn);

    // True when any xray.exe / sing-box.exe process is running whose command
    // line contains configFileName. Command-line read failures for a matching
    // exe fall back to process-name matching (conservative interception).
    static bool isProcessRunningWithConfig(const std::string& configFileName);

    // All processes whose executable base name equals exeName (e.g.
    // "xray.exe"), each with its command line (empty when unreadable).
    static std::vector<ProcessInfo> enumerateByName(const std::string& exeName);

    // Pure decision helper (unit-testable): combines the command-line match,
    // the exe-name match and the conservative read-failure fallback.
    static bool matchesConfig(const std::wstring& commandLine,
                              bool nameMatched, bool readFailed,
                              const std::string& configFileName);

    // Extracts the standalone config file name (e.g.
    // "standalone_<indexId>-xray.json") from a process command line. Returns
    // an empty string when the command line does not reference a standalone
    // config (used by dangling-process adoption). Pure/unit-testable.
    static std::string extractConfigFileName(const std::wstring& commandLine);

    // Extracts the full path (when present) of a standalone config file from a
    // process command line, e.g. "D:\\other\\standalone_<indexId>-xray.json".
    // Returns an empty string when no "standalone_*.json" token exists, or when
    // the token is a bare file name (no directory component). The bare-name case
    // is covered by extractConfigFileName plus the caller's directory probe.
    static std::string extractConfigFullPath(const std::wstring& commandLine);

    // Local time as "yyyy-MM-dd HH:mm:ss" (used for runtime-history sessions).
    static std::string nowTimestamp();

    // Milliseconds between two "yyyy-MM-dd HH:mm:ss" timestamps (end - start).
    // Returns 0 when either timestamp cannot be parsed.
    static long long durationMsBetween(const std::string& start,
                                       const std::string& end);

    // System creation time of the process with the given pid, formatted
    // "yyyy-MM-dd HH:mm:ss" (local time). Returns an empty string when the
    // process does not exist or its creation time cannot be read.
    static std::string processCreationTime(DWORD pid);

    // Same as processCreationTime(DWORD) but takes an already-open process
    // handle (used inside enumerateByName to reuse the OpenProcess handle).
    static std::string processCreationTime(HANDLE processHandle);

    // Converts a FILETIME (100ns ticks since 1601-01-01 UTC) to a local-time
    // "yyyy-MM-dd HH:mm:ss" string. Pure function, unit-testable.
    static std::string fileTimeToString(const FILETIME& ft);

private:
    // Test-only enumerator override (nullptr = use real process table).
    static EnumeratorFn g_testEnumerator_;
};

} // namespace proc

#endif // PROC_PROCESS_INSPECTOR_H