# Bugfix: Network Disconnect Does Not Stop Batch Testing

## Problem

When `NetworkMonitor` detects network disconnection, it logs `"Network connection LOST"` but has no mechanism to propagate the signal to running `ProxyBatchTester` workers. During a blocking `curl_easy_perform()` call, only the progress callback (which checks `cancelRequested_`) can abort — but no code sets `cancelRequested_` on network loss. Workers remain blocked until individual proxy timeouts, even though the network has been proven unreachable.

## Root Cause

- `NetworkMonitor::ThreadLoop()` correctly detects the `connected → disconnected` transition and logs at `ERR` level
- `ProxyBatchTester` workers only poll `IsConnected()` at scattered points between tests, and never during `curl_easy_perform()`
- The only way to abort an in-flight `curl_easy_perform()` is through the progress callback checking `cancelRequested_` — but nothing connects the dot between `NetworkMonitor` and the cancellation flag

## Solution

Added a `setCancelOnDisconnect(std::atomic<bool>*)` mechanism to `NetworkMonitor` that allows external code to register a cancellation flag. On the `connected → disconnected` transition, the monitor sets `*cancelOnDisconnect_ = true`, which triggers the full cancellation chain:

```
NetworkMonitor detects LOST
    → sets *cancelOnDisconnect_ (AppController::cancelRequested_)
        → ProxyBatchTester::isCancelled() returns true
            → Worker threads return early
                → curl progress callback fires → abort via CURLOPT_PROGRESSFUNCTION
```

## Files Changed

### 1. `include/NetworkMonitor.h`
- Added `void setCancelOnDisconnect(std::atomic<bool>* flag)` method (public)
- Added `std::atomic<bool>* cancelOnDisconnect_{nullptr}` member (private)

### 2. `src/NetworkMonitor.cpp` (lines 87-96)
- On `connected → disconnected` transition, conditionally sets the registered flag:
```cpp
if (cancelOnDisconnect_) {
    cancelOnDisconnect_->store(true);
    Logger::write("[NetworkMonitor] cancelOnDisconnect triggered", LogLevel::ERR);
}
```

### 3. `src/ui/AppController.cpp`
- In constructor, after `netMon_.Start()`: `netMon_.setCancelOnDisconnect(&cancelRequested_)`
- In `restartNetworkMonitor()`, after `netMon_.Start()`: same wiring

## Verification

- Build: 0 errors, 0 warnings
- Tests: 64/64 passed
- LSP diagnostics: clean on all 3 changed files

## Related Documents

- Design spec: `docs/superpowers/specs/2026-06-15-Spec-NetworkMonitor-batch-network-abort-on-disconnect-v1.0.md`
- Implementation plan: `docs/superpowers/plans/2026-06-15-NetworkMonitor-batch-network-abort-on-disconnect.md`
