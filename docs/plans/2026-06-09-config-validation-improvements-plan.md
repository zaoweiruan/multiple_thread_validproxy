# Config Validation Improvements Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Address all 5 improvement suggestions from the ConfigReader validation analysis report (§9), making configuration parsing more robust, testable, and user-friendly.

**Architecture:** Five independent tasks target `include/ConfigReader.h`, `src/ConfigReader.cpp`, `src/ui/AppController.cpp`, and `src/ui/MainFrame.cpp`. The unit-test task requires making `load()` testable by decoupling the Windows MessageBox calls — all other tasks are straightforward single-file changes.

**Tech Stack:** C++17, boost::json, wxWidgets (for GUI save notification), Google Test (for unit tests), Windows API (MessageBoxA).

**References:**
- Report: `docs/reports/2026-06-09-Reports-Validproxy-ConfigReader-Validation-v1.0.md`
- Bugfix record: `docs/bugfix/2026-06-09-configreader-load-error-popup-fix.md`

---

### Task 1: Add in-class initializers to AppConfig for all fields

**Files:**
- Modify: `include/ConfigReader.h:9-43`

**Problem:** Several `AppConfig` fields lack in-class initializers. If a code path creates an `AppConfig` without calling `load()`, int and bool fields have undefined values. The missing in-class initializers are:

| Field | Type | Should Default To |
|-------|------|-------------------|
| `xray_workers` | int | `1` |
| `xray_start_port` | int | `1083` |
| `xray_api_port` | int | `0` |
| `test_timeout_ms` | int | `5000` |
| `log_enabled` | bool | `true` |
| `log_network_failures` | bool | `false` |
| `dedup_enabled` | bool | `true` |
| `dedup_after_update` | bool | `false` |
| `blacklist_threshold` | int | `5` |
| `notification_enabled` | bool | `false` |
| `notification_on_update` | bool | `false` |
| `notification_on_test` | bool | `false` |

- [ ] **Step 1: Add in-class initializers**

Edit `include/ConfigReader.h`, lines 14-38. Add initializers to all fields that currently lack them:

```
int xray_workers = 1;
int xray_start_port = 1083;
int xray_api_port = 0;
int test_timeout_ms = 5000;
bool log_enabled = true;
bool log_network_failures = false;
bool dedup_enabled = true;
bool dedup_after_update = false;
int blacklist_threshold = 5;
bool notification_enabled = false;
bool notification_on_update = false;
bool notification_on_test = false;
```

Note: The `dedup_enabled` default in `ConfigReader::load()` line 223 is also `true`, but there's a discrepancy — when the `dedup` section exists but `enabled` field is missing, the else at line 193 sets it to `false`. This is intentional (section present but field absent = false). The in-class initializer covers the case where no load() was ever called, so `true` is appropriate there.

