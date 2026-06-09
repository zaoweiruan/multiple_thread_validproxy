---
title: "plan: UI Layout Redesign"
type: plan
status: draft
date: 2026-05-20
origin: "Migrated from docs/superpowers/plans/"
---

# UI Layout Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Redesign main application window to match main-layout.svg design with 3-column top layout and bottom log panel.

**Architecture:** Restructure MainFrame sizers to create subscription list (380px) | proxy list (620px) | detail panel (320px) on top row, with log panel spanning below proxy list. Remove TestPanel from main layout.

**Tech Stack:** wxWidgets (wxDataViewCtrl, wxBoxSizer, wxPanel), C++17

---

## Files to Modify

- `src/ui/MainFrame.cpp` - Main layout restructuring
- `src/ui/MainFrame.h` - Remove TestPanel member (keep LogPanel)
- `src/ui/LogPanel.cpp` - White background, black text
- `src/ui/ProxyListPanel.cpp` - Update columns to match design
- `src/ui/SubscriptionPanel.cpp` - Add # column
- `docs/superpowers/specs/2026-05-20-ui-layout-redesign.md` - Already created

---

### Task 1: LogPanel White Background Refactor

**Files:**
- Modify: `src/ui/LogPanel.cpp`
- Test: Visual inspection after build

- [ ] **Step 1: Change background to white and text to black**

```cpp
// Line 63-66 in LogPanel.cpp - change wxTE_RICH to simple and set colors
logCtrl_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                           wxDefaultPosition, wxDefaultSize,
                           wxTE_MULTILINE | wxTE_READONLY |  // Remove wxTE_RICH
                           wxHSCROLL);
logCtrl_->SetBackgroundColour(wxColour(255, 255, 255));  // White
logCtrl_->SetForegroundColour(wxColour(0, 0, 0));        // Black
```

- [ ] **Step 2: Remove per-level color coding in appendLog**

```cpp
// Replace lines 88-101 with simple black text:
void LogPanel::appendLog(const wxString& msg, LogLevel /*level*/) {
    if (static_cast<int>(minLevel_) > static_cast<int>(LogLevel::TRACE)) {
        return;
    }
    logCtrl_->AppendText(msg + "\n");
    logCtrl_->ShowPosition(logCtrl_->GetLastPosition());
}
```

- [ ] **Step 3: Run build to verify compilation**

Run: `cmake --build build --parallel 8`
Expected: Build succeeds without errors

---

### Task 2: MainFrame Header Cleanup

**Files:**
- Modify: `src/ui/MainFrame.h`

- [ ] **Step 1: Remove TestPanel forward declaration and member**

```cpp
// Line 19: Remove TestPanel forward decl
// Line 35: Remove getTestPanel() method
// Line 83: Remove testPanel_ member
```

- [ ] **Step 2: Build to verify**

Run: `cmake --build build --parallel 8`

---

### Task 3: MainFrame Layout Restructure

**Files:**
- Modify: `src/ui/MainFrame.cpp`

- [ ] **Step 1: Remove TestPanel initialization from initPanels (lines 246-265)**

```cpp
// Replace the entire initPanels function with:
void MainFrame::initPanels() {
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    // Top row: subscription | proxy list | detail
    wxBoxSizer* topSizer = new wxBoxSizer(wxHORIZONTAL);
    subPanel_ = new SubscriptionPanel(this, controller_);
    proxyPanel_ = new ProxyListPanel(this, controller_, db_, nullptr);  // no testPanel
    detailPanel_ = new ProxyDetailPanel(this);
    logPanel_ = new LogPanel(this);

    // Size hints for column widths
    subPanel_->SetMinSize(wxSize(380, -1));
    proxyPanel_->SetMinSize(wxSize(620, -1));
    detailPanel_->SetMinSize(wxSize(320, -1));

    topSizer->Add(subPanel_, 0, wxEXPAND | wxRIGHT, 2);
    topSizer->Add(proxyPanel_, 1, wxEXPAND | wxRIGHT, 2);
    topSizer->Add(detailPanel_, 0, wxEXPAND);
    sizer->Add(topSizer, 1, wxEXPAND | wxALL, 2);

    // Bottom row: log panel under proxy list area
    sizer->Add(logPanel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 2);
    logPanel_->SetMinSize(wxSize(620, 260));

    SetSizer(sizer);

    // Load initial data
    subPanel_->loadSubscriptions();
    proxyPanel_->loadProxies("");
}
```

- [ ] **Step 2: Update destructor to remove testPanel_ cleanup (lines 146-160)**

```cpp
// Remove lines 162-165 (configDialog_ deletion already exists)
// Remove testPanel_ references if any exist in destructor
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --parallel 8`

