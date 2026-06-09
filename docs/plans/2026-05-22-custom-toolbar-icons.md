---
title: "plan: Custom Toolbar Icons"
type: plan
status: draft
date: 2026-05-22
origin: "Migrated from docs/superpowers/plans/"
---

# Custom Toolbar Icons Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace stock wxArtProvider toolbar icons with custom 48px PNG icons extracted from `button_img_decoded.png` (333×48 design reference), with transparent background, runtime loading from `bin/icons/`, and graceful fallback to wxArtProvider.

## Files Created / Modified

| File | Action | Purpose |
|------|--------|---------|
| `bin/icons/` | **Create directory** | Store toolbar icon PNGs |
| `bin/icons/tool_update.png` | **Create** | Update All icon (37×45) |
| `bin/icons/tool_test.png` | **Create** | Test icon (38×45) |
| `bin/icons/tool_find.png` | **Create** | Find icon (37×45) |
| `bin/icons/tool_dedup.png` | **Create** | Dedup icon (38×45) |
| `bin/icons/tool_import.png` | **Create** | Import icon (37×45) |
| `bin/icons/tool_config.png` | **Create** | Config icon (38×45) |
| `src/ui/ToolbarIcons.h` | **Create** | Header-only runtime loader with wxArtProvider fallback |
| `src/ui/MainFrame.cpp` | **Modify** | Replace 6 `wxArtProvider::GetBitmapBundle(...)` calls with `ToolbarIcons::load(...)` |
| `docs/superpowers/plans/2026-05-22-custom-toolbar-icons.md` | **Create** | This plan |

## Prerequisites

