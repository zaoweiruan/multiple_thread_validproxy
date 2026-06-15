# Bugfix: ProxyBatchTester Worker-0 Join Timeout (v3 — Dynamic Join Timeout)

- **Date:** 2026-06-12
- **Status:** ✅ fixed (v3)

## Summary

Worker threads consistently timed out during join after Xray startup. Three versions of the fix:

| Version | Approach | Why insufficient |
|---------|----------|-----------------|
| v1 | Hardcoded `5000ms` → `test_timeout_ms+5000` (7s) | 7s still far too short for multi-proxy workers |
| v2 | `XrayApi::ping()` polling in `startXrayInstances()` | Each ping launches xray.exe (expensive, ~2s/call); polling delay pushed total startup beyond join timeout |
| **v3** | **Dynamic join timeout proportional to workload + lightweight warmup** | Worker has enough time to complete all proxies normally |

## Timeline (from log: `ui_20260612_150929.log`)

- `15:09:29` — Config load complete
- `15:09:58` — Worker-0 join timeout WARN (29s gap)

## Real Root Cause

1. Each worker processes `proxiesPerWorker = totalProxies / numWorkers` proxies (e.g., 25 for 100 proxies / 4 workers)
2. Each proxy takes ~3-4s (removeOutbound×2 + addOutbound retries + 2000ms cURL test)
3. Worker total runtime: 25 × 3.5s ≈ 88s
4. Join timeout was `test_timeout_ms + 5000` = 7000ms — **88s >> 7s**
5. Join always detaches, causing resource-race on stopAll()

v2's `XrayApi::ping()` polling worsened the problem: `xray api lsi` subprocess takes ~2s per call, and sequential polling of 4 instances with retries added ~18s to startup, delaying workers without fixing the join timeout mismatch.

## Fix (v3)

1. **`startXrayInstances()`** (`src/ProxyBatchTester.cpp:86-97`): Removed heavy `XrayApi::ping()` loop; replaced with simple 2s `sleep_for` warmup (avoids creating expensive xray subprocesses per ping).

2. **`testProxiesMultiThreaded()`** (`src/ProxyBatchTester.cpp:323-335`): Dynamic join timeout:
   - `proxiesPerWorker = ceil(totalProxies_ / numWorkers)`
   - `perProxyBudgetMs = test_timeout_ms + 5000` (curl test + API overhead)
   - `joinTimeoutMs = proxiesPerWorker × perProxyBudgetMs`
   - Clamped to [60s, 300s] range
   - For 100 proxies / 4 workers: 25 × 7s = 175s ≈ 3 min

## Files Changed

- `src/ProxyBatchTester.cpp` — `startXrayInstances()` simplified (2s warmup), join timeout dynamic

## Verification

- Build: CLI + GUI + all 9 test targets pass (64/64 ninja targets)
- Tests: 92 tests pass, 1 pre-existing skip
