// CMake target (add to CMakeLists.txt):
// add_executable(test_result_queue tests/test_result_queue_test.cpp
//     src/Logger.cpp src/LoggerInstance.cpp)
// target_include_directories(test_result_queue PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
// target_link_libraries(test_result_queue PRIVATE gtest_main gtest)
// set_target_properties(test_result_queue PROPERTIES
//     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
//     RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
//     RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
// )
// add_test(NAME ResultQueueTest COMMAND test_result_queue)

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <stdexcept>
#include "TestResultQueue.h"

namespace {

// Helper to collect flushed batches
struct FlushCollector {
    std::vector<TestResultQueue::ResultTuple> allFlushed;
    std::atomic<int> callCount{0};
    int failUntilCall{0};  // fail callback this many times before succeeding
    bool throwOnCall{false};

    bool operator()(const std::vector<TestResultQueue::ResultTuple>& batch) {
        callCount++;
        if (throwOnCall) {
            throw std::runtime_error("simulated callback exception");
        }
        if (failUntilCall > 0) {
            --failUntilCall;
            return false;
        }
        allFlushed.insert(allFlushed.end(), batch.begin(), batch.end());
        return true;
    }
};

} // anonymous namespace

// E1: callback returns false first, then success — data must be retained across retries
TEST(TestResultQueueRetryTest, CallbackFalseThenSuccessRetainsData) {
    FlushCollector collector;
    collector.failUntilCall = 1;  // fail first call, succeed on retry

    TestResultQueue queue([&collector](const std::vector<TestResultQueue::ResultTuple>& batch) {
        return collector(batch);
    });
    queue.start();

    queue.enqueue("id1", 100, true, "ok");
    queue.enqueue("id2", 200, false, "fail");

    // Flush via the interval trigger: wait for the batch to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(6000));

    queue.stop();

    EXPECT_EQ(collector.callCount, 2);
    ASSERT_EQ(collector.allFlushed.size(), 2u);
    EXPECT_EQ(std::get<0>(collector.allFlushed[0]), "id1");
    EXPECT_EQ(std::get<0>(collector.allFlushed[1]), "id2");
}

// E2: callback throws exception — retry must survive, not crash the queue
TEST(TestResultQueueRetryTest, CallbackThrowSurvives) {
    FlushCollector collector;
    collector.throwOnCall = true;  // always throws

    TestResultQueue queue([&collector](const std::vector<TestResultQueue::ResultTuple>& batch) {
        return collector(batch);
    });
    queue.start();

    queue.enqueue("id3", 50, true, "ok");

    // Flush via the interval trigger
    std::this_thread::sleep_for(std::chrono::milliseconds(6000));

    // Queue should still be alive, stop should not throw
    EXPECT_NO_THROW(queue.stop());

    // Data must NOT have been persisted (callback never succeeded)
    EXPECT_EQ(collector.allFlushed.size(), 0u);
    // But callback was called MAX_FLUSH_RETRIES + 1 times (initial + 3 retries)
    EXPECT_EQ(collector.callCount, 4);
}

// E3: successful callback on first try
TEST(TestResultQueueRetryTest, ImmediateSuccess) {
    FlushCollector collector;

    TestResultQueue queue([&collector](const std::vector<TestResultQueue::ResultTuple>& batch) {
        return collector(batch);
    });
    queue.start();

    queue.enqueue("id4", 300, true, "ok");

    std::this_thread::sleep_for(std::chrono::milliseconds(6000));
    queue.stop();

    EXPECT_EQ(collector.callCount, 1);
    ASSERT_EQ(collector.allFlushed.size(), 1u);
    EXPECT_EQ(std::get<0>(collector.allFlushed[0]), "id4");
}

// E4: stop() drains remaining items via flushBatch
TEST(TestResultQueueRetryTest, StopDrainsRemaining) {
    FlushCollector collector;

    TestResultQueue queue([&collector](const std::vector<TestResultQueue::ResultTuple>& batch) {
        return collector(batch);
    });
    queue.start();

    queue.enqueue("id5", 400, true, "ok");
    queue.enqueue("id6", 500, false, "timeout");

    // Stop immediately without waiting for the interval flush
    queue.stop();

    ASSERT_EQ(collector.allFlushed.size(), 2u);
    EXPECT_EQ(std::get<0>(collector.allFlushed[0]), "id5");
    EXPECT_EQ(std::get<0>(collector.allFlushed[1]), "id6");
}

// E5: concurrent enqueue from multiple threads
TEST(TestResultQueueRetryTest, ConcurrentEnqueue) {
    FlushCollector collector;

    TestResultQueue queue([&collector](const std::vector<TestResultQueue::ResultTuple>& batch) {
        return collector(batch);
    });
    queue.start();

    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 0; i < 10; ++i) {
                queue.enqueue("t" + std::to_string(t) + "_p" + std::to_string(i),
                              100 * (t + 1), true, "ok");
            }
        });
    }
    for (auto& w : workers) {
        w.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(6000));
    queue.stop();

    EXPECT_EQ(collector.allFlushed.size(), 40u);
}
