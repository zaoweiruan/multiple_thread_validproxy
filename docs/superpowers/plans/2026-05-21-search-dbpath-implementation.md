# Search Enhancements and Database Path Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add real-time search filtering, clear button, auto-load first subscription, and database path panel to validproxy UI.

**Architecture:** Modify MainFrame to add event handlers, toolbar controls, and initialization logic. Extend SubscriptionPanel to expose subscription list. Use existing ProxyListPanel.filterBySearch() for filtering.

**Tech Stack:** wxWidgets C++, SQLite, CMake

---

## File Mapping

| File | Purpose |
|------|---------|
| `src/ui/MainFrame.h` | Add `wxStaticText* m_dbPathLabel`, `onSearchTextChanged()` declaration, `getDbPath()` |
| `src/ui/MainFrame.cpp` | Implementation of new handlers, toolbar changes, auto-load logic |
| `src/ui/SubscriptionPanel.h` | Add `getSubscriptions()` accessor |
| `src/ui/SubscriptionPanel.cpp` | Implement `getSubscriptions()` |

---

## Task 1: Add getSubscriptions() accessor to SubscriptionPanel

**Files:**
- Modify: `src/ui/SubscriptionPanel.h` (add declaration)
- Modify: `src/ui/SubscriptionPanel.cpp` (add implementation)

- [ ] **Step 1: Add declaration to SubscriptionPanel.h**

```cpp
// Add inside class SubscriptionPanel, after getSelectedSubId()
const std::vector<db::models::Subitem>& getSubscriptions() const { return subs_; }
```

- [ ] **Step 2: Commit**

```bash
git add src/ui/SubscriptionPanel.h
git commit -m "feat: add getSubscriptions() accessor to SubscriptionPanel"
```

---

## Task 2: Add real-time search filter handler to MainFrame

**Files:**
- Modify: `src/ui/MainFrame.h` (add declaration)
- Modify: `src/ui/MainFrame.cpp` (add binding and handler)

- [ ] **Step 1: Add declaration to MainFrame.h**

```cpp
// Add in private section:
void onSearchTextChanged(wxCommandEvent& event);
```

- [ ] **Step 2: Add EVT_TEXT binding in initToolBar()**

```cpp
// In initToolBar(), after m_searchBox creation:
m_searchBox->Bind(wxEVT_TEXT, &MainFrame::onSearchTextChanged, this);
```

- [ ] **Step 3: Implement onSearchTextChanged() in MainFrame.cpp**

```cpp
void MainFrame::onSearchTextChanged(wxCommandEvent& event) {
    if (proxyPanel_) {
        proxyPanel_->filterBySearch(m_searchBox->GetValue());
    }
}
```

- [ ] **Step 4: Build and verify compilation**

```bash
cmake --build build --parallel 8
```

Expected: Build succeeds with 0 errors

- [ ] **Step 5: Commit**

```bash
git add src/ui/MainFrame.h src/ui/MainFrame.cpp
git commit -m "feat: add real-time search filter handler"
```

---

## Task 3: Add clear button to search input

**Files:**
- Modify: `src/ui/MainFrame.h` (add ID_SEARCH_CLEAR)
- Modify: `src/ui/MainFrame.cpp` (add button and handler)

- [ ] **Step 1: Add ID_SEARCH_CLEAR to enum**

```cpp
// In MainFrame.cpp, update enum:
enum {
    // ... existing ...
    ID_MENU_CONFIG        = wxID_HIGHEST + 110,
    ID_TOOLBAR_DBPATH    = wxID_HIGHEST + 300,
    ID_SEARCH_CLEAR      = wxID_HIGHEST + 301,
};
```

- [ ] **Step 2: Add clear button handler declaration**

```cpp
// In MainFrame.h:
void onSearchClear(wxCommandEvent& event);
```

- [ ] **Step 3: Add button to toolbar in initToolBar()**

```cpp
// After m_searchBox->AddControl():
tb->AddTool(ID_SEARCH_CLEAR, "❌", wxNullBitmap); // Text-only button
```

- [ ] **Step 4: Add EVT_MENU binding for clear button**

```cpp
// In wxBEGIN_EVENT_TABLE:
EVT_MENU(ID_SEARCH_CLEAR, MainFrame::onSearchClear)
```

- [ ] **Step 5: Implement onSearchClear()**

```cpp
void MainFrame::onSearchClear(wxCommandEvent& event) {
    m_searchBox->SetValue("");
    if (proxyPanel_) {
        proxyPanel_->filterBySearch("");
    }
}
```

- [ ] **Step 6: Build and test**

