---
title: "impl: Find proxy async + Delay refresh after test"
type: plan
status: draft
date: 2026-05-18
origin: "User: '制定计划，实现查找功能，修复测试结果无法更新界面上delay字段值'"
---

# Implementation Plan: Find Proxy Async + Delay Refresh After Test

## Overview

Three issues found in `src/ui/` codebase; two are separate problems, one is a
prerequisite for the other two to produce visible UI results.

| # | Problem | Root File |
|---|---------|-----------|
| **P1** | Find proxy blocks UI (both methods run on main thread) | `AppController.cpp` |
| **P2** | Delay column in `ProxyListPanel` never refreshes after any test | `ProxyListPanel.cpp`, `MainFrame.cpp` |
| **P3** | `testPanel_` passed to `ProxyListPanel` before it is instantiated | `MainFrame.cpp` |

---

## P1 — Find Proxy Blocks UI Thread

### Current state

```
MainFrame::onMenuFindProxy()     → controller_->findFirstProxy()  ← BLOCKS
MainFrame::onMenuFindBest()      → controller_->findBestProxy()   ← BLOCKS
                                         │
                    AppController::findFirstProxy()  // synchronous worker
                    AppController::findBestProxy()   // synchronous worker
```

Both methods (`AppController.cpp:104–118`) construct `ProxyFinder` on the
calling (main/GUI) thread and then scan every proxy via xray inject + curl.
No status update or progress is possible until the loop completes.

`ProxyFinder::findFirstWorkingProxy()` and `findWorkingProxy()` validate proxies
against the database by calling `loadFallbackProxies(100)` and testing each one
through `ProfileExItemDAO::updateTestResult` — this update actually writes to
the DB too.

### Fix: Make async, matching other operations (subscription update / batch test)

Add `findFirstProxyAsync()` + `findBestProxyAsync()` to `AppController`,
backed by private `doFindFirstProxy()` / `doFindBestProxy()` worker methods,
reporting `StatusUpdateEvent` on completion (same pattern as
`doUpdateSubscription`, `doTestSubscription`).

Since only one `workerThread_` exists and all operations guard with
`if (workerThread_.joinable()) workerThread_.join()` before starting,
reusing the same thread is safe — a new find request waits for the
current operation to finish.

---

## P2 — Delay Column Never Refreshes After Any Test

### Current state

```
ProxyListPanel::onTestProxy() (single test)
  └─ wxQueueEvent(testPanel_, ProxyTestProgressEvent{result})  → TestPanel ONLY
  └─ exItems_ NOT updated → Delay column remains "-"

doTestSubscription() (batch test)
  └─ AppController::testSubscriptionAsync(wxHandler=MainFrame)
     └─ ProxyBatchTester::runWithSubId()  → writes result to DB
     └─ wxQueueEvent(wxHandler, ->géTest ProxyTestProgressEvent{completed})  → MainFrame
        └─ no one reads this event as a "refresh DB" trigger
  └─ DB has new delay data, but ProxyListPanel exItems_ is stale
```

`ProxyListPanel::loadProxies()` (line 425) re-fetches both `proxies_` and
`exItems_` from DB — correct refresh path — but it is only called by
subscription selection change, not by test completion.

### Fix: Central refresh callable from `MainFrame` on test completion

Add `ProxyListPanel::refreshResults()` that calls
`controller_->loadProxyResults()` and `model_->setData(allProxies_, exItems_)`
— without re-fetching `proxies_`, preserving scroll/selection.

Have `MainFrame` `Bind(wxEVT_PROXY_TEST_PROGRESS, ...)` → when a test
completion event arrives, call `proxyPanel_->refreshResults()`.

This covers both single-proxy tests (event reaches TestPanel, then MainFrame
receives and propagates) and batch tests.

Actually: `ProxyListPanel::onTestProxy()` posts result events **directly to
`testPanel_`**.  `MainFrame` is not in that path.  But `MainFrame` posts its
own completion event from `doTestSubscription`.  For single-proxy tests, the
completion event also goes to `testPanel_` — so TestPanel sees it.  To make
`MainFrame` see both events, the clean approach is:

