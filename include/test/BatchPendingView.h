#pragma once

// tests helper header, linked into production too: ProxyBatchTester::
// getPendingProxies() reuses drainPendingView() so the UI list shows exactly
// the worker pickup order (bugfix 2026-11-03-Bugfix-CancelFlagPoisoning §3.3).
// Lives under include/test/ per the ProxyBatchQuery.h placement convention.

#include <cstddef>
#include <queue>
#include <vector>

namespace test {

// One pending-proxy reference: index into the source vector (proxies_ or
// insertedProxies_) + which vector it came from.
struct PendingIndexRef {
    int index = -1;
    bool inserted = false;   // true = high-priority insertedProxies_ queue
};

// Project the two batch queues into the worker pickup order:
// high-priority insertedQueue_ first (FIFO), then proxiesQueue_ (FIFO).
// Defensive bounds against the source vector sizes mirror the worker-side
// check (ProxyBatchTester.cpp:314-319) — a stale index is skipped, never
// rendered. Out-of-order kept: surviving items stay in queue order.
// limit == 0 yields nothing; at most `limit` refs are returned.
inline std::vector<PendingIndexRef> drainPendingView(const std::queue<int>& insertedQueue,
                                                     const std::queue<int>& mainQueue,
                                                     std::size_t insertedSize,
                                                     std::size_t mainSize,
                                                     std::size_t limit) {
    std::vector<PendingIndexRef> out;
    if (limit == 0) return out;
    std::queue<int> ins = insertedQueue;   // queues are copied: pure function
    std::queue<int> main = mainQueue;
    while (!ins.empty() && out.size() < limit) {
        const int idx = ins.front();
        ins.pop();
        if (idx >= 0 && static_cast<std::size_t>(idx) < insertedSize) {
            out.push_back(PendingIndexRef{idx, true});
        }
    }
    while (!main.empty() && out.size() < limit) {
        const int idx = main.front();
        main.pop();
        if (idx >= 0 && static_cast<std::size_t>(idx) < mainSize) {
            out.push_back(PendingIndexRef{idx, false});
        }
    }
    return out;
}

} // namespace test
