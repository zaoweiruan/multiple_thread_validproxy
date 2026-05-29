---
title: "fix: close window during batch test/find doesn't terminate workers"
type: fix
status: completed
date: 2026-05-26
---

# Close-Window Cancel Propagation Fix

## Bug Summary

Closing the GUI window (clicking X) while a batch proxy test or Find Proxy/Find Best operation is running does not terminate the background worker threads or xray processes. The app hangs for up to the full test timeout before exiting.

## Root Cause

**Dual atomic flags — no propagation.** `AppController` has its own `cancelRequested_` flag, but `ProxyBatchTester` and `ProxyFinder` each have their own separate flags or none at all. When `MainFrame::onClose()` → `controller_->cancelTest()` → sets `AppController::cancelRequested_ = true`, the worker objects never see it because:

| Class | Own cancel flag? | Checked by workers? | AppController cancel propagates? |
|-------|-----------------|---------------------|----------------------------------|
| `ProxyBatchTester` | Yes (`cancelRequested_`) | Yes (6+ checks) | **No** — separate flag |
| `ProxyFinder` | **No** (nonexistent) | **No** (no checks) | **No** — no mechanism |

Additionally, `ProxyBatchTester` and `ProxyFinder` are created as **local stack variables** inside the worker thread functions (`doTestSubscription`, `doTestSingleProxy`, `doFindFirstProxy`, `doFindBestProxy`), making them completely disconnected from the controller's cancellation state.

## Files Changed

| File | Change |
|------|--------|
| `include/ProxyBatchTester.h` | Added `std::atomic<bool>* externalCancel_` member; `isCancelled()` checks both internal + external |
| `src/ProxyBatchTester.cpp` | Constructor initializes `externalCancel_`; all `cancelRequested_.load()` → `isCancelled()` |
| `include/ProxyFinder.h` | Added `std::atomic<bool>* cancelRequested_` + `isCancelled()` method |
| `src/ProxyFinder.cpp` | Constructor param; both find loops check `isCancelled()` each iteration, return early |
| `src/ui/AppController.cpp` | Pass `&cancelRequested_` to all 4 `ProxyBatchTester` and 2 `ProxyFinder` constructions |

## Fix Design

**Shared external cancel pointer pattern:**

1. `ProxyBatchTester` and `ProxyFinder` accept an optional `std::atomic<bool>* externalCancel = nullptr` constructor parameter.
2. `isCancelled()` checks: if `externalCancel` is non-null and `*externalCancel == true`, treat as cancelled — regardless of the internal flag.
3. `AppController` passes `&cancelRequested_` (its own atomic flag) to every worker object.
4. When user closes window → `AppController::cancelRequested_ = true` → all active workers see it immediately via the shared pointer.

## Test Results

- [x] Build successful (Debug, Ninja)
- [x] All 3 unit test suites passed
- [x] LSP diagnostics: zero errors on all 5 modified files

## Files Modified (diffstat)

```
include/ProxyBatchTester.h  | +3
include/ProxyFinder.h       | +3
src/ProxyBatchTester.cpp    | 14 +++++++-------
src/ProxyFinder.cpp         | 16 ++++++++++++----
src/ui/AppController.cpp    |  8 ++++----
```
