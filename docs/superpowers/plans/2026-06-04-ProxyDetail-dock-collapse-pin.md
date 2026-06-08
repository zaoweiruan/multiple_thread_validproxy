# ProxyDetailPanel — Dock / Collapse / Pin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert ProxyDetailPanel from a fixed wxBoxSizer-managed panel into a wxAuiManager-managed pane that is collapsible (close/minimize), pinnable (fixed/auto-hide toggle), and docked to the right border — with a restore mechanism when closed.

**Architecture:** Leverage the existing but unused `wxAuiManager` (already declared in MainFrame.h, initialized in `initAuiManager()`, and linked via CMake `wx::wxaui`). Move ProxyDetailPanel from the vertical wxBoxSizer into a wxAui right-docked pane. The remaining three panels (SubscriptionPanel, ProxyListPanel, LogPanel) stay in a wrapped panel as the "center pane". Add a toolbar toggle button to restore the detail pane if closed.

**Tech Stack:** C++17, wxWidgets 3.2.5, wxAui, CMake/Ninja/MinGW

---

## File Structure

| File | Action | Purpose |
|------|--------|---------|
| `src/ui/MainFrame.h` | Modify | Add `onToggleDetailPane()` handler, detail pane state tracking |
| `src/ui/MainFrame.cpp` | Modify | Refactor `initPanels()` to use wxAui for detail panel; add toggle handler; add AUI pane close event handling |
| `src/ui/ProxyDetailPanel.h` | Modify | Add `onClose()` / `showDetail(bool)` method if needed |
| `src/ui/ProxyDetailPanel.cpp` | Modify | Possibly no changes — panel content stays identical |
| `CMakeLists.txt` | No change | `wx::wxaui` already linked at line 141 |

---

## Design

### Current Layout (wxBoxSizer, before)

```
┌────────────────────────────────────────────────────────────┐
│  Menu Bar + Toolbar                                        │
├──────────────────────────┬────────────────┬───────────────┤
│ SubscriptionPanel (380)  │ ProxyListPanel │ ProxyDetail   │
│                          │ (1-proportion) │ Panel (320)   │
│                          │                │               │
├──────────────────────────┴────────────────┴───────────────┤
│ LogPanel (620 min height)                                  │
└────────────────────────────────────────────────────────────┘
```

### Target Layout (wxAui, after)

```
┌────────────────────────────────────────────────────────────┐
│  Menu Bar + Toolbar  [  [Detail] toolbar button  ]        │
├────────────────────────────────────────────────────────────┤
│ ┌──────────────┬──────────────────┐ ╔═══════════════════╗ │
│ │Subscription  │ ProxyListPanel   │ ║ ProxyDetailPanel ║ │
│ │Panel (380)   │ (stretch)        │ ║ (right-docked)   ║ │
│ │              │                  │ ║ [X][_] [📌]      ║ │
│ ├──────────────┴──────────────────┤ ║                    ║ │
│ │ LogPanel                        │ ║ 33 fields          ║ │
│ └─────────────────────────────────┘ ║                    ║ │
│  (center pane via wxPanel wrapper)  ║                    ║ │
│                                     ╚════════════════════╝ │
└────────────────────────────────────────────────────────────┘
```

### wxAuiPaneInfo Flags Used

| Feature | Flag | Behavior |
|---------|------|----------|
| Right-docked | `.Right().Layer(0).Position(0)` | Docks to right edge |
| Fixed width | `.BestSize(320, -1).MinSize(250, -1)` | Maintains 320px default, no narrower than 250 |
| Collapsible | `.CloseButton(true)` | Shows X button on caption |
| Pinnable | `.PinButton(true)` | Shows pin toggle; unpinned = auto-hide tab, pinned = always visible |
| Caption | `.Caption("Proxy Details")` | Visible caption bar |
| Resizable | `.Resizable(true)` | User can drag left edge to resize |
| Float-able | `.Floatable(true)` | Can be dragged out as floating window |

### Restore Mechanism

When the user closes the detail pane via the X button, the pane is hidden in AUI. A toolbar button `[Detail]` (and/or a menu item under View) toggles its visibility. Internally this calls `auiManager_.GetPane(detailPanel_).Show(true).BestSize(320, -1)` followed by `auiManager_.Update()`.

---

## Tasks

### Task 1: Create center wrapper panel and remove ProxyDetail from wxBoxSizer

**Files:**
- Modify: `src/ui/MainFrame.cpp` (lines 371-407)

- [ ] **Step 1: Create a center-panel wrapper that holds Subscription, ProxyList, and Log panels**

