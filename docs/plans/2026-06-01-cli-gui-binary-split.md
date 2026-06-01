# CLI/GUI Binary Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the single hybrid `validproxy.exe` into two separate executables — `validproxy.exe` (GUI, WIN32 subsystem, no CLI code) and `validproxy-cli.exe` (CLI, CONSOLE subsystem, no GUI code).

**Architecture:** The existing `main.cpp` is renamed to `main_cli.cpp` with all `#ifdef HAS_WXWIDGETS` blocks stripped out. A new `main_gui.cpp` provides a minimal GUI-only entry point. CMakeLists.txt defines both targets sharing the same core source files but with different subsystems, link libraries, and compile definitions.

**Tech Stack:** C++17, CMake 3.20, wxWidgets 3.2, MinGW + Ninja

---

## File Structure

```
Files to CREATE:
  src/main_gui.cpp            — New GUI entry point (minimal)
  docs/design/cli-gui-binary-split-design.md  — Already written

Files to MODIFY:
  src/main.cpp → src/main_cli.cpp  — Rename and strip GUI code
  CMakeLists.txt                    — Add validproxy-cli target, refactor

Files to KEEP (no changes):
  src/ui/*              — GUI-only, only linked to validproxy target
  src/*.cpp (core)      — Shared between both targets
  include/*.h           — Shared, no changes needed
  tests/*               — Unchanged, no wxWidgets dependency
```

---

## Implementation Tasks

### Task 1: Create `src/main_gui.cpp` — GUI-only entry point

**Files:** Create `src/main_gui.cpp`

- [ ] **Step 1: Write the new entry point**

```cpp
// src/main_gui.cpp — Pure GUI entry point (WIN32 subsystem)
// No CLI command handling. No console output (stdout/stderr are discarded by WIN32 subsystem).
#include "UIApp.h"
#include <wx/app.h>
#include <wx/msgdlg.h>

#include <cstdlib>
#include <string>
#include <vector>
#include <filesystem>

int main(int argc, char* argv[]) {
    // Only parse -c/--config for config path
    std::string configPath = "config.json";
    std::string exeDir;
    {
        std::filesystem::path p(argv[0]);
        exeDir = p.parent_path().lexically_normal().string();
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "-c" || arg == "--config") {
            if (++i < argc) {
                std::filesystem::path fp(argv[i]);
                if (fp.is_relative()) {
                    fp = std::filesystem::path(exeDir) / fp;
                }
                configPath = fp.lexically_normal().string();
            }
        }
        // -ui, --ui, --gui: silently accepted for backward compatibility
        // -H, --help: silently ignored (no console to output to)
        // All other unknown args: silently ignored
    }

    wxApp::SetInstance(new UIApp());
    return wxEntry(argc, argv);
}
```

- [ ] **Step 2: Verify file compiles in isolation (conceptual check)**

No command needed — `main_gui.cpp` will compile when linked into the GUI target.

---

### Task 2: Rename `src/main.cpp` to `src/main_cli.cpp` and strip GUI code

**Files:**
- Rename: `src/main.cpp` → `src/main_cli.cpp`
- Modify: `src/main_cli.cpp` (strip 3 blocks)

- [ ] **Step 1: Git rename main.cpp to main_cli.cpp**

```bash
git mv src/main.cpp src/main_cli.cpp
```

- [ ] **Step 2: Remove `#ifdef HAS_WXWIDGETS` include block (lines 32-34)**

Remove these lines:
```cpp
#ifdef HAS_WXWIDGETS
#include "UIApp.h"
#endif
```

- [ ] **Step 3: Remove `#ifdef HAS_WXWIDGETS` block from Ctrl+C handler (lines 96-108)**

Remove lines 96-108 (the `#ifdef HAS_WXWIDGETS ... #endif` inside `consoleCtrlHandler`). The resulting function should be:

```cpp
BOOL WINAPI consoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        std::cout << "\nCtrl+C detected, stopping xray instances..." << std::endl;
        if (g_xrayManager) {
            g_xrayManager->stopAll();
        }
        return TRUE;
    }
    return FALSE;
}
```

- [ ] **Step 4: Add `-ui`/`--ui` error handling for CLI binary**

In the argument parsing loop, find lines 219-222:
```cpp
} else if (arg == "-ui" || arg == "--ui") {
    forceGuiMode = true;
```
Replace with:
```cpp
} else if (arg == "-ui" || arg == "--ui") {
    std::cerr << "Error: GUI mode not available in CLI build.\n"
              << "Use validproxy.exe for GUI mode.\n";
    return 1;
```

- [ ] **Step 5: Handle `--gui` flag in CLI binary (was GUI worker flag)**

Find:
```cpp
} else if (arg == "--gui") {
    // Internal flag: GUI worker process, handled after arg parsing
}
```
Replace with:
```cpp
} else if (arg == "--gui") {
    std::cerr << "Error: GUI mode not available in CLI build.\n"
              << "Use validproxy.exe for GUI mode.\n";
    return 1;
```

- [ ] **Step 6: Remove `forceGuiMode` variable and `shouldLaunchGui` branch (lines 206-429)**

