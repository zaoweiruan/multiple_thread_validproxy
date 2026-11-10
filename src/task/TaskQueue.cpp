#include "task/TaskQueue.h"

#include "Logger.h"
#include "task/TaskType.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace task {

namespace {

std::string generateId() {
    static std::atomic<unsigned long long> counter{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return "task-" + std::to_string(now) + "-" + std::to_string(counter.fetch_add(1));
}

} // namespace

TaskQueue::TaskQueue() {
    // Publish "worker alive" BEFORE the thread starts: if the destructor runs
    // before workerLoop's first iteration, a worker-side `running_.store(true)`
    // would resurrect the flag and the worker would wait on the cv forever
    // (destructor's join() then hangs the process).
    running_.store(true);
    worker_ = std::thread(&TaskQueue::workerLoop, this);
}

TaskQueue::~TaskQueue() {
    // Dequeue-and-stop BEFORE anything else: the worker must never execute a
    // queued task after the owner (AppController) has freed db_/XrayManager.
    // notify=false — UI handlers may already be destroyed at destructor time.
    stop(false);
}

void TaskQueue::stop(bool notify) {
    // 1) Cooperative cancel of the RUNNING task (same semantics as before).
    cancelCurrent();

    // 2) Drain the backlog FIRST (archived as CANCELLED), then publish
    //    running_=false. Order matters: workerLoop only breaks on
    //    (!running_ && queue_.empty()); setting running_ first would let the
    //    worker pick up and EXECUTE the backlog after the caller has already
    //    released shared resources (review critical, spec 2026-11-06 §2.6).
    std::vector<std::pair<TaskHandle, TaskCallbacks>> dropped;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            Entry e = queue_.top();
            queue_.pop();
            e.handle.status = TaskStatus::CANCELLED;
            e.handle.completedAt = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
            history_.push_back(e.handle);
            if (history_.size() > historyLimit_) history_.erase(history_.begin());
            dropped.emplace_back(std::move(e.handle), std::move(e.callbacks));
        }
        running_.store(false);
        busy_.store(false);
    }
    cv_.notify_all();

    // 3) Callbacks out of the lock (UI may re-enter snapshot(); non-recursive
    //    mutex_ would deadlock). Suppressed in destructor paths.
    if (notify) {
        for (std::pair<TaskHandle, TaskCallbacks>& item : dropped) {
            if (item.second.onComplete) {
                try { item.second.onComplete(item.first); } catch (...) {}
            }
        }
    }

    // 4) The worker now unwinds the current task (cancel flag set in 1) and
    //    exits on the empty-queue break, so this join is bounded by the
    //    current task's own cancellation latency.
    if (worker_.joinable()) worker_.join();
}

std::size_t TaskQueue::clearHistory() {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t n = history_.size();
    history_.clear();
    return n;
}

bool TaskQueue::removeFromHistory(const std::string& taskId) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t before = history_.size();
    history_.erase(std::remove_if(history_.begin(), history_.end(),
                                  [&taskId](const TaskHandle& h) { return h.taskId == taskId; }),
                   history_.end());
    return history_.size() != before;
}

TaskHandle TaskQueue::submit(TaskType type, TaskFunc func, int priority, TaskCallbacks callbacks,
                            const std::string& targetId) {
    Entry entry;
    entry.handle.taskId = generateId();
    entry.handle.type = type;
    entry.handle.status = TaskStatus::QUEUED;
    entry.handle.priority = priority;
    entry.handle.targetId = targetId;
    entry.handle.createdAt = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    entry.func = std::move(func);
    entry.callbacks = std::move(callbacks);
    entry.priority = priority;
    entry.createdAt = std::chrono::steady_clock::now();

    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    // store cancelFlag in handle? We'll keep in Entry via lambda capture? Need access in worker.
    // We'll store as shared_ptr in a parallel map? Simpler: put cancelFlag into Entry and pass raw to context.
    // But Entry is moved into queue; we need retrieve. We'll add field in Entry.
    // (Entry already has handle; add cancelFlag field dynamically? Add to struct in header.)
    // For now, store in a local and capture by value in wrapper.

    TaskHandle handle = entry.handle;

    // Unified queue trace: every submitted task logs here, so tasks whose
    // caller passes no onQueued callback are still visible in the log
    // (bugfix 2026-11-03 §3.2 — "no Task queued line" made queued work look
    // like it was never managed).
    Logger::write(std::string("TaskQueue: queued ") + taskTypeToString(type) +
                      " id=" + handle.taskId,
                  LogLevel::INFO);

    if (entry.callbacks.onQueued) {
        entry.callbacks.onQueued(handle);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        // wrap func to inject cancel flag
        auto flag = cancelFlag;
        TaskFunc original = entry.func;
        entry.func = [original, flag](TaskContext& ctx) {
            ctx.cancelFlag = flag.get();
            original(ctx);
        };
        queue_.push(std::move(entry));
    }
    cv_.notify_one();
    return handle;
}

