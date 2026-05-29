---
title: "feat: ConfigGenerator transaction batch — single query for all profile items"
type: feat
status: cancelled
date: 2026-05-11
origin: "Identified during performance review: ConfigGenerator::loadProfiles executes per-row processing that could be batched"
---

# feat: ConfigGenerator Transaction Batch

## Problem

`ConfigGenerator::loadProfiles()` currently processes profile items one at a time with individual database operations. When loading thousands of proxies, this creates significant overhead.

## Scope Boundaries

- Modify: `src/ConfigGenerator.cpp`, `src/ConfigGenerator.h`
- NOT modify: Database schema, `ProxyBatchTester`, `SubitemUpdaterV2`

## Detailed Changes

### U1: Wrap loadProfiles in explicit transaction
- Begin transaction before query loop
- Commit after all profiles processed

### U2: Single query approach
- Replace N individual SELECTs with a single batched query
- Use prepared statement with proper indexing

### U3: Error rollback
- On any failure in the batch, rollback transaction
- Log error with full context

## Verification

- Compile pass (0 errors)
- Unit test: All existing tests pass
- Function test: `-T <subid>` loads all proxies correctly