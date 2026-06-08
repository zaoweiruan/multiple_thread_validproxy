# Bug Fix: SubitemUpdaterV2 Hardcoded `"bin/config"` Path Fallback

## Problem Description

`SubitemUpdaterV2::getProxyPorts()` used a hardcoded relative path `"bin/config"` as fallback when `baseDir_` was empty. In GUI mode, all call sites (`AppController`) pass `""` for `baseDir_`, causing the config directory to resolve relative to the current working directory (CWD) instead of the executable's directory. When CWD differs from the project root, this creates or looks up `bin/config/` in the wrong location.

## Root Cause

In `SubitemUpdaterV2.cpp` line 1213:
```cpp
std::string configDir = baseDir_.empty() ? "bin/config" : baseDir_ + "/config";
```

The `"bin/config"` literal is a relative path anchored to CWD, not to the executable location. All other components in the codebase (AppController, main_cli, ProxyBatchTester, ConfigReader) correctly use `utils::getExecutableDir() + "/config"` to derive the config directory from the executable's location.

### Call Chain (GUI mode)

```
AppController methods (findFirstProxy, findBestProxy, etc.)
  → SubitemUpdaterV2(baseDir_ = "")
    → getProxyPorts()
      → baseDir_.empty() == true
        → configDir = "bin/config"  // relative to CWD, WRONG
```

### Working References

| Location | Code | Status |
|----------|------|--------|
| `AppController.cpp` (4 places) | `utils::getExecutableDir() + "/config"` | ✅ Correct |
| `main_cli.cpp` (2 places) | `exeBaseDir / "config"` | ✅ Correct |
| `ProxyBatchTester.cpp:17` | `exeBaseDir + "/config"` | ✅ Correct |
| `SubitemUpdaterV2.cpp:1213` | `"bin/config"` (hardcoded relative) | ❌ Broken |

## Files Modified

| File | Change |
|------|--------|
| `src/SubitemUpdaterV2.cpp` | Replaced `"bin/config"` with `utils::getExecutableDir() + "/config"` |

## Fix Details

### SubitemUpdaterV2.cpp (line 1213)

```diff
-    std::string configDir = baseDir_.empty() ? "bin/config" : baseDir_ + "/config";
+    std::string configDir = baseDir_.empty() ? utils::getExecutableDir() + "/config" : baseDir_ + "/config";
```

`utils::getExecutableDir()` is declared in `include/Utils.h` (line 8), which is already `#include`d in `SubitemUpdaterV2.cpp` (line 8). The implementation uses `GetModuleFileNameA(NULL, buffer, MAX_PATH)` on Windows to retrieve the executable's directory path.

## Verification

- Build: ✅ Success (both `validproxy.exe` and `validproxy-cli.exe`)
- Tests: ✅ 6/6 passed (CurlEasyHandleTest, DedupTest, LoggerTest, ShareLinkTest, ConfigGeneratorTest, DeleteSubscriptionTest)
- No remaining hardcoded `"bin/config"` strings in `.cpp`/`.h` files

## Scope Analysis

An audit of the entire codebase confirmed that this was the **only** instance of hardcoded `"bin/config"`:

- `AppController.cpp` — 4 occurrences, all correctly using `utils::getExecutableDir() + "/config"` (no change needed)
- `main_cli.cpp` — 2 occurrences, both using `exeBaseDir / "config"` with `exeBaseDir = utils::getExecutableDir()` (no change needed)
- `ProxyBatchTester.cpp` — 1 occurrence, uses caller-provided `exeBaseDir + "/config"` (no change needed)
- `SubitemUpdaterV2.cpp` — 1 occurrence, the only hardcoded relative path (fixed)