void TaskQueue::cancel(const std::string& taskId) {
    // Collect under the lock, invoke callbacks AFTER releasing it: onComplete is
    // UI code and may re-enter the queue (snapshot()/queueDepth()); calling it
    // under the non-recursive mutex_ would deadlock the UI thread.
    std::vector<std::pair<TaskHandle, TaskCallbacks>> cancelled;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // If current task, set cancel flag.
        if (current_.taskId == taskId) {
            cancelRequested_.store(true);
            if (currentCancelFlag_) currentCancelFlag_->store(true);
            return;
        }
        // Remove from queue if present (rebuild queue without it).
        std::priority_queue<Entry, std::vector<Entry>, EntryCompare> newQueue;
        while (!queue_.empty()) {
            Entry e = queue_.top();
            queue_.pop();
            if (e.handle.taskId == taskId) {
                e.handle.status = TaskStatus::CANCELLED;
                e.handle.completedAt = std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count());
                history_.push_back(e.handle);
                if (history_.size() > historyLimit_) history_.erase(history_.begin());
                cancelled.emplace_back(e.handle, e.callbacks);
            } else {
                newQueue.push(std::move(e));
            }
        }
        queue_ = std::move(newQueue);
    }
    for (const std::pair<TaskHandle, TaskCallbacks>& item : cancelled) {
        if (item.second.onComplete) {
            try { item.second.onComplete(item.first); } catch (...) {}
        }
    }
}

void TaskQueue::cancelAllPending() {
    std::vector<std::pair<TaskHandle, TaskCallbacks>> cancelled;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            Entry e = queue_.top();
            queue_.pop();
            e.handle.status = TaskStatus::CANCELLED;
            e.handle.completedAt = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
            history_.push_back(e.handle);
            if (history_.size() > historyLimit_) history_.erase(history_.begin());
            cancelled.emplace_back(e.handle, e.callbacks);
        }
    }
    for (const std::pair<TaskHandle, TaskCallbacks>& item : cancelled) {
        if (item.second.onComplete) {
            try { item.second.onComplete(item.first); } catch (...) {}
        }
    }
}

void TaskQueue::cancelCurrent() {
    cancelRequested_.store(true);
    if (currentCancelFlag_) currentCancelFlag_->store(true);
}

void TaskQueue::forceRelease() {
    busy_.store(false);
    cancelRequested_.store(false);
    if (currentCancelFlag_) currentCancelFlag_->store(false);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!current_.taskId.empty()) {
            if (current_.status != TaskStatus::COMPLETED && current_.status != TaskStatus::FAILED) {
                current_.status = TaskStatus::CANCELLED;
                archiveFinishedLocked(current_);
            }
            // Same visibility rule as the worker's normal finish path.
            recentParent_ = current_;
            recentChildren_ = children_;
            current_ = TaskHandle{};
            children_.clear();
        }
    }
}

std::size_t TaskQueue::queueDepth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

TaskHandle TaskQueue::currentTask() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_;
}

std::vector<TaskHandle> TaskQueue::history(std::size_t n) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskHandle> out;
    std::size_t start = history_.size() > n ? history_.size() - n : 0;
    for (std::size_t i = start; i < history_.size(); ++i) out.push_back(history_[i]);
    return out;
}

void TaskQueue::setChildTasks(std::vector<TaskHandle> children) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Bound to the current container task; a container that already finished
    // (or never started) must not collect rows.
    if (current_.taskId.empty()) return;
    for (TaskHandle& child : children) {
        child.parentTaskId = current_.taskId;
        // The breakdown is published as part of the parent's lifecycle, so a
        // fresh child inherits the parent's start stamp for the 提交于 column
        // (children rebuilt per transition keep it stable through current_).
        if (child.createdAt.empty()) child.createdAt = current_.startedAt;
    }
    int completed = 0, failed = 0;
    for (const TaskHandle& child : children) {
        if (child.status == TaskStatus::COMPLETED) ++completed;
        else if (child.status == TaskStatus::FAILED || child.status == TaskStatus::CANCELLED) ++failed;
    }
    current_.childTotal = static_cast<int>(children.size());
    current_.childCompleted = completed;
    current_.childFailed = failed;
    children_ = std::move(children);
}

void TaskQueue::clearChildTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    children_.clear();
    current_.childTotal = 0;
    current_.childCompleted = 0;
    current_.childFailed = 0;
}

void TaskQueue::setCurrentProgress(int current, int total, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_.taskId.empty()) return;   // idle: nothing to report on
    current_.progressCurrent = current;
    current_.progressTotal = total;
    current_.progressMessage = message;
}

bool TaskQueue::hasPending(TaskType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // priority_queue has no iteration; copy to a temp and drain.
    std::priority_queue<Entry, std::vector<Entry>, EntryCompare> tmp = queue_;
    while (!tmp.empty()) {
        if (tmp.top().handle.type == type) return true;
        tmp.pop();
    }
    return false;
}

