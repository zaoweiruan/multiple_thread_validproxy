#ifndef UI_ASYNC_OPERATION_GUARD_H
#define UI_ASYNC_OPERATION_GUARD_H

#include <atomic>
#include <thread>

#include <wx/wx.h>

#include "Events.h"

namespace ui {

/**
 * @brief RAII guard that protects async method entry points against re-entrance.
 *
 * Constructed at the start of an async method. On construction:
 *   - If workerThread_ is joinable AND isRunning_ is true, sends a wxQueueEvent
 *     reject message and sets allowed=false.
 *   - Otherwise joins any completed thread, resets cancelRequested_=false,
 *     isRunning_=true, and sets allowed=true.
 *
 * The caller MUST check isAllowed() after construction and return early if false.
 * isRunning_ is reset to false by ScopeGuard (on the worker thread), NOT by this
 * class's destructor.
 *
 * Usage:
 *     AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, handler};
 *     if (!guard.isAllowed()) return;
 */
class AsyncOperationGuard {
public:
    AsyncOperationGuard(
        std::thread& workerThread,
        std::atomic<bool>& isRunning,
        std::atomic<bool>& cancelRequested,
        wxEvtHandler* handler)
        : workerThread_(workerThread)
        , isRunning_(isRunning)
        , cancelRequested_(cancelRequested)
        , handler_(handler)
        , allowed_(false)
    {
        if (workerThread_.joinable()) {
            if (isRunning_) {
                if (handler_) {
                    wxQueueEvent(handler_, new StatusUpdateEvent(0,
                        "REJECT:Another operation is already in progress. "
                        "Please wait or cancel it first."));
                }
                return;  // allowed_ stays false
            }
            workerThread_.join();
        }
        cancelRequested_ = false;
        isRunning_ = true;
        allowed_ = true;
    }

    ~AsyncOperationGuard() = default;

    AsyncOperationGuard(const AsyncOperationGuard&) = delete;
    AsyncOperationGuard& operator=(const AsyncOperationGuard&) = delete;
    AsyncOperationGuard(AsyncOperationGuard&&) = delete;
    AsyncOperationGuard& operator=(AsyncOperationGuard&&) = delete;

    bool isAllowed() const { return allowed_; }

private:
    std::thread& workerThread_;
    std::atomic<bool>& isRunning_;
    std::atomic<bool>& cancelRequested_;
    wxEvtHandler* handler_;
    bool allowed_;
};

} // namespace ui

#endif // UI_ASYNC_OPERATION_GUARD_H
