# Code Refactoring Phase 1 Specification

> **For agentic workers:** REQUIRED SUB-SKILL: Use writing-plans to implement this spec.

**Goal:** Clean up dead code, redundant includes, and merge duplicated logic to improve code quality and maintainability.

**Architecture:** Systematic cleanup across 4 independent areas: deletion, includes, duplication merging, and dead code removal.

**Tech Stack:** C++17, CMake, MinGW/GCC

---

## 1. Dead Code Detection

Based on task analysis:

| File | Issue | Action |
|------|-------|--------|
| `src/ProxyFinder_temp.cpp` | Backup file, appears obsolete | Delete |
| `src/ProxyFinder_part1.cpp` | Fragment/backup file | Delete |
| `src/ProxyFinder.cpp:366-368` | `removeProxyFromXray()` contains only commented code | Remove or implement |
| `src/ui/MainFrame.cpp:673` | Commented-out `syncDatabasesAsync` call | Remove or document |

## 2. Redundant/Unused Includes

| File | Line | Issue | Action |
|------|------|-------|--------|
| `include/ProxyFinder.h:5` | 5 | `#include <curl/curl.h>` redundant with CurlEasyHandle.h | Remove |
| `include/CurlEasyHandle.h:8` | 8 | `#include <utility>` not used | Remove if unused |

## 3. Duplicated Logic

### 3.1 Network Validation
- `src/ConfigGenerator.cpp:52-80` - `isValidNetwork()`
- `src/SubitemUpdaterV2.cpp:51-65` - Identical function

**Action:** Merge into shared utility in `include/Utils.h`

### 3.2 Config Type to Protocol String
- `src/ShareLink.cpp:485-499` - `getConfigTypeName()`
- `src/ProxyFinder.cpp:23-34` - `configTypeToProtocol()`

**Action:** Merge into `include/ProxyTypeUtils.h` or similar shared location

## 4. Large Functions (>100 lines)

| File | Lines | Function | Recommendation |
|------|-------|----------|----------------|
| `src/SubitemUpdaterV2.cpp` | 2438 | `parseSubscription()` | Split by protocol type |
| `src/AppController.cpp` | 1004 | Multiple large methods | Extract helpers |
| `src/ProxyBatchTester.cpp` | 451 | `workerThreadFunc()` | Split into phases |
| `src/ShareLink.cpp` | 502 | `vmessToUri()`, etc. | Extract shared logic |
| `src/ConfigGenerator.cpp` | 706 | `buildStreamSettings()` | Split by network type |
| `src/ConfigReader.cpp` | 637 | `load()` | Extract section parsers |

## 5. Files with Excessive Responsibilities

| File | Issues |
|------|--------|
| `src/SubitemUpdaterV2.cpp` | Subscription fetching, proxy parsing, deduplication, database sync, Xray config generation |
| `src/AppController.cpp` | Config management, database switching, testing, find operations, export |
| `src/MainFrame.cpp` | UI construction, event handling, network monitor, tray icon logic mixed |

**Recommendation:** For Phase 1, focus only on cleanup (dead code, includes, simple duplicates). Major refactoring deferred to future phases.