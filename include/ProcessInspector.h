#ifndef PROC_PROCESS_INSPECTOR_H
#define PROC_PROCESS_INSPECTOR_H

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

namespace proc {

// One running process of interest (xray.exe / sing-box.exe), with its
// command line when it could be read (empty on PEB read failure).
struct ProcessInfo {
    DWORD pid = 0;
    std::wstring commandLine;
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

    // Local time as "yyyy-MM-dd HH:mm:ss" (used for runtime-history sessions).
    static std::string nowTimestamp();

    // Milliseconds between two "yyyy-MM-dd HH:mm:ss" timestamps (end - start).
    // Returns 0 when either timestamp cannot be parsed.
    static long long durationMsBetween(const std::string& start,
                                       const std::string& end);

private:
    // Test-only enumerator override (nullptr = use real process table).
    static EnumeratorFn g_testEnumerator_;
};

} // namespace proc

#endif // PROC_PROCESS_INSPECTOR_H