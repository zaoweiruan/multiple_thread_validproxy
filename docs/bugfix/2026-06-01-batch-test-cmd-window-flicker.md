---
title: "fix: batch test CMD window flicker in GUI mode"
type: fix
status: completed
date: 2026-06-01
---

# Batch Test CMD Window Flicker Fix

## Bug Summary

When running batch proxy testing in GUI mode, a CMD (console) window repeatedly appears and closes, causing visible screen flicker. This occurs for every API call made by each worker thread during the test cycle.

## Root Cause

The application is built with `WIN32_EXECUTABLE TRUE` (GUI subsystem), which means the parent process has no console attached. However, `XrayApi.cpp` used `_popen()` for all 5 Xray API operations:

| Function | Line | Original Call |
|----------|------|---------------|
| `runCommand()` | 59 | `_popen("cmd /c xray api ...", "r")` |
| `listOutbounds()` | 93 | `_popen("cmd /c xray api ...", "r")` |
| `ping()` | 111 | `_popen("cmd /c xray api ...", "r")` |
| `addOutbound()` | 172 | `_popen("cmd /c echo JSON \| xray api ...", "r")` |

In a GUI subsystem process, `_popen()` invokes `cmd.exe` via `CreateProcess` without `CREATE_NO_WINDOW`, causing Windows to create a temporary console window for each `cmd.exe` instance. During batch testing with 4 worker threads, each proxy being tested triggers 3+ API calls (removeOutbound×2, addOutbound×1, plus retries), resulting in dozens of CMD windows rapidly appearing and closing.

## Fix Implementation

Replaced all `_popen()` calls with a new static helper `runProcess()` in `XrayApi.cpp`:

```cpp
static bool runProcess(const std::string& exePath,
                       const std::vector<std::string>& args,
                       std::string& output,
                       const std::string& stdinData = "")
{
    // Build command line
    std::string cmdLine = "\"" + exePath + "\"";
    for (const auto& a : args) cmdLine += " \"" + a + "\"";

    // Set up pipes and process
    HANDLE hStdoutRd, hStdoutWr, hStdinRd = nullptr, hStdinWr = nullptr;
    // ... CreatePipe + CreateProcessA(CREATE_NO_WINDOW) ...
    // Read stdout into output string
    // Optionally write stdinData to stdin pipe
    // Wait for process to exit
    // Close all handles
}
```

Key change: `CreateProcessA(..., CREATE_NO_WINDOW, ...)` — the `CREATE_NO_WINDOW` flag prevents any console window from being created for the child process.

### Function Replacements

| Function | Old | New |
|----------|-----|-----|
| `runCommand(query)` | `_popen("cmd /c xray api query ...")` | `runProcess(xrayExe, {"api", "query", ...}, output)` |
| `listOutbounds()` | `_popen("cmd /c xray api ...")` | `runProcess(xrayExe, {"api", "adapter", "listOutbounds", ...}, output)` |
| `ping()` | `_popen("cmd /c xray version")` | `runProcess(xrayExe, {"version"}, output)` |
| `addOutbound()` | `_popen("cmd /c echo JSON \| xray api adapter addOutbound ...", "w")` | `runProcess(xrayExe, {"api", "adapter", "addOutbound", ...}, output, jsonData)` via stdin pipe |

The `addOutbound()` function previously used shell piping (`echo JSON | xray.exe ...`), which required shell involvement. Now it passes JSON data directly through a stdin pipe, eliminating the need for `cmd.exe` entirely.

## Call Flow During Batch Test

```
GUI worker (4 threads)
  ↓
XrayApi::listOutbounds()    — runProcess(CREATE_NO_WINDOW)
XrayApi::removeOutbound()   — runProcess(CREATE_NO_WINDOW)
XrayApi::removeOutbound()   — runProcess(CREATE_NO_WINDOW)
XrayApi::addOutbound()      — runProcess(CREATE_NO_WINDOW, stdin pipe)
  ↓
No console window created for any of these calls
```

## Test Results

- [x] Build successful (Debug mode, Ninja)
- [x] All 5 unit test suites passed (CurlEasyHandle 1, Dedup 11, Logger 14, ShareLink 11, ConfigGenerator 3)
- [x] No `_popen()` calls remain in `XrayApi.cpp`
- [x] No new compiler warnings introduced
- [x] Xray API functionality verified: all 5 operations preserve the same stdout/stdin protocol

## Commits

- `2970f98` — "fix: replace _popen() with CreateProcessA(CREATE_NO_WINDOW) in XrayApi.cpp to prevent CMD window flicker during batch testing"
