#ifndef UI_ICONS_H
#define UI_ICONS_H

// Self-contained headers to avoid ordering issues
#include <wx/bitmap.h>
#include <wx/image.h>

namespace Icons {

inline wxBitmap getClearIcon() {
    // Create a 16x16 bitmap with proper RGBA transparency.
    // wxArtProvider stock icons may lack alpha on wxMSW.
    const int size = 16;
    wxImage img(size, size);
    unsigned char* data = img.GetData();
    unsigned char* alpha = new unsigned char[size * size];

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int idx = y * size + x;
            int dx = x - (size - 1) / 2;
            int dy = y - (size - 1) / 2;
            int adx = (dx < 0) ? -dx : dx;
            int ady = (dy < 0) ? -dy : dy;

            // Thick X: |dx| == |dy| (main & anti-diagonal) with 1px neighbours
            bool onX = (adx == ady) || (adx == ady + 1) || (adx + 1 == ady);
            if (onX && (adx || ady)) {
                data[idx * 3]     = 180;  // R
                data[idx * 3 + 1] = 40;   // G
                data[idx * 3 + 2] = 40;   // B
                alpha[idx] = 255;          // opaque
            } else {
                alpha[idx] = 0;            // fully transparent
            }
        }
    }

    img.SetAlpha(alpha);
    return wxBitmap(img);
}

} // namespace Icons

#endif // UI_ICONS_H