---

### Task 4: ProxyListPanel Columns Update

**Files:**
- Modify: `src/ui/ProxyListPanel.cpp`

- [ ] **Step 1: Update columns to match design (# | Host | Port | Latency | Failures | Remarks | Message)**

```cpp
// Replace lines 56-63 with:
// Columns matching main-layout.svg: # (40) | Host (100) | Port (70) | Latency (80) | Failures (80) | Remarks (160) | Message (160)
listCtrl_->AppendTextColumn("#",        COL_INDEXID,  wxDATAVIEW_CELL_INERT, 40);
listCtrl_->AppendTextColumn("Host ↕",  COL_ADDRESS,  wxDATAVIEW_CELL_INERT, 100);
listCtrl_->AppendTextColumn("Port",     COL_TYPE,     wxDATAVIEW_CELL_INERT, 70);
listCtrl_->AppendTextColumn("Latency ↕", COL_DELAY,   wxDATAVIEW_CELL_INERT, 80);
listCtrl_->AppendTextColumn("Failures ↕", COL_FAILURES, wxDATAVIEW_CELL_INERT, 80);
listCtrl_->AppendTextColumn("Remarks",  COL_REMARKS,  wxDATAVIEW_CELL_EDITABLE, 160);
listCtrl_->AppendTextColumn("Message",  COL_MESSAGE,  wxDATAVIEW_CELL_INERT, 160);
```

- [ ] **Step 2: Update loadProxies to show Host as separate field**

```cpp
// Lines 96-103: Split address into host and port columns
for (const auto& p : proxies_) {
    wxVector<wxVariant> row;
    row.push_back(wxVariant(p.indexid));           // IndexId -> # 
    row.push_back(wxVariant(p.address));           // Host only
    row.push_back(wxVariant(p.port));              // Port only
    row.push_back(wxVariant(delayMap.count(p.indexid) ? delayMap[p.indexid] : "-"));
    row.push_back(wxVariant(std::to_string(failuresMap.count(p.indexid) ? failuresMap[p.indexid] : 0)));
    row.push_back(wxVariant(p.remarks));
    row.push_back(wxVariant(messageMap.count(p.indexid) ? messageMap[p.indexid] : ""));
    store_->AppendItem(row);
}
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build --parallel 8`

---

### Task 5: SubscriptionPanel # Column

**Files:**
- Modify: `src/ui/SubscriptionPanel.cpp`

- [ ] **Step 1: Add # column at beginning**

```cpp
// Line 51-54: Insert # column before On column
listCtrl_->AppendTextColumn("#", 0, wxDATAVIEW_CELL_INERT, 30);  // Add before existing
// Shift existing column indices: On->1, Name->2, Proxies->3, Update->4
listCtrl_->AppendToggleColumn("On", 1, wxDATAVIEW_CELL_ACTIVATABLE, 30);
listCtrl_->AppendTextColumn("Name", 2, wxDATAVIEW_CELL_EDITABLE, 170);  // reduced width
listCtrl_->AppendTextColumn("Count", 3, wxDATAVIEW_CELL_INERT, 60);    // renamed from Proxies
listCtrl_->AppendTextColumn("Update Time", 4, wxDATAVIEW_CELL_INERT, 110);
```

- [ ] **Step 2: Update loadSubscriptions to include row number**

```cpp
// Lines 88-98: Add row number to data
int rowNum = 1;
for (const auto& sub : subs_) {
    wxVector<wxVariant> row;
    row.push_back(wxVariant(rowNum++));  // # column
    row.push_back(wxVariant(sub.enabled == "1"));
    row.push_back(wxVariant(sub.remarks));
    auto proxies = controller_->loadProxies(sub.id);
    row.push_back(wxVariant(wxString::Format("%zu", proxies.size())));
    row.push_back(wxVariant(sub.updatetime));
    store_->AppendItem(row);
}
```

- [ ] **Step 3: Build**

Run: `cmake --build build --parallel 8`

---

### Task 6: Final Integration Test

**Files:**
- All modified files compiled together

- [ ] **Step 1: Full build with tests**

Run: `cmake --build build --parallel 8`
Expected: All compile without errors

- [ ] **Step 2: Run tests**

Run: `ctest -V`
Expected: All 3 tests pass

- [ ] **Step 3: Run application to verify layout**

Run: `./build/validproxy.exe`
Expected: Window shows 3-column top layout with white log panel at bottom

---

## Self-Review Checklist

1. **Spec coverage:** All requirements from design doc addressed
2. **No placeholders:** All code provided inline
3. **Type consistency:** Used same types as existing code (wxVariant, wxString, etc.)
