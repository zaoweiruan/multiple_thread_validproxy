---
title: "Add LogLevel::REPORT for summary/statistics output"
type: feat
status: completed
date: 2026-05-09
origin: "User request: 新增统计级别，日志功能完成后的统计信息"
---

# Add LogLevel::REPORT for summary/statistics output

## Summary

Add a new `LogLevel::REPORT = 5` (highest value) dedicated to summary/statistics output that always appears after an operation completes. Migrate all existing summary output calls (`printSummary()`, update summary, dedup summary, `"completed"/"failed"` messages) from `LogLevel::INFO` to `LogLevel::REPORT`, so they are visually distinct from routine INFO-level log messages and remain visible regardless of console_level settings.

---

## Problem

Currently all summary/statistics output uses `LogLevel::INFO`, making them:

1. **Indistinguishable** from routine operation logs — no way to filter or highlight final results
2. **Not guaranteed visible** on console — if console_level were raised above INFO, summaries would disappear
3. **Mixed in log files** — no easy way to grep for final results

---

## Requirements

- **R1**: Add `LogLevel::REPORT = 5` to the enum (highest value, always passes `>=` filter)
- **R2**: Support string conversion (`REPORT` ↔ `"REPORT"`) in Logger
- **R3**: Change all existing summary/statistics Logger calls to use `LogLevel::REPORT`
- **R4**: Must continue to write to both console and file (same as other levels)
- **R5**: Default `console_level=INFO(2)` and `file_level=DEBUG(1)` → REPORT always visible
- **R6**: Configurable: setting `log_console_level: 5` in config.json can hide REPORT output
- **R7**: Must compile on Windows/MinGW-w64 with C++17

---

## Scope Boundaries

- Modify: `include/Logger.h`, `src/Logger.cpp`, `src/ProxyBatchTester.cpp`, `src/SubitemUpdaterV2.cpp`, `src/main.cpp`
- NOT modify: `src/ProxyFinder.cpp`, `src/XrayApi.cpp` (no summary output)

---

## Change Inventory

### U1. `include/Logger.h` — Add `REPORT = 5` to enum

```cpp
enum class LogLevel {
    TRACE  = 0,
    DEBUG  = 1,
    INFO   = 2,
    WARN   = 3,
    ERR    = 4,
    REPORT = 5   // ← NEW: summary/statistics output
};
```

### U2. `src/Logger.cpp` — String conversion support

**`levelToString()`** — add case:
```cpp
case LogLevel::REPORT: return "REPORT";
```

**`stringToLevel()`** — add:
```cpp
if (s == "report") return LogLevel::REPORT;
```

### U3. `src/ProxyBatchTester.cpp` — Upgrade `printSummary()` to REPORT

| Line | Current | Target |
|------|---------|--------|
| ~200 | `Logger::write("========================")` | `Logger::write("========================", LogLevel::REPORT)` |
| ~201 | `Logger::write("Total: " + ...)` | `Logger::write("Total: " + ..., LogLevel::REPORT)` |
| ~202 | `Logger::write("Success: " + ...)` | `Logger::write("Success: " + ..., LogLevel::REPORT)` |
| ~203 | `Logger::write("Failed: " + ...)` | `Logger::write("Failed: " + ..., LogLevel::REPORT)` |
| ~205 | `Logger::write("========================")` | `Logger::write("========================", LogLevel::REPORT)` |

### U4. `src/SubitemUpdaterV2.cpp` — Upgrade summary blocks to REPORT

**`run()` summary** (lines ~236-252):
```
Logger::write("========================================", LogLevel::REPORT);
Logger::write("REPORT: Update Summary", LogLevel::REPORT);
Logger::write("REPORT: Total subscriptions: " + ..., LogLevel::REPORT);
Logger::write("REPORT: Phase 1 (Direct) - Success: " + ..., LogLevel::REPORT);
Logger::write("REPORT: Phase 2 (Proxy) - Success: " + ..., LogLevel::REPORT);
Logger::write("REPORT: Total - Success: " + ..., LogLevel::REPORT);
Logger::write("========================================", LogLevel::REPORT);
```

**`deduplicate()` summary** (lines ~1748-1794):
```
Logger::write("REPORT: Total proxies before: ...", LogLevel::REPORT);
Logger::write("REPORT: Phase 1 completed: ...", LogLevel::REPORT);
Logger::write("REPORT: Phase 2 completed: ...", LogLevel::REPORT);
Logger::write("REPORT: Total deleted: ...", LogLevel::REPORT);
Logger::write("REPORT: Total remaining: ...", LogLevel::REPORT);
```

**`importSubitemsFromFile()` summary** (lines ~2033-2058):
```
Logger::write("REPORT: Total lines: ...", LogLevel::REPORT);
Logger::write("REPORT: Success: ...", LogLevel::REPORT);
Logger::write("REPORT: Failed ...: ...", LogLevel::REPORT);
```

### U5. `src/main.cpp` — Upgrade final result messages to REPORT

| Approx Line | Current | Target |
|-------------|---------|--------|
| 515 | `logInfo("completed")` | `Logger::write("completed", LogLevel::REPORT)` |
| 549 | `logInfo("sync completed")` | `Logger::write("sync completed", LogLevel::REPORT)` |
| 599 | `logInfo("import completed")` | `Logger::write("import completed", LogLevel::REPORT)` |
| 644 | `logInfo("dedup completed")` | `Logger::write("dedup completed", LogLevel::REPORT)` |
| 717 | `logInfo(result ? "completed" : "failed")` | `Logger::write(result ? "completed" : "failed", LogLevel::REPORT)` |

---

## Verification

- Build: `cmake --build . --parallel 8`
- Console should show `[REPORT]` prefixed lines for all summary output
- Log file should contain `[REPORT]` prefixed lines
- Setting `log_console_level: 5` in config.json should hide REPORT messages from console
- Existing `INFO`/`ERR` logs remain unaffected

---

## Risks

| Risk | Mitigation |
|------|------------|
| `stringToLevel("5")` not handled | User must use string "report" in config; if numeric 5 is used, it currently falls through default → INFO. Can add a numeric path if needed. |
| Developers add summary output at INFO instead of REPORT | Code review convention |
