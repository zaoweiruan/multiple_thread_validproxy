#ifndef UI_TOOLBAR_ICONS_H
#define UI_TOOLBAR_ICONS_H

// -------------------------------------------------------------------
// ToolbarIcons — runtime loader for custom toolbar icon PNGs
//
// Loads icons from bin/icons/<name>.png relative to executable path.
// Falls back to wxArtProvider stock icons if the file is missing.
//
// Usage:
//   tb->AddTool(ID_TOOL_FIND, "Find", ToolbarIcons::load("tool_find"));
// -------------------------------------------------------------------

#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/artprov.h>
#include <wx/stdpaths.h>
#include <wx/file.h>

namespace ToolbarIcons {

// -------------------------------------------------------------------
//  Helper: remove light-grey background pixels (safety net)
//  Extracted PNGs already have proper alpha from chroma-key.
//  This catches any residual near-white/grey background pixels
//  that anti-aliasing or compression may have left behind.
// -------------------------------------------------------------------
inline void makeBgTransparent(wxImage& img) {
    if (!img.HasAlpha()) {
        img.InitAlpha();
    }
    unsigned char* data  = img.GetData();
    unsigned char* alpha = img.GetAlpha();
    int w = img.GetWidth();
    int h = img.GetHeight();

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx  = (y * w + x) * 3;
            int aIdx = y * w + x;
            int r = static_cast<int>(data[idx]);
            int g = static_cast<int>(data[idx + 1]);
            int b = static_cast<int>(data[idx + 2]);
            // Light grey: all channels ≥ 190 and relatively desaturated
            int mx = r; if (g > mx) mx = g; if (b > mx) mx = b;
            int mn = r; if (g < mn) mn = g; if (b < mn) mn = b;
            if (r >= 190 && g >= 190 && b >= 190 && (mx - mn) <= 35) {
                alpha[aIdx] = 0;
            }
        }
    }
}

// -------------------------------------------------------------------
//  Prepare bitmap for wxMSW toolbar.
//
//  wxMSW native toolbar creates image lists with ILC_COLOR32 | ILC_MASK.
//  When BOTH alpha channel and mask are present on a 32bpp bitmap,
//  the native toolbar renders incorrectly (black/dark background).
//
//  Fix: remove alpha entirely and use mask-only transparency.
//  Semi-transparent edge pixels are blended with the mask colour
//  so anti-aliasing is preserved with the mask.
// -------------------------------------------------------------------
inline wxBitmap prepareForToolbar(wxImage& img) {
    const int n = img.GetWidth() * img.GetHeight();
    unsigned char* const data = img.GetData();

    if (img.HasAlpha()) {
        unsigned char* const alpha = img.GetAlpha();
        const int MASK_R = 240, MASK_G = 240, MASK_B = 240;
        for (int i = 0; i < n; ++i) {
            int idx = i * 3;
            int a = alpha[i];
            if (a < 128) {
                // Mostly transparent → fully mask out
                data[idx] = static_cast<unsigned char>(MASK_R);
                data[idx + 1] = static_cast<unsigned char>(MASK_G);
                data[idx + 2] = static_cast<unsigned char>(MASK_B);
            } else if (a < 255) {
                // Semi-transparent edge → blend with mask colour
                double t = a / 255.0;
                data[idx]     = static_cast<unsigned char>(data[idx]     * t + MASK_R * (1.0 - t));
                data[idx + 1] = static_cast<unsigned char>(data[idx + 1] * t + MASK_G * (1.0 - t));
                data[idx + 2] = static_cast<unsigned char>(data[idx + 2] * t + MASK_B * (1.0 - t));
            }
            // alpha=255 → keep original pixel (fully opaque)
        }
        // Remove alpha — wxMSW toolbar uses mask, not alpha
        img.ClearAlpha();
    }

    // Set mask colour — wxBitmap(wxImage) will create a wxMask from this
    img.SetMaskColour(240, 240, 240);
    wxBitmap bmp(img);
    return bmp;
}

// -------------------------------------------------------------------
//  load — runtime load with fallback
// -------------------------------------------------------------------
inline wxBitmapBundle load(const wxString& name) {
    wxString exeDir = wxStandardPaths::Get().GetExecutablePath().BeforeLast('\\');
    wxString path   = exeDir + "\\icons\\" + name + ".png";

    if (wxFile::Exists(path)) {
        wxImage img(path);
        if (img.IsOk()) {
            makeBgTransparent(img);
            return wxBitmapBundle::FromBitmap(prepareForToolbar(img));
        }
    }

    // Fallback map: each custom icon name → stock wxArtID
    struct Fallback { const char* name; wxArtID artId; };
    static const Fallback fbs[] = {
        { "tool_update", wxART_EXECUTABLE_FILE },
        { "tool_test",   wxART_TICK_MARK },
        { "tool_find",   wxART_FIND },
        { "tool_dedup",  wxART_LIST_VIEW },
        { "tool_import", wxART_FILE_OPEN },
        { "tool_config", wxART_LIST_VIEW },
    };
    for (const auto& fb : fbs) {
        if (name == fb.name) {
            return wxArtProvider::GetBitmapBundle(fb.artId);
        }
    }
    return wxArtProvider::GetBitmapBundle(wxART_MISSING_IMAGE);
}

} // namespace ToolbarIcons

#endif // UI_TOOLBAR_ICONS_H
