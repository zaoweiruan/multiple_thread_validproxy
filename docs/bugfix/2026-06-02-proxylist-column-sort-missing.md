# Bug Fix: ProxyListPanel Row# Column Sorts by IndexId + SubscriptionPanel Sort Reset Missing detectIdOffset

## Problem Description
Two related sorting issues in the UI:
1. The `#` (row number) column in ProxyListPanel sorts incorrectly - sorted by `IndexId` string instead of row number
2. SubscriptionPanel `onColumnHeaderClick` missing `detectIdOffset()` call when clearing sort, causing stale ID mappings

## Root Cause Analysis

### Issue 1: ProxyListModel.cpp (lines 217-220)
```cpp
case COL_ROWNUM:
    cmp = a.indexid.compare(b.indexid);  // WRONG: sorted by IndexId, not row number
```
The `#` column displays sequential row numbers (`row + 1`) in `GetValueByRow()`, but `Compare()` was comparing IndexId strings.

### Issue 2: SubscriptionPanel.cpp (lines 272-274)
```cpp
model_->Reset(0);
model_->Reset(static_cast<unsigned int>(subs_.size()));
// Missing detectIdOffset() call after Reset
```
After `Reset()`, the model's internal ID offset state was stale, causing incorrect lookups.

## Files Modified

| File | Change |
|------|--------|
| `src/ui/ProxyListModel.cpp` | Fixed `COL_ROWNUM` case to sort by row indices |
| `src/ui/SubscriptionPanel.cpp` | Added `model_->detectIdOffset()` after `Reset()` in sort-clear branch |

## Fix Details

### ProxyListModel.cpp - Compare for COL_ROWNUM
```cpp
case COL_ROWNUM:
    // Sort by row number (1, 2, 3...) numerically
    cmp = static_cast<int>(idx1 - idx2);  // FIXED: use view row indices
    break;
```

### SubscriptionPanel.cpp - onColumnHeaderClick sort-clear branch
```cpp
model_->Reset(0);
model_->Reset(static_cast<unsigned int>(subs_.size()));
model_->detectIdOffset();  // Added to restore correct ID mapping
```

## Verification
- Build: ✅ Success (CMake + Ninja Debug)
- Tests: ✅ 6/6 passed (44 test cases, 1 skipped)