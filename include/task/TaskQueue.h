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

// Point-in-time view of the queue for the monitoring UI.
struct TaskQueueSnapshot {
    TaskHandle current;                 // current task; taskId empty = none running
    std::vector<TaskHandle> pending;    // queued tasks, in actual dequeue order
                                        // (priority desc -> createdAt asc)
    // Monitoring rows for the current container task (auto-task steps).
    // Never executed by the worker; valid only while current is the container.
    std::vector<TaskHandle> children;
    // Last FINISHED task kept visible after completion/cancellation so the
    // user can see the outcome (esp. the breakdown) instead of the rows
    // vanishing the moment the task ends (owner feedback 2026-11-05:
    // "批量更新队列没有实现" — a 6s UPDATE_ALL left nothing on screen).
    // Valid only while NO task is running; replaced on every task end.
    TaskHandle recentParent;
    std::vector<TaskHandle> recentChildren;
    // Terminal (COMPLETED/FAILED/CANCELLED) task records, newest first,
    // capped at historyLimit_. Finished rows STAY in the monitoring list
    // until the user clears them via clearHistory()/removeFromHistory()
    // (spec 2026-11-06 §3: 完成任务不清除只标识状态 + 右键清理).
    std::vector<TaskHandle> history;
    std::size_t queueDepth = 0;
    bool busy = false;                  // a task is executing (the UI's busy gate)
    bool workerAlive = false;           // worker thread alive (debug only; true once
                                        // the loop started, NOT "a task is running")
};

class TaskQueue {
public:
    TaskQueue();
    ~TaskQueue();

    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    // Submit a task. Returns handle (status may be REJECTED if mutex policy rejects).
    TaskHandle submit(TaskType type, TaskFunc func, int priority = 1,
                      TaskCallbacks callbacks = {},
                      const std::string& targetId = "");

    void cancel(const std::string& taskId);
    void cancelCurrent();
    // Cancel every QUEUED task (current task untouched). Callbacks run after the
    // lock is released, once per removed task (status=CANCELLED via onComplete).
    void cancelAllPending();

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

    // Drop every QUEUED task (archived as CANCELLED) BEFORE publishing
    // running_=false, then join the worker. Clearing queue_ first is the whole
    // point: workerLoop breaks only on (!running_ && queue_.empty()), so
    // without the clear the worker would keep EXECUTING queued tasks after the
    // owner already released db_/XrayManager (spec 2026-11-06 §2.6, review
    // critical: shutdown with a backlog executed tasks on freed resources).
    // onComplete of the dropped tasks fires only when notify is true (runtime
    // cancel path); destructors pass false — UI handlers may already be gone
    // (spec §7 row 2).
    void stop(bool notify = true);

    // UI-facing finished-list management (spec 2026-11-06 §3.1). history_ is
    // the single ledger serving both history(n) diagnostics and snapshot().history;
    // clearHistory empties it wholesale (returns cleared count), removeFromHistory
    // drops one record by taskId (returns whether it existed).
    std::size_t clearHistory();
    bool removeFromHistory(const std::string& taskId);

    // Aggregate read-only view for the monitoring panel. Takes mutex_ once; must
    // NOT call the other locking getters (non-recursive mutex -> self-deadlock).
    TaskQueueSnapshot snapshot() const;

    // Phase A (ProxyTestUnified): inspect pending (queued, not yet running)
    // tasks so callers can decide whether to inject into a live pool or create
    // a new one. hasPending(type) returns true if any queued task has the given
    // type (e.g. TEST_SINGLE_PROXY or TEST_ALL).
    bool hasPending(TaskType type) const;
    std::vector<TaskHandle> pendingTasks() const;

    bool isRunning() const { return running_.load(); }
    bool isBusy() const { return busy_.load(); }

    // Publish coarse progress of the RUNNING task (called from the task body's
    // worker thread). The monitoring panel polls snapshot().current and the
    // batch-queue gauge reuses these counters for update tasks (owner
    // 2026-11-06: 更新订阅只显示整体进度，不拆解订阅名). No-op when idle.
    void setCurrentProgress(int current, int total, const std::string& message);

    // ── container/subtask view (Scheme A, auto-task step breakdown) ──
    // Children are MONITORING rows only: they never enter queue_, so the
    // worker never executes them and hasPending()/pendingTasks() (the
    // ProxyTestUnified injection gates) stay untouched. snapshot() returns
    // them in a dedicated field; the panel renders them under the running
    // parent. setChildTasks is a full replace (called by AppController on
    // every auto-task step transition) and refreshes the parent's
    // childTotal/childCompleted/childFailed counters from the list.
    void setChildTasks(std::vector<TaskHandle> children);
    // Drop the current parent's children (auto-task finished/cancelled).
    void clearChildTasks();

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
    // Append a terminal handle to history_ WITHOUT duplicating: forceRelease()
    // archives the current task as CANCELLED while the worker is still winding
    // down, and the worker's normal finish path then archives the same taskId
    // again — with the finished list now visible in the UI (spec 2026-11-06
    // §3.2) a second row per task would show. Re-archives REPLACE in place.
    // Caller must hold mutex_.
    void archiveFinishedLocked(const TaskHandle& handle);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::priority_queue<Entry, std::vector<Entry>, EntryCompare> queue_;
    std::thread worker_;

    std::atomic<bool> running_{false};
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool>* currentCancelFlag_ = nullptr;

    TaskHandle current_;
    // Monitoring rows bound to current_ (auto-task steps); never queued,
    // cleared whenever current_ changes (workerLoop / forceRelease).
    std::vector<TaskHandle> children_;
    // Last FINISHED task + its breakdown, shown (snapshot recentParent/
    // recentChildren) while the queue is idle until the next task starts.
    TaskHandle recentParent_;
    std::vector<TaskHandle> recentChildren_;
    std::vector<TaskHandle> history_;
    std::size_t historyLimit_ = 100;

    std::function<bool()> probeRunningQuery_;
};

} // namespace task