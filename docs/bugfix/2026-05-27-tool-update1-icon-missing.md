# Toolbar Update Icon Black Background Fix

## Issue

Two issues reported with the "更新" toolbar icon after replacing `tool_update` with `tool_update1`:

1. **Wrong image displayed** — the icon was not the expected `tool_update1` design
2. **Black background** — icon background changed from transparent to solid black

## Root Cause

The icon file `tool_update1.png` existed only in the design directory at `docs/design/ui/icon/png/tool_update1.png` but was **never copied** to the runtime icon directory `bin/icons/`.

The `ToolbarIcons::load("tool_update1")` call follows this fallback chain:

1. `bin/icons/tool_update1.png` → ❌ File does not exist
2. `bin/icons/tool_update1.ico` → ❌ File does not exist
3. Fallback map (stock wxArtIDs) → ❌ `"tool_update1"` has no matching entry (only `"tool_update"` is registered)
4. Returns **`wxART_MISSING_IMAGE`** → ⬛ wxWidgets stock "missing image" icon with black background

The original commit (`f5c8b27`) only changed the string in `MainFrame.cpp` but omitted the file copy step.

## Solution

**Copy the design icon to the runtime directory:**

```
docs/design/ui/icon/png/tool_update1.png → bin/icons/tool_update1.png
```

Now `ToolbarIcons::load("tool_update1")` finds the PNG file and processes it through the normal pipeline:
- `makeBgTransparent()` — removes residual light-grey pixels
- `prepareForToolbar()` — converts alpha channel to mask for wxMSW toolbar
- Result: icon displays correctly with transparent background

## Files Modified

| File | Change |
|------|--------|
| `bin/icons/tool_update1.png` | **Added** — copied from design directory |

> No source code changes needed; `MainFrame.cpp` already referenced `"tool_update1"` correctly.

## Verification

```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
ctest -V
```

Result: All 3 tests pass. No recompilation needed (only runtime asset added).

## Prevention

When adding new toolbar icons, ensure the PNG file is **always** placed in both:
- `docs/design/ui/icon/png/` — design source
- `bin/icons/` — runtime deployment

The build system does not automatically copy assets; this is a manual step.
