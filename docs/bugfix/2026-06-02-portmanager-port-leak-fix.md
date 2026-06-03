# Bug Fix: PortManager Port Leak on Xray Instance Shutdown

## Problem Description
`PortManager::usedPorts_` vector grew indefinitely without cleanup. When Xray instances were stopped, the ports remained marked as "used" in the tracking vector, causing:
- Unnecessary port skipping on subsequent runs
- Potential port exhaustion in long-running scenarios

## Root Cause Analysis
The `PortManager` class tracked allocated ports in `usedPorts_` but had no mechanism to release them. The `XrayManager::stopAll()` method stopped instances but never cleared the port tracking.

## Files Modified

| File | Change |
|------|--------|
| `include/PortManager.h` | Added `clearPorts()` method declaration |
| `src/PortManager.cpp` | Implemented `clearPorts()` method |
| `src/XrayManager.cpp` | Call `PortManager::clearPorts()` after stopping instances |

## Fix Details

### PortManager.h (line 12)
```cpp
static void clearPorts();  // Release all tracked ports for reuse
```

### PortManager.cpp (line 51-54)
```cpp
void PortManager::clearPorts() {
    usedPorts_.clear();
}
```

### XrayManager.cpp (line 89)
```cpp
void XrayManager::stopAll() {
    // ... stop instances ...
    instances_.clear();
    PortManager::clearPorts();  // Release ports for reuse on next start
    Logger::write("[XrayManager][stopAll] cleared " + std::to_string(prevSize) + " instance(s)", LogLevel::INFO);
}
```

## Related Code Paths Calling stopAll()
- `SubitemUpdaterV2::releaseProxyPorts()` - subscription cleanup after update
- `ProxyBatchTester::~ProxyBatchTester()` - batch test cleanup
- `XrayManager::release()` - singleton destruction

## Verification
- Build: ✅ Success (CMake + Ninja Debug)
- Tests: ✅ 6/6 passed (42 test cases)