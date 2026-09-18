#include "ProcessExitListener.h"

#include <chrono>
#include <ctime>
#include <string>
#include <vector>

namespace proc {

ProcessExitListener::ProcessExitListener() = default;
ProcessExitListener::~ProcessExitListener() { shutdown(); }

ProcessExitListener::WatchKey ProcessExitListener::watch(
    HANDLE processHandle, const std::string& indexId, int64_t historyId,
    InsertFn insertFn, AliveFn aliveFn, EnumerateFn enumerateFn,
    FinalizeFn finalizeFn, NotifyFn notifyFn,
    TakeoverFn takeoverFn, const std::string& configFileName,
    HeartbeatFn heartbeatFn, int heartbeatIntervalMs,
    int64_t baselineElapsedMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Defensive: once shutdown has been requested, never start a new watcher
    // thread — it would capture callbacks into a caller that is going away.
    if (shutdownRequested_.load()) { return 0; }
    WatchKey key = nextKey_++;
    auto w = std::make_unique<Watcher>();
    w->processHandle = processHandle;
    w->indexId = indexId;
    w->historyId = historyId;
    w->insertFn = std::move(insertFn);
    w->aliveFn = std::move(aliveFn);
    w->enumerateFn = std::move(enumerateFn);
    w->finalizeFn = std::move(finalizeFn);
    w->notifyFn = std::move(notifyFn);
    w->takeoverFn = std::move(takeoverFn);
    w->configFileName = configFileName;
    w->heartbeatFn = std::move(heartbeatFn);
    if (heartbeatIntervalMs > 0) { w->heartbeatIntervalMs = heartbeatIntervalMs; }
    w->baselineElapsedMs = baselineElapsedMs;
    w->threadStartTime = std::chrono::steady_clock::now();
    // Auto-reset wake event used by shutdown()/unwatch() to break a watcher
    // out of a long WaitForMultipleObjects immediately.
    w->wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    watchers_[key] = std::move(w);
    watchers_[key]->thread = std::thread([this, key]() {
        Watcher* w = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = watchers_.find(key);
            if (it == watchers_.end()) return;
            w = it->second.get();
        }
        const DWORD intervalMs = static_cast<DWORD>(w->heartbeatIntervalMs);
        HANDLE waitHandles[2] = { w->processHandle, w->wakeEvent };
        // Fallback: if the wake event could not be created, wait on the
        // process handle alone so watching still works (just without the
        // instant-shutdown wake path).
        const bool hasWakeEvent = (w->wakeEvent != nullptr);
        for (;;) {
            DWORD waitResult = hasWakeEvent
                ? WaitForMultipleObjects(2, waitHandles, FALSE, intervalMs)
                : WaitForSingleObject(w->processHandle, intervalMs);
            if (waitResult == WAIT_OBJECT_0) {
                DWORD exitCode = 0;
                GetExitCodeProcess(w->processHandle, &exitCode);
                handleProcessExit(w, exitCode);
                break;
            }
            if (!w->running.load()) { break; }
            if (waitResult == WAIT_TIMEOUT) {
                if (w->heartbeatFn) {
                    const auto now = std::chrono::steady_clock::now();
                    const auto elapsedMs =
                        w->baselineElapsedMs +
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - w->threadStartTime).count();
                    w->heartbeatFn(w->indexId, w->historyId, elapsedMs);
                }
                continue;
            }
            // WAIT_FAILED: handle became invalid (closed externally); exit loop.
            break;
        }
    });
    return key;
}

void ProcessExitListener::unwatch(WatchKey key) {
    // Move the whole Watcher (thread + callbacks + handles) out under the
    // lock, then wake + join OUTSIDE the lock. Erasing from the map before
    // the join would free the Watcher while its thread still loops on the
    // raw pointer — the same UAF class this listener was fixed against.
    std::unique_ptr<Watcher> w;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = watchers_.find(key);
        if (it == watchers_.end()) { return; }
        w = std::move(it->second);
        watchers_.erase(it);
    }
    w->running.store(false);
    if (w->wakeEvent) { SetEvent(w->wakeEvent); }
    if (w->thread.joinable()) { w->thread.join(); }
    // w destroyed here, after the join.
}

void ProcessExitListener::shutdown() {
    shutdownRequested_.store(true);
    // Move watchers out under the lock, then join their threads OUTSIDE the
    // lock. The old detach+clear sequence freed the Watcher objects while
    // the detached threads were still looping on them (freed std::function
    // heartbeatFn / running flag / process handle) — the source of the
    // close-time execute-AV crash. Joining under the lock would deadlock
    // with a watcher thread that has not yet passed its initial lock scope.
    std::map<WatchKey, std::unique_ptr<Watcher>> pending;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending = std::move(watchers_);
        watchers_.clear();
        for (auto& kv : pending) {
            kv.second->running.store(false);
        }
    }
    for (auto& kv : pending) {
        kv.second->running.store(false);
        if (kv.second->wakeEvent) { SetEvent(kv.second->wakeEvent); }
    }
    // pending destruction closes each Watcher (handles + callbacks) only
    // after its thread has been joined.
    for (auto& kv : pending) {
        if (kv.second->thread.joinable()) {
            kv.second->thread.join();
        }
    }
}

void ProcessExitListener::handleProcessExit(Watcher* w, DWORD exitCode) {
    if (shutdownRequested_.load()) { return; }
    ProcessExitEvent event;
    event.exitCode = static_cast<int>(exitCode);
    event.shouldFinalize = false;
    const auto allRows = w->enumerateFn();
    const std::string* foundStartedAt = nullptr;
    for (const auto& row : allRows) {
        if (row.first == w->historyId) { foundStartedAt = &row.second; break; }
    }
    const std::string emptyStartedAt;
    const std::string& resolvedStartedAt =
        foundStartedAt ? *foundStartedAt : emptyStartedAt;
    event.shouldFinalize =
        ShouldFinalize(event, w->indexId, w->historyId, allRows, resolvedStartedAt);
    bool takeoverAttempted = false;
    if (event.shouldFinalize && w->takeoverFn && !w->configFileName.empty()) {
        DWORD pid = 0;
        if (w->processHandle) { pid = GetProcessId(w->processHandle); }
        if (pid != 0) {
            takeoverAttempted = w->takeoverFn(
                static_cast<int>(pid), w->indexId, w->historyId, w->configFileName);
        }
    }
    if (w->notifyFn) { w->notifyFn(w->indexId, w->historyId); }
    if (!event.shouldFinalize) { return; }
    if (takeoverAttempted) { return; }
const auto now = std::chrono::steady_clock::now();
    const auto durationMs =
        w->baselineElapsedMs +
        std::chrono::duration_cast<std::chrono::milliseconds>(now - w->threadStartTime).count();
    const auto timeT = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&timeT));
    const std::string endedAtStr(timeBuf);
    if (w->finalizeFn) {
        w->finalizeFn(nullptr, w->indexId, w->historyId, endedAtStr, event.exitCode, durationMs);
    }
}

bool ProcessExitListener::ShouldFinalize(
    const ProcessExitEvent& event, const std::string& indexId,
    int64_t historyId,
    const std::vector<std::pair<int64_t, std::string>>& allRows,
    const std::string& currentStartedAt) {
    (void)indexId; (void)historyId;
    if (event.exitCode == 0) { return true; }
    int count = static_cast<int>(allRows.size());
    if (count != 1) { return false; }
    if (currentStartedAt.empty()) { return false; }
    return true;
}

}  // namespace proc
