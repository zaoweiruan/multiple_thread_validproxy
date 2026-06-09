---
title: "plan: Proxy UI Enhancement"
type: plan
status: draft
date: 2026-05-20
origin: "Migrated from docs/superpowers/plans/"
---

# Proxy UI Enhancement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add consecutive_failures and message columns to ProxyListPanel, create ProxyDetailPanel component, add search functionality.

**Architecture:** Modify ProxyListPanel to display additional proxy data columns from ProfileExItem model, create ProxyDetailPanel component for proxy details view.

**Tech Stack:** C++17, wxWidgets, SQLite, CMake/Ninja

---

## Current State Analysis

- Current columns: IndexId, Type, Address, Delay, Speed, Remarks
- ProfileExItem has: delay, speed, sort, message, consecutive_failures
- Remarks column shows Profileitem.remarks, not ProfileExItem data

## Files Overview

| File | Action | Purpose |
|------|--------|---------|
| ProxyListPanel.h | Modify | Add column enums |
| ProxyListPanel.cpp | Modify | Add consecutive_failures and message columns |
| ProxyDetailPanel.h | Create | Detail window header |
| ProxyDetailPanel.cpp | Create | Detail window implementation |
| MainFrame.cpp | Modify | Add search box to toolbar |
| MainFrame.h | Modify | Add search box member |

---

## Task 1: Add consecutive_failures column to ProxyListPanel

**Files:**
- Modify: `src/ui/ProxyListPanel.cpp` (enum and column setup)

- [ ] **Step 1: Update column enum in ProxyListPanel.cpp**

```cpp
enum {
    COL_INDEXID  = 0,
    COL_TYPE     = 1,
    COL_ADDRESS  = 2,
    COL_DELAY    = 3,
    COL_SPEED    = 4,
    COL_REMARKS  = 5,
    COL_MESSAGE  = 6,    // NEW
    COL_FAILURES = 7,    // NEW
    COL_COUNT    = 8,
};
```

- [ ] **Step 2: Add new columns to listCtrl setup**

```cpp
listCtrl_->AppendTextColumn("Message",       COL_MESSAGE, wxDATAVIEW_CELL_INERT, 120);
listCtrl_->AppendTextColumn("Failures",      COL_FAILURES, wxDATAVIEW_CELL_INERT, 80, wxALIGN_RIGHT);
```

- [ ] **Step 3: Update loadProxies to populate new columns**

```cpp
// Build message and failures lookup
std::unordered_map<std::string, std::string> msgMap;
std::unordered_map<std::string, int> failuresMap;
for (const auto& ex : exItems_) {
    msgMap[ex.indexid] = ex.message;
    failuresMap[ex.indexid] = ex.consecutive_failures;
}

// In the row loop:
row.push_back(wxVariant(msgMap.count(p.indexid) ? msgMap[p.indexid] : ""));
row.push_back(wxVariant(std::to_string(failuresMap.count(p.indexid) ? failuresMap[p.indexid] : 0)));
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --parallel 8`
Expected: Build succeeds

---

## Task 2: Add sorting support for message and failures columns

**Files:**
- Modify: `src/ui/ProxyListPanel.cpp`

- [ ] **Step 1: Add sorting cases in sortProxiesByColumn**

```cpp
case COL_MESSAGE: {
    std::string msgA = msgMap.count(a.indexid) ? msgMap[a.indexid] : "";
    std::string msgB = msgMap.count(b.indexid) ? msgMap[b.indexid] : "";
    cmp = msgA.compare(msgB);
    break;
}
case COL_FAILURES: {
    int failA = failuresMap.count(a.indexid) ? failuresMap[a.indexid] : 0;
    int failB = failuresMap.count(b.indexid) ? failuresMap[b.indexid] : 0;
    cmp = (failA > failB) - (failA < failB);
    break;
}
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build --parallel 8`
Expected: Build succeeds

---

## Task 3: Create ProxyDetailPanel component

**Files:**
- Create: `src/ui/ProxyDetailPanel.h`
- Create: `src/ui/ProxyDetailPanel.cpp`

- [ ] **Step 1: Create ProxyDetailPanel.h**

```cpp
#pragma once
#include <wx/panel.h>
#include <wx/textctrl.h>
#include "Profileitem.h"
#include "ProfileExItem.h"

class ProxyDetailPanel : public wxPanel {
public:
    ProxyDetailPanel(wxWindow* parent);
    void UpdateDetail(const db::models::Profileitem& proxy, 
                      const db::models::ProfileExItem& result);
    
private:
    wxTextCtrl* m_hostText;
    wxTextCtrl* m_portText;
    wxTextCtrl* m_delayText;
    wxTextCtrl* m_messageText;
    wxTextCtrl* m_failuresText;
    wxTextCtrl* m_remarksText;
};
```

