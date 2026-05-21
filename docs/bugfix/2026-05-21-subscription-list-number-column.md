---
title: "fix: Subscription list number column displays blank"
type: fix
status: completed
date: 2026-05-21
---

# Subscription List: Number Column Blank Fix

## Bug Summary

Subscription list "#" column displays blank instead of row numbers.

## Root Cause

In `src/ui/SubscriptionPanel.cpp` line 99:
```cpp
row.push_back(wxVariant(rowNum++));  // Wrong - passes integer to wxVariant
```

The `wxDataViewListStore` text column requires string values. Passing integer to `wxVariant` doesn't display correctly.

## Fix

Changed to match `ProxyListPanel.cpp` pattern:
```cpp
row.push_back(wxVariant(wxString::Format("%d", rowNum++)));
```

## Test Results
- [x] Build successful
- [x] 11 unit tests passed
- [x] Subscription list displays row numbers correctly