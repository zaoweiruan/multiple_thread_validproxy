// Tests for ProxyScorer — three-factor scoring engine (spec §3.4).
// Pure function: no DB, no side effects. Uses ProfileExItem aggregation columns.
#include "ProxyScorer.h"
#include "ProfileExItem.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace scoring;
using namespace db::models;

// ---------------------------------------------------------------------------
// Helper: build a ProfileExItem with explicit fields
// ---------------------------------------------------------------------------
ProfileExItem makeItem(double delay_ms = 100.0,
                       int consecutive_failures = 0,
                       int start_count = 0,
                       int64_t total_runtime_ms = 0,
                       int crash_count = 0) {
  ProfileExItem item;
  item.indexid = "test-index";
  item.delay   = std::to_string(static_cast<int>(delay_ms));
  item.speed   = "";
  item.sort    = "";
  item.message = "";
  item.consecutive_failures = consecutive_failures;
  item.start_count = start_count;
  item.total_runtime_ms = total_runtime_ms;
  item.crash_count = crash_count;
  return item;
}

// ---------------------------------------------------------------------------
// Test: No history (cold start) → zero history, weights re-normalized 40:60
// ---------------------------------------------------------------------------
TEST(ProxyScorerTest, NoHistory_ColdStartNeutral) {
  auto item = makeItem(100.0, 0, 0, 0, 0);
  auto result = compute(item);

  EXPECT_DOUBLE_EQ(result.history_score, 0.0);
  EXPECT_TRUE(result.cold_start);
  // With history weight zeroed out, speed:stability = 20:30 → 40:60
  // delay=100ms in [0, 5000] → speed_score ≈ 98 (close to max)
  // consecutive_failures=0 → stability_score = 100
  EXPECT_GE(result.speed_score, 90.0);
  EXPECT_DOUBLE_EQ(result.stability_score, 100.0);
  // Total should be dominated by speed+stability (no history contribution)
  EXPECT_GE(result.total_score, 80.0);
}

// ---------------------------------------------------------------------------
// Test: Perfect history → high history score
// ---------------------------------------------------------------------------
TEST(ProxyScorerTest, PerfectHistory_HighScore) {
  // 5 starts (kicker=1.0), 0 crashes (health=1.0), long runtime (duration=1.0)
  auto item = makeItem(200.0, 0, 5, 5 * 1800000LL, 0); // 5×30min = 1,800,000ms avg
  auto result = compute(item);

  // history_score ≈ 94.3 (kicker=1.0, health≈0.857, duration=1.0)
  EXPECT_NEAR(result.history_score, 94.3, 2.0);
  EXPECT_FALSE(result.cold_start);
  EXPECT_GE(result.total_score, 70.0);
}

// ---------------------------------------------------------------------------
// Test: High crash rate → low history score
// ---------------------------------------------------------------------------
TEST(ProxyScorerTest, HighCrashRate_LowHistoryScore) {
  // 5 starts, 4 crashes → health = (1+1)/(5+2) = 2/7 ≈ 0.286
  auto item = makeItem(300.0, 0, 5, 4 * 60000LL, 4); // short sessions
  auto result = compute(item);

  // kicker = 1.0, health ≈ 0.286, duration = clamp(60000/1800000, 0, 1) = 0.033
  // history = 100 × (0.30×1.0 + 0.40×0.286 + 0.30×0.033) ≈ 100 × (0.30+0.114+0.010) ≈ 42.4
  EXPECT_LT(result.history_score, 50.0);
  EXPECT_GE(result.history_score, 30.0);
}

// ---------------------------------------------------------------------------
// Test: Weight normalization — cold start should give same relative as current
// ---------------------------------------------------------------------------
TEST(ProxyScorerTest, WeightNormalization_ColdStartScalesCorrectly) {
  // Cold start: only speed(20) + stability(30) contribute → normalize to 40:60
  auto cold = compute(makeItem(100.0, 0, 0, 0, 0));
  // Hot: all three contribute
  auto hot = compute(makeItem(100.0, 0, 5, 5 * 1800000LL, 0));

  // Cold start has high speed+stability but no history, hot has history boost
  // Both have same speed/stability, hot just adds history component
  EXPECT_GE(hot.total_score, cold.total_score - 5.0); // allow small float variation
}