```cpp
void MainFrame::initPanels() {
    // ── Create individual panels (as before) ──
    subPanel_ = new SubscriptionPanel(this, controller_);
    proxyPanel_ = new ProxyListPanel(this, controller_, db_);
    detailPanel_ = new ProxyDetailPanel(this);
    logPanel_ = new LogPanel(this);

    subPanel_->SetMinSize(wxSize(380, -1));
    proxyPanel_->SetMinSize(wxSize(620, -1));
    // detailPanel_ is NO LONGER added to the wxBoxSizer

    // ── Center panel: wraps the original layout minus detail ──
    wxPanel* centerPanel = new wxPanel(this);
    wxBoxSizer* centerSizer = new wxBoxSizer(wxVERTICAL);

    // Top row: subscription | proxy list  (no detail panel)
    wxBoxSizer* topSizer = new wxBoxSizer(wxHORIZONTAL);
    topSizer->Add(subPanel_, 0, wxEXPAND | wxRIGHT, 2);
    topSizer->Add(proxyPanel_, 1, wxEXPAND);
    centerSizer->Add(topSizer, 1, wxEXPAND);

    // Bottom row: log panel
    centerSizer->Add(logPanel_, 0, wxEXPAND | wxTOP, 2);
    logPanel_->SetMinSize(wxSize(620, 260));

    centerPanel->SetSizer(centerSizer);

    // ── Main frame sizer: only the center panel ──
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    mainSizer->Add(centerPanel, 1, wxEXPAND | wxALL, 2);
    SetSizer(mainSizer);
}
```

- [ ] **Step 2: Build to verify compilation still works**

Run: `cmake --build build --parallel 8`
Expected: Build succeeds (detail panel exists but is not laid out — will be invisible)

---

### Task 2: Add ProxyDetailPanel as a wxAui right-docked pane

**Files:**
- Modify: `src/ui/MainFrame.cpp` (initPanels, after creating panels)

- [ ] **Step 1: Register ProxyDetailPanel with wxAuiManager as right-docked pane**

After the `mainSizer->Add(centerPanel, ...)` line, add:

```cpp
    // ── Right-docked detail pane via wxAui ──
    auiManager_.AddPane(detailPanel_, wxAuiPaneInfo()
        .Name("detailPane")
        .Caption("Proxy Details")
        .Right()                       // dock to right edge
        .Layer(0).Position(0)          // first position in layer 0
        .BestSize(wxSize(320, -1))
        .MinSize(wxSize(250, 400))
        .CloseButton(true)             // collapsible via X
        .PinButton(true)               // pinnable: pin = fixed, unpin = auto-hide
        .Resizable(true)               // user can resize left edge
        .Floatable(true)               // can be dragged out as floating window
    );
    auiManager_.Update();
```

- [ ] **Step 2: Remove obsolete sizer assignment from frame constructor**

In the `MainFrame` constructor (line 172), ensure `initPanels()` is called AFTER `initAuiManager()` (it already is, per line 171-172).

- [ ] **Step 3: Build to verify**

Run: `cmake --build build --parallel 8`
Expected: Build succeeds; when launched, ProxyDetailPanel appears as a right-docked pane with caption bar, close button, pin button.

---

### Task 3: Add toolbar toggle button for detail panel visibility

**Files:**
- Modify: `src/ui/MainFrame.h` — add handler and ID constant
- Modify: `src/ui/MainFrame.cpp` — add toolbar button, event handler, event table entry

- [ ] **Step 1: Add toolbar ID in MainFrame.cpp enum**

Add after `ID_TOOLBAR_DBPATH` (line 52):

```cpp
    ID_TOOL_DETAIL_TOGGLE = wxID_HIGHEST + 302,
```

- [ ] **Step 2: Add handler declaration in MainFrame.h**

Add after line 88 (`onSearchClear`):

```cpp
    void onToggleDetailPane(wxCommandEvent& event);
```

- [ ] **Step 3: Add event table entry in MainFrame.cpp**

Add after line 84 (`EVT_SEARCH_CANCEL`):

```cpp
    EVT_MENU(ID_TOOL_DETAIL_TOGGLE, MainFrame::onToggleDetailPane)
```

- [ ] **Step 4: Add toggle button to toolbar in `initToolBar()`**

After `tb->AddControl(m_dbPathLabel);` (line 355) and before `tb->Realize();` (line 357):

```cpp
    tb->AddSeparator();
    tb->AddTool(ID_TOOL_DETAIL_TOGGLE, wxEmptyString,
                wxArtProvider::GetBitmap(wxART_LIST_VIEW, wxSize(32, 32)),
                "Toggle Detail Panel");
```

- [ ] **Step 5: Implement the toggle handler**

Add after `onSearchClear` (around line 714):

```cpp
void MainFrame::onToggleDetailPane(wxCommandEvent&) {
    wxAuiPaneInfo& pane = auiManager_.GetPane("detailPane");
    if (pane.IsOk()) {
        pane.Show(!pane.IsShown());
        if (pane.IsShown()) {
            // Ensure it has reasonable size when re-shown
            pane.BestSize(wxSize(320, -1));
        }
        auiManager_.Update();
    }
}
```

