---
title: "plan: Subscription Panel Chinese Localization"
type: plan
status: draft
date: 2026-05-29
origin: "Migrated from docs/superpowers/plans/"
---

# Subscription Panel UI Chinese Localization & Cleanup Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Localize subscription panel context menu to Chinese, remove redundant Add/Refresh buttons, add Refresh to context menu, and widen database path label.

**Architecture:** All changes are in two files — `src/ui/SubscriptionPanel.cpp` (context menu labels, button bar) and `src/ui/MainFrame.cpp` (db path label width). No structural changes, no new files.

**Tech Stack:** C++17, wxWidgets, wxDataViewCtrl

---

### Task 1: Translate Subscription Panel Context Menu to Chinese

**Files:**
- Modify: `src/ui/SubscriptionPanel.cpp:157-178`

- [ ] **Step 1: Replace English menu labels with Chinese in `onContextMenu()`**

  Change:
  ```cpp
  menu.Append(ID_SUB_UPDATE, "Update");
  menu.Append(ID_SUB_TEST, "Test");
  menu.Append(ID_SUB_EDIT, "Edit...");
  menu.Append(ID_SUB_DELETE, "Delete");
  menu.Append(ID_SUB_IMPORT, "Import from URL...");
  ```
  To:
  ```cpp
  menu.Append(ID_SUB_UPDATE, "更新订阅");
  menu.Append(ID_SUB_TEST, "测试订阅");
  menu.Append(ID_SUB_EDIT, "编辑订阅");
  menu.Append(ID_SUB_DELETE, "删除订阅");
  menu.Append(ID_SUB_IMPORT, "添加订阅");
  ```

- [ ] **Step 2: Remove "Add..." from context menu and add "刷新"**

  The current menu has 7 items in 3 groups:
  ```
  Update
  Test
  ---
  Edit...
  Delete
  ---
  Add...           ← REMOVE this
  Import from URL... ← rename to 添加订阅
  ```

  Change to 6 items in 3 groups:
  ```
  更新订阅
  测试订阅
  ---
  编辑订阅
  删除订阅
  ---
  刷新
  添加订阅
  ```

  In `onContextMenu()`, replace:
  ```cpp
  menu.Append(ID_SUB_ADD, "Add...");
  menu.Append(ID_SUB_IMPORT, "Import from URL...");
  ```
  With:
  ```cpp
  menu.Append(ID_SUB_REFRESH, "刷新");
  menu.Append(ID_SUB_IMPORT, "添加订阅");
  ```

- [ ] **Step 3: Add ID_SUB_REFRESH event ID and handler**

  In `SubscriptionPanel.cpp`, add `ID_SUB_REFRESH` to the enum (after `ID_SUB_IMPORT`):
  ```cpp
  ID_SUB_REFRESH,
  ```

  In the event table, add:
  ```cpp
  EVT_MENU(ID_SUB_REFRESH, SubscriptionPanel::onRefreshSubscription)
  ```

  In `SubscriptionPanel.h`, add handler declaration:
  ```cpp
  void onRefreshSubscription(wxCommandEvent& event);
  ```

- [ ] **Step 4: Implement `onRefreshSubscription()` handler**

  In `SubscriptionPanel.cpp`, add:
  ```cpp
  void SubscriptionPanel::onRefreshSubscription(wxCommandEvent&) {
      loadSubscriptions();
  }
  ```

  This simply reloads the subscription list, same as the current Refresh button does.

### Task 2: Remove "+" and "Refresh" Buttons

**Files:**
- Modify: `src/ui/SubscriptionPanel.cpp:62-72`

- [ ] **Step 1: Remove button bar with "+" and "Refresh" buttons**

  Replace the entire button bar section (lines 62-72):
  ```cpp
  // Button bar
  wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
  wxButton* addBtn = new wxButton(this, ID_SUB_ADD, "+", wxDefaultPosition, wxSize(28, 28));
  addBtn->SetToolTip("Add subscription");
  wxButton* refreshBtn = new wxButton(this, wxID_ANY, "Refresh", wxDefaultPosition, wxSize(-1, 28));
  refreshBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { loadSubscriptions(); });

  btnSizer->Add(addBtn, 0, wxRIGHT, 4);
  btnSizer->Add(refreshBtn, 0);
  btnSizer->AddStretchSpacer();
  sizer->Add(btnSizer, 0, wxEXPAND | wxALL, 2);
  ```
  With just a spacer or nothing (no button bar):
  ```cpp
  // (no button bar — Add and Refresh are now in context menu)
  ```

- [ ] **Step 2: Clean up unused `ID_SUB_ADD` event binding**

  Remove `ID_SUB_ADD` from the enum and event table since the `+` button is removed and "Add..." is removed from context menu. The `onAddSubscription()` handler and `showAddDialog()` can both be removed (they're only accessible through the `+` button and menu "Add..." item).

  Remove from enum:
  ```cpp
  ID_SUB_ADD = wxID_HIGHEST + 400,    // REMOVE
  ```

  Remove from event table:
  ```cpp
  EVT_MENU(ID_SUB_ADD, SubscriptionPanel::onAddSubscription)  // REMOVE
  ```

  Remove handler:
  ```cpp
  // Remove whole method
  void SubscriptionPanel::onAddSubscription(wxCommandEvent&) {
      showAddDialog();
  }
  ```

  Remove `showAddDialog()` method and its declaration from SubscriptionPanel.h.

  Remove `onAddSubscription()` declaration from SubscriptionPanel.h.

### Task 3: Widen Database Path Label

**Files:**
- Modify: `src/ui/MainFrame.cpp:326-328`

- [ ] **Step 1: Increase default width of `m_dbPathLabel` from 300 to 500px**

  In `initToolBar()`, change:
  ```cpp
  m_dbPathLabel = new wxStaticText(tb, wxID_ANY, wxString(getDbPath()),
                                    wxDefaultPosition, wxSize(300, -1),
                                    wxALIGN_RIGHT | wxST_ELLIPSIZE_START);
  ```
  To:
  ```cpp
  m_dbPathLabel = new wxStaticText(tb, wxID_ANY, wxString(getDbPath()),
                                    wxDefaultPosition, wxSize(500, -1),
                                    wxALIGN_RIGHT | wxST_ELLIPSIZE_START);
  ```

- [ ] **Step 2: Increase minimum width in `onResize()` fallback**

  In the defensive fallback (line 605), change:
  ```cpp
  m_dbPathLabel->SetSize(60, -1);
  ```
  To:
  ```cpp
  m_dbPathLabel->SetSize(200, -1);
  ```

- [ ] **Step 3: Increase minimum width in the normal branch**

  In the normal branch (line 614), change:
  ```cpp
  dbWidth = std::max(60, dbWidth);
  ```
  To:
  ```cpp
  dbWidth = std::max(200, dbWidth);
  ```

### Task 4: Build & Test

**Files:**
- Build: whole project

- [ ] **Step 1: Rebuild project**

  Run: `cd /d E:\eclipse_workspace\multiple_thread_validproxy && cmake --build build --parallel 8 2>&1`
  Expected: Build succeeds with no new errors

- [ ] **Step 2: Run tests**

  Run: `cd /d E:\eclipse_workspace\multiple_thread_validproxy && ctest --test-dir build -V 2>&1`
  Expected: All 3 tests pass

- [ ] **Step 3: Verify binary runs**

  Run: `bin\validproxy.exe -show-sub`
  Expected: App starts and shows subscriptions (no crash)

---
