# Spec: NetworkMonitor — Batch Network Abort on Disconnect

- **Date**: 2026-06-15
- **Version**: 1.0
- **Module**: `NetworkMonitor`
- **Affected modules**: `ProxyBatchTester`, `SubitemUpdaterV2`, `AutoTaskManager`, `ConfigReader`, `MainFrame`

---

## 1. Problem

When network connectivity to Chinese mainland websites is interrupted (DNS failure, ISP outage, firewall interruption), batch operations (`test_all`, `update_all`) continue running — each worker thread attempting connections, timing out, and ultimately failing. This results in:

- Long delays before the user discovers the network is down
- Wasted bandwidth and CPU on doomed connection attempts
- Frustrating UX: batch appears stuck with no status feedback

## 2. Design

### 2.1 Architecture

A standalone `NetworkMonitor` class runs a background thread that periodically sends cURL HEAD requests to a configurable set of well-known Chinese mainland URLs. It exposes a single `bool IsConnected() const` query backed by `std::atomic<bool>`.

```
┌─────────────────┐  reads flag  ┌──────────────────────┐
│  ProxyBatchTester│◄─────────────│  NetworkMonitor       │
│  SubitemUpdater  │              │  (background thread)  │
└─────────────────┘              │  atomic<bool> conn    │
                                  │  HEAD → baidu.com     │
┌─────────────────┐              │  HEAD → qq.com        │
│  MainFrame       │◄─wxTimer─2s─│  HEAD → taobao.com    │
│  status bar ●/✕  │              └──────────────────────┘
└─────────────────┘                       ▲
                                           │ reads config
                                   ┌───────┴────────┐
                                   │  ConfigReader   │
                                   │  network_monitor│
                                   │  section        │
                                   └────────────────┘
```

### 2.2 Class: NetworkMonitor

**File**: `include/NetworkMonitor.h`, `src/NetworkMonitor.cpp`

**Public interface**:

```cpp
class NetworkMonitor {
public:
    NetworkMonitor();
    ~NetworkMonitor();

    bool Start(const std::vector<std::string>& urls,
               int checkIntervalMs,
               int checkTimeoutMs);
    void Stop();

    bool IsConnected() const;
};
```

**Internal thread loop**:
```
while (!stopRequested_) {
    bool ok = false;
    for (auto& url : urls_) {
        ok = sendHEAD(url, timeoutMs_);
        if (ok) break;  // any one success = connected
    }
    connected_.store(ok);
    std::this_thread::sleep_for(interval_);
}
```

- HEAD request via `CurlEasyHandle` (existing RAII wrapper, `include/CurlEasyHandle.h`)
- Success = HTTP response code in 2xx-3xx range OR TCP connection succeeded
- Timeout per URL = `check_timeout_ms` (default: 5s)
- Sleep between rounds = `check_interval_ms` (default: 10s)

**Transition logging**: when connected_.store() value changes, log at `WARN` level:
- `"[NetworkMonitor] connection LOST"` (true→false)
- `"[NetworkMonitor] connection RESTORED"` (false→true)

### 2.3 Config — `config.json`

```json
{
  "network_monitor": {
    "enabled": true,
    "check_urls": ["https://www.baidu.com", "https://www.qq.com", "https://www.taobao.com"],
    "check_interval_ms": 10000,
    "check_timeout_ms": 5000,
    "lost_action": "abort_batch"
  }
}
```

- `enabled`: false = feature completely disabled (default: true)
- `check_urls`: must have at least 1 entry; if empty, feature is disabled
- `lost_action`: reserved for future — currently only `"abort_batch"`

**ConfigReader**: parse `config_json["network_monitor"]` into a struct, provide getter.

### 2.4 Integration Points

**ProxyBatchTester (`src/ProxyBatchTester.cpp`)**:
- Constructor receives `const NetworkMonitor* netMon` (nullable; null = disabled)
- In `run()` worker loop (after checking `stopRequested_`), also check `netMon_ && !netMon_->IsConnected()`:
  - If disconnected: set `stopRequested_ = true`, let threads drain naturally
  - Log `WARN: "[ProxyBatchTester] network disconnected — aborting batch test"`

**SubitemUpdaterV2 (`src/SubitemUpdaterV2.cpp`)**:
- Constructor receives `const NetworkMonitor* netMon`
- In the update loop over subitems, check `netMon_ && !netMon_->IsConnected()`:
  - If disconnected: break out of loop, return `false`
  - Log `WARN: "[SubitemUpdaterV2] network disconnected — aborting update"`