- [ ] **Step 2: Create ProxyDetailPanel.cpp**

```cpp
#include "ProxyDetailPanel.h"
#include <wx/sizer.h>
#include <wx/stattext.h>

ProxyDetailPanel::ProxyDetailPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    auto addField = [this, &sizer](const wxString& label, wxTextCtrl*& ctrl) {
        sizer->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALL, 5);
        ctrl = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
        sizer->Add(ctrl, 0, wxEXPAND|wxALL, 5);
    };
    
    addField("Host:", m_hostText);
    addField("Port:", m_portText);
    addField("Delay:", m_delayText);
    addField("Message:", m_messageText);
    addField("Failures:", m_failuresText);
    addField("Remarks:", m_remarksText);
    
    SetSizer(sizer);
}

void ProxyDetailPanel::UpdateDetail(const db::models::Profileitem& proxy,
                                    const db::models::ProfileExItem& result) {
    m_hostText->SetValue(proxy.address);
    m_portText->SetValue(proxy.port);
    m_delayText->SetValue(result.delay);
    m_messageText->SetValue(result.message);
    m_failuresText->SetValue(std::to_string(result.consecutive_failures));
    m_remarksText->SetValue(proxy.remarks);
}
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --parallel 8`
Expected: Build succeeds

---

## Task 4: Add search box to MainFrame toolbar

**Files:**
- Modify: `src/ui/MainFrame.h`
- Modify: `src/ui/MainFrame.cpp`

- [ ] **Step 1: Add search box member in MainFrame.h**

```cpp
private:
    wxTextCtrl* m_searchBox;
```

- [ ] **Step 2: Create search box in MainFrame.cpp toolbar**

```cpp
m_searchBox = new wxTextCtrl(m_toolbar, wxID_ANY, "", wxDefaultPosition, wxSize(200, -1));
m_searchBox->SetHint("搜索代理...");
m_toolbar->AddControl(m_searchBox);
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --parallel 8`
Expected: Build succeeds

---

## Task 5: Integrate ProxyDetailPanel into main layout

**Files:**
- Modify: `src/ui/MainFrame.h`
- Modify: `src/ui/MainFrame.cpp`

- [ ] **Step 1: Include header and declare panel**

```cpp
#include "ProxyDetailPanel.h"
// ...
private:
    ProxyDetailPanel* m_detailPanel;
```

- [ ] **Step 2: Create and add to AUI manager**

```cpp
m_detailPanel = new ProxyDetailPanel(m_manager.GetFrame());
m_manager.AddPane(m_detailPanel, wxAuiPaneInfo()
    .Right()
    .Caption("代理详情")
    .BestSize(320, 690));
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --parallel 8`
Expected: Build succeeds

---

## Task 6: Connect proxy selection to detail panel update

**Files:**
- Modify: `src/ui/ProxyListPanel.h`
- Modify: `src/ui/MainFrame.cpp`

- [ ] **Step 1: Add selection event in ProxyListPanel.h**

```cpp
void onSelectProxy(wxDataViewEvent& event);
```

- [ ] **Step 2: Bind selection event in ProxyListPanel constructor**

```cpp
Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &ProxyListPanel::onSelectProxy, this);
```

- [ ] **Step 3: Implement selection handler in ProxyListPanel.cpp**

```cpp
void ProxyListPanel::onSelectProxy(wxDataViewEvent& event) {
    wxDataViewItem item = listCtrl_->GetSelection();
    if (!item.IsOk()) return;
    
    wxVariant idxVar;
    store_->GetValue(idxVar, item, COL_INDEXID);
    std::string indexId = idxVar.GetString().ToStdString();
    
    // Find proxy and result
    db::models::Profileitem proxy;
    db::models::ProfileExItem result;
    for (const auto& p : proxies_) {
        if (p.indexid == indexId) { proxy = p; break; }
    }
    for (const auto& r : exItems_) {
        if (r.indexid == indexId) { result = r; break; }
    }
    
    // Notify parent
    wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED, ID_PROXY_SELECTED);
    ProxyDetailData* data = new ProxyDetailData{proxy, result};
    evt.SetClientData(data);
    wxPostEvent(GetParent(), evt);
}
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build --parallel 8`
Expected: Build succeeds

---

## Task 7: Run full test suite

**Files:**
- Test: Manual verification

- [ ] **Step 1: Build All**

Run: `cmake --build build --parallel 8`
Expected: All builds pass

- [ ] **Step 2: Run Tests**

Run: `ctest -V`
Expected: All tests pass

- [ ] **Step 3: Launch Application**

Run: `build/bin/ProxyManager.exe`
Expected: UI shows new columns and detail panel
