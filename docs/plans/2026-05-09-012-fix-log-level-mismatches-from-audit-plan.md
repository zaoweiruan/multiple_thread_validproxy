---
title: "fix: Fix log level mismatches found by log audit - network=INFO, WARN=usage, CONFIG_ERROR=ERR"
type: fix
status: superseded
date: 2026-05-09
origin: Log file audit of bin/log/
---

# fix: Fix log level mismatches found by log audit

## Summary

Log audit of `bin/log/` revealed 3 systematic log level violations and 1 missing level usage. This plan fixes:

1. **Network failures in ProxyFinder** still using `ERR` instead of `INFO` (found 328× in findminproxy log)
2. **CONFIG_ERROR / XRAY_ERROR missing explicit LogLevel** in some code paths (default to INFO, should be ERR)
3. **WARN level never used** — add to appropriate boundary cases
4. **Missing error detail on sync failure** — add Logger::write before failing

---

## Problem

### P1: ProxyFinder network errors still at ERR level

`bin/log/findminproxy_20260508_165826.log` contains 328 ERROR entries:
- 223 timeouts
- 105 SSL connection errors

These are pure network failures and should be INFO per established standard. The previous fix (`2026-05-08-003`) was supposed to downgrade them, but the audit confirms they're still at ERR in the running binary.

### P2: CONFIG_ERROR and XRAY_ERROR without explicit LogLevel (defaults to INFO)

`bin/log/test_20260507_151356.log` — CONFIG_ERROR and XRAY_ERROR messages appear at INFO level because `Logger::write()` calls omit the LogLevel parameter, defaulting to INFO.

### P3: No WARN usage anywhere

Zero WARN messages across all 37 log files. Some boundary conditions are better expressed as WARN than INFO (e.g., skipped proxies, partial failures).

### P4: sync failed with no details

`sync_20260509_124030.log` only says `[REPORT] sync failed` with zero diagnostic information about why.

---

## Requirements

- **R1**: Network errors (timeout, SSL, connection refused) → `LogLevel::INFO`
- **R2**: All CONFIG_ERROR and XRAY_ERROR → `LogLevel::ERR`
- **R3**: Add WARN level to appropriate boundary cases
- **R4**: sync failure must include error details
- **R5**: Must compile on Windows/MinGW-w64 with C++17

---

## Targeted Changes

### U1. `src/ProxyFinder.cpp` — Fix persistent network ERR→INFO

| Line(s) | Current | Target |
|---------|---------|--------|
| ~79 | `LogLevel::ERR` for timeout | `LogLevel::INFO` |
| ~146 | `LogLevel::ERR` for SSL error | `LogLevel::INFO` |

### U2. `src/ProxyBatchTester.cpp` — Ensure all ERR paths have explicit LogLevel

| Line(s) | Current | Target |
|---------|---------|--------|
| ~89 | `Logger::write("CONFIG_ERROR: ...")` (default INFO) | `LogLevel::ERR` |
| ~124 | `Logger::write("注入xray outbound 错误: ...")` (default INFO) | `LogLevel::ERR` |
| ~168 | `Logger::write("failed to build conf: ...")` (default INFO) | `LogLevel::ERR` |

### U3. Add WARN level usage

| File | Location | Message | Level |
|------|----------|---------|-------|
| `ProxyBatchTester.cpp` | ~213 | "No proxies to test" | `WARN` |
| `ProxyBatchTester.cpp` | ~245 | "No proxies to test for sub" | `WARN` |
| `ProxyBatchTester.cpp` | ~224 | "Failed to start xray instances" | `WARN` |

### U4. `src/SubitemUpdaterV2.cpp` — Add error detail before sync failure

| Line(s) | Current | Target |
|---------|---------|--------|
| syncDatabases() failure return | silent return | Add `Logger::write(error_msg, LogLevel::ERR)` before return false |

---

## Verification

- Build: `cmake --build . --parallel 8`
- Quick check: Run with `-T <test_sub_id>` and confirm CONFIG_ERROR at ERR level
- Check no new WARN messages appear at wrong level
- Build must produce 0 warnings, 0 errors

---

## Files Changed

- `src/ProxyFinder.cpp` — ERR→INFO for 2 network error locations
- `src/ProxyBatchTester.cpp` — Explicit LogLevel::ERR for 3 existing calls + add WARN for 3 boundary cases
- `src/SubitemUpdaterV2.cpp` — Add error log before sync failure return
