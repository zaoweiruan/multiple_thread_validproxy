# -TA / -test-all CLI Parameter — Design

**Date**: 2026-06-01
**Status**: Approved

## Summary

Add `-TA` / `-test-all` CLI parameter to run the default proxy test (all proxies in the database) in CLI mode, executing `ProxyBatchTester::run()`.

## Background

Currently there is no CLI way to run a full proxy test on all proxies in the database. The `-T` flag tests a specific subscription (`-T <subId>`), while the default test code (lines 831-887 in `main.cpp`) only runs as a fallback path that is effectively unreachable in CLI mode because a blank `commandMode` causes the GUI to launch first.

## Design

### Changes

**Single file**: `src/main.cpp`

### 1. New function `runDefaultTest`

Extract lines 831-887 into a named function in the anonymous namespace:

```cpp
static int runDefaultTest(const std::string& configPath, const std::string& exeDir,
                          const std::filesystem::path& logDir, const std::string& logMode);
```

Parameters:
- `configPath` — path to config.json
- `exeDir` — executable directory (for xray path resolution)
- `logDir` — log directory (filesystem::path)
- `logMode` — log prefix string (e.g., "test-all" or "test")

**Note**: `Logger::init`, `openDatabase`, `logInfo`, `logError`, and `g_xrayManager` are all in the anonymous namespace scope already, so the function has access.

### 2. Arg parsing

Add to the argument loop (around line 178):

```cpp
} else if (arg == "-TA" || arg == "-test-all" || arg == "--test-all") {
    commandMode = "test-all";
}
```

### 3. Command dispatch

Before the GUI check (line 250), add:

```cpp
if (commandMode == "test-all") {
    return runDefaultTest(configPath, exeDir, logDir, "test-all");
}
```

### 4. Replace fallback code

Replace lines 831-887 with:

```cpp
return runDefaultTest(configPath, exeDir, logDir, "test");
```

### 5. Help text

Add line after `-T` entry in `printHelp()`:

```
<< "  -TA, -test-all      Test all proxies in the database\n"
```

### Data Flow

```
$ validproxy -TA
  → commandMode = "test-all"
  → commandMode non-empty → skip GUI launch
  → match commandMode == "test-all"
  → runDefaultTest(configPath, exeDir, logDir, "test-all")
     → load config.json
     → init Logger (prefix = "test-all")
     → open database
     → create ProxyBatchTester(db, config, exeDir)
     → tester.run()  // tests ALL proxies using config.sql_query
     → send notification if configured
     → XrayManager::release()
     → close DB + Logger
     → return 0/1
```

### Compatibility

- No-arg → GUI (unchanged)
- `-TA` / `-test-all` → CLI full test (new)
- `-T <subId>` → subscription-specific test (unchanged)
- Fallback path still uses log prefix `"test"` for backward compatibility

## Implementation notes

- The `logMode` parameter distinguishes CLI-invoked (`"test-all"`) vs fallback (`"test"`) for log file naming
- No new dependencies or files
- Total change: ~25 lines added, ~55 lines moved
