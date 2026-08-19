#include "ProxyScorer.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace scoring {

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ScoreResult compute(const db::models::ProfileExItem& item,
                    double speed_ms_min,
                    double speed_ms_max) {
  ScoreResult result;

  // Parse delay (ms)
  double delay_ms = 0.0;
  try {
    delay_ms = std::stod(item.delay);
  } catch (...) {
    delay_ms = 1000.0; // fallback
  }

  // --- Speed score: linear mapping [min, max] → [100, 0] -------------------
  double clamped_delay = std::clamp(delay_ms, speed_ms_min, speed_ms_max);
  result.speed_score = std::max(0.0, 100.0 * (1.0 - clamped_delay / speed_ms_max));

  // --- Stability score: consecutive_failures penalty -----------------------
  constexpr int kBlacklistThreshold = 10;
  if (item.consecutive_failures >= kBlacklistThreshold) {
    result.stability_score = 0.0;
  } else {
    result.stability_score = 100.0 * (1.0 - static_cast<double>(item.consecutive_failures) / kBlacklistThreshold);
  }

  // --- History score: three-factor Bayesian --------------------------------
  bool cold_start = (item.start_count == 0);
  result.cold_start = cold_start;

  if (cold_start) {
    // Cold start: zero history, no evaluation data yet.
    result.history_score = 0.0;
  } else {
    // Kicker: log2(start_count+1)/log2(6), saturates at 5 starts
    double kicker = std::min(std::log2(static_cast<double>(item.start_count) + 1.0) /
                             std::log2(6.0), 1.0);

    // Health: (stable+1)/(start_count+2) — Bayesian smoothing
    int stable = item.start_count - item.crash_count;
    if (stable < 0) stable = 0;
    double health = static_cast<double>(stable + 1) /
                    static_cast<double>(item.start_count + 2);

    // Duration: average session length, clamped to [0, 1] at 30 min
    double avg_runtime_ms = static_cast<double>(item.total_runtime_ms) /
                            static_cast<double>(item.start_count);
    double duration = std::clamp(avg_runtime_ms / 1800000.0, 0.0, 1.0);

    result.history_score = 100.0 * (0.30 * kicker + 0.40 * health + 0.30 * duration);
  }

  // --- Total weighted score ------------------------------------------------
  // Configurable weights: speed=20, stability=30, history=50 (from config)
  const double kSpeedW     = 0.20;
  const double kStabilityW = 0.30;
  const double kHistoryW   = 0.50;

  if (cold_start) {
    // Re-normalize: speed:stability = 20:30 → 40:60
    result.total_score = 0.40 * result.speed_score + 0.60 * result.stability_score;
  } else {
    result.total_score = kSpeedW * result.speed_score +
                         kStabilityW * result.stability_score +
                         kHistoryW   * result.history_score;
  }

  return result;
}

std::vector<ScoreResult> computeBatch(
    const std::vector<db::models::ProfileExItem>& items,
    double speed_ms_min,
    double speed_ms_max) {
  std::vector<ScoreResult> results;
  results.reserve(items.size());
  for (const auto& item : items) {
    results.push_back(compute(item, speed_ms_min, speed_ms_max));
  }
  return results;
}

} // namespace scoring
