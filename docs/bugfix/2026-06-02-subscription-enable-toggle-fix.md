# Bug Fix: Subscription Enable Toggle Not Working

## Problem Description
The "启用" (Enable) checkbox column in SubscriptionPanel was not responding to clicks. Users could not toggle subscription enabled/disabled state through the UI.

## Root Cause Analysis
During the refactoring from `wxDataViewListStore` to `SubscriptionListModel` (virtual model approach), two issues were introduced in commit 7c1cc03:

1. **Missing SetValueByRow handler**: `SubscriptionListModel::SetValueByRow()` only handled `SUB_COL_NAME` column, ignoring `SUB_COL_ENABLED` (column 1)
2. **Wrong column type**: `GetColumnType()` returned `"string"` for all columns, but toggle columns require `"bool"` type for proper editing behavior

## Files Modified

| File | Change |
|------|--------|
| `src/ui/SubscriptionListModel.cpp` | Fixed `GetColumnType()` to return `"bool"` for `SUB_COL_ENABLED` |
| `src/ui/SubscriptionListModel.cpp` | Added `SUB_COL_ENABLED` handling in `SetValueByRow()` |
| `src/ui/SubscriptionPanel.cpp` | Removed redundant model update code (now handled by model layer) |

## Fix Details

### GetColumnType (line 43-49)
```cpp
wxString SubscriptionListModel::GetColumnType(unsigned int col) const {
    if (col == SUB_COL_ENABLED) {
        return wxT("bool");
    }
    return wxT("string");
}
```

### SetValueByRow (line 108-111)
```cpp
if (col == SUB_COL_ENABLED) {
    (*subscriptions_)[row].enabled = variant.GetBool() ? "1" : "0";
    return true;
}
```

## Verification
- Build: ✅ Success (CMake + Ninja Debug)
- Tests: ✅ 6/6 passed (42 test cases)