**AutoTaskManager (`src/AutoTaskManager.cpp`)**:
- Constructs `NetworkMonitor` as a member (or receives from AppController)
- Calls `NetworkMonitor::Start()` at AutoTask beginning, `Stop()` at end
- Passes `NetworkMonitor*` to `ProxyBatchTester` and `SubitemUpdaterV2` constructors
- After each step, checks `networkMonitor_.IsConnected()` — if false, breaks the pipeline

### 2.5 UI Feedback — MainFrame

- `MainFrame` holds a `wxTimer` (interval: 2000ms)
- Each tick calls `appController_->IsNetworkConnected()` → reads `NetworkMonitor::IsConnected()`
- Updates **status bar field 1** (index 0) with:
  - Green: `" ● Network OK"` (when connected)
  - Red: `" ✕ Network Down"` (when disconnected)
- Color via `wxStatusBar::SetStatusText()` and `wxStatusBar::SetStatusStyles()` (foreground color)

### 2.6 Lifecycle

```
AutoTask::run()
  ├─ networkMonitor_.Start(...)
  ├─ stepUpdateAll(..., &networkMonitor_)
  │    └─ SubitemUpdaterV2 checks IsConnected() in loop
  ├─ stepTestAll(..., &networkMonitor_)
  │    └─ ProxyBatchTester checks IsConnected() in worker loop
  ├─ ...
  └─ networkMonitor_.Stop()

MainFrame::OnTimer()
  └─ reads IsConnected(), updates status bar field 1
```

When no AutoTask is running, `NetworkMonitor` is stopped (no background thread). Only active during batch operations.

## 3. Error Handling

| Scenario | Behavior |
|----------|----------|
| All URLs unreachable | `IsConnected()` → false; batch aborts; status bar shows red |
| One URL unreachable, another OK | `IsConnected()` → true (any one success = connected) |
| All URLs invalid/malformed | Start() logs warning, starts anyway with what it has |
| Empty URL list | Start() returns false, feature disabled |
| cURL init failure (global) | Start() logs error, returns false, IsConnected() → true (fail-open) |
| Background thread crash | Destructor joins thread; no recovery — IsConnected() stays at last value |

## 4. Files Changed / Created

| File | Action |
|------|--------|
| `include/NetworkMonitor.h` | **Create** — class declaration |
| `src/NetworkMonitor.cpp` | **Create** — implementation |
| `include/ConfigReader.h` | Modify — add `NetworkMonitorConfig` struct |
| `src/ConfigReader.cpp` | Modify — parse `network_monitor` JSON section |
| `include/ProxyBatchTester.h` | Modify — constructor takes `const NetworkMonitor*` |
| `src/ProxyBatchTester.cpp` | Modify — check flag in worker loop |
| `include/SubitemUpdaterV2.h` | Modify — constructor takes `const NetworkMonitor*` |
| `src/SubitemUpdaterV2.cpp` | Modify — check flag in update loop |
| `include/AutoTaskManager.h` | Modify — add NetworkMonitor member |
| `src/AutoTaskManager.cpp` | Modify — lifecycle management + pass to children |
| `include/AppController.h` | Modify — add `IsNetworkConnected()` |
| `src/ui/AppController.cpp` | Modify — implement, forward to AutoTaskManager |
| `src/ui/MainFrame.h` | Modify — add wxTimer member, event handler |
| `src/ui/MainFrame.cpp` | Modify — timer setup, status bar update |
| `bin/test_config.json` | Modify — add `network_monitor` section |

## 5. Testing Strategy

**New tests in `tests/`**:

| Test | Approach |
|------|----------|
| `NetworkMonitorHttpTest` | Start with real URLs, verify IsConnected() after short sleep |
| `NetworkMonitorInvalidUrlTest` | Start with malformed URLs, verify IsConnected() → false |
| `NetworkMonitorEmptyUrlTest` | Start with empty list, verify returns false |
| `NetworkMonitorStartStopTest` | Start/stop cycle, no crash, no thread leak |
| `ProxyBatchTesterNetworkAbortTest` | Mock NetworkMonitor, start test, set disconnected mid-batch, verify abort |
| `SubitemUpdaterV2NetworkAbortTest` | Mock NetworkMonitor, start update, set disconnected mid-loop, verify abort |
| `ConfigReaderNetworkMonitorTest` | Parse config with valid/invalid/missing network_monitor section |