- Append to the batch-test completion path: `MainFrame` already gets
  `wxEVT_PROXY_TEST_PROGRESS` → bind a handler there.
- For single-proxy tests: the result row event (`isCompleted=false`) is sent
  to `testPanel_` (which handles it in `onProgress`).  The follow-up
  `isCompleted=true` event is *also* sent to `testPanel_`.  `MainFrame`
  **should be an additional target** for both so it can call
  `refreshResults()` after every test cycle.

Both paths: after test completes → `MainFrame::onTestRefresh`
→ `proxyPanel_->refreshResults()`.  ✓

---

## P3 — `testPanel_` Nullptr in `ProxyListPanel` Construction

### Current (broken)

```cpp
// MainFrame.cpp initPanels() lines 184–188 (as found on disk):
void MainFrame::initPanels() {
    subPanel_   = new SubscriptionPanel(this, controller_);
    testPanel_  = new TestPanel(this);          // created SECOND
    proxyPanel_ = new ProxyListPanel(..., testPanel_);  // received nullptr
    ...
}
```

Note: the disk file is **already swapped** compared to plan
`1779070922736-shiny-comet.md` (which documented it as the broken state).
The issue description says test results were empty — but the code as-read
already has the swap.  The plan-unit below documents the state found on
disk to avoid confusion: `testPanel_` is constructed **before** `proxyPanel_`.

**If the disk state is already swapped** (testPanel_ first):
- `ProxyTestProgressEvent` from `onTestProxy` sends to `testPanel_` correctly.
- BUT `ProxyListPanel::refreshResults()` still needs to be added so the
  `Delay` column updates after test.

**If the disk state still has proxyPanel_ first** (pre-swap):
- U1 is a one-liner swap: `testPanel_ = new TestPanel(this);` then
  `proxyPanel_ = new ProxyListPanel(..., testPanel_);` — exactly as
  documented in `1779070922736-shiny-comet.md`.

---

## Implementation Units (in dependency order)

### [P3] U1 — `MainFrame.cpp` initPanels() — verify/set construction order

**File**: `src/ui/MainFrame.cpp`, `MainFrame::initPanels()`

Confirm on disk that `testPanel_` is constructed **before** `proxyPanel_`.
If construction is already swapped, skip; otherwise swap lines.

```diff
 void MainFrame::initPanels() {
-    proxyPanel_ = new ProxyListPanel(this, controller_, db_, testPanel_);
-    testPanel_  = new TestPanel(this);
+    testPanel_  = new TestPanel(this);
+    proxyPanel_ = new ProxyListPanel(this, controller_, db_, testPanel_);
     logPanel_   = new LogPanel(this);
```

---

### [P2] U2 — `ProxyListPanel.h` — Add `refreshResults()` declaration

**File**: `src/ui/ProxyListPanel.h`

Add after `loadProxies()`:

```cpp
    void loadProxies(const std::string& subId = "");
+   void refreshResults();  // reload exItems_ from DB; keep proxy list intact
```

---

### [P2] U3 — `ProxyListPanel.cpp` — Implement `refreshResults()`

**File**: `src/ui/ProxyListPanel.cpp`

```cpp
void ProxyListPanel::refreshResults() {
    exItems_ = controller_->loadProxyResults();
    model_->setData(allProxies_, exItems_);
}
```

`setData()` internally calls `model_->Reset(count)` which causes
`wxDataViewCtrl` to re-draw every visible cell — the Delay column shows
new values immediately without full-page reload.  Log: `"Delay refreshed"
at LogLevel::DEBUG`.

---

### [P1] U4 — `AppController.h` — Add async find declarations

**File**: `src/ui/AppController.h`

Add after existing `findFirstProxy()` / `findBestProxy()`:

