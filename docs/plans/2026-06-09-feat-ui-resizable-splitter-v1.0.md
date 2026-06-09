---
title: "feat(UI): Resizable horizontal splitter for Subscription and ProxyList panels"
type: feat
status: completed
date: 2026-06-09
origin: "User: '调整功能：将订阅窗口、proxylist的宽度分配调整为可横向拖动'"
---

# Implementation Plan: Resizable Splitter for Subscription and ProxyList Panels

## Overview

Replace the fixed-proportion wxBoxSizer horizontal layout with wxSplitterWindow to allow users to drag and resize the subscription panel and proxy list panel widths interactively.

## Current State

In `src/ui/MainFrame.cpp:initPanels()`:
- SubscriptionPanel and ProxyListPanel are placed in a `wxBoxSizer(wxHORIZONTAL)` with proportions 1:2
- Widths are fixed at runtime; users cannot adjust the split

## Solution

Use `wxSplitterWindow` between subscription and proxy panels:
1. Create a `wxSplitterWindow` with centerPanel as parent
2. Add SubscriptionPanel to left pane, ProxyListPanel to right pane  
3. Set initial split position (e.g., 360px ≈ 30% of window width)
4. Set minimum pane sizes to prevent collapse (250px)

## Implementation

### Changes to MainFrame.h
- Added `#include <wx/splitter.h>`
- Added `wxSplitterWindow* splitter_{nullptr};` member

### Changes to MainFrame.cpp

```cpp
// Lines 443-450: Replace horizontal sizer with wxSplitterWindow
wxSplitterWindow* splitter_ = new wxSplitterWindow(centerPanel, wxID_ANY,
                                                wxDefaultPosition, wxDefaultSize,
                                                wxSP_3DSASH);
splitter_->SetMinimumPaneSize(250);
splitter_->SplitVertically(subPanel_, proxyPanel_, 360);

centerSizer->Add(splitter_, 1, wxEXPAND);
```

### Critical Fix (Bug Found During Implementation)

**Issue**: wxWidgets Debug Alert assertion failure in splitter.cpp(842) - "windows must have splitter as parent"

**Root Cause**: Panels `subPanel_` and `proxyPanel_` were created with `centerPanel` as parent, but wxSplitterWindow requires its panes to have splitter as parent.

**Fix**: Reorder init sequence in `initPanels()`:
1. Create `splitter_` first (with `centerPanel` as parent)
2. Create `subPanel_` and `proxyPanel_` with `splitter_` as parent  
3. Call `splitter_->SplitVertically()` after panels exist

### Files Modified

| File | Changes |
|------|---------|
| `src/ui/MainFrame.h` | Added splitter_ member and include |
| `src/ui/MainFrame.cpp` | Replaced horizontal sizer with wxSplitterWindow, reordered panel creation |

## Verification Steps

```powershell
# Build
cmake --build build --parallel 8

# Run
.\build\validproxy.exe

# Test: Drag the sash between subscription and proxy list panels
# - Subscription panel cannot collapse below 250px
# - Proxy list panel cannot collapse below 250px  
# - Initial split at 360px (left), rest for right panel
```

## Result

- Build succeeded
- Horizontal splitter allows interactive resize between subscription and proxy list panels
- Minimum pane size enforced at 250px each
- Initial position set to 360px for subscription panel