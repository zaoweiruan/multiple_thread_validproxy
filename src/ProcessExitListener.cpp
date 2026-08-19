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
    HeartbeatFn heartbeatFn, int heartbeatIntervalMs) {
    std::lock_guard<std::mutex> lock(mutex_);
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
    w->threadStartTime = std::chrono::steady_clock::now();
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
        for (;;) {
            DWORD waitResult = WaitForSingleObject(w->processHandle, intervalMs);
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
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = watchers_.find(key);
    if (it == watchers_.end()) { return; }
    it->second->running.store(false);
    if (it->second->thread.joinable()) { it->second->thread.join(); }
    watchers_.erase(it);
}

void ProcessExitListener::shutdown() {
    shutdownRequested_.store(true);
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& kv : watchers_) {
        kv.second->running.store(false);
        if (kv.second->thread.joinable()) { kv.second->thread.detach(); }
    }
    watchers_.clear();
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
