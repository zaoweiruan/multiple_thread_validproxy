// tests/ui/framework/Text.h - narrow the wide strings the UIA/Win32 APIs hand
// back so they can be streamed into Catch2 INFO / CHECK messages.
//
// Why this exists: every framework report function returns std::wstring because
// that is what COM gives us, but Catch2's message stream is std::ostream
// (char based). On this MinGW toolchain there is no implicit wide->narrow
// conversion, and Catch2 does not special case wchar_t. Wrapping every report
// in toUtf8() keeps the test code readable and - just as importantly - keeps
// the Chinese control names in the failure output readable, because we convert
// with CP_UTF8 rather than letting the C locale eat them.

#pragma once
#include <windows.h>
#include <string>

namespace uitest {

// Wide -> UTF-8 narrow. Handles embedded NULs by taking the whole string.
inline std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int needed = ::WideCharToMultiByte(
        CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr,
        nullptr);
    if (needed <= 0) return std::string();
    std::string out(static_cast<size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                          &out[0], needed, nullptr, nullptr);
    return out;
}

// Narrow -> wide, for feeding a literal into a UIA locator at runtime.
inline std::wstring toWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int needed = ::MultiByteToWideChar(
        CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                          &out[0], needed);
    return out;
}

}  // namespace uitest