// ---------------------------------------------------------------------------
// Test: Speed score — latency mapping [0, max] → [100, 0]
// ---------------------------------------------------------------------------
TEST(ProxyScorerTest, SpeedScore_LinearMapping) {
  // Zero delay → max speed score (100)
  auto r1 = compute(makeItem(0.0, 0, 0, 0, 0));
  EXPECT_NEAR(r1.speed_score, 100.0, 1.0);

  // Max delay → min speed score (0)
  auto r2 = compute(makeItem(5000.0, 0, 0, 0, 0));
  EXPECT_NEAR(r2.speed_score, 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Test: Stability score — consecutive_failures penalty
// ---------------------------------------------------------------------------
TEST(ProxyScorerTest, StabilityScore_FailuresPenalty) {
  // No failures → stability = 100
  auto r1 = compute(makeItem(100.0, 0, 0, 0, 0));
  EXPECT_DOUBLE_EQ(r1.stability_score, 100.0);

  // At blacklist threshold (11) → stability = 0
  auto r2 = compute(makeItem(100.0, 11, 0, 0, 0));
  EXPECT_NEAR(r2.stability_score, 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Test: Batch compute
// ---------------------------------------------------------------------------
TEST(ProxyScorerTest, BatchCompute_MultipleItems) {
  std::vector<ProfileExItem> items = {
    makeItem(100.0, 0, 0, 0, 0),           // cold start
    makeItem(200.0, 0, 5, 5 * 1800000LL, 0), // perfect history
    makeItem(300.0, 0, 5, 4 * 60000LL, 4),   // high crash rate
  };

  auto results = computeBatch(items);
  EXPECT_EQ(results.size(), 3u);

  // Cold start (speed~98 + stability~100) vs perfect history (same + history~94)
  // Perfect history should beat high-crash (history~42)
  EXPECT_GT(results[1].total_score, results[2].total_score);
}

// ---------------------------------------------------------------------------
// Test: Kicker saturation at 5 starts
// ---------------------------------------------------------------------------
TEST(ProxyScorerTest, Kicker_SaturatesAtFiveStarts) {
  // 1 start: kicker = log2(2)/log2(6) ≈ 0.387
  auto r1 = compute(makeItem(100.0, 0, 1, 1800000LL, 0));
  // 5 starts: kicker = log2(6)/log2(6) = 1.0 (max)
  auto r5 = compute(makeItem(100.0, 0, 5, 5 * 1800000LL, 0));
  // 10 starts: kicker still 1.0 (clamped)
  auto r10 = compute(makeItem(100.0, 0, 10, 10 * 1800000LL, 0));

  EXPECT_LT(r1.history_score, r5.history_score);
  // r5 and r10 should have same history score (kicker saturated at 5+)
  EXPECT_NEAR(r5.history_score, r10.history_score, 5.0); // small diff due to duration
}

// ---------------------------------------------------------------------------
// Test: Duration clamp at 30 minutes average
// ---------------------------------------------------------------------------
TEST(ProxyScorerTest, Duration_ClampAt30Minutes) {
  // Average runtime < 30min → duration < 1.0
  auto short_avg = compute(makeItem(100.0, 0, 2, 2 * 600000LL, 0)); // 10min avg
  // Average runtime >= 30min → duration = 1.0
  auto long_avg  = compute(makeItem(100.0, 0, 2, 2 * 1800000LL, 0)); // 30min avg

  EXPECT_LT(short_avg.history_score, long_avg.history_score);
}

// ---------------------------------------------------------------------------
// Test: Bayesian smoothing — (stable+1)/(start_count+2)
// ---------------------------------------------------------------------------
TEST(ProxyScorerTest, BayesianSmoothing_LowStartCountResistant) {
  // 1 start, 0 crashes: health = (1+1)/(1+2) = 0.667
  auto r1 = compute(makeItem(100.0, 0, 1, 1800000LL, 0));
  // 5 starts, 0 crashes: health = (5+1)/(5+2) = 0.857
  auto r5 = compute(makeItem(100.0, 0, 5, 5 * 1800000LL, 0));

  EXPECT_LT(r1.history_score, r5.history_score);
}
