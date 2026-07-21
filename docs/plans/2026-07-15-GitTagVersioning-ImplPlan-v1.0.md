# Git Tag Versioning Implementation Plan

> **For agentic workers:** Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace hardcoded version strings with Git-tag-derived version numbers at CMake configure time, generated into `include/version.h`.

**Architecture:** CMake `execute_process(git describe ...)` extracts version from Git tag → 4-level fallback chain → `configure_file(version.h.in → version.h)` → consumers (About dialog, startup logger) read `#define` macros.

**Tech Stack:** CMake 3.20+, Git, C++17, wxWidgets

---

### Task 1: Create `cmake/GetGitVersion.cmake`

**Files:**
- Create: `cmake/GetGitVersion.cmake`

- [ ] **Write `cmake/GetGitVersion.cmake`**

Content: Function `get_git_version()` that attempts 3 strategies:
  1. `git describe --tags --dirty --always` → parse SemVer with optional `-<N>-g<hash>` and `-dirty`
  2. Hash-only fallback → `0.0.0` + commit hash
  3. Unknown → `0.0.0` + `"unknown"`

Sets `VERSION_MAJOR`, `VERSION_MINOR`, `VERSION_PATCH`, `VERSION_TAG`, `VERSION_COMMIT`, `VERSION_DIRTY`, `VERSION_AHEAD` in PARENT_SCOPE.

### Task 2: Create `include/version.h.in`

**Files:**
- Create: `include/version.h.in`

- [ ] **Write `include/version.h.in`**

Template with 11 macros: `APP_VERSION_MAJOR`, `APP_VERSION_MINOR`, `APP_VERSION_PATCH`, `APP_VERSION`, `APP_VERSION_FULL`, `APP_GIT_TAG`, `APP_GIT_COMMIT`, `APP_GIT_DIRTY`, `APP_BUILD_TYPE`, `APP_BUILD_TIME`, `APP_NAME`.

### Task 3: Modify `CMakeLists.txt`

**Files:**
- Modify: `CMakeLists.txt:2-13` (after project() line)

- [ ] **Insert Git version detection block**

Add after `project(...)`:
- `include(cmake/GetGitVersion.cmake)`
- `get_git_version()`
- Conditional `set(PROJECT_VERSION_* ...)` override
- `string(TIMESTAMP BUILD_TIMESTAMP ...)`
- `configure_file(version.h.in → version.h)`

### Task 4: Update `.gitignore`

**Files:**
- Modify: `.gitignore`

- [ ] **Add `/include/version.h` exclusion**

Add after `!/include/**` to re-ignore the auto-generated version header.

### Task 5: Refactor About dialog in `MainFrame.cpp`

**Files:**
- Modify: `src/ui/MainFrame.cpp:903-905`

- [ ] **Replace hardcoded About text with `version.h` macros**

Add `#include "version.h"` at top, replace `"validproxy v1.0\n..."` with dynamic string using `APP_NAME`, `APP_VERSION`, `APP_VERSION_FULL`, `APP_GIT_TAG`, `APP_GIT_COMMIT`, `APP_BUILD_TYPE`, `APP_BUILD_TIME`, `__VERSION__`.

### Task 6: Add startup version log

**Files:**
- Modify: `src/main_gui.cpp`
- Modify: `src/main_cli.cpp`

- [ ] **Add `#include "version.h"` and version log at Logger initialization**

In `main_gui.cpp`: after Logger::init and config load, log version info at REPORT level.
In `main_cli.cpp`: after Logger::init in `runDefaultTest()`, log version info at REPORT level.

### Task 7: Build & Verify

- [ ] **CMake configure + build**
```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
```

- [ ] **Run tests**
```powershell
ctest -V
```
