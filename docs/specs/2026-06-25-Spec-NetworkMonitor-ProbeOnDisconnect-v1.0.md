# Spec: NetworkMonitor — Probe on Disconnect (Pause + Consecutive Probe Threshold)

- **Date**: 2026-06-25
- **Version**: 1.0
- **Module**: `NetworkMonitor`
- **Affected modules**: `ProxyBatchTester`, `ConfigReader`, `AppController`, `ConfigJsonSerializer`, `NetworkMonitorConfigParser`

---

## 1. Problem

When `NetworkMonitor` detects network disconnection, the current implementation immediately sets `cancelOnDisconnect_` → `AppController::cancelRequested_` → `ProxyBatchTester::isCancelled()` returns `true` → all workers abort. There is no grace window.

This is too aggressive: a transient network glitch (e.g., brief ISP hiccup, DNS timeout, carrier reset) permanently cancels an entire batch test. The user must manually re-trigger the test after the network recovers.

### Current Behavior

```
NetworkMonitor detects LOST (single HEAD failure)
  → cancelOnDisconnect_ = true
    → ProxyBatchTester workers set cancelRequested_ = true
      → All workers exit, batch test aborted
```

### Desired Behavior

```
NetworkMonitor detects LOST (single HEAD failure)
  → consecutiveFailures_++  (no immediate cancel)
  → ProxyBatchTester pauses (doesn't waste time on doomed proxies)
  → ThreadLoop continues probing URLs at check_interval_ms
  → On RESTORED → consecutiveFailures_ = 0 → workers resume
  → On N consecutive failures (N = max_probes) → cancelOnDisconnect_ = true
```

---

## 2. Design

### 2.1 Three Pillars

| Pillar | Component | Responsibility |
|--------|-----------|---------------|
| **Probe Counter** | `NetworkMonitor` | Tracks consecutive failed checks; triggers `cancelOnDisconnect_` only when `consecutiveFailures_ >= maxProbes_`; resets on any successful probe |
| **Pause** | `ProxyBatchTester` | When worker detects disconnect, enters wait-loop polling `IsConnected()` instead of immediately canceling; breaks only when `isCancelled()` returns true (set by NetworkMonitor after max probes) |
| **Config** | `ConfigReader` | Single `maxProbes` field under `network_monitor` (0 = immediate cancel, >0 = probe mode) |

### 2.2 Data Flow

```
config.json
  network_monitor.maxProbes
    └─ 0 = immediate cancel (legacy)
    └─ >0 = probe N times before cancel

NetworkMonitor::ThreadLoop()
  ├─ CheckURL() success → consecutiveFailures_ = 0, connected_ = true
  ├─ CheckURL() fail #1 → consecutiveFailures_ = 1, connected_ = false (no cancel yet)
  ├─ CheckURL() fail #2 → consecutiveFailures_ = 2
  ├─ CheckURL() fail N → consecutiveFailures_ = N >= maxProbes_ → set cancelOnDisconnect_
  └─ CheckURL() success before max → consecutiveFailures_ = 0, connected_ = true

ProxyBatchTester::workerThreadFunc()
  └─ Check: if (!netMon_->IsConnected())
       → enter wait loop: sleep 500ms, re-check IsConnected()
       → until: IsConnected() returns true (recovery) → resume testing
       → until: isCancelled() returns true (max probes exceeded) → break
```

### 2.3 `NetworkMonitor` Changes

**New Members** (`include/NetworkMonitor.h`):
```cpp
bool probeEnabled_{true};
int  maxProbes_{3};
std::atomic<int> consecutiveFailures_{0};
```

**New Public Method**:
```cpp
void setProbeOnDisconnect(bool enabled, int maxProbes);
```

**Modified `ThreadLoop()`** (`src/NetworkMonitor.cpp`):
- On `prev=true → allOk=false` transition (LOST):
  - `consecutiveFailures_++`
  - If `consecutiveFailures_ >= maxProbes_` → set `cancelOnDisconnect_`
  - Log: `"Network connection LOST (probe N/M)"`
- On `prev=false → allOk=true` transition (RESTORED):
  - `consecutiveFailures_ = 0`
  - Log: `"Network connection RESTORED (probes reset)"`
- No change for steady-state (still-connected or still-disconnected)

### 2.4 `ProxyBatchTester` Changes

**New private method** (`include/ProxyBatchTester.h`):
```cpp
/// Waits for network recovery or cancellation.
/// Returns true if network is connected (resume), false if cancelled (exit).
bool waitForNetworkRecovery();
```

**Modified worker loop** (`src/ProxyBatchTester.cpp`):
Replace all `if (netMon_ && !netMon_->IsConnected()) { cancelRequested_ = true; return/break; }` with:
```cpp
if (netMon_ && !netMon_->IsConnected()) {
    if (!waitForNetworkRecovery()) return; // or break based on context
    continue;
}
```

