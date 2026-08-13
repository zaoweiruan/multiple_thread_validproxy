# Bugfix: ProfileExItemDAO updateTestResultBatch Nested Transaction

## Problem
`ProfileExItemDAO::updateTestResultBatch()` unconditionally executed `BEGIN TRANSACTION` for every sub-batch. When the caller already had an active transaction on the same `sqlite3*` connection, SQLite returned error: "cannot start a transaction within a transaction".

## Root Cause
- Caller opened an outer transaction: `BEGIN TRANSACTION`
- `updateTestResultBatch()` attempted to begin another transaction for the same connection without checking the current transaction state
- SQLite does not support nested transactions on the same connection by default

## Solution
Made `updateTestResultBatch()` transaction-aware in `src/ProfileExItemDAO.cpp`:
- Before `BEGIN`, check `sqlite3_get_autocommit(execDb)`. Only start a new transaction if the connection is in autocommit mode.
- Track `beganTransaction` per sub-batch.
- Only emit `COMMIT` / `ROLLBACK` when `beganTransaction == true`.
- This lets the function safely participate in an existing outer transaction without crashing.

## Files Changed
- `src/ProfileExItemDAO.cpp` — `updateTestResultBatch()` now checks `sqlite3_get_autocommit()` before `BEGIN` and only commits/rolls back transactions it started
- `tests/test_profile_ex_item_dao.cpp` — Added regression test `UpdateTestResultBatch_SucceedsInsideOuterTransaction`

## Regression Test
`UpdateTestResultBatch_SucceedsInsideOuterTransaction`:
- Opens an outer `BEGIN TRANSACTION` on the connection
- Calls `updateTestResultBatch` from inside that transaction
- Asserts success, verifies data is written correctly
- Rolls back the outer transaction and asserts data does not persist

## Verification
- Build: 347/347 targets compiled successfully
- Tests: 24/24 PASSED (56.52s), including the new nested-transaction regression test
