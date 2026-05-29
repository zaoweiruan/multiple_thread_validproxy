---
title: "fix: Proxy list indexId column, search box, and log filtering"
type: fix
status: completed
date: 2026-05-20
---

# Proxy List IndexId, Search Box, and Log Filter Fix

## Bug Summary

1. Proxy list displays row numbers but needs actual indexId for proxy lookups
2. Search box doesn't filter the proxy list
3. Log output only shows TRACE level messages on startup
4. Subscription update time shows Unix timestamp instead of readable datetime

## Root Cause

### Bug 1: IndexId Column
Row number displayed in COL_INDEXID column, but handlers expected actual indexId for lookups.

### Bug 2: Search Box
`onSearchBoxEnter()` only updated status bar, didn't call filter method.

### Bug 3: Log Filtering
`minLevel_` initialized to TRACE but dropdown defaults to INFO - mismatch.

### Bug 4: DateTime Format
Raw Unix timestamp displayed instead of formatted datetime.

## Fix Implementation

### Files Changed
| File | Changes |
|------|---------|
| `src/ui/ProxyListPanel.h` | Added `allProxies_`, `currentSubId_`, `filterBySearch()` |
| `src/ui/ProxyListPanel.cpp` | Split COL_INDEXID/COL_ROWNUM, implemented filterBySearch() |
| `src/ui/MainFrame.cpp` | Wired search box to filterBySearch() |
| `src/ui/LogPanel.cpp` | Initialize minLevel_ to match default filter selection |
| `src/ui/SubscriptionPanel.cpp` | Added `formatUpdateTime()` for datetime formatting |

## Test Results
- [x] Build successful (0 errors)
- [x] 14 unit tests passed (test_model: 3, test_dedup: 11)