The helper method:
```cpp
bool ProxyBatchTester::waitForNetworkRecovery() {
    Logger::write("[ProxyBatchTester] network disconnected — pausing, waiting for recovery", LogLevel::WARN);
    while (!isCancelled()) {
        if (netMon_->IsConnected()) {
            Logger::write("[ProxyBatchTester] network restored — resuming", LogLevel::WARN);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    Logger::write("[ProxyBatchTester] network probe exhausted or cancelled — exiting", LogLevel::WARN);
    return false;
}
```

**Scenarios**:

| Scenario | Behavior |
|----------|----------|
| Disconnect → recover before max_probes | Workers pause briefly, resume when `IsConnected()` true |
| Disconnect → never recover | After max_probes, `cancelOnDisconnect_` fires → `isCancelled()` true → workers exit |
| Disconnect → manual cancel | `cancelRequested_ = true` → `isCancelled()` true → workers exit immediately |
| No disconnect | Unchanged behavior |

### 2.5 `AppConfig` Changes

**New field** (`include/ConfigReader.h`):
```cpp
int maxProbes{3};  // Inside network_monitor struct. 0 = immediate cancel, >0 = probe mode
```

### 2.6 Config Parser Changes

`include/config/sections/NetworkMonitorConfigParser.h`: Parse `probe_on_disconnect.max_probes` (int64) from JSON. If absent, defaults to 3.

### 2.7 Config Serializer Changes

`src/config/ConfigJsonSerializer.cpp`: Serialize `probe_on_disconnect` as `{"max_probes": ...}`.

### 2.8 `AppController` Changes

In constructor and `restartNetworkMonitor()` (after `netMon_.Start()`):
```cpp
netMon_.setProbeOnDisconnect(config_.network_monitor.maxProbes);
```

### 2.9 Config JSON Changes

```json
{
  "network_monitor": {
    "enabled": true,
    "check_urls": ["https://www.baidu.com", "https://www.qq.com", "https://www.taobao.com"],
    "check_interval_ms": 10000,
    "check_timeout_ms": 5000,
    "probe_on_disconnect": {
      "max_probes": 3
    }
  }
}
```

- `max_probes = 0`: immediate cancel (legacy behavior)
- `max_probes > 0`: probe N times before cancel (grace window)

### 2.10 Logging

| Level | Message | Location |
|-------|---------|----------|
| WARN | `[ProxyBatchTester] network disconnected — pausing, waiting for recovery` | Once per worker on first disconnect detection |
| WARN | `[ProxyBatchTester] network restored — resuming` | Once per worker when recovery detected |
| WARN | `[ProxyBatchTester] network probe exhausted or cancelled — exiting` | Once per worker when cancelled |
| ERR | `Network connection LOST (probe N/M)` | Each disconnect probe from NetworkMonitor |
| ERR | `Network connection RESTORED (probes reset)` | On recovery from NetworkMonitor |
| ERR | `[NetworkMonitor] cancelOnDisconnect triggered after N failed probes` | When maxProbes threshold reached |

---

## 3. File Change Summary

| File | Action |
|------|--------|
| `include/NetworkMonitor.h` | Modify — add `probeEnabled_`, `maxProbes_`, `consecutiveFailures_`, `setProbeOnDisconnect()` |
| `src/NetworkMonitor.cpp` | Modify — ThreadLoop uses consecutive-failure logic |
| `include/ProxyBatchTester.h` | Modify — add `waitForNetworkRecovery()` |
| `src/ProxyBatchTester.cpp` | Modify — replace immediate cancel with pause+wait in all 6 checkpoints |
| `include/ConfigReader.h` | Modify — add `probe_on_disconnect` to `network_monitor` |
| `include/config/sections/NetworkMonitorConfigParser.h` | Modify — parse `probe_on_disconnect` fields |
| `src/config/ConfigJsonSerializer.cpp` | Modify — serialize `probe_on_disconnect` fields |
| `src/ui/AppController.cpp` | Modify — pass probe config to NetworkMonitor |
| `bin/config.json` | Modify — add `probe_on_disconnect` section |
| `bin/test_config.json` | Modify — add `probe_on_disconnect` section |
| `bin/worker/config.json` | Modify — add `probe_on_disconnect` section |
| `docs/INDEX.md` | Modify — add this spec reference |

---

## 4. Testing Strategy

| Test | Approach |
|------|----------|
| Build | `cmake --build build --parallel 8` — 0 errors |
| Existing tests | `ctest -V` — all pass unchanged |
| Manual (future) | Start batch test, disconnect network → verify workers pause, not cancel; reconnect → verify resume; stay disconnected → verify cancel after N probes |

---

## 5. Backward Compatibility

- `probe_on_disconnect` is **optional** in JSON. When absent, defaults to `maxProbes=3` (probe mode enabled).
- `maxProbes=0` explicitly disables probe mode for users who want immediate cancel.
