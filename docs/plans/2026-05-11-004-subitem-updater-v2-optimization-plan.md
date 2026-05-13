---
title: "refactor: SubitemUpdaterV2 optimization — batch update, connection reuse, progress callback"
type: refactor
status: completed
date: 2026-05-11
origin: "Identified during log-level audit: SubitemUpdaterV2 has N+1 query pattern, no connection reuse, no progress reporting"
---

# refactor: SubitemUpdaterV2 Optimization

## Problem

`SubitemUpdaterV2` currently suffers from performance issues when updating large subscriptions:

1. **N+1 query pattern**: Each proxy item updates the database individually
2. **No connection reuse**: Database connections are opened/closed per operation
3. **No progress callback**: Long-running updates provide no feedback to users
4. **Transaction overhead**: Each insert/update is auto-committed individually

## Scope Boundaries

- Modify: `src/SubitemUpdaterV2.cpp`, `src/SubitemUpdaterV2.h`
- NOT modify: Database schema, `ConfigGenerator`, `XrayInstance`

## Detailed Changes

### U1: Batch transaction support
- Wrap all updates in a single transaction
- Commit at end of batch instead of per-item

### U2: Connection reuse
- Pass sqlite3* connection through update chain instead of reopening per operation

### U3: Progress callback
- Add periodic progress logging every N items (e.g., every 100)

### U4: Prepared statement reuse
- Prepare statements once, bind/step/finalize in loop

## Verification

- Compile pass (0 errors)
- Unit test: `test_dedup` still passes
- Function test: `-U <subid>` completes without error