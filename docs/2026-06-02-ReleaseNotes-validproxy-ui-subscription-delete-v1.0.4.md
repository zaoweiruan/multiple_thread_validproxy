---
title: "feat: subscription panel right-click delete functionality"
type: Release Notes
status: completed
date: 2026-06-02
---

# Subscription Panel Right-Click Delete Feature - Release Notes

## Version Information
- **Version**: 1.0.4
- **Release Date**: 2026-06-02
- **Component**: UI Subscription Panel
- **Category**: Feature Addition

## Summary
Implemented right-click context menu delete functionality for subscriptions in the UI. Users can now right-click on any subscription row and select "删除订阅" (Delete Subscription) to remove both the subscription record and all associated proxy profiles.

## New Features

### Subscription Delete Operation
- **Right-click context menu**: Added "删除订阅" menu item alongside existing "编辑订阅" and "更新订阅"
- **Confirmation dialog**: Users must confirm deletion before the operation proceeds
- **Cascade deletion**: Deleting a subscription automatically removes all associated proxy profiles from the ProfileItem table

### Database Operations
- `SubitemDAO::deleteById(id)` - Deletes subscription record by ID
- `ProfileitemDAO::deleteBySubId(subId)` - Deletes all proxies belonging to a subscription
- `AppController::deleteSubscription(subId)` - Orchestrates delete operation with proper transaction handling

## Technical Changes

### Modified Files
| File | Change |
|------|--------|
| `include/Subitem.h` | Added `escape()` and `deleteById()` methods |
| `include/Profileitem.h` | Added `escape()` and `deleteBySubId()` methods |
| `src/ui/AppController.cpp` | Added `deleteSubscription()` method |
| `src/ui/SubscriptionPanel.cpp` | Implemented `onDeleteSubscription()` handler |
| `tests/test_delete_subscription.cpp` | Added 4 unit tests for delete functionality |

### Test Coverage
- 4 new test cases covering both DAO delete methods
- All 6 test suites pass (53 tests total including new tests)

## Verification
- Build: ✅ Clean compile with no errors
- Tests: ✅ All 4 DeleteSubscriptionTest tests passed
- Integration: ✅ GUI loads and responds correctly

## Commits
```
ff3d7e2 - fix: correct DAO method indentation
2d73b1e - test: add DeleteSubscriptionTest for DAO delete methods
4b3a00f - feat: wire deleteSubscription in SubscriptionPanel onDelete handler
2297033 - feat: add deleteSubscription method to AppController
cd8077a - feat: add deleteById and deleteBySubId methods to DAO classes
```

## Dependencies
- No new external dependencies
- Uses existing SQLite3 infrastructure
- Follows existing patterns from `updateEnabled()` and `updateSubitem()` methods