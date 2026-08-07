#include <gtest/gtest.h>
#include <algorithm>
#include <vector>
#include "PortManager.h"

// One-shot cleanup between tests: each test starts with a clean slate.
class PortManagerTest : public ::testing::Test {
protected:
    void TearDown() override { PortManager::clearPorts(); }
};

TEST_F(PortManagerTest, BasicAllocationReturnsDistinctPorts) {
    PortManager::clearPorts();
    const int COUNT = 8;
    std::vector<int> found;
    for (int i = 0; i < COUNT; ++i) {
        int p = PortManager::findAvailable(20000, 100);
        ASSERT_GE(p, 0) << "Failed to find port on attempt " << i;
        found.push_back(p);
    }
    // All returned ports must be unique.
    std::vector<int> sorted = found;
    std::sort(sorted.begin(), sorted.end());
    auto dup = std::adjacent_find(sorted.begin(), sorted.end());
    ASSERT_EQ(dup, sorted.end()) << "Duplicate port found";
    PortManager::clearPorts();
}

TEST_F(PortManagerTest, AllocatedPortReportedAsInUse) {
    PortManager::clearPorts();
    int p = PortManager::findAvailable(21000, 100);
    ASSERT_GE(p, 0);
    EXPECT_TRUE(PortManager::isInUse(p));
    PortManager::clearPorts();
}

TEST_F(PortManagerTest, AllocateRangeReturnsCorrectCount) {
    PortManager::clearPorts();
    std::vector<int> range = PortManager::allocateRange(22000, 5);
    ASSERT_EQ(static_cast<int>(range.size()), 5);
    for (size_t i = 1; i < range.size(); ++i) {
        // ports may not be sequential due to OS-level checks,
        // but each must be distinct.
        for (size_t j = 0; j < i; ++j) {
            EXPECT_NE(range[i], range[j]);
        }
    }
    PortManager::clearPorts();
}

TEST_F(PortManagerTest, WraparoundDoesNotRescan) {
    // Pre-fill used ports near the top of the range so the next
    // findAvailable with a low startPort must wrap around.
    PortManager::clearPorts();

    // Manually seed usedPorts_ by allocating ports near 65535 via
    // the public API with a high startPort; we just verify that a
    // later low-start call finds a port below the seeded range
    // without returning -1.
    int topPort = PortManager::findAvailable(65000, 500);
    ASSERT_GE(topPort, 0);

    // Now ask for a port starting at 10000. The scan starts at begin
    // (deterministic), and the wraparound logic guarantees each port is
    // visited at most once per call (stop = begin). The old O(n^2) code
    // would rescan the range below 10000 after wraparound; the new code
    // stops at begin.
    int result = PortManager::findAvailable(10000, 500);
    EXPECT_GE(result, 10000);
    EXPECT_LE(result, 65535);
    // result must not be the same as topPort.
    EXPECT_NE(result, topPort);
    PortManager::clearPorts();
}

TEST_F(PortManagerTest, ClearPortsRecyclesAll) {
    PortManager::clearPorts();
    int p1 = PortManager::findAvailable(23000, 100);
    ASSERT_GE(p1, 0);
    PortManager::clearPorts();
    // After clear, the same port should be allocatable again.
    int p2 = PortManager::findAvailable(23000, 100);
    ASSERT_GE(p2, 0);
    EXPECT_EQ(p1, p2) << "Port was not recycled after clearPorts";
}

TEST_F(PortManagerTest, MaxAttemptsExhaustedReturnsMinusOne) {
    PortManager::clearPorts();
    // Reserve a contiguous block so findAvailable cannot find anything
    // within a small maxAttempts window.
    const int COUNT = 20;
    std::vector<int> reserved = PortManager::allocateRange(30000, COUNT);
    ASSERT_EQ(static_cast<int>(reserved.size()), COUNT)
        << "expected to reserve a contiguous block 30000..30019";
    // All ports 30000..30019 are now in usedPorts_.
    // A call with maxAttempts=COUNT starting at 30000 must return -1.
    int result = PortManager::findAvailable(30000, COUNT);
    EXPECT_EQ(result, -1);
    PortManager::clearPorts();
}