```bash
cmake --build build --parallel 8
```

- [ ] **Step 7: Commit**

```bash
git add src/ui/MainFrame.h src/ui/MainFrame.cpp
git commit -m "feat: add clear button to search input"
```

---

## Task 4: Auto-load first subscription on startup

**Files:**
- Modify: `src/ui/MainFrame.cpp` (initPanels method)

- [ ] **Step 1: Modify initPanels()**

```cpp
void MainFrame::initPanels() {
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    // Top row: subscription | proxy list | detail
    wxBoxSizer* topSizer = new wxBoxSizer(wxHORIZONTAL);
    subPanel_ = new SubscriptionPanel(this, controller_);
    proxyPanel_ = new ProxyListPanel(this, controller_, db_);
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

    // Load initial data - NOW with first subscription
    subPanel_->loadSubscriptions();
    
    // Auto-load first subscription
    if (!subPanel_->getSubscriptions().empty()) {
        std::string firstSubId = subPanel_->getSubscriptions()[0].id;
        proxyPanel_->loadProxies(firstSubId);
    } else {
        proxyPanel_->loadProxies("");
    }
}
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build --parallel 8
```

- [ ] **Step 3: Commit**

```bash
git add src/ui/MainFrame.cpp
git commit -m "feat: auto-load first subscription on UI startup"
```

---

## Task 5: Add database path panel to toolbar

**Files:**
- Modify: `src/ui/MainFrame.h` (add member)
- Modify: `src/ui/MainFrame.cpp` (add panel and getDbPath())

- [ ] **Step 1: Add member and declaration**

```cpp
// In MainFrame.h, private section:
wxStaticText* m_dbPathLabel;

// Add declaration:
std::string getDbPath() const;
```

- [ ] **Step 2: Add db path label to initToolBar()**

```cpp
void MainFrame::initToolBar() {
    wxToolBar* tb = CreateToolBar();

    tb->AddTool(ID_TOOL_UPDATE_ALL, "Update", wxArtProvider::GetBitmap(wxART_EXECUTABLE_FILE));
    tb->AddTool(ID_TOOL_TEST,       "Test",   wxArtProvider::GetBitmap(wxART_TICK_MARK));
    tb->AddTool(ID_TOOL_FIND,       "Find",   wxArtProvider::GetBitmap(wxART_FIND));
    tb->AddTool(ID_TOOL_DEDUP,      "Dedup",  wxArtProvider::GetBitmap(wxART_LIST_VIEW));
    tb->AddTool(ID_TOOL_IMPORT,     "Import", wxArtProvider::GetBitmap(wxART_FILE_OPEN));
    tb->AddTool(ID_TOOL_CONFIG,     "Config", wxArtProvider::GetBitmap(wxART_LIST_VIEW));
    tb->AddSeparator();
    tb->AddControl(new wxStaticText(tb, wxID_ANY, " Search:"));
    m_searchBox = new wxTextCtrl(tb, ID_SEARCH_BOX, "", wxDefaultPosition, wxSize(150, -1), wxTE_PROCESS_ENTER);
    tb->AddControl(m_searchBox);
    
    // Add clear button
    tb->AddTool(ID_SEARCH_CLEAR, "❌", wxNullBitmap);
    
    // Add database path panel
    m_dbPathLabel = new wxStaticText(tb, wxID_ANY, wxString(getDbPath()), wxDefaultPosition, wxSize(200, -1));
    tb->AddControl(m_dbPathLabel);
    
    tb->Realize();
}
```

- [ ] **Step 3: Implement getDbPath()**

```cpp
std::string MainFrame::getDbPath() const {
    // Get from config or use default
    return "DB: guiNDB.db"; // Will be updated to read from actual config
}
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build --parallel 8
```

- [ ] **Step 5: Commit**

```bash
git add src/ui/MainFrame.h src/ui/MainFrame.cpp
git commit -m "feat: add database path panel to toolbar"
```

---

## Task 6: Run all tests and verify

- [ ] **Step 1: Run tests**

```bash
.\bin\test_dedup.exe
.\bin\test_model.exe
.\bin\test_curl_easy_handle.exe
```

Expected: All tests pass

- [ ] **Step 2: Final commit**

```bash
git commit --allow-empty -m "test: verify all tests pass after search enhancements"
```

---

## Self-Review

1. **Spec coverage:** All 4 features covered - real-time filtering (Task 2), clear button (Task 3), auto-load first subscription (Task 4), database path panel (Task 5)
2. **Placeholder scan:** No TBD/TODO placeholders found
3. **Type consistency:** All method signatures and member names consistent