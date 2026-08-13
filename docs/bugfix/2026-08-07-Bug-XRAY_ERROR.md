# Bugfix: Intermittent `grpcConnect` WSA10061 Connection Refused During XRAY_ERROR

- **Date**: 2026-08-07
- **Severity**: High (intermittent proxy test failures)
- **Affected Components**: `XrayManager`, `ProxyBatchTester`
- **Affected Files**: `src/XrayManager.cpp`, `src/ProxyBatchTester.cpp`

## 1. Symptom

When running batch proxy tests, workers intermittently fail with:

```
[Worker-3] XRAY_ERROR - <indexId> (tag=proxy) - grpcConnect: connect() refused (WSA10061)
```

The Xray instance was started, but its gRPC API port was not yet accepting connections when the worker
attempted to inject the outbound.

## 2. Root Cause

### 2.1 `pollApiPortReady` never retried on `WSAECONNREFUSED`

The original `pollApiPortReady()` function (XrayManager.cpp, lines 18-60) created a **single** blocking
socket and reused it across all connect attempts. On the very first `connect()` that returned
`WSAECONNREFUSED`, the function immediately **broke** out of the loop and returned `false`.

```cpp
// BUG: breaks immediately on WSAECONNREFUSED
if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
    break;   // <-- never retries; port might just be starting up
}
```

When a listening socket is reset/replaced (common during Xray process startup), a connection attempt
returns `WSAECONNREFUSED` even though the port will accept connections moments later. The function
treated this as terminal and gave up after a single failed probe — typically within ~100ms.

### 2.2 `XrayManager::start()` committed dead instances on probe failure

In the `start()` method, when `pollApiPortReady()` returned `false`, the code logged a `WARN` and
**still committed the instance** to `instances_`:

```cpp
// BUG: commits instance even when API port probe failed
if (!pollApiPortReady(apiPortAddr)) {
    Logger::write("...not ready within timeout...", LogLevel::WARN);
}
startedInstances.push_back(std::move(instance));   // <-- committed despite failure
```

Workers receiving port pairs from this list would then attempt gRPC calls against a non-ready or
dead Xray API, producing the `WSA10061` errors.

### 2.3 Worker retry budget too short

The worker's `addOutboundDirect` retry loop (ProxyBatchTester.cpp, lines 186-221) used exponential
backoff starting at 50ms with 3 retries max:

```
retry 0: 50ms (2^0)
retry 1: 100ms (2^1)
retry 2: 200ms (2^2)
Total:   ~350ms  (but actually ~150ms due to early loop exit)
```

When the API port was slow to become ready (normal for Xray process startup), the worker exhausted
its retry budget before the port opened.

## 3. Fix

### 3.1 Rewrote `pollApiPortReady()` to genuinely poll

- Creates a **fresh blocking socket** per iteration (avoids stale socket state).
- On `WSAECONNREFUSED`, `WSAETIMEDOUT`, or `WSAEWOULDBLOCK`, **continues** to the next iteration
  instead of breaking.
- Still uses a ~5s total timeout with 100ms steps.
- Removed the unnecessary `WSAStartup`/`WSACleanup` calls (already initialized at process level).

### 3.2 Hard-fail in `XrayManager::start()` on probe failure

When the API port probe fails, the instance is now:

1. Stopped (`instance->stop()`)
2. Port pair freed (`PortManager::freePort` for both)
3. **Not** committed to `instances_` — the loop breaks with `actualCount` reflecting only working instances
4. Logged at `ERR` level instead of `WARN`

### 3.3 Extended worker retry backoff

| Parameter | Before | After |
|-----------|--------|-------|
| Base backoff | 50ms | 200ms |
| Retry 1 | 50ms | 200ms |
| Retry 2 | 100ms | 400ms |
| Total budget | ~150ms | ~600ms+ |

The backoff still respects `waitInterruptible()` for graceful cancellation.

## 4. Verification

- Clean build: 284/284 targets compiled (Ninja + w64devkit g++ 14.2.0)
- Tests: 179/179 PASSED across all test suites
- No new compiler warnings introduced