```cpp
    // --- Find operations ---
+   // NOTE: findFirstProxy/findBestProxy are kept for the CLI path (main.cpp).
+   // GUI 路径请使用 findFirstProxyAsync / findBestProxyAsync。
+   // TODO: CLI 路径也改为 async 变体（不阻塞终端）。
    ProxyFinder::TestResult findFirstProxy();
    ProxyFinder::TestResult findBestProxy();
    void findFirstProxyAsync(wxEvtHandler* wxHandler);
    void findBestProxyAsync(wxEvtHandler* wxHandler);
```

Add two new private worker method declarations:

```cpp
+   // Find proxy 完成时的字符串事件: FOUND:<id>:<addr> / NOTFOUND / ERR:<msg>
+   void doFindFirstProxy(wxEvtHandler* wxHandler);
+   void doFindBestProxy(wxEvtHandler* wxHandler);
+   ProxyFinder::TestResult lastFindResult_{false, -1, "", "", "", 0, 0};
```

---

### [P1] U5 — `AppController.cpp` — Implement async find workers

**File**: `src/ui/AppController.cpp`

Public entry points:

```cpp
void AppController::findFirstProxyAsync(wxEvtHandler* wxHandler) {
    cancelRequested_ = false;
    if (workerThread_.joinable()) workerThread_.join();
    workerThread_ = std::thread(&AppController::doFindFirstProxy, this, wxHandler);
}

void AppController::findBestProxyAsync(wxEvtHandler* wxHandler) {
    cancelRequested_ = false;
    if (workerThread_.joinable()) workerThread_.join();
    workerThread_ = std::thread(&AppController::doFindBestProxy, this, wxHandler);
}
```

Private worker methods — encode three outcomes directly in the `StatusUpdateEvent`
payload string so `MainFrame::Bind` can route to `selectProxyByIndexId` or
`wxMessageBox` without any shared struct copy.

```
Payload:  "FOUND:<indexId>:<address>"  → MainFrame selects row + popup "Found: ..."
          "NOTFOUND"                  → MainFrame popup "No working proxy found."
          "ERR:<msg>"                 → MainFrame popup "Find error: ..."
```

No schema change needed — `StatusUpdateEvent::getText()` returns `std::string`.
The worker serialises `FOUND`, `NOTFOUND`, `ERR:` in the string;
`MainFrame::Bind` deserialises with `rfind`/`substr`.  No shared struct fields.

---

### [P1+P2] U6 — `MainFrame.cpp` — Bind events + async find handlers

**File**: `src/ui/MainFrame.cpp`

**6a. Constructor** — add two `Bind` calls near other setup (before or after
`initPanels()`). `<wx/event.h>` is already included.

```cpp
MainFrame::MainFrame(const config::AppConfig& cfg, sqlite3* db)
    : wxFrame(nullptr, wxID_ANY, "validproxy - Proxy Manager",
              wxDefaultPosition, wxSize(1200, 800)),
      db_(db)
{
    controller_ = new AppController(db, cfg);

+   // ── Find-proxy completion ──────────────────────────────────────────
+   // Encoded payload: "FOUND:<indexId>:<address>" | "NOTFOUND" | "ERR:..."
+   Bind(wxEVT_STATUS_UPDATE, [this](StatusUpdateEvent& evt) {
+       std::string payload = evt.getText().ToStdString();
+       if (payload.rfind("FOUND:", 0) == 0) {
+           if (proxyPanel_) {
+               std::string mid = payload.substr(6);
+               std::string indexId = mid.substr(0, mid.find(':'));
+               proxyPanel_->selectProxyByIndexId(indexId);
+           }
+           wxMessageBox("Found: " + payload.substr(6),
+                        "Proxy Found", wxOK | wxICON_INFORMATION);
+       } else if (payload == "NOTFOUND") {
+           wxMessageBox("No working proxy found.",
+                        "Result", wxOK | wxICON_INFORMATION);
+       } else if (payload.rfind("ERR:", 0) == 0) {
+           wxMessageBox("Find error: " + payload.substr(4),
+                        "Error", wxOK | wxICON_ERROR);
+       }
+   });

+   // ── Test completion → refresh Delay column ──────────────────────────
+   Bind(wxEVT_PROXY_TEST_PROGRESS, [this](ProxyTestProgressEvent& evt) {
+       if (evt.isCompleted() && proxyPanel_) {
+           proxyPanel_->refreshResults();
+       }
+   });

    initMenuBar();
    initToolBar();
    initStatusBar();
    initAuiManager();
    initPanels();
    initTrayIcon();
    loadSettings();
}
```

