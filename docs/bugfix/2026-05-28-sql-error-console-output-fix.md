---
title: "fix: SQL errors output to console instead of log window in GUI mode"
type: fix
status: completed
date: 2026-05-28
---

# SQL Error Console Output Fix

## Bug Summary

When launching `validproxy -ui` with an invalid database path or non-database file, the error "SQL错误: file is not a database" was output directly to the console via `std::cerr` instead of being routed to the log window (LogPanel).

## Root Cause Analysis

In GUI mode (main.cpp line 259), `Logger::setConsoleEnabled(false)` is called to suppress console output and route all messages to the LogPanel. However, several DAO methods in header files bypass this mechanism by using `std::cerr` directly:

| File | Line | Method | Issue |
|------|------|--------|-------|
| `include/Subitem.h` | 149 | `SubitemDAO::getAll()` | Uses `std::cerr` |
| `include/Subitem.h` | 167 | `SubitemDAO::getEnabledSubscriptions()` | Uses `std::cerr` |
| `include/ProfileExItem.h` | 98 | `ProfileExItemDAO::getAll()` | Uses `std::cerr` |
| `include/Profileitem.h` | 325 | `ProfileitemDAO::getAll()` | Uses `std::cerr` |
| `include/DatabaseHelper.h` | 30, 42 | `Database::open()` / `Database::execute()` | Uses `std::cerr` |

Additionally, 4 more `std::cerr` SQL error calls were found and fixed on **2026-05-29**:

| File | Line | Method | Issue |
|------|------|--------|-------|
| `include/ProfileExItem.h` | 137, 153 | `updateDelay()` / `updateDelay()` | English SQL error via `std::cerr` |
| `include/Subitem.h` | 184, 212 | `updateEnabled()` / `updateSubitem()` | English SQL error via `std::cerr` |

These direct `std::cerr` calls ignore the Logger's console enable/disable state.

## Fix Implementation

Replaced all `std::cerr` SQL error output with `Logger::write()` calls:

```cpp
// Before
std::cerr << "SQL错误: " << sqlite3_errmsg(db_) << std::endl;

// After
Logger::write("SQL错误: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
```

Also added `#include "Logger.h"` to each affected file since they did not previously include it.

## Call Flow in UI Mode

```
main(): openDatabase() (config.database_path)
  → sqlite3_open() returns SQLITE_OK for any file (doesn't validate format)
  → SubitemDAO::getAll() called
     → sqlite3_prepare_v2() fails with "file is not a database"
     → BEFORE: std::cerr outputs to console (bypasses Logger)
     → AFTER: Logger::write() respects setConsoleEnabled(false)
```

Note: `sqlite3_open()` succeeds for any file path; the actual format validation occurs at `sqlite3_prepare_v2()` when SQL is attempted. This is when the error appears.

## Test Results

- [x] Build successful (Debug mode, Ninja)
- [x] All 3 unit test suites passed
- [x] GUI launches correctly
- [x] SQL errors now appear in LogPanel (via Logger) instead of console
- [x] No `std::cerr` SQL error calls remain in any header or source file
- [x] UI mode produces zero console output (stdout/stderr empty) with invalid database
- [x] CLI mode shows errors properly formatted via Logger: `[2026-05-29 08:35:03] [ERROR] SQL错误: file is not a database`

## Commit

`db9f01a` - "fix: redirect SQL errors to Logger instead of std::cerr"