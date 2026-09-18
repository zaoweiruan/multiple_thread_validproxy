// tests/ui/framework/Wait.cpp
#include "framework/Wait.h"
#include <chrono>
#include <windows.h>
namespace uitest {
bool waitFor(int timeoutMs, int pollIntervalMs, const std::function<bool()>& condition) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) return true;
        ::Sleep(static_cast<DWORD>(pollIntervalMs));
    }
    return condition();
}
}