Remove:
- Line 206: `bool forceGuiMode = false;  // Set by -ui/--ui flag`
- Lines 321-429: The entire GUI branch:
  ```cpp
  // ------------------------------------------------------------------
  // GUI mode: default when no CLI mode specified, or when -ui/--ui is given
  // 启动后立即返回（非阻塞），不等待 GUI 关闭
  // ------------------------------------------------------------------
  bool shouldLaunchGui = forceGuiMode || commandMode.empty();
  
  if (shouldLaunchGui) {
  #ifdef HAS_WXWIDGETS
      ... (lines 328-419)
  #else
      ... (lines 420-428)
  #endif
  }
  ```

- [ ] **Step 7: Verify the untouched CLI dispatch logic remains intact**

After stripping, the remaining CLI dispatch code (lines 431-908) must be untouched:
```cpp
if (commandMode == "generator") { ... }
if (commandMode == "show-sub") { ... }
if (commandMode == "find-proxy" || commandMode == "findminproxy") { ... }
if (commandMode == "tourl") { ... }
if (commandMode == "sync") { ... }
if (commandMode == "import-sub") { ... }
if (commandMode == "dedup") { ... }
if (!singleSubId.empty()) { ... }
return runDefaultTest(...);
```

- [ ] **Step 8: Check that empty commandMode fallback shows help**

After removing the GUI branch, when `commandMode` is empty (no CLI args), the code should fall through to `printHelp()` at the end. Verify the code path:

```cpp
// In the stripped version:
// commandMode is empty → none of the if-checks match → falls through to:
// (check if this fallback exists, if not add it)
if (commandMode.empty()) {
    printHelp();
    return 0;
}
```

---

### Task 3: Update CMakeLists.txt for two targets

**Files:** Modify `CMakeLists.txt`

- [ ] **Step 1: Define shared core source list**

After line 28 (`set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ...)`), add:

```cmake
# Shared core sources (compiled into both CLI and GUI targets)
set(CORE_SOURCES
    src/ConfigGenerator.cpp
    src/ConfigReader.cpp
    src/Logger.cpp
    src/PortManager.cpp
    src/ProxyBatchTester.cpp
    src/ProxyFinder.cpp
    src/ProxyTester.cpp
    src/ShareLink.cpp
    src/SubitemUpdaterV2.cpp
    src/UrlFetcher.cpp
    src/Utils.cpp
    src/XrayApi.cpp
    src/XrayInstance.cpp
    src/XrayManager.cpp
)
```

- [ ] **Step 2: Change existing `add_executable(validproxy ...)` to use `main_cli.cpp` and create CLI target**

Replace the existing `add_executable(validproxy ...)` block (lines 30-46) with:

```cmake
# ===== CLI target (CONSOLE subsystem) =====
add_executable(validproxy-cli
    src/main_cli.cpp
    ${CORE_SOURCES}
)

target_link_libraries(validproxy-cli PRIVATE
    Threads::Threads
    CURL::libcurl
    E:/vcpkg/installed/x64-mingw-static/lib/libsqlite3.a
    D:/boost_1_88_0/lib/libboost_json-mgw14-mt-x64-1_88.a
)
# Console subsystem is default — no WIN32_EXECUTABLE property
```

- [ ] **Step 3: Create GUI target**

Before the `if(wxWidgets_FOUND)` check, or inside it (at the appropriate location), add:

```cmake
# ===== GUI target (WIN32 subsystem) =====
add_executable(validproxy
    src/main_gui.cpp
    ${UI_SOURCES}
    ${CORE_SOURCES}
)

target_link_libraries(validproxy PRIVATE
    Threads::Threads
    CURL::libcurl
    E:/vcpkg/installed/x64-mingw-static/lib/libsqlite3.a
    D:/boost_1_88_0/lib/libboost_json-mgw14-mt-x64-1_88.a
)

if(wxWidgets_FOUND)
    target_link_libraries(validproxy PRIVATE
        wx::wxbase wx::wxcore wx::wxadv wx::wxaui wx::wxhtml wx::wxpropgrid wx::wxnet
    )
    target_include_directories(validproxy PRIVATE "E:/vcpkg/installed/x64-mingw-dynamic/include")
    target_include_directories(validproxy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/ui)
    set_target_properties(validproxy PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_definitions(validproxy PRIVATE HAS_WXWIDGETS=1)
    copy_wx_dlls(validproxy)
endif()
```

- [ ] **Step 4: Remove the old `target_sources` and `target_link_libraries` for the combined target in the `wxWidgets_FOUND` block**

Inside the `if(wxWidgets_FOUND)` block, remove or replace lines 107-118 that add UI sources and wxWidgets links to the old combined target — this is now handled by the GUI target above.

Ensure the `if(wxWidgets_FOUND)` block still contains:
- The UI source list (for use by the GUI target)
- The `target_link_libraries(validproxy PRIVATE wx::...)` (already moved to step 3)
- The `target_compile_definitions(validproxy PRIVATE HAS_WXWIDGETS=1)`
- The `copy_wx_dlls(validproxy)`
- The resource file handling (`icons.rc`, `CMAKE_RC_FLAGS`)

