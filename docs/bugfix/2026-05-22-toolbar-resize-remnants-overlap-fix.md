# Toolbar Resize Remnants and Overlap Fix

## Issue

After replacing `wxTextCtrl` with `wxSearchCtrl` and adding `EVT_SIZE` for dynamic control sizing, two problems appeared when resizing the main window:

1. **Visual remnants**: The search input box left pixel artifacts (残影) on the toolbar at its previous position after resize
2. **Overlap**: The search box occasionally covered/overlapped the database path panel on the right

## Root Cause Analysis

### Problem 1: Visual Remnants

`wxSearchCtrl` on wxMSW wraps a native Windows EDIT control. When `onResize` calls `SetSize()` to narrow the control:
- The native child window shrinks, but the **old area** it previously occupied on the toolbar is **not automatically invalidated**
- The toolbar's `TBBS_SEP` separator (used internally by `wxToolBar` for `AddControl` children) does not repaint that region
- Result: old pixels from the native edit control remain visible — appearing as smeared remnants

This is a known wxMSW behavior: native child windows in toolbars require explicit parent invalidation after repositioning/resizing.

### Problem 2: Overlap

Toolbar layout sequence: `[6 tools ~192px] [stretch] [Search: label] [wxSearchCtrl] [dbPath wxStaticText]`

When the frame is narrowed:
1. `AddStretchableSpace()` shrinks to 0
2. `wxToolBar::RepositionControls()` places right-side controls sequentially using their **current widths**
3. If a previous `onResize` call widened the search box (up to 200px), the toolbar uses that width for layout
4. The dbPath label gets placed at `searchBox.x + 200`, potentially extending beyond the toolbar's client area
5. After placement, `onResize` recalculates — but by then the positions are already set by the toolbar using stale sizes

## Solution

Three changes in `src/ui/MainFrame.cpp`:

### 1. Force Toolbar Repaint After Resize (`onResize`, line 537-538)

```cpp
// Force toolbar to repaint — clears visual remnants left by the native
// wxSearchCtrl window at its old position after SetSize narrows it.
tb->Refresh();
tb->Update();
```

`Refresh()` invalidates the entire toolbar window. `Update()` forces immediate WM_PAINT rather than waiting for the next idle paint cycle. Together they ensure the area formerly occupied by the native search control is freshly painted with the toolbar background.

### 2. Defensive Overlap Check (`onResize`, lines 519-522)

```cpp
// Defensive: if toolbar is too narrow and controls overlap, fall back to minima
if (dbPos.x <= searchPos.x) {
    m_searchBox->SetSize(80, -1);
    m_dbPathLabel->SetSize(60, -1);
} else {
    // ... normal calculation ...
}
```

When the toolbar is so narrow that `RepositionControls()` places the dbPath label at or before the search box position (`dbPos.x <= searchPos.x`), fall back to minimum sizes immediately instead of using positional math that would produce nonsensical (negative or tiny) widths.

### 3. Frame Minimum Size (constructor, line 120)

```cpp
// Prevent too-narrow window that breaks toolbar right-side control layout
SetMinSize(wxSize(900, 600));
```

Ensures the frame cannot be resized narrower than 900px, which is sufficient for the left tools (~192px) + right-side controls (~500px) + margin. This prevents the overlap scenario at the source.

## Files Modified

| File | Change |
|------|--------|
| `src/ui/MainFrame.cpp` | Added `SetMinSize(wxSize(900,600))` in constructor |
| `src/ui/MainFrame.cpp` | Rewrote `onResize` with defensive overlap check + forced toolbar repaint |

## Verification

```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
ctest -V
```

Result: Build success, LSP diagnostics clean, all 3 tests pass.

## Affected Functions

### `MainFrame::onResize(wxSizeEvent&)` (complete, lines 507-539)

```
1. event.Skip()                                    — default resize handling
2. Get toolbar + control pointers
3. Query tbWidth, searchPos, dbPos
4. If dbPos.x <= searchPos.x:                      — overlap detected
   → set minima (80, 60)
   else:                                            — normal case
   → searchWidth = dbPos.x - searchPos.x ≈ [80..200]
   → dbWidth = tbWidth - dbPos.x - 8 ≥ 60
5. tb->Refresh(); tb->Update()                     — force full repaint
```

### `MainFrame::MainFrame` (line 120, new addition)

```
SetMinSize(wxSize(900, 600));    // inserted before Bind() calls
```

## Note

The combination of `Refresh()` and `Update()` is important:
- `Refresh()` alone: marks the window as needing repaint, but the actual paint happens on the next idle event — other native children might paint over it first
- `Refresh()` + `Update()`: forces **synchronous** repaint of the invalidated region, ensuring the toolbar background is fully drawn before Windows returns to its event loop
