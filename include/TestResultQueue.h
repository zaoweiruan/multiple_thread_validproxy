#ifndef TEST_RESULT_QUEUE_H
#define TEST_RESULT_QUEUE_H

#include <string>
#include <vector>
#include <tuple>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <atomic>

/// Thread-safe queue that batches test results and flushes via callback.
/// Designed for the ProxyBatchTester flush thread pattern:
/// workers enqueue() → flush thread consumes in batches → callback writes to DB.
class TestResultQueue {
public:
    using ResultTuple = std::tuple<std::string, long, bool, std::string>;  // indexid, latencyMs, success, curlMsg
    using FlushCallback = std::function<bool(const std::vector<ResultTuple>&)>;

    static constexpr int BATCH_SIZE = 50;
    static constexpr int FLUSH_INTERVAL_MS = 5000;

    explicit TestResultQueue(FlushCallback callback)
        : callback_(std::move(callback)), running_(false) {}

    ~TestResultQueue() {
        stop();
    }

    // Non-copyable, non-movable (owns a thread)
    TestResultQueue(const TestResultQueue&) = delete;
    TestResultQueue& operator=(const TestResultQueue&) = delete;
    TestResultQueue(TestResultQueue&&) = delete;
    TestResultQueue& operator=(TestResultQueue&&) = delete;

    /// Start the background flush thread. Must be called before enqueue().
    void start() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) return;
        running_ = true;
        flushThread_ = std::thread(&TestResultQueue::flushLoop, this);
    }

    /// Stop the flush thread. Drains remaining items. Blocks until done.
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) return;
            running_ = false;
        }
        cv_.notify_one();
        if (flushThread_.joinable()) {
            flushThread_.join();
        }
        // Final drain after thread stops
        flushBatch();
    }

    /// Enqueue a single test result. Thread-safe, can be called from any worker.
    void enqueue(const std::string& indexid, long latencyMs, bool success, const std::string& curlMsg) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            buffer_.emplace_back(indexid, latencyMs, success, curlMsg);
        }
        cv_.notify_one();
    }

    /// Number of pending results (approximate, under lock).
    std::size_t pendingCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.size();
    }

private:
    void flushLoop() {
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(FLUSH_INTERVAL_MS), [this]() {
                return !running_ || static_cast<int>(buffer_.size()) >= BATCH_SIZE;
            });

            // Extract current buffer under lock
            std::vector<ResultTuple> batch;
            batch.swap(buffer_);
            lock.unlock();

            if (!batch.empty()) {
                if (callback_) {
                    callback_(batch);
                }
            }

            if (!running_) {
                // Drain any remaining items after stop signal
                lock.lock();
                batch.swap(buffer_);
                lock.unlock();
                if (!batch.empty() && callback_) {
                    callback_(batch);
                }
                return;
            }
        }
    }

    void flushBatch() {
        std::vector<ResultTuple> batch;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!buffer_.empty() && callback_) {
                batch.swap(buffer_);
            }
        }
        if (!batch.empty() && callback_) {
            callback_(batch);
        }
    }

    FlushCallback callback_;
    std::vector<ResultTuple> buffer_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread flushThread_;
    std::atomic<bool> running_;
};

#endif // TEST_RESULT_QUEUE_H
