#include "task/TaskFormat.h"

#include <ctime>
#include <stdexcept>

namespace {

// Shared steady-stamp parse core: stoll + reject trailing garbage, mirroring
// the validation inside elapsedSecondsSince so all consumers agree on what an
// "invalid" stamp is. Returns false for empty / non-numeric / trailing-garbage
// / out-of-range input; on success writes the raw tick count to `out`.
bool parseSteadyTicks(const std::string& steadyCount, long long& out) {
    if (steadyCount.empty()) {
        return false;
    }
    long long raw = 0;
    std::size_t parsed = 0;
    try {
        raw = std::stoll(steadyCount, &parsed);
    } catch (const std::exception&) {
        return false;   // "abc" (invalid_argument), oversized (out_of_range)
    }
    if (parsed != steadyCount.size()) {
        return false;   // trailing garbage: "12abc" must not read as 12
    }
    out = raw;
    return true;
}

} // namespace

namespace task {

long long elapsedSecondsSince(const std::string& steadyCount,
                              std::chrono::steady_clock::time_point now) {
    long long raw = 0;
    if (!parseSteadyTicks(steadyCount, raw)) {
        return -1;   // empty / non-numeric / trailing garbage / out of range
    }
    // Ticks-per-second from the clock period (e.g. 10'000'000 for 100ns ticks).
    const long long ticksPerSecond =
        static_cast<long long>(std::chrono::steady_clock::period::den) /
        static_cast<long long>(std::chrono::steady_clock::period::num);
    const long long nowTicks =
        static_cast<long long>(now.time_since_epoch().count());
    const long long diff = nowTicks - raw;
    if (diff <= 0) {
        return 0;    // future stamp (or same instant): never render "-5s"
    }
    return diff / ticksPerSecond;
}

std::string wallClockText(const std::string& steadyCount,
                          std::chrono::steady_clock::time_point steadyNow,
                          std::chrono::system_clock::time_point wallNow) {
    const long long secs = elapsedSecondsSince(steadyCount, steadyNow);
    if (secs < 0) {
        return "- -";   // empty / non-numeric / trailing garbage
    }
    const std::time_t when =
        std::chrono::system_clock::to_time_t(wallNow - std::chrono::seconds(secs));
    // Same convention as the rest of the codebase (ShareLinkExportService.cpp
    // / AppController.cpp): localtime + strftime, UI-thread-only caller.
    const std::tm* local = std::localtime(&when);
    if (local == nullptr) return "- -";
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", local);
    return std::string(buf);
}

std::string wallClockDateTime(const std::string& steadyCount,
                              std::chrono::steady_clock::time_point steadyNow,
                              std::chrono::system_clock::time_point wallNow) {
    // Same core sentinel as wallClockText: elapsedSecondsSince returns -1 for
    // empty / non-numeric / trailing-garbage input. wallClockText renders that
    // as "- -"; the report-format wrapper renders it as the empty string.
    const long long secs = elapsedSecondsSince(steadyCount, steadyNow);
    if (secs < 0) {
        return std::string();
    }
    const std::time_t when =
        std::chrono::system_clock::to_time_t(wallNow - std::chrono::seconds(secs));
    const std::tm* local = std::localtime(&when);
    if (local == nullptr) {
        return std::string();
    }
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", local);
    return std::string(buf);
}

long long durationMinutesBetween(const std::string& startSteadyCount,
                                 const std::string& endSteadyCount) {
    long long startTicks = 0;
    long long endTicks = 0;
    if (!parseSteadyTicks(startSteadyCount, startTicks) ||
        !parseSteadyTicks(endSteadyCount, endTicks)) {
        return -1;   // either stamp empty / non-numeric / trailing garbage
    }
    // Ticks-per-second from the clock period (e.g. 1'000'000'000 for 1ns ticks).
    const long long ticksPerSecond =
        static_cast<long long>(std::chrono::steady_clock::period::den) /
        static_cast<long long>(std::chrono::steady_clock::period::num);
    const long long diffTicks = endTicks - startTicks;
    // Integer division of a non-negative tick delta floors naturally.
    return diffTicks / ticksPerSecond / 60;
}

} // namespace task
