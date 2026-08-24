// tests/ui/framework/Screenshot.cpp
#include "framework/Screenshot.h"
#include <gdiplus.h>
#include <objidl.h>
#include <memory>

namespace uitest {

// GDI+ token wrapper (startup once per process is enough for tests).
struct GdiplusSession {
    GdiplusSession() {
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&token_, &in, nullptr);
    }
    ~GdiplusSession() { if (token_) Gdiplus::GdiplusShutdown(token_); }
    ULONG_PTR token_ = 0;
};
static GdiplusSession& gdiplus() { static GdiplusSession s; return s; }

// Standard PNG encoder CLSID ({557CF406-1A04-11D3-9A73-0000F81EF32E}).
static CLSID pngEncoderClsid() {
    CLSID c;
    CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &c);
    return c;
}

bool saveWindowPng(HWND hwnd, const std::wstring& path) {
    if (!hwnd) return false;
    gdiplus();

    RECT rc{};
    if (!::GetWindowRect(hwnd, &rc)) return false;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return false;

    HDC wndDc = ::GetWindowDC(hwnd);
    HDC memDc = ::CreateCompatibleDC(wndDc);
    HBITMAP bmp = ::CreateCompatibleBitmap(wndDc, w, h);
    HGDIOBJ old = ::SelectObject(memDc, bmp);
    bool captured = ::BitBlt(memDc, 0, 0, w, h, wndDc, 0, 0, SRCCOPY | CAPTUREBLT) != FALSE;

    bool saved = false;
    if (captured) {
        std::unique_ptr<Gdiplus::Bitmap> bitmap(Gdiplus::Bitmap::FromHBITMAP(bmp, nullptr));
        if (bitmap) {
            const CLSID enc = pngEncoderClsid();
            saved = bitmap->Save(path.c_str(), &enc, nullptr) == Gdiplus::Ok;
        }
    }

    ::SelectObject(memDc, old);
    ::DeleteObject(bmp);
    ::DeleteDC(memDc);
    ::ReleaseDC(hwnd, wndDc);
    return saved;
}

}
