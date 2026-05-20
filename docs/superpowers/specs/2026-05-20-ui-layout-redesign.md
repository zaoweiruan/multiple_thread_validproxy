# UI Layout Redesign - Main Frame

**Date:** 2026-05-20
**Author:** Kilo
**Status:** Proposed

## Overview

Redesign the main application window layout to match the `main-layout.svg` design sketch. This involves reorganizing the panel structure, removing TestPanel from the main layout, and adjusting LogPanel to use white background with black text.

## Current Layout Analysis

### Current Structure (MainFrame.cpp)
```
┌─────────────────────────────────────────────────────────────┐
│ Menu Bar + Toolbar                                           │
├─────────────────────────────────────────────────────────────┤
│ SubscriptionPanel (1/2 width)  TestPanel (1/2 width)         │
│ Sub list                       Progress + Results + Log     │
├─────────────────────────────────────────────────────────────┤
│ ProxyListPanel (3/4 width)  ProxyDetailPanel (1/4 width)     │
│ Proxy table                 Proxy details                   │
└─────────────────────────────────────────────────────────────┘
```

### Issues Identified
1. TestPanel is embedded in the top row, mixing test UI with subscriptions
2. Log is inside TestPanel (dark theme) - should be standalone white theme
3. No top-right search box
4. ProxyDetailPanel doesn't span full height
5. Column proportions don't match design

## Target Layout

### New Structure
```
┌─────────────────────────────────────────────────────────────┐
│ Title Bar: "代理管理面板"                   Search Box       │
├─────────────────────────────────────────────────────────────┤
│ SubscriptionPanel  │ ProxyListPanel  │                     │
│ (380×380)          │ (620×300)       │ ProxyDetailPanel    │
│ Sub list           │ Proxy table     │ (320×690, full     │
│                    │                 │  height)            │
├──────────────────────────────────────┼─────────────────────┤
│ LogPanel                                        │           │
│ (620×260, below ProxyList)           │                     │
└─────────────────────────────────────────────────────────────┘
```

## Implementation Plan

### Phase 1: LogPanel Refactor
- [ ] Change background to white, text to black
- [ ] Remove per-level color coding (keep simple black text)
- [ ] Keep filter dropdown but simplify styling

### Phase 2: MainFrame Layout Restructure
- [ ] Add search box to toolbar (top-right)
- [ ] Remove TestPanel from main layout
- [ ] Reorganize sizers to match 3-column top + log bottom
- [ ] Adjust column proportions (sub: 380, proxy: 620, detail: 320)
- [ ] Make ProxyDetailPanel span full height (vertically stretch)

### Phase 3: ProxyListPanel Column Alignment
- [ ] Update columns to match design: # | Host | Port | Latency | Failures | Remarks | Message
- [ ] Add click-to-sort indicators (↕) to sortable columns

### Phase 4: SubscriptionPanel Update
- [ ] Add # column at start
- [ ] Update column widths to match design proportions
- [ ] Ensure proper 380px width constraint

## Technical Details

### Sizer Structure (New)
```cpp
wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

// Top row: Subscriptions | ProxyList | Detail
wxBoxSizer* topRow = new wxBoxSizer(wxHORIZONTAL);
topRow->Add(subPanel_, 0, wxEXPAND | wxRIGHT, 2);   // 380px fixed width
topRow->Add(proxyPanel_, 1, wxEXPAND | wxRIGHT, 2);  // 620px proportional
topRow->Add(detailPanel_, 0, wxEXPAND);              // 320px fixed width

// Bottom: Log panel spanning under proxy list area
wxBoxSizer* bottomRow = new wxBoxSizer(wxHORIZONTAL);
bottomRow->Add(logPanel_, 1, wxEXPAND);

mainSizer->Add(topRow, 1, wxEXPAND);
mainSizer->Add(bottomRow, 0, wxEXPAND);
```

### Panel Width Constraints
- SubscriptionPanel: `SetMinSize(wxSize(380, -1))`
- ProxyListPanel: Allow proportional growth up to ~620px
- ProxyDetailPanel: `SetMinSize(wxSize(320, -1))`

## Risks & Considerations

1. **TestPanel removal**: Test functionality must still work via controller events
2. **Log callback conflict**: Both TestPanel and LogPanel register callbacks - resolve ownership
3. **Window resize**: Need min/max constraints to maintain proportions

## Success Criteria

- [ ] Layout matches SVG dimensions proportionally
- [ ] Search box appears at top-right
- [ ] Log panel shows in white background
- [ ] ProxyDetailPanel spans from top to bottom
- [ ] All existing functionality works