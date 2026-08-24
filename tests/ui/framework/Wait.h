// tests/ui/framework/Wait.h
#pragma once
#include <functional>
namespace uitest {
// Polls condition every pollIntervalMs until true or timeoutMs elapsed.
// Final evaluation happens once more AFTER the last sleep (no infinite waits).
bool waitFor(int timeoutMs, int pollIntervalMs, const std::function<bool()>& condition);
}
