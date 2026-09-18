#ifndef PROXY_SCORER_H
#define PROXY_SCORER_H

#include "ProfileExItem.h"
#include <cstdint>

namespace scoring {

// Scoring result for a single proxy.
struct ScoreResult {
  double total_score = 0.0;       // [0, 100] weighted aggregate
  double speed_score = 0.0;       // [0, 100] latency-based
  double stability_score = 0.0;   // [0, 100] consecutive_failures + success rate
  double history_score = 0.0;     // [0, 100] 3-factor historical health
  bool   cold_start  = false;     // true when start_count == 0
};

// Compute the three-factor score for a single ProfileExItem.
// The engine is a pure function: no DB access, no side effects.
ScoreResult compute(const db::models::ProfileExItem& item,
                    double speed_ms_min = 0.0,
                    double speed_ms_max = 5000.0);

// Compute scores for a batch of items. Returns vector aligned with input.
std::vector<ScoreResult> computeBatch(
    const std::vector<db::models::ProfileExItem>& items,
    double speed_ms_min = 0.0,
    double speed_ms_max = 5000.0);

} // namespace scoring

#endif // PROXY_SCORER_H
