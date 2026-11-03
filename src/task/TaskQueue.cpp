#include "task/TaskQueue.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace task {

namespace {

std::string generateId() {
    static std::atomic<unsigned long long> counter{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return "task-" + std::to_string(now) + "-" + std::to_string(counter.fetch_add(1));
}

} // namespace

TaskQueue::TaskQueue() {
    worker_ = std::thread(&TaskQueue::workerLoop, this);
}

TaskQueue::~TaskQueue() {
    cancelCurrent();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_.store(false);
        busy_.store(false);
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

TaskHandle TaskQueue::submit(TaskType type, TaskFunc func, int priority, TaskCallbacks callbacks) {
    Entry entry;
    entry.handle.taskId = generateId();
    entry.handle.type = type;
    entry.handle.status = TaskStatus::QUEUED;
    entry.handle.priority = priority;
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
    std::lock_guard<std::mutex> lock(mutex_);
    // If current task, set cancel flag.
    if (current_.taskId == taskId) {
        cancelRequested_.store(true);
        if (currentCancelFlag_) currentCancelFlag_->store(true);
        return;
    }
    // Remove from queue if present (rebuild queue without it).
    std::priority_queue<Entry, std::vector<Entry>, EntryCompare> newQueue;
    bool found = false;
    while (!queue_.empty()) {
        auto e = queue_.top();
        queue_.pop();
        if (e.handle.taskId == taskId) {
            found = true;
            e.handle.status = TaskStatus::CANCELLED;
            history_.push_back(e.handle);
            if (e.callbacks.onComplete) e.callbacks.onComplete(e.handle);
        } else {
            newQueue.push(std::move(e));
        }
    }
    queue_ = std::move(newQueue);
    (void)found;
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
                history_.push_back(current_);
                if (history_.size() > historyLimit_) history_.erase(history_.begin());
            }
            current_ = TaskHandle{};
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

void TaskQueue::workerLoop() {
    running_.store(true);
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
            current_ = TaskHandle{};
            history_.push_back(entry.handle);
            if (history_.size() > historyLimit_) history_.erase(history_.begin());
        }

        if (entry.callbacks.onComplete) entry.callbacks.onComplete(entry.handle);

        busy_.store(false);
        currentCancelFlag_ = nullptr;
    }
    running_.store(false);
}

} // namespace task