- [ ] **Step 2: Build and run all tests**

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
if ($LASTEXITCODE -eq 0) { ctest -V } else { Write-Host "Build failed" }
```

Expected: Build succeeds, all 6 tests pass (no behavior change, only safety net).

- [ ] **Step 3: Commit**

```bash
git add include/ConfigReader.h
git commit -m "fix: add in-class initializers to all AppConfig fields"
```

---

### Task 2: Log WARN when JSON field has wrong type in load()

**Files:**
- Modify: `src/ConfigReader.cpp:74-264`

**Problem:** When `ConfigReader::load()` encounters a JSON field with an unexpected type (e.g., `"workers": "abc"` when `int64` expected), the field is silently skipped and default value applied. User has no indication their config has an invalid entry.

**Strategy:** For every field that checks `&& field.is_*()`, add a WARN log when the key exists but has the wrong type. Do NOT change behavior, only add logging.

- [ ] **Step 1: Add a logging helper macro/function**

Add at the top of `ConfigReader::load()` body (after `AppConfig config;`), a small lambda for consistent warning messages:

```cpp
auto warnWrongType = [&](const std::string& section, const std::string& field, const std::string& expected) {
    Logger::write("WARNING: config." + section + "." + field + " has wrong type (expected " + expected + "), using default", LogLevel::WARN);
};
```

- [ ] **Step 2: Add WARN calls for all type-mismatch branches**

For each field extraction in the load function, add the WARN call in the `else` branch when key exists but wrong type.

**database.path** (line 83):
Change from:
```cpp
if (db.contains("path") && db["path"].is_string()) {
    config.database_path = resolvePath(db["path"].as_string().c_str(), exeDir);
}
```
To:
```cpp
if (db.contains("path") && db["path"].is_string()) {
    config.database_path = resolvePath(db["path"].as_string().c_str(), exeDir);
} else if (db.contains("path")) {
    warnWrongType("database", "path", "string");
}
```

Apply the same pattern to ALL fields inside each section. Here's the complete list of fields that need the else-if branch:

**database section** (lines 82-90):
- `db.contains("path") && db["path"].is_string()`
- `db.contains("sql") && db["sql"].is_string()`
- `db.contains("sql_by_subid") && db["sql_by_subid"].is_string()`

**xray section** (lines 94-114):
- `xray.contains("executable") && xray["executable"].is_string()`
- `xray.contains("workers") && xray["workers"].is_int64()`
- `xray.contains("start_port") && xray["start_port"].is_int64()`
- `xray.contains("api_port") && xray["api_port"].is_int64()`

**test section** (lines 116-127):
- `test.contains("url") && test["url"].is_string()`
- `test.contains("timeout_ms") && test["timeout_ms"].is_int64()`

**log section** (lines 129-156):
- `log.contains("enabled") && log["enabled"].is_bool()`
- `log.contains("network_failures") && log["network_failures"].is_bool()`
- `log.contains("console_level") && log["console_level"].is_string()`
- `log.contains("file_level") && log["file_level"].is_string()`

**subscription section** (lines 158-187):
- `sub.contains("priority_mode") && sub["priority_mode"].is_string()`
- `sub.contains("check_auto_update_interval") && sub["check_auto_update_interval"].is_bool()`
- `sub.contains("connect_timeout_ms") && sub["connect_timeout_ms"].is_int64()`
- `sub.contains("timeout_ms") && sub["timeout_ms"].is_int64()`

**dedup section** (lines 189-228):
- `dedup.contains("enabled") && dedup["enabled"].is_bool()`
- `dedup.contains("dedup_after_update") && dedup["dedup_after_update"].is_bool()`
- `dedup.contains("blacklist_threshold") && dedup["blacklist_threshold"].is_int64()`
- `dedup.contains("blacklist_enabled") && dedup["blacklist_enabled"].is_bool()`
- `dedup.contains("blacklist_subid") && dedup["blacklist_subid"].is_string()`
- (subids array items already have `sid.is_string()` check — add WARN for non-string elements in the loop, line 217-219)

**notification section** (lines 230-251):
- `notification.contains("enabled") && notification["enabled"].is_bool()`
- `notification.contains("on_update") && notification["on_update"].is_bool()`
- `notification.contains("on_test") && notification["on_test"].is_bool()`

**sync section** (lines 253-264):
- `sync.contains("source_db") && sync["source_db"].is_string()`
- `sync.contains("target_db") && sync["target_db"].is_string()`
- `sync.contains("sync_skip_subids") && sync["sync_skip_subids"].is_bool()`

Also add section-level type warnings: When `obj.contains("xray")` but `!obj["xray"].is_object()`, etc. (lines 94, 116, 129, 158, 189, 230, 253).

- [ ] **Step 3: Build and run all tests**

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
if ($LASTEXITCODE -eq 0) { ctest -V } else { Write-Host "Build failed" }
```

Expected: Build succeeds, all tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/ConfigReader.cpp
git commit -m "feat: log WARN when config JSON field has wrong type"
```

---

### Task 3: Check xray_executable file existence in load()

**Files:**
- Modify: `src/ConfigReader.cpp:300-316` (near the DB file existence check)

**Problem:** `xray_executable` is only validated at runtime (when XrayManager tries to spawn the process). If the path is wrong, the user gets a non-obvious error. Add an existence check at config load time with a WARN log.

- [ ] **Step 1: Add xray_executable existence check**

Add after the database file existence check (after line 316, before the final return) in `ConfigReader::load()`:

```cpp
if (!config.xray_executable.empty()) {
    std::filesystem::path xrayPath(config.xray_executable);
    if (!std::filesystem::exists(xrayPath)) {
        Logger::write("WARNING: xray executable not found: " + config.xray_executable, LogLevel::WARN);
    }
}
```

Note: This is only a WARN — we do NOT fail config loading because the user might set the correct path later via the GUI config dialog, or use CLI modes that don't need xray.

- [ ] **Step 2: Build and run all tests**

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
if ($LASTEXITCODE -eq 0) { ctest -V } else { Write-Host "Build failed" }
```

Expected: Build succeeds, all tests pass.

- [ ] **Step 3: Commit**

```bash
git add src/ConfigReader.cpp
git commit -m "feat: warn when xray executable not found at config load"
```

---

### Task 4: Notify user when saveConfig() fails in GUI

**Files:**
- Modify: `src/ui/MainFrame.cpp:652-696`

