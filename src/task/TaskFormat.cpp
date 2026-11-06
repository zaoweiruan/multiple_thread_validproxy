#include "task/TaskFormat.h"

#include <ctime>
#include <stdexcept>

namespace task {

long long elapsedSecondsSince(const std::string& steadyCount,
                              std::chrono::steady_clock::time_point now) {
    if (steadyCount.empty()) {
        return -1;
    }
    long long raw = 0;
    std::size_t parsed = 0;
    try {
        raw = std::stoll(steadyCount, &parsed);
    } catch (const std::exception&) {
        return -1;   // "abc" (invalid_argument), oversized (out_of_range)
    }
    if (parsed != steadyCount.size()) {
        return -1;   // trailing garbage: "12abc" must not read as 12
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

} // namespace task
