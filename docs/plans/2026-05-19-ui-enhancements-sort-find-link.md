---
title: "feat(UI): ProxyListPanel column sorting + Find individual proxy + Subscription linkage"
type: feat
status: draft
date: 2026-05-19
origin: "User: '解析前期计划，制定实现功能文档'"
---

# Implementation Plan: UI Enhancements

## Overview

Three UI enhancement features to improve user experience for proxy management:

| # | Feature | Description |
|---|---------|-------------|
| F1 | Column Sorting | Click Delay/Speed/Address columns for ascending/descending/cancel sort |
| F2 | Find Individual Proxy | Find and highlight a proxy by criteria, show "not found" if absent |
| F3 | Subscription-Proxy Linkage | ProxyListPanel filters by selected subscription |

---

## F1 — Column Sorting (Delay/Speed/Address)

### Current State

`ProxyListPanel` uses `wxDataViewListStore` directly (not a model class with `Compare()`). Column headers show no sort indicator.

### Solution

Since `wxDataViewListStore` doesn't support `Compare()`, we'll implement in-place sorting:

1. Track sort state: `{column, direction}` where direction = `none`/`asc`/`desc`
2. On column header click, cycle: `none` → `asc` → `desc` → `none`
3. Sort the underlying `proxies_` vector by the column field
4. Rebuild the store while preserving selection when possible

### Implementation

```cpp
// ProxyListPanel.h — add member
enum class SortDirection { None, Asc, Desc };
struct SortState {
    int column = -1;
    SortDirection direction = SortDirection::None;
};
SortState sortState_;

// ProxyListPanel.cpp — add handler
void ProxyListPanel::onColumnHeaderClick(wxDataViewEvent& event) {
    int col = event.GetColumn();
    
    // Cycle direction
    if (sortState_.column == col) {
        sortState_.direction = (sortState_.direction == SortDirection::Asc) 
                              ? SortDirection::Desc 
                              : (sortState_.direction == SortDirection::Desc 
                                 ? SortDirection::None 
                                 : SortDirection::Asc);
    } else {
        sortState_.column = col;
        sortState_.direction = SortDirection::Asc;
    }
    
    if (sortState_.direction != SortDirection::None) {
        sortProxiesByColumn(sortState_.column, sortState_.direction);
    } else {
        resetSort();  // reload original order
    }
}

void ProxyListPanel::sortProxiesByColumn(int col, SortDirection dir) {
    std::sort(proxies_.begin(), proxies_.end(), [col, dir](const auto& a, const auto& b) {
        int cmp = 0;
        switch (col) {
            case COL_ADDRESS: 
                cmp = a.address.compare(b.address);
                break;
            case COL_DELAY:
                cmp = compareDelayStr(a.indexid, b.indexid);  // from exItems_
                break;
            case COL_SPEED:
                cmp = compareSpeed(a, b);
                break;
        }
        return dir == SortDirection::Asc ? cmp < 0 : cmp > 0;
    });
    
    rebuildStore();
}
```

### Files to Modify

| File | Changes |
|------|---------|
| `src/ui/ProxyListPanel.h` | Add `SortState`, `sortState_`, `sortProxiesByColumn()` |
| `src/ui/ProxyListPanel.cpp` | Implement sorting logic, bind `wxEVT_DATAVIEW_COLUMN_HEADER_CLICKED` |

---

## F2 — Find Individual Proxy

### Current State

`findFirstProxyAsync` and `findBestProxyAsync` exist but find ANY working proxy. Need ability to find a specific proxy by criteria and highlight it.

### Solution

Add `findProxyByIndexId()` that:
1. Queries the proxy by IndexId
2. Tests it via `runWithIndexId()`
3. On success, selects and highlights the row in ProxyListPanel
4. On failure, shows "Proxy not found or test failed" message

### Implementation

```cpp
// AppController.h — add declaration
void findProxyByIndexId(const std::string& indexId);

// AppController.cpp — implement
void AppController::findProxyByIndexId(const std::string& indexId) {
    // First check if proxy exists
    auto proxies = loadProxies();
    auto it = std::find_if(proxies.begin(), proxies.end(),
        [&indexId](const auto& p) { return p.indexid == indexId; });
    
    if (it == proxies.end()) {
        wxMessageBox("Proxy not found: " + indexId, "Find Proxy", 
                     wxOK | wxICON_WARNING);
        return;
    }
    
    // Test the proxy
    testSingleProxyAsync(indexId, nullptr);
}

// MainFrame handler — already exists in Bind:
// FOUND:<indexId>:<address> → proxyPanel_->selectProxyByIndexId()
```

### Files to Modify

| File | Changes |
|------|---------|
| `src/ui/AppController.cpp` | Add `findProxyByIndexId()` method |
| `src/ui/MainFrame.cpp` | Add menu item `ID_MENU_FIND_PROXY_BY_ID` |

---

## F3 — Subscription-Proxy Linkage

### Current State

`SubscriptionPanel::onSelectionChanged` sends `SubscriptionSelectedEvent` with subId, but `MainFrame` doesn't bind this event to update `ProxyListPanel`.

### Solution

Bind `wxEVT_SUBSCRIPTION_SELECTED` in `MainFrame` constructor to call `proxyPanel_->loadProxies(subId)`.

```cpp
// MainFrame.cpp constructor — add after initPanels()
Bind(wxEVT_SUBSCRIPTION_SELECTED, [this](SubscriptionSelectedEvent& evt) {
    std::string subId = evt.getSubId();
    if (proxyPanel_) {
        proxyPanel_->loadProxies(subId);
    }
    setStatusText(0, "Loaded subscription: " + subId);
});
```

### Files to Modify

| File | Changes |
|------|---------|
| `src/ui/MainFrame.cpp` | Add `Bind(wxEVT_SUBSCRIPTION_SELECTED, ...)` |

---

## Implementation Units

| Unit | Description | Files | Est. Time |
|------|-------------|-------|-----------|
| U1 | Column header click handler + sorting logic | ProxyListPanel.h/.cpp | 1.5h |
| U2 | Find individual proxy by IndexId | AppController.cpp | 0.5h |
| U3 | Subscription selection binding | MainFrame.cpp | 0.25h |

**Total: ~2.25 hours**

---

## Verification Steps

```bash
# 1. Build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build

# 2. Test sorting
#    - Click Address column: ascending → descending → none
#    - Click Delay column: ascending (numerically) → descending → none

# 3. Test find individual proxy
#    - Menu → "Find Proxy by ID" → enter IndexId
#    - Expected: proxy selected/highlighted OR "not found" message

# 4. Test subscription linkage
#    - Click a subscription in SubscriptionPanel
#    - Expected: ProxyListPanel shows only proxies for that subscription
```

---

## Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Sorting large proxy lists may be slow | Use `rebuildStore()` efficiently, preserve selection |
| Find proxy may conflict with running tests | Use same worker thread pattern with `workerThread_` guard |
| Subscription linkage may cause flicker | Load proxies asynchronously if needed |