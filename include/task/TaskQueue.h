#pragma once

#include "TaskType.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace task {

struct TaskContext {
    std::atomic<bool>* cancelFlag = nullptr;
    std::function<void(int, int, const std::string&)> reportProgress;

    bool isCancelled() const { return cancelFlag && cancelFlag->load(); }
    void progress(int cur, int total, const std::string& msg) {
        if (reportProgress) reportProgress(cur, total, msg);
    }
};

using TaskFunc = std::function<void(TaskContext&)>;
struct TaskCallbacks {
    std::function<void(const TaskHandle&)> onQueued;
    std::function<void(const TaskHandle&)> onProgress;
    std::function<void(const TaskHandle&)> onComplete;
    std::function<void(const TaskHandle&)> onRejected;
};

class TaskQueue {
public:
    TaskQueue();
    ~TaskQueue();

    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    // Submit a task. Returns handle (status may be REJECTED if mutex policy rejects).
    TaskHandle submit(TaskType type, TaskFunc func, int priority = 1,
                      TaskCallbacks callbacks = {});

    void cancel(const std::string& taskId);
    void cancelCurrent();

    // Force-release the busy flag even if the worker thread is stuck inside a
    // task lambda. Call after cancelCurrent() when the worker cannot be
    // interrupted (e.g. a batch test blocked in a non-cooperative teardown).
    // NOTE: the stuck worker thread may still be alive holding locks/resources;
    // this only restores the isBusy()/isRunning() UI guards, it does NOT
    // reclaim the stuck thread. Prefer to close and reopen the app before
    // starting a new heavy task if the worker never unwinds.
    void forceRelease();

    std::size_t queueDepth() const;
    TaskHandle currentTask() const;
    std::vector<TaskHandle> history(std::size_t n) const;

    // Phase A (ProxyTestUnified): inspect pending (queued, not yet running)
    // tasks so callers can decide whether to inject into a live pool or create
    // a new one. hasPending(type) returns true if any queued task has the given
    // type (e.g. TEST_SINGLE_PROXY or TEST_ALL).
    bool hasPending(TaskType type) const;
    std::vector<TaskHandle> pendingTasks() const;

    bool isRunning() const { return running_.load(); }
    bool isBusy() const { return busy_.load(); }

    void setProbeRunningQuery(std::function<bool()> q) { probeRunningQuery_ = std::move(q); }

private:
    struct Entry {
        TaskHandle handle;
        TaskFunc func;
        TaskCallbacks callbacks;
        int priority = 1;
        std::chrono::steady_clock::time_point createdAt;
    };

    struct EntryCompare {
        bool operator()(const Entry& a, const Entry& b) const {
            if (a.priority != b.priority) return a.priority < b.priority; // max-heap by priority
            return a.createdAt > b.createdAt; // earlier first
        }
    };

    void workerLoop();
    std::string makeTaskId() const;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::priority_queue<Entry, std::vector<Entry>, EntryCompare> queue_;
    std::thread worker_;

    std::atomic<bool> running_{false};
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool>* currentCancelFlag_ = nullptr;

    TaskHandle current_;
    std::vector<TaskHandle> history_;
    std::size_t historyLimit_ = 100;

    std::function<bool()> probeRunningQuery_;
};

} // namespace task