**Problem:** `AppController::saveConfig()` can fail (returns false), but `MainFrame::onMenuEditConfig()` ignores the return value at line 652 and 696. User never knows if config persistence failed.

- [ ] **Step 1: Check return value at line 652 and show error**

Change lines 650-652 from:
```cpp
config::AppConfig cfg = configDialog_->getConfig();
std::string oldDbPath = config_.database_path;
controller_->saveConfig(cfg);
```
To:
```cpp
config::AppConfig cfg = configDialog_->getConfig();
std::string oldDbPath = config_.database_path;
bool saveOk = controller_->saveConfig(cfg);
if (!saveOk) {
    wxMessageBox("Failed to save configuration to file.\n"
                 "Your changes may not persist after restart.",
                 "Save Error", wxOK | wxICON_WARNING);
}
```

- [ ] **Step 2: Check return value at line 696 and show error**

Change line 696 from:
```cpp
controller_->saveConfig(cfg);
```
To:
```cpp
bool restoreOk = controller_->saveConfig(cfg);
if (!restoreOk) {
    wxMessageBox("Failed to restore previous database path in config file.\n"
                 "You may need to manually edit config.json.",
                 "Save Error", wxOK | wxICON_WARNING);
}
```

- [ ] **Step 3: Build and run all tests**

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
if ($LASTEXITCODE -eq 0) { ctest -V } else { Write-Host "Build failed" }
```

Expected: Build succeeds, all tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/ui/MainFrame.cpp
git commit -m "feat: show warning when config save fails in GUI"
```

---

### Task 5: Add unit tests for ConfigReader::load()

**Files:**
- Create: `tests/test_config_reader_load.cpp` (new file to keep existing test_config_reader.cpp clean)
- Modify: `CMakeLists.txt:289-292`
- Modify: `src/ConfigReader.cpp` (refactor MessageBoxA calls behind a test-overridable function)

**Problem:** `ConfigReader::load()` cannot be unit tested because it calls `MessageBoxA()` (Windows GUI API). Tests that trigger error paths would pop up blocking dialogs. We need to decouple the UI from the parsing/validation logic.

**Strategy:** Replace direct `MessageBoxA` calls in `load()` with a call to a static function pointer / callback that can be replaced in tests. The default implementation shows MessageBoxA. Tests replace it with a no-op.

- [ ] **Step 1: Add a test-overridable error handler**

In `include/ConfigReader.h`, add a public static member:

```cpp
class ConfigReader {
public:
    // Error reporting hook — defaults to MessageBoxA. Tests can replace to suppress popups.
    using ErrorReporter = void(*)(const std::string& title, const std::string& message);
    static ErrorReporter errorReporter_;
    
    static std::optional<AppConfig> load(const std::string& configPath);
    static bool save(const std::string& configPath, const AppConfig& config);
    static std::string getDefaultConfigPath();
    // ...
};
```

In `src/ConfigReader.cpp`, replace all 4 `MessageBoxA` calls:

```cpp
// At top, default implementation
ConfigReader::ErrorReporter ConfigReader::errorReporter_ = [](const std::string& title, const std::string& message) {
    MessageBoxA(NULL, message.c_str(), title.c_str(), MB_ICONERROR | MB_OK);
};
```

Replace each MessageBoxA call (lines 38, 57, 66, 313) with:
```cpp
errorReporter_("Configuration Error", errMsg);
// or for DB Error:
errorReporter_("Database Error", errMsg);
```

**Important:** Keep the `#include <windows.h>` since the default reporter uses it. Tests will provide a reporter that doesn't.

- [ ] **Step 2: Write the new test file**

Create `tests/test_config_reader_load.cpp`:

