---
title: "plan: Sync Toolbar Button"
type: plan
status: draft
date: 2026-05-27
origin: "Migrated from docs/superpowers/plans/"
---

# 同步工具栏按钮 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a "同步" toolbar button that triggers database sync from configured source_db to target_db.

**Architecture:** Follow existing async worker pattern (`AppController::doXxx` + `isRunning_` re-entry guard). Add `ID_TOOL_SYNC` ID, toolbar button, event handler, and async method pair to AppController.

**Tech Stack:** C++17, wxWidgets 3.2, SQLite (SubitemUpdaterV2::syncDatabases)

---

## File Structure

| File | Change |
|------|--------|
| `bin/icons/tool_synchronize.png` | **Create** — copy from `docs/design/ui/icon/png/tool_synchronize.png` |
| `src/ui/MainFrame.cpp` | **Modify** — add ID enum, toolbar button, event table entry, handler |
| `src/ui/MainFrame.h` | **Modify** — add `onToolSync` declaration |
| `src/ui/AppController.h` | **Modify** — add `syncDatabasesAsync` and `doSyncDatabases` declarations |
| `src/ui/AppController.cpp` | **Modify** — add async implementation following existing pattern |

### Task 1: Copy icon PNG to runtime directory

**Files:**
- Create: `bin/icons/tool_synchronize.png`

- [ ] **Step 1: Copy the icon file**

```bash
cp "docs/design/ui/icon/png/tool_synchronize.png" "bin/icons/tool_synchronize.png"
```

Verify: `ls -la bin/icons/tool_synchronize.png` exists.

- [ ] **Step 2: Commit**

```bash
git add bin/icons/tool_synchronize.png
git commit -m "chore: add tool_synchronize.png toolbar icon"
```

### Task 2: Add ID_TOOL_SYNC + toolbar button + event table entry

**Files:**
- Modify: `src/ui/MainFrame.cpp` lines 24-50 (enum) and lines 279-295 (initToolBar) and lines 53-82 (event table)

- [ ] **Step 1: Add `ID_TOOL_SYNC` to the enum**

In the anonymous enum block, after `ID_TOOL_CANCEL_TEST = wxID_HIGHEST + 208`, insert:
```cpp
ID_TOOL_SYNC        = wxID_HIGHEST + 209,
```

- [ ] **Step 2: Add the toolbar button in `initToolBar()`**

In the toolbar button sequence, add after the cancel test button (between line 291 and 292):
```cpp
tb->AddTool(ID_TOOL_SYNC, wxEmptyString, ToolbarIcons::load("tool_synchronize"), "同步");
```

- [ ] **Step 3: Add event table entry**

In the event table, after `EVT_MENU(ID_TOOL_CANCEL_TEST, MainFrame::onToolCancelTest)`, insert:
```cpp
EVT_MENU(ID_TOOL_SYNC,       MainFrame::onToolSync)
```

- [ ] **Step 4: Commit**

```bash
git add src/ui/MainFrame.cpp
git commit -m "feat: add ID_TOOL_SYNC enum, toolbar button, and event table entry"
```

### Task 3: Add handler declaration to MainFrame.h

**Files:**
- Modify: `src/ui/MainFrame.h` line 74

- [ ] **Step 1: Add `onToolSync` method declaration**

After `void onToolCancelTest(wxCommandEvent& event);` (line 74), insert:
```cpp
void onToolSync(wxCommandEvent& event);
```

- [ ] **Step 2: Commit**

```bash
git add src/ui/MainFrame.h
git commit -m "feat: declare onToolSync handler"
```

### Task 4: Implement onToolSync handler

**Files:**
- Modify: `src/ui/MainFrame.cpp` — add after `onToolCancelTest` (~line 625)

- [ ] **Step 1: Add handler implementation**

After the `onToolCancelTest` method, insert:
```cpp
void MainFrame::onToolSync(wxCommandEvent&) {
    setStatusText(0, "同步中…");
    controller_->syncDatabasesAsync(this);
}
```

- [ ] **Step 2: Commit**

```bash
git add src/ui/MainFrame.cpp
git commit -m "feat: implement onToolSync handler"
```

### Task 5: Add AppController async sync methods

**Files:**
- Modify: `src/ui/AppController.h` lines 54-62 (add declarations)
- Modify: `src/ui/AppController.cpp` lines 334-338 (add implementation)

- [ ] **Step 1: Add declarations to AppController.h**

After `void findProxyByIndexIdAsync(const std::string& indexId, wxEvtHandler* wxHandler);` (line 54), insert:
```cpp
void syncDatabasesAsync(wxEvtHandler* wxHandler);
```

After `void doFindBestProxy(wxEvtHandler* wxHandler);` (line 69), insert:
```cpp
void doSyncDatabases(wxEvtHandler* wxHandler);
```

- [ ] **Step 2: Add async entry point to AppController.cpp**

After the `findProxyByIndexIdAsync` method, insert the public entry point:

```cpp
void AppController::syncDatabasesAsync(wxEvtHandler* wxHandler) {
    if (workerThread_.joinable()) {
        if (isRunning_) {
            if (wxHandler) {
                wxQueueEvent(wxHandler, new StatusUpdateEvent(0,
                    "REJECT:Another operation is already in progress. Please wait or cancel it first."));
            }
            return;
        }
        workerThread_.join();
    }
    cancelRequested_ = false;
    isRunning_ = true;
    workerThread_ = std::thread(&AppController::doSyncDatabases, this, wxHandler);
}
```

- [ ] **Step 3: Add worker implementation to AppController.cpp**

After the `doFindBestProxy` method, insert the worker:

```cpp
void AppController::doSyncDatabases(wxEvtHandler* wxHandler) {
    bool ok = syncDatabases();
    std::string msg = ok ? "数据库同步完成" : "数据库同步失败，请查看日志";
    if (wxHandler) {
        wxQueueEvent(wxHandler, new StatusUpdateEvent(0, msg));
    }
    Logger::write(msg, ok ? LogLevel::REPORT : LogLevel::ERR);
    isRunning_ = false;
}
```

- [ ] **Step 4: Commit**

```bash
git add src/ui/AppController.h src/ui/AppController.cpp
git commit -m "feat: add syncDatabasesAsync with worker thread pattern"
```

### Task 6: Build and verify

- [ ] **Step 1: Build the project**

```bash
cmake --build build --parallel 8
```

Expected: Build succeeds with no errors. Verify new sync button compiles into toolbar.

- [ ] **Step 2: Run tests**

```bash
ctest -V --test-dir build
```

Expected: All 3 tests pass.

- [ ] **Step 3: Commit any build fix if needed**

If build passed:
```bash
git add -A
git commit -m "chore: finalize sync button implementation"
```
