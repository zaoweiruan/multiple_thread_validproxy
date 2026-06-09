---
title: "feat: subscription panel column sorting functionality"
type: Release Notes
status: completed
date: 2026-06-02
---

# Subscription Panel Column Sorting - Release Notes

## Version Information
- **Version**: 1.0.4
- **Release Date**: 2026-06-02
- **Component**: UI Subscription Panel
- **Category**: Feature Addition

## Summary
Implemented column header click sorting functionality for the subscription panel. Users can click column headers (Name, Proxies, Update) to sort the subscription list in three-state cycle (ascending → descending → none).

## New Features

### Column Sorting
- **Three-state sorting**: Clicking a column cycles through sort states: no indicator → ↕ (ascending) → ↕ (descending) → no indicator
- **Name column**: String sort on subscription name (remarks)
- **Proxies column**: Numeric sort on proxy count
- **Update column**: Sort by update timestamp, "Never" values appear last (oldest)

### Visual Indicators
- Column headers display ↕ symbol to indicate sortable columns
- wxDataViewColumn sort arrow shown during active sort

## Technical Changes

### New Files
| File | Purpose |
|------|---------|
| `src/ui/SubscriptionListModel.h` | Virtual sortable model extending `wxDataViewIndexListModel` |
| `src/ui/SubscriptionListModel.cpp` | `Compare()` implementation with three-state sort logic |

### Modified Files
| File | Change |
|------|--------|
| `src/ui/SubscriptionPanel.h` | Replaced `wxDataViewListStore*` with `SubscriptionListModel*`, added `sortState_` member |
| `src/ui/SubscriptionPanel.cpp` | Replaced store_ with model_, added `onColumnHeaderClick()` handler, updated column labels |
| `CMakeLists.txt` | Added `SubscriptionListModel.h/cpp` to UI_SOURCES |

### Architecture
- Converted from immediate-mode store (`wxDataViewListStore`) to virtual-mode model (`wxDataViewIndexListModel`)
- Follows the same pattern as `ProxyListPanel`/`ProxyListModel` for consistency
- Non-owning pointers to data vectors for efficient memory usage

## Verification
- Build: ✅ Clean compile with no errors
- Tests: ✅ All existing tests pass (53 tests total)

## Commits
```
7c1cc03 - feat: add subscription panel column sorting for Name/Proxies/Update
```