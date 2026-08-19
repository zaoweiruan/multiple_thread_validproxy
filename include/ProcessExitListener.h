#ifndef PROCESS_EXIT_LISTENER_H
#define PROCESS_EXIT_LISTENER_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

struct sqlite3;

namespace proc {

using FinalizeFn = std::function<bool(sqlite3* db, const std::string& indexId,
                                      int64_t historyId, const std::string& endedAt,
                                      int exitCode, int64_t durationMs)>;

using InsertFn = std::function<int64_t(sqlite3* db, const std::string& indexId,
                                       const std::string& startedAt)>;

using AliveFn = std::function<bool(int64_t historyId, sqlite3* db)>;

using EnumerateFn = std::function<std::vector<std::pair<int64_t, std::string>>()>;

using NotifyFn = std::function<void(const std::string& indexId, int64_t historyId)>;

// Called when the watched process exited but a new process with the same config
// is still alive (R6 auto-takeover). The callback should finalize the old session,
// open a handle to the new process, and restart watching. Returns true if takeover
// succeeded, false if the listener should fall through to normal finalization.
using TakeoverFn = std::function<bool(int pid, const std::string& indexId,
                                       int64_t historyId, const std::string& configFileName)>;

// Called periodically while the watched process is still running (heartbeat).
// Lets the caller refresh runtime history (e.g. running duration) so an abnormal
// exit or an externally-stopped process still leaves evaluation data behind.
using HeartbeatFn = std::function<void(const std::string& indexId, int64_t historyId,
                                       int64_t elapsedMs)>;

struct ProcessExitEvent {
    int pid = 0;
    int exitCode = -1;
    bool shouldFinalize = false;
};

class ProcessExitListener final {
public:
    ProcessExitListener();
    ~ProcessExitListener();

    ProcessExitListener(const ProcessExitListener&) = delete;
    ProcessExitListener& operator=(const ProcessExitListener&) = delete;

    using WatchKey = uint64_t;

    WatchKey watch(HANDLE processHandle, const std::string& indexId, int64_t historyId,
                   InsertFn insertFn, AliveFn aliveFn, EnumerateFn enumerateFn,
                   FinalizeFn finalizeFn, NotifyFn notifyFn,
                   TakeoverFn takeoverFn = nullptr,
                   const std::string& configFileName = "",
                   HeartbeatFn heartbeatFn = nullptr,
                   int heartbeatIntervalMs = 30000);

    void unwatch(WatchKey key);

    void shutdown();

    static bool ShouldFinalize(const ProcessExitEvent& event,
                               const std::string& indexId,
                               int64_t historyId,
                               const std::vector<std::pair<int64_t, std::string>>& allRows,
                               const std::string& currentStartedAt);

private:
    struct Watcher {
        HANDLE processHandle = nullptr;
        std::string indexId;
        int64_t historyId = -1;
        InsertFn insertFn;
        AliveFn aliveFn;
        EnumerateFn enumerateFn;
        FinalizeFn finalizeFn;
        NotifyFn notifyFn;
        TakeoverFn takeoverFn = nullptr;
        std::string configFileName;  // R6: config file name for takeover process lookup
        HeartbeatFn heartbeatFn;     // periodic refresh while process is alive
        int heartbeatIntervalMs = 30000;
        std::thread thread;
        std::atomic<bool> running{true};
        std::chrono::steady_clock::time_point threadStartTime;
    };

    void handleProcessExit(Watcher* w, DWORD exitCode);

    std::mutex mutex_;
    std::map<WatchKey, std::unique_ptr<Watcher>> watchers_;
    WatchKey nextKey_{1};
    std::atomic<bool> shutdownRequested_{false};
};

}  // namespace proc

#endif  // PROCESS_EXIT_LISTENER_H
