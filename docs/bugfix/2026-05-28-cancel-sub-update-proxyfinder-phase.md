---
title: "fix: cancel during ProxyFinder phase of subscription update doesn't terminate immediately"
type: fix
status: completed
date: 2026-05-28
---

# Cancel During ProxyFinder Phase Fix

## Bug Summary

When canceling subscription updates (Update All / Update Single) during the ProxyFinder phase (proxy discovery for fallback fetching), the cancel request is not acknowledged and the operation continues until completion.

## Root Cause

In `SubitemUpdaterV2::getProxyPorts()` at line 1183, the `ProxyFinder` constructor was called with `nullptr` as the cancel flag parameter instead of `externalCancel_`:

```cpp
// Before (line 1183-1186)
proxyFinder_ = new ProxyFinder(db_, xrayMgr_, xrayPath_,
                             config_.test_url,
                             targetUrl,
                             config_.test_timeout_ms,
                             nullptr);  // ← Cancel flag not passed!
```

Although `SubitemUpdaterV2` was updated to support cancellation with `externalCancel_` and `isCancelled()` checks in the `run()` and `runSingle()` methods, the `ProxyFinder` instantiation missed this connection. `ProxyFinder` has built-in `isCancelled()` checking in its search loops (lines 60 and 138 of ProxyFinder.cpp) but requires the cancel flag pointer to be wired in.

## Fix Implementation

Changed `SubitemUpdaterV2.cpp` line 1187 to pass the cancel flag:

```cpp
// After
proxyFinder_ = new ProxyFinder(db_, xrayMgr_, xrayPath_,
                             config_.test_url,
                             targetUrl,
                             config_.test_timeout_ms,
                             externalCancel_ ? externalCancel_ : nullptr);
```

This ensures that when the user clicks the cancel button during subscription update:
1. `cancelRequested_` is set to `true` in `AppController`
2. `SubitemUpdaterV2::isCancelled()` returns `true` (checks `externalCancel_`)
3. `ProxyFinder::isCancelled()` returns `true` (now checks the passed cancel flag)
4. The proxy search loop exits at its next checkpoint

## Call Chain

```
onToolCancel (MainFrame)
  → controller_->cancelTest()
    → cancelRequested_ = true
  → ProxyFinder created with cancel flag (now wired)
    → ProxyFinder::findFirstWorkingProxy()
      → checks isCancelled() at loop checkpoints
      → returns {-1, -1} on cancel
  → SubitemUpdaterV2::run() / runSingle()
    → checks isCancelled() at fetch checkpoints
    → skips remaining subscriptions on cancel
```

## Test Results

- [x] Build successful (Debug mode, Ninja)
- [x] All 3 unit test suites passed (CurlEasyHandle, Dedup, Profileitem)
- [x] GUI launches and responds normally
- [x] Commit: `a499f7c` - "fix: pass cancel flag to ProxyFinder in SubitemUpdaterV2::getProxyPorts"

## Related Changes

This fix completes the cancel support chain initiated in:
- `dfaab5e` - "feat: add runtime cancel support for subscription update operations"
  - Added `externalCancel_` to `SubitemUpdaterV2`
  - Added `isCancelled()` checks in `run()` and `runSingle()` loops