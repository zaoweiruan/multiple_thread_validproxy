---
title: "fix: Standardize error levels for proxy testing - network=INFO, config/inject=ERR"
type: fix
status: superseded
date: 2026-05-08
origin: User request (via opencode session)
---

# fix: Standardize error levels for proxy testing

## Summary

Standardize the log levels used across proxy-testing modules: **network errors** (curl timeout, connection refused, DNS failure) → `INFO`, **config generation errors** and **inject outbound errors** → `ERR`. Currently, `ProxyFinder.cpp` and `SubitemUpdaterV2.cpp` incorrectly use `ERR` for network errors, while `ProxyBatchTester.cpp` omits LogLevel parameters on all calls (defaulting to `INFO`).

---

## Problem

`src/ProxyBatchTester.cpp` never specifies `LogLevel` — all calls default to `INFO`, making config and inject errors indistinguishable from routine network failures.

`src/ProxyFinder.cpp` uses `ERR` even for network-level proxy failures, which floods ERROR logs during normal operation.

`src/XrayApi.cpp` logs addOutbound failures at `DEBUG` only, hiding them from console and file at default levels.

---

## Requirements

- **R1**: Network errors (curl timeout, connection refused, DNS, HTTP non-200/204) → `LogLevel::INFO`
- **R2**: Config generation errors (checkRequired, generateConfig exception) → `LogLevel::ERR`
- **R3**: Inject outbound errors (addOutbound failure) → `LogLevel::ERR`
- **R4**: All proxy testing modules use consistent levels (no ambiguity)
- **R5**: Must compile on Windows/MinGW-w64 with C++17

---

## Scope Boundaries

- Modify: `src/ProxyBatchTester.cpp`, `src/ProxyFinder.cpp`, `src/SubitemUpdaterV2.cpp`, `src/XrayApi.cpp`
- Will NOT change: `src/ConfigGenerator.cpp` (no Logger calls), `src/ProxyTester.cpp` (returns struct, no Logger calls)

---

## Targeted Changes

### U1. `src/ProxyBatchTester.cpp` — Add explicit LogLevel parameters

| Line(s) | Current | Target | Category |
|---------|---------|--------|----------|
| ~89 | `Logger::write("CONFIG_ERROR: ...")` (default INFO) | `LogLevel::ERR` | config |
| ~124 | `Logger::write("注入xray outbound 错误: ...")` (default INFO) | `LogLevel::ERR` | inject |
| ~160 | `Logger::write("FAILED - ...")` (default INFO) | `LogLevel::INFO` | network |
| ~168 | `Logger::write("failed to build conf: ...")` (default INFO) | `LogLevel::ERR` | config |

### U2. `src/ProxyFinder.cpp` — Downgrade network errors to INFO

| Line(s) | Current | Target | Category |
|---------|---------|--------|----------|
| 79 | `LogLevel::ERR` | `LogLevel::INFO` | network |
| 146 | `LogLevel::ERR` | `LogLevel::INFO` | network |

### U3. `src/SubitemUpdaterV2.cpp` — Downgrade fetch errors to INFO

| Line(s) | Current | Target | Category |
|---------|---------|--------|----------|
| 374 | `LogLevel::ERR` | `LogLevel::INFO` | network |
| 397 | `LogLevel::ERR` | `LogLevel::INFO` | network |

### U4. `src/XrayApi.cpp` — Upgrade addOutbound failure to ERR

| Line(s) | Current | Target | Category |
|---------|---------|--------|----------|
| 81-82 | `LogLevel::DEBUG` | `LogLevel::ERR` | inject |

---

## Verification

- Build: `cmake --build . --parallel 8`
- Integration: Run `-F`, `-FMIN`, `-T 1`, and default mode against test config
- Confirm: ERROR-level logs appear only for config/inject failures, not ordinary network timeouts

---

## Risks

| Risk | Mitigation |
|------|------------|
| INFO-level network errors disappear if console_level > INFO | Default console_level = INFO, so they remain visible; file_level = DEBUG catches everything |
| Overlooked Logger::write calls not covered | Use grep to audit all `Logger::write` in these 4 files after changes |
