#pragma once
// UnifiedMonitorRows.h
// Pure function: merge StandaloneMonitorRow + PoolMemberView into UnifiedMonitorRow.
// Extracted for testability without linking the full AppController dependency chain.

#include <vector>
#include <string>
#include "AppController.h"   // StandaloneMonitorRow, UnifiedMonitorRow, MonitorType
#include "StandaloneProxyPool.h" // proxy::PoolMemberView

// Merge standalone monitor rows and pool member views into a unified list.
// Standalone rows appear first, pool rows second (stable ordering, no cross-lock dependency).
inline std::vector<UnifiedMonitorRow> buildUnifiedMonitorRows(
    const std::vector<StandaloneMonitorRow>& standalone,
    const std::vector<proxy::PoolMemberView>& members)
{
    std::vector<UnifiedMonitorRow> rows;
    rows.reserve(standalone.size() + members.size());

    // Phase 1: standalone rows (sorted first)
    for (std::size_t i = 0; i < standalone.size(); ++i) {
        const StandaloneMonitorRow& s = standalone[i];
        UnifiedMonitorRow row;
        row.type       = MonitorType::Standalone;
        row.indexId    = s.indexId;
        row.host       = s.host;
        row.socksPort  = s.socksPort;
        row.pid        = s.pid;
        row.durationMs = s.durationMs;
        // UTF-8 for "运行中" = \xe8\xbf\x90\xe8\xa1\x8c\xe4\xb8\xad
        row.state      = "\xe8\xbf\x90\xe8\xa1\x8c\xe4\xb8\xad";
        row.lastDelayMs = s.lastDelayMs;
        row.lastAlive  = false;
        row.failStreak = 0;
        // lastError remains default empty
        rows.push_back(row);
    }

    // Phase 2: pool rows (sorted second)
    for (std::size_t i = 0; i < members.size(); ++i) {
        const proxy::PoolMemberView& m = members[i];
        UnifiedMonitorRow row;
        row.type       = MonitorType::Pool;
        row.indexId    = std::to_string(m.indexId); // PoolMemberView::indexId is int
        row.tag        = m.tag;
        row.host       = m.host;
        row.socksPort  = m.socksPort;
        row.pid        = static_cast<int64_t>(m.pid);
        row.durationMs = 0;
        row.state      = m.state; // "active" / "remove-requested" / "draining"
        row.lastDelayMs = m.lastDelayMs;
        row.lastAlive  = m.lastAlive;
        row.failStreak = m.failStreak;
        row.lastError  = m.lastError;
        rows.push_back(row);
    }

    return rows;
}
