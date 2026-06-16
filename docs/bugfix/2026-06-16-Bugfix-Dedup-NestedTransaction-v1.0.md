# Bugfix: Nested Transaction Error in Deduplicate

## Problem
`deduplicate()` in SubitemUpdaterV2.cpp starts a transaction at line 2072, then calls `deduplicateConfigErrorPhase()` which internally calls `deleteByIndexId()`. The `deleteByIndexId()` method begins its own transaction with `BEGIN`, causing SQLite error: "cannot start a transaction within a transaction".

## Root Cause
- `deduplicate()` → `BEGIN TRANSACTION` (outer)
- `deduplicateConfigErrorPhase()` → `deleteByIndexId()` → `BEGIN` (inner, fails)

## Solution
Added `deleteByIndexIdNoTx()` method in ProfileitemDAO that performs DELETE operations without transaction wrapper. Updated `deduplicateConfigErrorPhase()` to use this new method.

## Files Changed
- `src/ProfileitemDAO.cpp` — Added `deleteByIndexIdNoTx()` (lines 149-181)
- `src/SubitemUpdaterV2.cpp` — Updated to use `deleteByIndexIdNoTx()` instead of `deleteByIndexId()`
- `tests/test_dedup.cpp` — Added test `DeleteWithinTransactionNoNestedError`

## Status
DONE - Verified with 14/14 dedup tests passing.