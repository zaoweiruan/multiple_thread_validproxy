---
title: "fix: ProxyListPanel column sorting, ProxyDetailPanel scrollbar and all fields"
type: fix
status: completed
date: 2026-05-20
---

# ProxyListPanel: Column Sorting & Detail Panel Scrollbar Fix

> **Date:** 2026-05-20  
> **Component:** ProxyListPanel, ProxyDetailPanel

## Bug Summary

1. Clicking sortable columns (Latency ↕, Failures ↕) in proxy list had no effect
2. Proxy detail panel needed scrollbar for all ProfileItem fields
3. Display all ProfileItem table fields in detail panel

## Root Cause

### Bug 1: Sorting Not Working
**Location:** `ProxyListPanel.cpp` event table (lines 28-33)

The `onColumnHeaderClick` handler exists but the event was commented out:
```cpp
// Missing: EVT_DATAVIEW_COLUMN_HEADER_CLICK
```

### Bug 2 & 3: Detail Panel
**Location:** `ProxyDetailPanel`
- Using `wxBoxSizer` directly without `wxScrolled` wrapper
- Only 9 fields implemented instead of 33+ ProfileItem fields

## Fix Implementation

### Files Changed
| File | Changes |
|------|---------|
| `src/ui/ProxyListPanel.cpp` | Added `EVT_DATAVIEW_COLUMN_HEADER_CLICK` in event table |
| `src/ui/ProxyDetailPanel.h` | Changed base class to `wxScrolled<wxPanel>`, added field labels |
| `src/ui/ProxyDetailPanel.cpp` | Implemented scrolling, added all ProfileItem fields |

## Test Results
- [x] Build successful
- [x] Column header click event now bound
- [x] Detail panel uses wxScrolled for scrollbar