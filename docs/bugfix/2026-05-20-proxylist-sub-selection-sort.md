---
title: "fix: ProxyListPanel subscription selection and sorting coordination"
type: fix
status: completed
date: 2026-05-20
---

# ProxyListPanel: Subscription Selection & Sorting Coordination Fix

## Issues Fixed

1. **Subscription selection not loading proxies** - clicking subscription had no effect
2. **Sorting reset on subscription switch** - sorted list would show unsorted data when switching subscriptions
3. **Proxy list sorting not working** - column header clicks had no effect
4. **Search box not filtering** - toolbar search did nothing

## Root Cause

`loadProxies()` handled two scenarios with conflicting logic:
1. **Subscription switch**: needs to reset sort state to show fresh data
2. **Sort refresh**: needs to preserve sort state to maintain sorted view

The original code always reset `proxies_` to `allProxies_`, losing sort state.

## Fix Implementation

### Code Change (ProxyListPanel.cpp)
```cpp
void ProxyListPanel::loadProxies(const std::string& subId) {
    bool subIdChanged = (subId != currentSubId_);
    
    currentSubId_ = subId;
    allProxies_ = controller_->loadProxies(subId);
    
    if (subIdChanged) {
        // New subscription - reset sort state
        sortState_.column = -1;
        sortState_.direction = SortDirection::None;
        proxies_ = allProxies_;
        // else: keep existing sorted proxies_ (called from sortProxiesByColumn)
    }
    
    exItems_ = controller_->loadProxyResults();
    // ... rebuild view
}
```

### Files Changed
| File | Changes |
|------|---------|
| `src/ui/ProxyListPanel.cpp` | Fixed column indices, enabled event binding, added subId check |
| `src/ui/ProxyListPanel.h` | Added `allProxies_`, `currentSubId_`, `filterBySearch()` |
| `src/ui/SubscriptionPanel.cpp` | `wxPostEvent` fix, database persistence for enabled toggle |
| `src/ui/SubscriptionPanel.h` | Added `updateSubscriptionEnabled` method |
| `include/Subitem.h` | Added `updateEnabled()` to SubitemDAO |
| `src/ui/AppController.cpp/h` | Added `updateSubscriptionEnabled()` wrapper |

## Test Results
- [x] Build successful: 0 errors
- [x] Unit tests passed: 14 tests (test_dedup: 11, test_model: 3)
- [x] GUI runs and shows subscription list with row numbers