```cpp
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include "test_utils.h"
#include "ConfigReader.h"

using namespace config;

class ConfigReaderLoadTest : public ::testing::Test {
protected:
    TempDir tempDir_;

    void SetUp() override {
        // Suppress MessageBoxA during tests — store original and replace
        originalReporter_ = ConfigReader::errorReporter_;
        ConfigReader::errorReporter_ = [](const std::string&, const std::string&) {
            // no-op: suppress popup
        };
    }

    void TearDown() override {
        ConfigReader::errorReporter_ = originalReporter_;
    }

    void writeConfig(const std::string& filename, const std::string& content) {
        std::ofstream file(tempDir_.path() + "/" + filename);
        file << content;
        file.close();
    }

    std::string configPath(const std::string& filename) {
        return tempDir_.path() + "/" + filename;
    }

private:
    ConfigReader::ErrorReporter originalReporter_;
};

// We need an actual DB file for valid configs; create empty file for load tests
static void touchFile(const std::string& path) {
    std::ofstream f(path);
    f.close();
}

TEST_F(ConfigReaderLoadTest, FileNotFound) {
    auto result = ConfigReader::load(configPath("nonexistent.json"));
    EXPECT_FALSE(result.has_value());
}

TEST_F(ConfigReaderLoadTest, InvalidJsonSyntax) {
    writeConfig("bad.json", "{invalid json here}");
    auto result = ConfigReader::load(configPath("bad.json"));
    EXPECT_FALSE(result.has_value());
}

TEST_F(ConfigReaderLoadTest, NotAJsonObject) {
    writeConfig("arr.json", "[1, 2, 3]");
    auto result = ConfigReader::load(configPath("arr.json"));
    EXPECT_FALSE(result.has_value());
}

TEST_F(ConfigReaderLoadTest, EmptyConfigWithDefaultDb) {
    // Empty object — all defaults, but missing DB path means no file check
    writeConfig("empty.json", "{}");
    auto result = ConfigReader::load(configPath("empty.json"));
    // Should succeed because database_path is empty (no file check)
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->xray_workers, 1);
    EXPECT_EQ(result->xray_start_port, 1083);
    EXPECT_EQ(result->test_timeout_ms, 5000);
    EXPECT_EQ(result->log_console_level, "INFO");
    EXPECT_EQ(result->log_file_level, "DEBUG");
    EXPECT_EQ(result->priority_mode, "direct_first");
    EXPECT_TRUE(result->dedup_enabled);       // section missing → true
    EXPECT_TRUE(result->blacklist_enabled);    // section missing → true
}

TEST_F(ConfigReaderLoadTest, MissingDbFile) {
    writeConfig("nodbfile.json", R"({
        "database": "/nonexistent/path/db.db"
    })");
    auto result = ConfigReader::load(configPath("nodbfile.json"));
    EXPECT_FALSE(result.has_value());
}

TEST_F(ConfigReaderLoadTest, FullConfigRoundTrip) {
    // Create a DB file that actually exists
    std::string dbPath = tempDir_.path() + "/test.db";
    touchFile(dbPath);

    writeConfig("full.json", R"({
        "database": ")" + dbPath + R"(",
        "xray": {
            "executable": "xray.exe",
            "workers": 4,
            "start_port": 2080,
            "api_port": 2081
        },
        "test": {
            "url": "https://example.com",
            "timeout_ms": 3000
        },
        "log": {
            "enabled": true,
            "network_failures": false,
            "console_level": "INFO",
            "file_level": "DEBUG"
        },
        "subscription": {
            "priority_mode": "proxy_first",
            "check_auto_update_interval": true,
            "connect_timeout_ms": 5000,
            "timeout_ms": 20000
        },
        "dedup": {
            "enabled": true,
            "dedup_after_update": true,
            "blacklist_threshold": 10,
            "blacklist_enabled": true,
            "blacklist_subid": "bl_sub",
            "subids": ["sub1", "sub2"]
        },
        "notification": {
            "enabled": true,
            "on_update": true,
            "on_test": false
        },
        "sync": {
            "source_db": "src.db",
            "target_db": "dst.db",
            "sync_skip_subids": true
        }
    })");
    auto result = ConfigReader::load(configPath("full.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->database_path, dbPath);
    EXPECT_EQ(result->xray_executable, "xray.exe");
    EXPECT_EQ(result->xray_workers, 4);
    EXPECT_EQ(result->xray_start_port, 2080);
    EXPECT_EQ(result->xray_api_port, 2081);
    EXPECT_EQ(result->test_url, "https://example.com");
    EXPECT_EQ(result->test_timeout_ms, 3000);
    EXPECT_TRUE(result->log_enabled);
    EXPECT_EQ(result->log_console_level, "INFO");
    EXPECT_EQ(result->priority_mode, "proxy_first");
    EXPECT_TRUE(result->check_auto_update_interval);
    EXPECT_EQ(result->subscription_connect_timeout_ms, 5000);
    EXPECT_EQ(result->subscription_timeout_ms, 20000);
    EXPECT_TRUE(result->dedup_enabled);
    EXPECT_TRUE(result->dedup_after_update);
    EXPECT_EQ(result->blacklist_threshold, 10);
    EXPECT_EQ(result->blacklist_subid, "bl_sub");
    EXPECT_EQ(result->dedup_subids.size(), 2);
    EXPECT_EQ(result->dedup_subids[0], "sub1");
    EXPECT_EQ(result->dedup_subids[1], "sub2");
    EXPECT_TRUE(result->notification_enabled);
    EXPECT_TRUE(result->notification_on_update);
    EXPECT_FALSE(result->notification_on_test);
    EXPECT_EQ(result->sync.source_db, "src.db");
    EXPECT_EQ(result->sync.target_db, "dst.db");
    EXPECT_TRUE(result->sync.sync_skip_subids);
}

TEST_F(ConfigReaderLoadTest, DatabaseAsObject) {
    std::string dbPath = tempDir_.path() + "/test.db";
    touchFile(dbPath);

    writeConfig("dbobj.json", R"({
        "database": {
            "path": ")" + dbPath + R"(",
            "sql": "SELECT * FROM profiles",
            "sql_by_subid": "SELECT * FROM profiles WHERE subid = '{subid}'"
        }
    })");
    auto result = ConfigReader::load(configPath("dbobj.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->database_path, dbPath);
    EXPECT_EQ(result->sql_query, "SELECT * FROM profiles");
    EXPECT_EQ(result->sql_by_subid, "SELECT * FROM profiles WHERE subid = '{subid}'");
}

TEST_F(ConfigReaderLoadTest, WrongTypeDefaults) {
    // Test that wrong types produce defaults
    writeConfig("wrong.json", R"({
        "xray": {
            "workers": "not-an-int",
            "start_port": true,
            "api_port": "bad"
        }
    })");
    auto result = ConfigReader::load(configPath("wrong.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->xray_workers, 1);        // default from clamping
    EXPECT_EQ(result->xray_start_port, 1083);   // default from clamping
    EXPECT_EQ(result->xray_api_port, 0);        // default from in-class initializer
}

TEST_F(ConfigReaderLoadTest, ClampingOutOfRange) {
    writeConfig("clamp.json", R"({
        "xray": {
            "workers": -5,
            "start_port": 0
        },
        "test": {
            "timeout_ms": -100
        }
    })");
    auto result = ConfigReader::load(configPath("clamp.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->xray_workers, 1);         // clamped from -5
    EXPECT_EQ(result->xray_start_port, 1083);    // clamped from 0
    EXPECT_EQ(result->test_timeout_ms, 5000);    // clamped from -100
}

TEST_F(ConfigReaderLoadTest, UnknownSqlPlaceholderWarning) {
    // Placeholder detection doesn't fail, just logs — test parsing works
    writeConfig("placeholder.json", R"({
        "database": {
            "sql": "SELECT * FROM {unknown_placeholder}"
        }
    })");
    auto result = ConfigReader::load(configPath("placeholder.json"));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->sql_query, "SELECT * FROM {unknown_placeholder}");
}
```