- [ ] **Step 5: Update the `else()` branch for wxWidgets not found**

When wxWidgets is not found, only the CLI target should be available. The GUI target should still build but without wxWidgets support (it will fail at runtime if started).

Keep the fallback in the GUI target's `target_link_libraries` (it will link without wxWidgets libraries when not found).

---

### Task 4: Build and verify both targets

- [ ] **Step 1: Clean and configure**

```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
```
Expected: Configuration succeeds. Both `validproxy` and `validproxy-cli` targets are defined.

- [ ] **Step 2: Build both targets**

```bash
cmake --build build --parallel 8
```
Expected: Both targets compile and link successfully.

- [ ] **Step 3: Check CLI binary subsystem**

```bash
python3 -c "
import struct
with open(r'E:\eclipse_workspace\multiple_thread_validproxy\bin\validproxy-cli.exe', 'rb') as f:
    d = f.read()
e_lfanew = struct.unpack('<I', d[0x3C:0x40])[0]
subsys = struct.unpack('<H', d[e_lfanew+4+20+68:e_lfanew+4+20+70])[0]
print(f'CLI subsystem: {subsys} ({\"WINDOWS_CUI\" if subsys == 3 else \"WINDOWS_GUI\" if subsys == 2 else \"OTHER\"})')
"
```
Expected: `CLI subsystem: 3 (WINDOWS_CUI)`

- [ ] **Step 4: Check GUI binary subsystem**

```bash
python3 -c "
import struct
with open(r'E:\eclipse_workspace\multiple_thread_validproxy\bin\validproxy.exe', 'rb') as f:
    d = f.read()
e_lfanew = struct.unpack('<I', d[0x3C:0x40])[0]
subsys = struct.unpack('<H', d[e_lfanew+4+20+68:e_lfanew+4+20+70])[0]
print(f'GUI subsystem: {subsys} ({\"WINDOWS_CUI\" if subsys == 3 else \"WINDOWS_GUI\" if subsys == 2 else \"OTHER\"})')
"
```
Expected: `GUI subsystem: 2 (WINDOWS_GUI)`

- [ ] **Step 5: Verify CLI binary does not link wxWidgets**

```bash
strings bin/validproxy-cli.exe | grep -i "wx" | head -5
```
Expected: No output or only false positives (wx is not in the binary).

- [ ] **Step 6: Verify CLI binary starts correctly for a simple command**

```bash
bin/validproxy-cli.exe --help
```
Expected: Prints help text.

- [ ] **Step 7: Verify CLI binary rejects -ui/--ui**

```bash
bin/validproxy-cli.exe -ui
```
Expected: `Error: GUI mode not available in CLI build. Use validproxy.exe for GUI mode.`

- [ ] **Step 8: Quick blocking test for CLI binary**

```cmd
echo Start: %TIME% && bin\validproxy-cli.exe -FMIN && echo End: %TIME%
```
Expected: Time difference reflects search duration (cmd blocks until finish).

---

### Task 5: Run full test suite

- [ ] **Step 1: Build test targets**

```bash
cmake --build build --parallel 8
```

- [ ] **Step 2: Run all tests**

```bash
ctest -V
```
Expected: All 5+ tests pass (CurlEasyHandleTest, DedupTest, LoggerTest, ShareLinkTest, ConfigGeneratorTest).

- [ ] **Step 3: Fix any build or test failures**

If any target fails to compile:
1. Check the CMakeLists.txt target definitions for missing source files or link libraries
2. Check that `main_cli.cpp` was correctly stripped (no lingering `#ifdef HAS_WXWIDGETS`)
3. Verify `main_gui.cpp` has all necessary includes

---

### Task 6: Commit

- [ ] **Step 1: Stage all changes**

```bash
git add src/main_gui.cpp
git add src/main_cli.cpp    # git already tracks the rename
git add CMakeLists.txt
git add docs/design/cli-gui-binary-split-design.md
git add docs/plans/2026-06-01-cli-gui-binary-split.md
```

- [ ] **Step 2: Verify staged files**

```bash
git status
```
Expected: Shows rename `src/main.cpp → src/main_cli.cpp`, new file `src/main_gui.cpp`, modified `CMakeLists.txt`, new design/plan docs.

- [ ] **Step 3: Commit with descriptive message**

```bash
git commit -m "refactor: split validproxy into separate CLI and GUI binaries

Root cause: WIN32_EXECUTABLE TRUE set PE subsystem to WINDOWS_GUI,
causing cmd.exe to return immediately instead of blocking for CLI modes.

Changes:
- Rename main.cpp -> main_cli.cpp, strip all #ifdef HAS_WXWIDGETS blocks
- Create main_gui.cpp as pure GUI entry point (minimal, no CLI code)
- Define validproxy-cli target (CONSOLE subsystem, no wxWidgets)
- Define validproxy target (WIN32 subsystem, with wxWidgets)
- Both targets share core source files via CORE_SOURCES CMake variable
- Add -ui/--ui error message for CLI binary

Design: docs/design/cli-gui-binary-split-design.md
Plan: docs/plans/2026-06-01-cli-gui-binary-split.md"
```
