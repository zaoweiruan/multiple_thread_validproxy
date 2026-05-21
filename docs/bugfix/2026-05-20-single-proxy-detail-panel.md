---
title: "fix: Single proxy test failure and proxy detail panel sync"
type: fix
status: completed
date: 2026-05-20
---

# ProxyListPanel: Single Proxy Test & Detail Panel Sync Fix

> **Date:** 2026-05-20  
> **Component:** ProxyListPanel, ProxyDetailPanel, AppController

## Bug Summary

1. Testing single proxy fails with "Proxy not found" - row number used instead of actual indexId
2. Proxy detail panel shows no content and doesn't synchronize with table selection

## Root Cause

### Bug 1: Single Proxy Test Failure
**Location:** `ProxyListPanel.cpp` line 92

The "#" column displayed row count (1, 2, 3...) instead of actual `indexid` from database. When testing proxy, `onTestProxy` retrieved this row number as indexId, which didn't match any database record.

### Bug 2: Detail Panel Sync
**Location:** `ProxyListPanel.cpp` and `ProxyDetailPanel.cpp`
- Wrong indexId stored in column
- Detail panel missing advanced fields

## Fix Implementation

### Files Changed
| File | Changes |
|------|---------|
| `src/ui/ProxyListPanel.cpp` | Store actual `p.indexid` in last column; Fixed column indices ordering |
| `src/ui/ProxyDetailPanel.h` | Added 9 advanced field labels |
| `src/ui/ProxyDetailPanel.cpp` | Complete rewrite with group boxes and all fields |
| `src/ui/AppController.h` | Added `getProxyByIndexId()` method |
| `src/ui/AppController.cpp` | Implemented `getProxyByIndexId()` |
| `src/ui/MainFrame.cpp` | Updated to fetch and pass full proxy data |

## Test Results
- [x] Build successful with 0 errors
- [x] ProxyListPanel stores actual indexId for correct proxy lookup
- [x] ProxyDetailPanel displays all ProfileItem fields