- [ ] **Step 3: Register the test in CMakeLists.txt**

Replace the commented-out lines 289-292 in `CMakeLists.txt`:

```cmake
add_executable(test_config_reader tests/test_config_reader.cpp tests/test_config_reader_load.cpp src/ConfigReader.cpp src/Logger.cpp)
target_include_directories(test_config_reader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_config_reader PRIVATE gtest_main gtest D:/boost_1_88_0/lib/libboost_json-mgw14-mt-x64-1_88.a)
add_test(NAME ConfigReaderTest COMMAND test_config_reader)
```

Note: If the old commented block had `#` lines, replace the entire 4-line block with the above (removing the `#` comment markers).

- [ ] **Step 4: Build and run the new test**

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
ctest -R ConfigReaderTest -V
```

Expected: All test cases pass (10+ test cases from both test files).

- [ ] **Step 5: Run all tests to ensure no regression**

```powershell
ctest -V
```

Expected: All 7+ tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/ConfigReader.h src/ConfigReader.cpp tests/test_config_reader_load.cpp CMakeLists.txt
git commit -m "test: add unit tests for ConfigReader::load() with refactored error reporter"
```

---

## Execution Order

Tasks are **independent** — they modify different parts of the code:
- **Task 1** → `include/ConfigReader.h` only
- **Task 2** → `src/ConfigReader.cpp` only (field parsing section)
- **Task 3** → `src/ConfigReader.cpp` only (post-parse section)
- **Task 4** → `src/ui/MainFrame.cpp` only
- **Task 5** → `include/ConfigReader.h`, `src/ConfigReader.cpp`, `tests/test_config_reader_load.cpp`, `CMakeLists.txt`

Tasks 1-4 can be done in any order. Task 5 touches the same files as Tasks 1-3 but changes are additive (they won't conflict if done in order 1→2→3→5, or Task 5 last after all others).

**Recommended order:** 1 → 2 → 3 → 4 → 5
