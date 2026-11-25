#pragma once

#include <chrono>
#include <string>

namespace task {

// Convert a TaskHandle timestamp (steady_clock::now().time_since_epoch().count()
// rendered as a decimal string, see src/task/TaskQueue.cpp) into "seconds since
// that stamp", evaluated against `now`. steady_clock has no wall-clock epoch, so
// relative time is the only thing the UI can honestly display.
// Returns -1 for empty / non-numeric / out-of-range input; clamps negative
// (future) results to 0.
long long elapsedSecondsSince(const std::string& steadyCount,
                              std::chrono::steady_clock::time_point now);

// Convert a TaskHandle steady stamp into the LOCAL wall-clock "HH:MM:SS" the
// task was created/started at: wall(stamp) = wallNow - (steadyNow - stamp).
// Returns "- -" for empty / non-numeric / trailing-garbage input.
// (owner request 2026-11-04: 提交于列显示 hh:mm:ss 而非相对秒数)
std::string wallClockText(const std::string& steadyCount,
                          std::chrono::steady_clock::time_point steadyNow,
                          std::chrono::system_clock::time_point wallNow);

// Same mapping as wallClockText but with the FULL local wall-clock date+time
// "yyyy-mm-dd hh:mm:ss" (report format v2). Returns an empty string for
// empty / non-numeric / trailing-garbage input (vs wallClockText's "- -").
std::string wallClockDateTime(const std::string& steadyCount,
                              std::chrono::steady_clock::time_point steadyNow,
                              std::chrono::system_clock::time_point wallNow);

// Whole elapsed minutes between two steady stamps, as
// floor((end-start)/ticksPerSecond/60). Returns -1 if either stamp is empty /
// non-numeric / trailing-garbage. Both stamps share the steady_clock period.
long long durationMinutesBetween(const std::string& startSteadyCount,
                                 const std::string& endSteadyCount);

} // namespace task
