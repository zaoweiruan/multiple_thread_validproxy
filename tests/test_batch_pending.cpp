// tests/test_batch_pending.cpp — pure pending-view projection for the batch
// test queues (bugfix 2026-11-03 §3.3, spec v1.2 增补).
//
// drainPendingView mirrors ProxyBatchTester's worker pickup order:
//   high-priority insertedQueue_ first (FIFO), then proxiesQueue_ (FIFO),
//   with the same defensive bounds check the worker applies (:314-319).
// The UI panel must show exactly this order, so the projection lives in one
// pure function tested here and reused by getPendingProxies().
#include <gtest/gtest.h>

#include <queue>
#include <vector>

#include "test/BatchPendingView.h"

namespace {

std::queue<int> makeQueue(std::initializer_list<int> values) {
    std::queue<int> q;
    for (const int v : values) {
        q.push(v);
    }
    return q;
}

// Compact "5i 0m" rendering for readable failures.
std::string render(const std::vector<test::PendingIndexRef>& refs) {
    std::string out;
    for (const test::PendingIndexRef& r : refs) {
        if (!out.empty()) out += " ";
        out += std::to_string(r.index);
        out += r.inserted ? "i" : "m";
    }
    return out;
}

} // namespace

TEST(BatchPendingViewTest, InsertedFirstThenMainFifo) {
    const std::vector<test::PendingIndexRef> refs = test::drainPendingView(
        makeQueue({5, 7}), makeQueue({0, 1, 2}), /*insertedSize=*/8, /*mainSize=*/3,
        /*limit=*/100);
    EXPECT_EQ(render(refs), "5i 7i 0m 1m 2m");
}

TEST(BatchPendingViewTest, LimitTruncatesInPickupOrder) {
    const std::vector<test::PendingIndexRef> refs = test::drainPendingView(
        makeQueue({5, 7}), makeQueue({0, 1, 2}), 8, 3, /*limit=*/3);
    EXPECT_EQ(render(refs), "5i 7i 0m");
}

TEST(BatchPendingViewTest, OutOfRangeAndNegativeIndicesAreSkipped) {
    // Worker-side defensive bounds check parity: a stale index must never
    // reach the UI (vectors may have been reloaded between queue push and read).
    const std::vector<test::PendingIndexRef> refs = test::drainPendingView(
        makeQueue({-1, 99, 2}), makeQueue({3, 1, 500}), /*insertedSize=*/3,
        /*mainSize=*/4, /*limit=*/100);
    EXPECT_EQ(render(refs), "2i 3m 1m");   // 2i, then main queue order kept (3, 1; 500 skipped)
}

TEST(BatchPendingViewTest, EmptyQueuesYieldEmpty) {
    const std::vector<test::PendingIndexRef> refs = test::drainPendingView(
        makeQueue({}), makeQueue({}), 0, 0, /*limit=*/100);
    EXPECT_TRUE(refs.empty());
}

TEST(BatchPendingViewTest, ZeroLimitYieldsEmpty) {
    const std::vector<test::PendingIndexRef> refs = test::drainPendingView(
        makeQueue({5}), makeQueue({0}), 6, 1, /*limit=*/0);
    EXPECT_TRUE(refs.empty());
}