std::vector<TaskHandle> TaskQueue::pendingTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TaskHandle> out;
    std::priority_queue<Entry, std::vector<Entry>, EntryCompare> tmp = queue_;
    while (!tmp.empty()) {
        out.push_back(tmp.top().handle);
        tmp.pop();
    }
    return out;
}

TaskQueueSnapshot TaskQueue::snapshot() const {
    TaskQueueSnapshot snap;
    std::lock_guard<std::mutex> lock(mutex_);
    snap.current = current_;
    snap.children = children_;
    if (current_.taskId.empty()) {
        // Idle: surface the last finished task's breakdown until the next run.
        snap.recentParent = recentParent_;
        snap.recentChildren = recentChildren_;
    }
    snap.queueDepth = queue_.size();
    // Finished ledger for the monitoring panel, newest first (spec §3.1).
    snap.history.assign(history_.rbegin(), history_.rend());
    // std::priority_queue's underlying container `c` is protected and NOT
    // accessible from a non-derived class member; copy the queue and drain it.
    // Drain order == pop order == the real execution order (priority desc, then
    // createdAt asc per EntryCompare), so no std::sort is needed and the
    // TaskHandle::createdAt string never gets compared lexicographically.
    std::priority_queue<Entry, std::vector<Entry>, EntryCompare> tmp = queue_;
    snap.pending.reserve(tmp.size());
    while (!tmp.empty()) {
        snap.pending.push_back(tmp.top().handle);
        tmp.pop();
    }
    snap.busy = busy_.load(std::memory_order_relaxed);
    snap.workerAlive = running_.load(std::memory_order_relaxed);
    return snap;
}

void TaskQueue::archiveFinishedLocked(const TaskHandle& handle) {
    for (TaskHandle& existing : history_) {
        if (existing.taskId == handle.taskId) {
            existing = handle;   // forceRelease-archived row refreshed in place
            return;
        }
    }
    history_.push_back(handle);
    if (history_.size() > historyLimit_) history_.erase(history_.begin());
}

void TaskQueue::workerLoop() {
    // NOTE: running_ is published by the constructor BEFORE this thread starts.
    // Storing it here would resurrect the flag if ~TaskQueue already observed
    // "worker alive" and asked the loop to stop (dtor then joins forever).
    while (true) {
        Entry entry;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || !running_.load(); });
            if (!running_.load() && queue_.empty()) break;
            if (queue_.empty()) continue;
            entry = std::move(const_cast<Entry&>(queue_.top()));
            queue_.pop();
            busy_.store(true);
        }

        // reset cancel flag for this task
        cancelRequested_.store(false);
        // We need cancel flag pointer; store in currentCancelFlag_? But worker single-threaded.
        // We'll create a local atomic and point currentCancelFlag_ to it.
        std::atomic<bool> localCancel{false};
        currentCancelFlag_ = &localCancel;

        entry.handle.status = TaskStatus::RUNNING;
        entry.handle.startedAt = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());

        {
            std::lock_guard<std::mutex> lock(mutex_);
            current_ = entry.handle;
            // Children belong to the previous container (if any) — drop them
            // so the panel never shows stale sub-rows under a new task. The
            // previous run's outcome moves to recent* (see snapshot fields)
            // only when the next task STARTS, so the finished rows stay on
            // screen while the queue is idle.
            children_.clear();
            recentParent_ = TaskHandle{};
            recentChildren_.clear();
        }

        TaskContext ctx;
        ctx.cancelFlag = &localCancel;
        ctx.reportProgress = [this, &entry](int cur, int total, const std::string& msg) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                current_.progressCurrent = cur;
                current_.progressTotal = total;
                current_.progressMessage = msg;
            }
            if (entry.callbacks.onProgress) entry.callbacks.onProgress(current_);
        };

        bool cancelled = false;
        bool failed = false;
        std::string err;
        try {
            entry.func(ctx);
            if (localCancel.load() || cancelRequested_.load()) cancelled = true;
        } catch (const std::exception& e) {
            failed = true;
            err = e.what();
        } catch (...) {
            // Safety net: a non-std exception (or one thrown while another
            // exception is unwinding) must never reach std::terminate and kill
            // the whole process. Mark the task failed so the worker loop and
            // UI guards stay consistent.
            failed = true;
            err = "unknown non-std exception";
        }

        entry.handle.status = cancelled ? TaskStatus::CANCELLED :
                              (failed ? TaskStatus::FAILED : TaskStatus::COMPLETED);
        entry.handle.completedAt = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        entry.handle.error = err;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Keep the finished parent + its breakdown visible (snapshot().
            // recentParent/recentChildren) until the NEXT task starts, so a
            // short task does not disappear before the user looks over.
            recentParent_ = entry.handle;
            recentChildren_ = children_;
            current_ = TaskHandle{};
            children_.clear();   // container children die with their parent
            archiveFinishedLocked(entry.handle);
        }

        if (entry.callbacks.onComplete) entry.callbacks.onComplete(entry.handle);

        busy_.store(false);
        currentCancelFlag_ = nullptr;
    }
    running_.store(false);
}

} // namespace task