**6b. Switch find menu handlers to async:**

```diff
 void MainFrame::onMenuFindProxy(wxCommandEvent&) {
-     auto result = controller_->findFirstProxy();
-     if (result.success) {
-         wxMessageBox("Found: " + result.indexId + " (" + result.address + ")",
-                      "Proxy Found", wxOK | wxICON_INFORMATION);
-     } else {
-         wxMessageBox("No working proxy found.",
-                      "Result", wxOK | wxICON_INFORMATION);
+     setStatusText(0, "Finding first working proxy...");
+     controller_->findFirstProxyAsync(this);
 }

 void MainFrame::onMenuFindBest(wxCommandEvent&) {
-     auto result = controller_->findBestProxy();
-     if (result.success) {
-         wxMessageBox("Best: " + result.indexId + " (" + result.address + ")\n"
-                      "Delay: " + std::to_string(result.delay) + " ms",
-                      "Best Proxy Found", wxOK | wxICON_INFORMATION);
-     } else {
-         wxMessageBox("No working proxy found.",
-                      "Result", wxOK | wxICON_INFORMATION);
+     setStatusText(0, "Finding best proxy...");
+     controller_->findBestProxyAsync(this);
 }
```

---

### [P2 detail] U2b — `ProxyListPanel.h` — `selectProxyByIndexId()` declaration

**File**: `src/ui/ProxyListPanel.h`, add to public section:

```cpp
    void loadProxies(const std::string& subId = "");
    void refreshResults();
+   void selectProxyByIndexId(const std::string& indexId);
```

Implementation: linear scan `proxies_` vector, `O(n)` — acceptable for ≤100 000
entries. No DB access needed — `exItems_` already in memory.

---

## Files Changed

| File | Changes |
|------|---------|
| `src/ui/MainFrame.h` | None (Bind added in ctor; event table unchanged) |
| `src/ui/MainFrame.cpp` | U1: initPanels() order; U6a: Bind refresh+find handlers; U6b: async find handlers |
| `src/ui/ProxyListPanel.h` | U2: add `refreshResults()` + `selectProxyByIndexId()` declarations |
| `src/ui/ProxyListPanel.cpp` | U3: implement `refreshResults()` |
| `src/ui/AppController.h` | U4: async find decls + worker field |
| `src/ui/AppController.cpp` | U5: async find entry points + worker methods |

---

## Verification

1. **Build**: `cmake --build build --parallel 8` — must pass
2. **Find First**: GUI → Menu "Proxy" → "Find Proxy" — status bar shows progress; "Found: xxx" on success; no UI freeze
3. **Find Best**: Same menu → "Find Best" — status bar shows best proxy + delay
4. **Single-proxy test**: Right-click proxy → "Test This Proxy" — result row appears in TestPanel AND Delay column updates in ProxyListPanel
5. **Batch test**: Toolbar → Test → select subscription → results post; delay column refreshed on completion
6. **ctest**: `ctest -V` — all existing unit tests pass

---

## Ordering Summary

| # | Unit | Prereq | Type |
|---|------|--------|------|
| P3 U1 | MainFrame initPanels() order | none | refactor (verify on disk) |
| P2 U2 | ProxyListPanel.h — refreshResults() | U1 | new API |
| P2 U3 | ProxyListPanel.cpp — implement refreshResults() | U2 | new logic |
| P1 U4 | AppController.h — async find decls | none | new API |
| P1 U5 | AppController.cpp — async find workers | U4 | new logic |
| P1+P2 U6 | MainFrame.cpp — Bind + async handlers | U3, U5 | wiring |
