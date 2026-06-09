---
title: "refactor: downgrade diagnostic log level from INFO/DEBUG to TRACE"
type: refactor
status: completed
date: 2026-06-01
---

# Diagnostic Log Level Adjustment Report

## Summary

All `[DIAG]`-prefixed log messages were downgraded from `LogLevel::INFO` or `LogLevel::DEBUG` to `LogLevel::TRACE`. These messages are development/debugging aids and should not appear in default `INFO`-level log output during normal application use.

## Changes

| File | Lines | Old Level | New Level | Description |
|------|-------|-----------|-----------|-------------|
| `src/ui/ProxyListPanel.cpp` | 78, 85, 89, 98, 103 | `INFO` | `TRACE` | Diagnostics during `loadProxies()`: proxy/exItem counts, model reset/GetCount |
| `src/ui/AppController.cpp` | 184, 191, 195 | `INFO` | `TRACE` | Diagnostics during `loadProxies()`: total/filtered counts, subid mismatch dump |
| `src/ui/ProxyListModel.cpp` | 72, 92 | `DEBUG` | `TRACE` | `detectIdOffset()` result and `GetCount()` value logging |

## Not Changed

`WARN`-level `[DIAG]` messages in `ProxyListModel.cpp` (lines 111, 115, 126, 296) were **not** changed — they indicate real data-inconsistency states (out-of-bounds access attempts or invalid model state) that deserve visibility at WARN level.

## Test Results

- [x] Build successful (Debug mode, Ninja)
- [x] All 5 unit test suites passed (CurlEasyHandle 1, Dedup 11, Logger 14, ShareLink 11, ConfigGenerator 3)
- [x] Zero `[DIAG]` log messages at `INFO` or `DEBUG` level remain in source tree

## Commit

- `ec56a31` — "refactor: downgrade diagnostic log level from INFO/DEBUG to TRACE"
