# Spec: NetworkMonitor Enabled Toggle

- **Date**: 2026-06-16
- **Version**: 1.0
- **Module**: `NetworkMonitor`
- **Dependencies**: `2026-06-15-Spec-NetworkMonitor-batch-network-abort-on-disconnect-v1.0.md`

---

## 1. Problem

When `network_monitor.enabled = false` in config, `NetworkMonitor::Start()` is never called, but `IsConnected()` returns `false` (initial value of `connected_`). This causes all modules checking `IsConnected()` to incorrectly trigger network-disconnect abort behavior.

## 2. Design

### 2.1 Core Change — NetworkMonitor

**File**: `include/NetworkMonitor.h`, `src/NetworkMonitor.cpp`

Add `enabled_` flag to track whether monitoring is active:

```
before: connected_{false}  → IsConnected() returns false
after:  enabled_{true}     → IsConnected() returns true if disabled (skip check)
        connected_{false}
```

**Modified IsConnected()**:
```cpp
bool IsConnected() const {
    if (!enabled_) return true;  // When disabled, always report "connected" to skip abort
    return connected_.load();
}
```

### 2.2 Constructor Update

```cpp
NetworkMonitor(bool enabled = true);  // Accept enabled flag
```

### 2.3 File Changes

| File | Change |
|------|--------|
| `include/NetworkMonitor.h` | Add `bool enabled_{true};`, update constructor signature |
| `src/NetworkMonitor.cpp` | Initialize `enabled_`, implement logic in `IsConnected()` |
| `src/ui/AppController.cpp` | Pass `config_.network_monitor.enabled` to `netMon_` constructor |
| `src/ui/ConfigDialog.cpp` | Add checkbox `m_cbNetMonEnabled` bound to `network_monitor.enabled` |
| `src/ui/ConfigDialog.h` | Add `wxCheckBox* m_cbNetMonEnabled;` member |
| `src/ui/MainFrame.cpp` | Hide status bar panel when `enabled=false` |

### 2.4 Status Bar Handling

**MainFrame changes**:
- Add `bool netMonEnabled_` member to track config state
- In `onNetMonTimer()` (line 606): if `!netMon->IsEnabled()`, hide `netMonPanel_` and skip refresh
- Status bar panel hidden when `enabled=false`

### 2.5 Behavior Matrix

| `enabled` | `connected_` | `IsConnected()` 返回 | 行为 |
|----------|--------------|---------------------|------|
| false | N/A | true | 跳过所有网络中断检查，继续操作 |
| true | true | true | 网络正常，继续操作 |
| true | false | false | 网络断开，触发中止逻辑 |

### 2.6 Backward Compatibility

- Default `enabled_ = true` maintains existing behavior
- `Start()` only spawns thread when `enabled == true` (already implemented in AppController.cpp:30-34)
- Existing config files without `network_monitor.enabled` default to `true` via in-class initializer