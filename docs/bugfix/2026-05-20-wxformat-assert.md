---
title: "fix: wxWidgets debug assertion format string mismatch"
type: fix
status: completed
date: 2026-05-20
---

# wxWidgets Debug Assertion Format String Fix

> **Date:** 2026-05-20  
> **Component:** UI Panel Code

## Bug Summary

wxWidgets Debug Alert: Format specifier `%zu` doesn't match argument type in `wxArgNormalizer()` on MinGW-w64 builds.

## Root Cause

Using `%zu` format specifier with wxWidgets `wxString::Format()` is not portable on MinGW-w64 where format string checking is strict.

## Fix Implementation

### Files Changed
| File | Line | Change |
|------|------|--------|
| `src/ui/ProxyListPanel.cpp` | 92 | `%zu` → `%d` + `static_cast<int>()` |
| `src/ui/SubscriptionPanel.cpp` | 98 | `%zu` → `%d` + `static_cast<int>()` |

## Before/After

```cpp
// Before
row.push_back(wxVariant(wxString::Format("%zu", proxies.size())));

// After
row.push_back(wxVariant(wxString::Format("%d", static_cast<int>(proxies.size()))));
```

## Test Results
- [x] Build successful (0 errors)
- [x] All 3 CTest tests passed