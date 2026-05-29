---
title: "feat: Search enhancements, auto-load first subscription, and database path panel"
type: feat
status: draft
date: 2026-05-21
---

# Design: Search Input Box Enhancements and UI Improvements

## Overview

This feature adds four related UI enhancements to the validproxy application:
1. Real-time search filtering as user types
2. Clear button (❌) in search input box
3. Auto-load first subscription on UI startup
4. Database path display panel in toolbar

## Current State Analysis

### Existing Code
- `MainFrame::initToolBar()` (line 219-233): Creates search box `m_searchBox` with `ID_SEARCH_BOX`
- `MainFrame::onSearchBoxEnter()` (line 440-447): Currently only updates status bar
- `ProxyListPanel::filterBySearch()` (line 427-442): Already implemented filtering logic
- `SubscriptionPanel::loadSubscriptions()` (line 92-112): Loads subscriptions into list
- `ProxyListPanel::loadProxies()` (line 102-145): Loads proxies for a subscription

### Gaps
1. No `EVT_TEXT` handler for real-time filtering
2. No clear button implementation
3. `initPanels()` loads proxies with empty subId instead of first subscription
4. No database path display in toolbar

## Proposed Design

### 1. Real-time Search Filtering

**File: `src/ui/MainFrame.cpp`**

```cpp
// In initToolBar(), change to bind EVT_TEXT:
m_searchBox->Bind(wxEVT_TEXT, &MainFrame::onSearchTextChanged, this);

// New handler:
void MainFrame::onSearchTextChanged(wxCommandEvent& event) {
    if (proxyPanel_) {
        proxyPanel_->filterBySearch(m_searchBox->GetValue());
    }
}
```

**Event Flow:**
1. User types in search box → `wxEVT_TEXT` triggered
2. Handler calls `proxyPanel_->filterBySearch(query)`
3. `filterBySearch()` filters `proxies_` based on address/remarks/indexId match
4. `loadProxies(currentSubId_)` rebuilds the view with filtered list

### 2. Clear Button (❌)

**Approach: Custom composite control using wxComboCtrl**

```cpp
// In MainFrame.h:
class SearchCtrl : public wxComboCtrl {
    wxTextCtrl* m_textCtrl;
    wxBitmapButton* m_clearBtn;
};

// In initToolBar():
m_searchBox = new SearchCtrl(tb, ID_SEARCH_BOX);
```

**Alternative (simpler): Overlay bitmap button**
```cpp
// Keep existing wxTextCtrl, add clear button as separate toolbar control
tb->AddControl(m_searchBox);
tb->AddTool(ID_SEARCH_CLEAR, "❌", /* bitmap */);
```

### 3. Auto-load First Subscription

**File: `src/ui/MainFrame.cpp`**

```cpp
void MainFrame::initPanels() {
    // ... existing code ...
    
    subPanel_->loadSubscriptions();
    
    // Load first subscription's proxies automatically
    if (proxyPanel_ && !subPanel_->getSubscriptions().empty()) {
        std::string firstSubId = subPanel_->getSubscriptions()[0].id;
        proxyPanel_->loadProxies(firstSubId);
    }
}
```

**Add to `SubscriptionPanel.h`:**
```cpp
const std::vector<db::models::Subitem>& getSubscriptions() const { return subs_; }
```

### 4. Database Path Panel

**File: `src/ui/MainFrame.cpp`**

```cpp
void MainFrame::initToolBar() {
    // ... existing controls ...
    
    tb->AddControl(m_searchBox);
    
    // Add database path static text
    wxString dbPath = wxString::FromUTF8(controller_->getDbPath());
    m_dbPathLabel = new wxStaticText(tb, wxID_ANY, dbPath, wxDefaultPosition, wxSize(200, -1));
    tb->AddControl(m_dbPathLabel);
    
    tb->Realize();
}
```

**Add to `MainFrame.h`:**
```cpp
wxStaticText* m_dbPathLabel;
std::string getDbPath() const; // from controller or config
```

## File Changes Summary

| File | Changes |
|------|---------|
| `src/ui/MainFrame.h` | Add `wxStaticText* m_dbPathLabel`, `onSearchTextChanged()` declaration, `getDbPath()` |
| `src/ui/MainFrame.cpp` | Add `wxEVT_TEXT` binding, `onSearchTextChanged()` handler, auto-load first sub, db path label |
| `src/ui/SubscriptionPanel.h` | Add `getSubscriptions()` accessor |

## Success Criteria

1. [ ] Typing in search box immediately filters proxy list (case-insensitive)
2. [ ] Clear button (❌) clears search text and restores full list
3. [ ] Application starts with first subscription's proxies loaded
4. [ ] Database path visible in toolbar's right side
5. [ ] All existing tests pass
6. [ ] Build succeeds with 0 errors

## Alternative Approaches Considered

| Approach | Pros | Cons |
|----------|------|------|
| Use wxSearchCtrl | Built-in clear button | Different look/feel from current |
| Debounced filtering | Better performance | Adds complexity, delay |
| Menu item for DB path | More space | Less discoverable