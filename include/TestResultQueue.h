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
#include <chrono>
#include "Logger.h"

/// Thread-safe queue that batches test results and flushes via callback.
/// Designed for the ProxyBatchTester flush thread pattern:
/// workers enqueue() → flush thread consumes in batches → callback writes to DB.
class TestResultQueue {
public:
    using ResultTuple = std::tuple<std::string, long, bool, std::string>;  // indexid, latencyMs, success, curlMsg
    using FlushCallback = std::function<bool(const std::vector<ResultTuple>&)>;

    static constexpr int BATCH_SIZE = 50;
    static constexpr int FLUSH_INTERVAL_MS = 5000;
    static constexpr int MAX_FLUSH_RETRIES = 3;
    static constexpr int INITIAL_BACKOFF_MS = 100;

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
    /// Attempt callback with retry + backoff on false/exception.
    /// Retries up to MAX_FLUSH_RETRIES times with exponential backoff (INITIAL_BACKOFF_MS * 2^i).
    /// On exhausted retries, logs ERR with indexid list via Logger::write and drops batch.
    /// Never throws; returns true on success, false on permanent failure.
    bool flushWithRetry(const std::vector<ResultTuple>& batch) {
        std::vector<std::string> indexids;
        indexids.reserve(batch.size());
        for (size_t i = 0; i < batch.size(); ++i) {
            indexids.push_back(std::get<0>(batch[i]));
        }

        int retryCount = 0;
        int backoffMs = INITIAL_BACKOFF_MS;

        while (retryCount <= MAX_FLUSH_RETRIES) {
            bool success = false;
            try {
                if (callback_) {
                    success = callback_(batch);
                } else {
                    success = true;
                }
            } catch (...) {
                success = false;
            }

            if (success) {
                return true;
            }

            ++retryCount;
            if (retryCount > MAX_FLUSH_RETRIES) {
                Logger::write("TestResultQueue: flush failed for " +
                              std::to_string(batch.size()) + " results, indexids: " +
                              joinStrings(indexids, ","), LogLevel::ERR);
                return false;
            }

            // Sleep outside the mutex (buffer already swapped out in callers)
            std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
            backoffMs *= 2;
        }

        return false;
    }

    /// Join vector of strings with a delimiter.
    static std::string joinStrings(const std::vector<std::string>& parts, const std::string& delim) {
        std::string result;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) result += delim;
            result += parts[i];
        }
        return result;
    }

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
                flushWithRetry(batch);
            }

            if (!running_) {
                // Drain any remaining items after stop signal
                lock.lock();
                std::vector<ResultTuple> drainBatch;
                drainBatch.swap(buffer_);
                lock.unlock();
                if (!drainBatch.empty()) {
                    flushWithRetry(drainBatch);
                }
                return;
            }
        }
    }

    void flushBatch() {
        std::vector<ResultTuple> batch;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            batch.swap(buffer_);
        }
        if (!batch.empty()) {
            flushWithRetry(batch);
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
