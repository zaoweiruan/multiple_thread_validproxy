# Bugfix: SubitemUpdaterV2 Missing netMon_ Causes Silent Network Disconnect Bypass

## Problem

When `doUpdateAllSubscriptions()` or `doUpdateSubscription()` is triggered during a network disconnection,
`SubitemUpdaterV2` silently proceeds with subscription fetching and testing instead of early-exiting.
Users observe subscription updates proceeding normally, followed by curl timeouts, rather than seeing an
immediate "Network disconnected, skipping" message.

## Root Cause

`AppController::doUpdateAllSubscriptions()` and `AppController::doUpdateSubscription()` construct
`SubitemUpdaterV2` without passing the 7th parameter `const NetworkMonitor* netMon`:

```cpp
// Before fix — netMon defaults to nullptr
SubitemUpdaterV2 updater(db_, xrayPath, config_, logOut, baseDir, &cancelRequested_);
```

`SubitemUpdaterV2` has 6 guard points in the form `if (netMon_ && !netMon_->IsConnected())` 
(lines 190, 213, 249, 268, 346, 392 of `src/SubitemUpdaterV2.cpp`). When `netMon_` is `nullptr`,
the null-check short-circuits to `false`, and the network disconnect logic is never evaluated.

**Contrast**: `ProxyBatchTester` was already correctly passing `&netMon_` at all construction sites
in `AppController.cpp`, so batch testing properly honored network state.

## Solution

Pass `&netMon_` as the 7th argument to `SubitemUpdaterV2` in both missing call sites:

```cpp
// After fix
SubitemUpdaterV2 updater(db_, xrayPath, config_, logOut, baseDir, &cancelRequested_, &netMon_);
```

## Files Changed

### 1. `src/ui/AppController.cpp`

| Line | Method | Change |
|------|--------|--------|
| 560 | `doUpdateSubscription()` | Added `&netMon_` as 7th constructor arg |
| 582 | `doUpdateAllSubscriptions()` | Added `&netMon_` as 7th constructor arg |

## Verification

- Build: 0 errors, 0 warnings
- LSP diagnostics: clean

## Related Documents

- Design spec: `docs/superpowers/specs/2026-06-15-Spec-NetworkMonitor-batch-network-abort-on-disconnect-v1.0.md`
- Probe-on-disconnect spec: `docs/specs/2026-06-25-Spec-NetworkMonitor-ProbeOnDisconnect-v1.0.md`
- Probe flow analysis: `docs/reports/2026-06-26-Report-NetworkMonitorProbeFlow.md`
