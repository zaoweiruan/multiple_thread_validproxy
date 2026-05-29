---
title: "Fix log file NUL header corruption caused by duplicate ofstream"
type: fix
status: completed
date: 2026-05-09
origin: "User reported: log file starts with NUL bytes before actual content"
---

# Fix log file NUL header corruption caused by duplicate ofstream

## Summary

Remove redundant `std::ofstream logOut` creation in `main.cpp` that opens the same file path as the Logger's internal stream, causing file truncation interleaving and NUL byte padding at log file start.

## Root Cause

In `src/main.cpp`, two blocks create a separate `std::ofstream logOut` with `std::ios::trunc`:

| Block | Logger creates | Redundant `logOut` creates | Effect |
|-------|---------------|---------------------------|--------|
| `dedup` (L628-633) | `dedup_{timestamp}.log` | `dedup_{timestamp}.log` | Second open truncates Logger's file → Logger writes at stale offset → OS pads NUL |
| `singleSubId` (L671-677) | `{commandMode}_{timestamp}.log` | `{commandMode}_{timestamp}.log` | Same corruption |

Both use `time(nullptr)` for the timestamp, so when `Logger::init()` and the `logOut` constructor run within the same second, filenames collide, the second `trunc` resets the file, and Logger's `outFile_` stream has a stale write position beyond end-of-file → OS fills gap with `\0`.

## Scope

- `src/main.cpp` — Remove redundant `logOut` creation in dedup and singleSubId blocks
- `include/SubitemUpdaterV2.h` — Optional: deprecate unused `logOut_` member

## Changes

### U1. `src/main.cpp` — dedup block (L628-633)

**Remove** (4 lines):
```cpp
char timestamp[32];
time_t now = time(nullptr);
strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));
std::string logFileName = "dedup_" + std::string(timestamp) + ".log";
std::string logFile = (logDir / logFileName).string();
std::ofstream logOut(logFile, std::ios::out | std::ios::trunc);
```

Change to:
```cpp
update::SubitemUpdaterV2 subUpdaterV2(db, appConfig->xray_executable, *appConfig, nullptr, exeDir);
```

### U2. `src/main.cpp` — singleSubId block (L671-679)

**Remove** (7 lines):
```cpp
char timestamp[32];
time_t now = time(nullptr);
strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));
std::string logFileName = commandMode + "_" + std::string(timestamp) + ".log";
std::string logFile = (logDir / logFileName).string();
std::ofstream logOut(logFile, std::ios::out | std::ios::trunc);
std::cout << "Log file: " << logFile << std::endl;
```

Log file path printing should use Logger's own path instead:
```cpp
std::cout << "Log file: " << (logDir / (Logger::getPrefix() + "_" + std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())) + ".log")).string() << std::endl;
```
OR simply remove the line and rely on Logger's own timestamped filename.

## Verification

1. Build: `cmake --build . --parallel 8` — 0 errors, 0 new warnings
2. Run `./bin/validproxy.exe -T <subid>` — check log file in `bin/log/` has clean header (no NUL bytes)
3. Run `./bin/validproxy.exe -D` (dedup) — check log file has clean header
4. Run `ctest -V` — all tests pass
