---
title: "Fix Logger::write() after Logger::close() in 3 branches"
type: fix
status: completed
date: 2026-05-09
origin: "Agent audit: tourl, dedup, singleSubId branches call Logger::write() after Logger::close(), silently losing final REPORT messages"
---

# Fix Logger::write() after Logger::close() in 3 branches

## Summary

Three branches in `main.cpp` call `Logger::write()` **after** `Logger::close()`, meaning the final REPORT-level completion/failure message is silently discarded (since `close()` sets `enabled_ = false`, and `write()` returns immediately when `!enabled_`).

## Root Cause

Both `sync` (L549-550) and `import-sub` (L599-601) correctly order the calls:
```
Logger::write(result ? "completed" : "failed", LogLevel::REPORT);  // write first
Logger::close();                                                     // close after
```

But three branches reversed them:
```
Logger::close();                                                     // close → disabled
Logger::write(result ? "completed" : "failed", LogLevel::REPORT);  // write → silently dropped
```

## Scope

`src/main.cpp` — 3 blocks, each swapping the order of `Logger::write()` and `Logger::close()`.

## Changes

### U1. tourl block (L512-515)

```cpp
// Before:
sqlite3_close(db);
Logger::close();
Logger::write(result ? "completed" : "failed", LogLevel::REPORT);
return result ? 0 : 1;

// After:
Logger::write(result ? "completed" : "failed", LogLevel::REPORT);
sqlite3_close(db);
Logger::close();
return result ? 0 : 1;
```

### U2. dedup block (L635-638)

```cpp
// Before:
sqlite3_close(db);
Logger::close();
Logger::write(result ? "completed" : "failed", LogLevel::REPORT);
return result ? 0 : 1;

// After:
Logger::write(result ? "completed" : "failed", LogLevel::REPORT);
sqlite3_close(db);
Logger::close();
return result ? 0 : 1;
```

### U3. singleSubId block (L695-698)

```cpp
// Before:
sqlite3_close(db);
Logger::close();
Logger::write(result ? "completed" : "failed", LogLevel::REPORT);
return result ? 0 : 1;

// After:
Logger::write(result ? "completed" : "failed", LogLevel::REPORT);
sqlite3_close(db);
Logger::close();
return result ? 0 : 1;
```

## Verification

1. Build: `cmake --build . --parallel 8` — 0 errors, 0 new warnings
2. Run `ctest -V` — all tests pass
3. For each fixed branch, the final `[REPORT] completed/failed` line should appear in the log file AFTER the fix, and be absent BEFORE the fix.
