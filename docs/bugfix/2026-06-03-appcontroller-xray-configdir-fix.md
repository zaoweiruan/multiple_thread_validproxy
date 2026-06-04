# Bug Fix: AppController Empty Config Directory in XrayManager Calls

## Problem Description
`AppController::findFirstProxy()`, `findBestProxy()`, `doFindFirstProxy()`, and `doFindBestProxy()` passed an empty string `""` for the `configDir` parameter to `XrayManager::getInstance()`. This caused xray configuration JSON files (`xray_config_<port>.json`) to be generated in the current working directory instead of the executable's config directory.

## Root Cause Analysis
In `XrayInstance.cpp` line 12:
```cpp
configPath_ = configDir + "/xray_config_" + std::to_string(socksPort) + ".json";
```

When `configDir` is empty, this produces a relative path like `xray_config_1080.json` instead of `bin/config/xray_config_1080.json`.

**Working example** in `ProxyBatchTester.cpp` line 17:
```cpp
std::string configDir = exeBaseDir + "/config";
xrayManager_ = XrayManager::getInstance(config_.xray_executable, configDir, config_.xray_workers);
```

## Files Modified

| File | Change |
|------|--------|
| `src/ui/AppController.cpp` | Added `configDir` parameter to 4 `XrayManager::getInstance()` calls |

## Fix Details

### AppController.cpp

#### findFirstProxy() (lines 353-359)
```cpp
ProxyFinder::TestResult AppController::findFirstProxy() {
    std::string xrayPath = config_.xray_executable;
    std::string configDir = utils::getExecutableDir() + "/config";  // Added
    XrayManager* manager = XrayManager::getInstance(xrayPath, configDir, config_.xray_workers);
    ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms);
    finder.findFirstWorkingProxy();
    return finder.getLastResult();
}
```

#### findBestProxy() (lines 361-368)
```cpp
ProxyFinder::TestResult AppController::findBestProxy() {
    std::string xrayPath = config_.xray_executable;
    std::string configDir = utils::getExecutableDir() + "/config";  // Added
    XrayManager* manager = XrayManager::getInstance(xrayPath, configDir, config_.xray_workers);
    ProxyFinder finder(db_, manager, xrayPath, config_.test_url, "", config_.test_timeout_ms);
    finder.findWorkingProxy();
    return finder.getLastResult();
}
```

#### doFindFirstProxy() (line 684)
```cpp
std::string xrayPath = config_.xray_executable;
std::string configDir = utils::getExecutableDir() + "/config";  // Added
XrayManager* manager = XrayManager::getInstance(xrayPath, configDir, config_.xray_workers);
```

#### doFindBestProxy() (line 749)
```cpp
std::string xrayPath = config_.xray_executable;
std::string configDir = utils::getExecutableDir() + "/config";  // Added
XrayManager* manager = XrayManager::getInstance(xrayPath, configDir, config_.xray_workers);
```

## Additional Fix: Test Output Directory

CMakeLists.txt updated to output test executables to `tests/` directory instead of `bin/`:
- `test_curl_easy_handle` → tests/
- `test_dedup` → tests/
- `test_logger` → tests/
- `test_sharelink` → tests/
- `test_config_generator` → tests/
- `test_delete_subscription` → tests/

Main executables (`validproxy.exe`, `validproxy-cli.exe`) remain in `bin/`.

## Verification
- Build: ✅ Success (CMake + Ninja Debug)
- Tests: ✅ 6/6 passed (50 test cases)