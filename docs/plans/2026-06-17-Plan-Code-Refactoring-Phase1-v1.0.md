# Code Refactoring Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use executing-plans or subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor codebase by removing dead code, cleaning redundant includes, and merging duplicated logic.

**Architecture:** Four independent cleanup tasks executed sequentially with verification between each.

**Tech Stack:** C++17, CMake, MinGW/GCC, GoogleTest

---

## Task 1: Delete Dead Code Files

**Files:**
- Delete: `src/ProxyFinder_temp.cpp`
- Delete: `src/ProxyFinder_part1.cpp`

- [ ] **Step 1: Verify files are not referenced in CMake**
  Run: `Get-Content CMakeLists.txt | Select-String -Pattern "ProxyFinder_temp|ProxyFinder_part1"`
  Expected: No matches

- [ ] **Step 2: Delete the files**
  ```powershell
  Remove-Item -LiteralPath 'src/ProxyFinder_temp.cpp'
  Remove-Item -LiteralPath 'src/ProxyFinder_part1.cpp'
  ```

- [ ] **Step 3: Build and verify**
  Run: `cmake --build build --parallel 8`
  Expected: Clean build, no errors

---

## Task 2: Clean Redundant Includes

**Files:**
- Modify: `include/ProxyFinder.h` - Remove `#include <curl/curl.h>` if redundant

- [ ] **Step 1: Verify CurlEasyHandle.h usage**
  Check: `Get-Content include/CurlEasyHandle.h | Select-String -Pattern "curl/"`

- [ ] **Step 2: Remove redundant include if confirmed**
  Edit: Remove line 5 if `#include <curl/curl.h>` is unnecessary

- [ ] **Step 3: Build and verify**
  Run: `cmake --build build --parallel 8`

---

## Task 3: Merge isValidNetwork Duplicate

**Files:**
- Create: `include/IsValidNetwork.h` - Shared utility declaration
- Modify: `src/Utils.cpp` - Add shared implementation
- Modify: `src/ConfigGenerator.cpp` - Remove local function, include shared
- Modify: `src/SubitemUpdaterV2.cpp` - Remove local function, include shared

- [ ] **Step 1: Write failing test for shared utility**
  Create test in `tests/test_utils.cpp` for `isValidNetwork()` function

- [ ] **Step 2: Build and verify test fails**
  Run: `cmake --build build --parallel 8 && .\tests\test_utils.exe --gtest_filter=*IsValidNetwork*`

- [ ] **Step 3: Implement shared utility**
  Add to `include/Utils.h`:
  ```cpp
  bool isValidNetwork(const std::string& network);
  ```
  Add to `src/Utils.cpp` the implementation from `ConfigGenerator.cpp`

- [ ] **Step 4: Update callers**
  Remove local `isValidNetwork()` from both files, add `#include "Utils.h"`

- [ ] **Step 5: Build and verify tests pass**
  Run: `cmake --build build --parallel 8 && .\tests\test_utils.exe`

---

## Task 4: Merge Proxy Type String Mapping

**Files:**
- Create: `include/ProxyTypeStrings.h` - Shared enum-to-string mapping
- Modify: `src/ShareLink.cpp` - Remove `getConfigTypeName()`
- Modify: `src/ProxyFinder.cpp` - Remove `configTypeToProtocol()`

- [ ] **Step 1: Analyze both implementations**
  Compare `ShareLink.cpp:485-499` and `ProxyFinder.cpp:23-34` for differences

- [ ] **Step 2: Create shared header**
  Add to `include/ProxyTypeStrings.h`:
  ```cpp
  namespace ProxyTypeStrings {
      std::string_view protocolName(int configType);
  }
  ```

- [ ] **Step 3: Update both files to use shared function**
  Replace local functions with calls to shared utility

- [ ] **Step 4: Build and verify**
  Run: `cmake --build build --parallel 8`