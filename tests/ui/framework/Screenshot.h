// tests/ui/framework/Screenshot.h - window capture to PNG via GDI+.
#pragma once
#include <windows.h>
#include <string>

namespace uitest {

// Captures the client area (fallback: window rect) of hwnd and saves it as
// PNG at path. Returns false on any failure; never throws.
bool saveWindowPng(HWND hwnd, const std::wstring& path);

}
