# -TA / -test-all CLI Parameter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `-TA` / `-test-all` CLI parameter to run full proxy test (`ProxyBatchTester::run()`) in CLI mode.

**Architecture:** Extract the existing full-test code from lines 831-887 of `main.cpp` into a named `runDefaultTest()` function; add arg parsing for `-TA`; add a command dispatch branch; replace the fallback code path with the function call.

**Tech Stack:** C++17, main.cpp (single file change)

---

### Task 1: Extract `runDefaultTest()` function

**Files:**
- Modify: `src/main.cpp` (anonymous namespace, before `main()`)

This task extracts lines 831-887 into a `runDefaultTest()` static function, using a `logMode` parameter for the log prefix. All needed dependencies (`openDatabase`, `logInfo`, `logError`, `g_xrayManager`) are in the same anonymous namespace.

- [ ] **Step 1: Add `runDefaultTest()` function to the anonymous namespace**

Insert this complete function right before `main()` (after line 113, before `int main`):

```cpp
static int runDefaultTest(const std::string& configPath, const std::string& exeDir,
                          const std::filesystem::path& logDir, const std::string& logMode) {
    auto appConfig = config::ConfigReader::load(configPath);
    if (!appConfig) {
        logError("Failed to load config from: " + configPath);
        Logger::close();
        return 1;
    }

    Logger::init(logDir.string(), logMode);
    Logger::setFileEnabled(appConfig->log_enabled);
    Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
    Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
    logInfo("validproxy starting...");
    logInfo("Config loaded from: " + configPath);
    logInfo("Database path: " + appConfig->database_path);

    int numWorkers = appConfig->xray_workers;
    int startPort = appConfig->xray_start_port;

    logInfo("Workers: " + std::to_string(numWorkers));
    logInfo("Start port: " + std::to_string(startPort));

    auto startTime = std::chrono::system_clock::now();
    time_t startTimeT = std::chrono::system_clock::to_time_t(startTime);
    char startTimeStr[32];
    strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %H:%M:%S", localtime(&startTimeT));

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&startTimeT));

    std::cout << "Log file: " << Logger::getLogDir() << "/" << Logger::getPrefix() << "_" << timestamp << ".log" << std::endl;

    sqlite3* db = nullptr;
    if (!openDatabase(*appConfig, db, "[main] default")) {
        Logger::close();
        return 1;
    }
    sqlite3_busy_timeout(db, 5000);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    std::cout << "Database opened: " << appConfig->database_path << std::endl;

    ProxyBatchTester tester(db, *appConfig, exeDir);
    g_xrayManager = tester.getXrayManager();
    bool testResult = tester.run();

    if (appConfig->notification_enabled && appConfig->notification_on_test) {
        utils::sendNotification("Proxy Test Complete", testResult ? "All tests completed successfully" : "Some tests failed");
    }

    if (g_xrayManager) {
        XrayManager::release();
    }

    sqlite3_close(db);
    Logger::close();

    std::cout << "validproxy " << (testResult ? "finished" : "failed") << std::endl;
    return testResult ? 0 : 1;
}
```

- [ ] **Step 2: Verify the file is syntactically correct**

Run: `cmake --build build --parallel 8 2>&1 | head -50` and check for compile errors.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "refactor: extract runDefaultTest() function from fallback test code"
```

---

### Task 2: Add `-TA` / `-test-all` CLI parameter support

**Files:**
- Modify: `src/main.cpp`

This task adds arg parsing for `-TA`, adds a command dispatch branch for `commandMode == "test-all"`, replaces the fallback code at lines 831-887 with a function call, and updates the help text.

- [ ] **Step 1: Add arg parsing — insert after `-TU` / `-tourl` handler**

In the arg parsing loop (after line 179), insert:

```cpp
} else if (arg == "-TA" || arg == "-test-all" || arg == "--test-all") {
    commandMode = "test-all";
```

- [ ] **Step 2: Add command dispatch — insert before the GUI check**

Before line 248 (`bool shouldLaunchGui = ...`), insert:

```cpp
if (commandMode == "test-all") {
    return runDefaultTest(configPath, exeDir, logDir, "test-all");
}
```

- [ ] **Step 3: Replace fallback code with function call**

Replace lines 831-887 (the block from `auto appConfig = config::ConfigReader::load(configPath);` to `return testResult ? 0 : 1;`) with:

```cpp
return runDefaultTest(configPath, exeDir, logDir, "test");
```

- [ ] **Step 4: Update help text**

In `printHelp()`, after the `-T, -test-sub <id>` line (line 80), add:

```cpp
<< "  -TA, -test-all      Test all proxies in the database\n"
```

- [ ] **Step 5: Build and verify**

```bash
cmake --build build --parallel 8
```

Expected: clean compilation with no errors or warnings.

- [ ] **Step 6: Run unit tests**

```bash
ctest -V
```

Expected: all existing tests pass (no regressions).

- [ ] **Step 7: Quick functional verification**

```bash
./build/validproxy --help
```

Expected: help text shows `-TA, -test-all` line.

```bash
./build/validproxy --test-all
```

Expected: starts full proxy test in CLI mode (will run if a valid config and DB exist in context).

- [ ] **Step 8: Commit**

```bash
git add src/main.cpp
git commit -m "feat: add -TA / -test-all CLI parameter for full proxy test"
```