- Python 3 with Pillow (`pip install Pillow`) for icon extraction
- CMake build system with Ninja
- `button_img_decoded.png` exists at project root (333×48 px, 24bpp RGB, #F0F0F0 background)

## Task 1 — Extract icon PNGs with alpha transparency

**File to create:** `bin/icons/` directory + 6 PNG files

**Goal:** Extract 6 icon regions from the design ref, convert #F0F0F0 to transparent alpha, save as 32bpp RGBA PNGs.

### Toolbar icon regions (from analysis of 333×48 design ref)

| # | Tool ID | X range | Width | File |
|---|---------|---------|-------|------|
| 1 | UpdateAll | 4–40 | 37 | `tool_update.png` |
| 2 | Test | 50–87 | 38 | `tool_test.png` |
| 3 | Find | 97–133 | 37 | `tool_find.png` |
| 4 | Dedup | 143–180 | 38 | `tool_dedup.png` |
| 5 | Import | 190–226 | 37 | `tool_import.png` |
| 6 | Config | 236–273 | 38 | `tool_config.png` |

### Extraction script logic (Python + Pillow)

```python
from PIL import Image

img = Image.open("button_img_decoded.png").convert("RGBA")
pixels = img.load()

# Convert #F0F0F0 to transparent (tolerance ±2 to handle anti-aliasing)
BG = (240, 240, 240)
for y in range(img.height):
    for x in range(img.width):
        r, g, b, a = pixels[x, y]
        if abs(r - BG[0]) <= 2 and abs(g - BG[1]) <= 2 and abs(b - BG[2]) <= 2:
            pixels[x, y] = (r, g, b, 0)

regions = {
    "tool_update": (4, 0, 40, 48),
    "tool_test":   (50, 0, 87, 48),
    "tool_find":   (97, 0, 133, 48),
    "tool_dedup":  (143, 0, 180, 48),
    "tool_import": (190, 0, 226, 48),
    "tool_config": (236, 0, 273, 48),
}

for name, (x1, y1, x2, y2) in regions.items():
    icon = img.crop((x1, y1, x2, y2))
    # Crop inner bbox to remove empty padding rows
    bbox = icon.getbbox()
    if bbox:
        icon = icon.crop(bbox)
    icon.save(f"bin/icons/{name}.png")
```

- All 6 files saved to `bin/icons/`
- Each has proper alpha channel (transparent background)
- Content is cropped to tight bounding box (removing padding rows/cols)

### Verification

- `ls -la bin/icons/` shows 6 PNG files
- Each file < 10KB

## Task 2 — Create `src/ui/ToolbarIcons.h`

**File to create:** `src/ui/ToolbarIcons.h`

**Goal:** Header-only utility that loads toolbar icons from `bin/icons/` relative to executable path, with graceful fallback to `wxArtProvider`.

### Design decisions

- **Header-only**: Matches the existing `Icons.h` pattern in the same directory
- **No new .cpp**: Keeps build system untouched
- **Fallback map**: Each custom icon has a corresponding `wxArtID` fallback if the PNG is missing
- **Alpha safety net**: Even if PNG loads without alpha, `makeBgTransparent()` handles #F0F0F0→alpha conversion

### Implementation

```cpp
#ifndef UI_TOOLBAR_ICONS_H
#define UI_TOOLBAR_ICONS_H

#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/artprov.h>
#include <wx/stdpaths.h>
#include <wx/file.h>

namespace ToolbarIcons {

// Helper: convert wxImage's #F0F0F0 background to transparent alpha
inline void makeBgTransparent(wxImage& img) {
    if (!img.HasAlpha()) {
        img.InitAlpha();
    }
    unsigned char* data = img.GetData();
    unsigned char* alpha = img.GetAlpha();
    int w = img.GetWidth(), h = img.GetHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int i = (y * w + x) * 3;
            int ai = y * w + x;
            unsigned char r = data[i], g = data[i+1], b = data[i+2];
            if (abs(r - 240) <= 2 && abs(g - 240) <= 2 && abs(b - 240) <= 2) {
                alpha[ai] = 0;  // transparent
            }
        }
    }
}

inline wxBitmapBundle load(const wxString& name) {
    wxString exeDir = wxStandardPaths::Get().GetExecutablePath().BeforeLast('\\');
    wxString path = exeDir + "\\icons\\" + name + ".png";

    if (wxFile::Exists(path)) {
        wxImage img(path);
        if (img.IsOk()) {
            makeBgTransparent(img);
            return wxBitmapBundle::FromBitmap(wxBitmap(img));
        }
    }

    // Fallback to stock icon
    struct Fallback { wxString name; wxArtID artId; };
    static const Fallback fbs[] = {
        { "tool_update", wxART_EXECUTABLE_FILE },
        { "tool_test",   wxART_TICK_MARK },
        { "tool_find",   wxART_FIND },
        { "tool_dedup",  wxART_LIST_VIEW },
        { "tool_import", wxART_FILE_OPEN },
        { "tool_config", wxART_LIST_VIEW },
    };
    for (auto& fb : fbs) {
        if (fb.name == name) {
            return wxArtProvider::GetBitmapBundle(fb.artId);
        }
    }
    return wxArtProvider::GetBitmapBundle(wxART_MISSING_IMAGE);
}

} // namespace ToolbarIcons
#endif // UI_TOOLBAR_ICONS_H
```

### Key includes

- `wx/bitmap.h` — wxBitmap
- `wx/image.h` — wxImage (alpha channel manipulation)
- `wx/artprov.h` — wxArtProvider fallback
- `wx/stdpaths.h` — exe path resolution
- `wx/file.h` — wxFile::Exists

## Task 3 — Update `MainFrame::initToolBar()`

**File to modify:** `src/ui/MainFrame.cpp`

**Changes:**

1. Add include at top of file:
   ```cpp
   #include "ToolbarIcons.h"
   ```

2. In `initToolBar()`, replace all 6 calls (around lines 268–273):
   ```cpp
   // BEFORE:
   tb->AddTool(ID_TOOL_UPDATE_ALL, "Update", wxArtProvider::GetBitmapBundle(wxART_EXECUTABLE_FILE));
   tb->AddTool(ID_TOOL_TEST,       "Test",   wxArtProvider::GetBitmapBundle(wxART_TICK_MARK));
   tb->AddTool(ID_TOOL_FIND,       "Find",   wxArtProvider::GetBitmapBundle(wxART_FIND));
   tb->AddTool(ID_TOOL_DEDUP,      "Dedup",  wxArtProvider::GetBitmapBundle(wxART_LIST_VIEW));
   tb->AddTool(ID_TOOL_IMPORT,     "Import", wxArtProvider::GetBitmapBundle(wxART_FILE_OPEN));
   tb->AddTool(ID_TOOL_CONFIG,     "Config", wxArtProvider::GetBitmapBundle(wxART_LIST_VIEW));

   // AFTER:
   tb->AddTool(ID_TOOL_UPDATE_ALL, "Update", ToolbarIcons::load("tool_update"));
   tb->AddTool(ID_TOOL_TEST,       "Test",   ToolbarIcons::load("tool_test"));
   tb->AddTool(ID_TOOL_FIND,       "Find",   ToolbarIcons::load("tool_find"));
   tb->AddTool(ID_TOOL_DEDUP,      "Dedup",  ToolbarIcons::load("tool_dedup"));
   tb->AddTool(ID_TOOL_IMPORT,     "Import", ToolbarIcons::load("tool_import"));
   tb->AddTool(ID_TOOL_CONFIG,     "Config", ToolbarIcons::load("tool_config"));
   ```

3. No other changes needed — rest of initToolBar() remains identical.

## Task 4 — Build and verify

**Commands:**
```bash
cd <project_root>
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
ctest -V
```

**Verification checklist:**
- [ ] Build succeeds with 0 errors, 0 warnings
- [ ] 3/3 unit tests pass
- [ ] `validproxy.exe` starts without crash
- [ ] Toolbar shows 6 custom icons (not stock wxArtProvider icons)
- [ ] PNG files exist at `bin/icons/` and are readable

**If buttons still have wrong background:**
- Verify `bin/icons/*.png` files have alpha channel
- Check that `ToolbarIcons::makeBgTransparent()` is executing
- Temporarily add `wxLogMessage` to confirm PNG loading path