- [ ] **Step 6: Build to verify**

Run: `cmake --build build --parallel 8`
Expected: Build succeeds; toolbar has a toggle button that shows/hides the detail panel.

---

### Task 4: Handle AUI pane close event (sync toolbar state)

**Files:**
- Modify: `src/ui/MainFrame.cpp` — bind AUI pane close event

- [ ] **Step 1: Bind `wxEVT_AUI_PANE_CLOSE` in MainFrame constructor**

After `initTrayIcon()` (line 211), add:

```cpp
    // Track detail pane close event (for toolbar button state if needed)
    auiManager_.Bind(wxEVT_AUI_PANE_CLOSE, [this](wxAuiManagerEvent& evt) {
        wxAuiPaneInfo* pane = evt.GetPane();
        if (pane && pane->name == "detailPane") {
            // Nothing special needed — the toolbar toggle will re-show it.
            // Future enhancement: update toolbar button icon/state here.
        }
        evt.Skip();
    });
```

- [ ] **Step 2: Build to verify**

Run: `cmake --build build --parallel 8`
Expected: Build succeeds.

---

### Task 5: Preserve detail pane state across frame hide/show

**Files:**
- Modify: `src/ui/MainFrame.cpp` — update `onClose()` to save pane state
- Modify: `src/ui/MainFrame.h` — add state flag

- [ ] **Step 1: Add state member in MainFrame.h**

Add after `config::AppConfig config_;` (line 104):

```cpp
    bool detailPaneVisible_{true};
```

- [ ] **Step 2: Track visibility changes in the pane close handler**

Update the `wxEVT_AUI_PANE_CLOSE` binding (from Task 4) to update the flag:

```cpp
    auiManager_.Bind(wxEVT_AUI_PANE_CLOSE, [this](wxAuiManagerEvent& evt) {
        wxAuiPaneInfo* pane = evt.GetPane();
        if (pane && pane->name == "detailPane") {
            detailPaneVisible_ = false;
        }
        evt.Skip();
    });
```

Similarly, in the toggle handler, set `detailPaneVisible_` accordingly.

- [ ] **Step 3: Build to verify**

Run: `cmake --build build --parallel 8`
Expected: Build succeeds.

---

### Task 6: Update ProxyDetailPanel to handle pinned/collapsed visual state

**Files:**
- Modify: `src/ui/ProxyDetailPanel.h` — add method to respond to show/hide
- Modify: `src/ui/ProxyDetailPanel.cpp` — optionally add visual feedback

- [ ] **Step 1: Add `OnShow()` override in ProxyDetailPanel.h**

```cpp
public:
    void OnShow(bool show);
```

- [ ] **Step 2: Implement `OnShow()` in ProxyDetailPanel.cpp**

```cpp
void ProxyDetailPanel::OnShow(bool show) {
    if (show) {
        // Refresh content when panel becomes visible again
        // (content is already set via UpdateDetail — just ensure it's visible)
        Refresh();
    }
}
```

- [ ] **Step 3: Build to verify**

Run: `cmake --build build --parallel 8`
Expected: Build succeeds.

---

### Task 7: Run full test suite and verify

**Files:**
- Test: Full build + test

- [ ] **Step 1: Build all**

Run: `cmake --build build --parallel 8`
Expected: All builds pass with zero warnings.

- [ ] **Step 2: Run unit tests**

Run: `ctest -V`
Expected: All tests pass (706/706 as baseline).

- [ ] **Step 3: Manual UI verification checklist**

1. Launch `build/validproxy.exe`
2. Verify ProxyDetailPanel appears docked on the right with caption "Proxy Details"
3. Verify close button (X) collapses the panel
4. Verify pin button toggles between auto-hide (unpinned) and fixed (pinned)
5. Verify the panel can be resized by dragging its left edge
6. Verify selecting a proxy in ProxyListPanel updates the detail panel content
7. Click the toolbar toggle button — panel should re-appear if closed
8. Drag the panel out to float — verify it re-docks when dragged back to right edge
9. Verify LogPanel and SubscriptionPanel are unaffected
10. Resize the window — verify everything re-lays out properly

---

## Rollback Plan

If any step fails:

1. **Build failure**: Check for missing includes (`wx/aui/aui.h` already in MainFrame.h), verify enum ID doesn't conflict
2. **Pane not visible**: Verify `auiManager_.Update()` is called after `AddPane()`, and nothing else hides it
3. **Layout breakup**: Restore `initPanels()` to original; comment out AUI detail pane addition; debug step by step
4. **Double-free / crash on close**: Ensure `auiManager_.UnInit()` is called BEFORE panel deletion in destructor (already the case at line 218)
