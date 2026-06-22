#include "ui/UiOperationRunner.h"
#include "Logger.h"
#include <future>
#include <chrono>

namespace ui {

UiOperationRunner::UiOperationRunner()
    : cancelRequested_(false), isRunning_(false) {
}

UiOperationRunner::~UiOperationRunner() {
    cancelRequested_ = true;
    if (workerThread_.joinable()) {
        std::future<void> fut = std::async(std::launch::async, [this]() {
            if (workerThread_.joinable()) workerThread_.join();
        });
        if (fut.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            workerThread_.detach();
            Logger::write("[UiOperationRunner] Destructor: detach due to timeout", LogLevel::WARN);
        }
    }
}

bool UiOperationRunner::runAsync(std::function<void()> func) {
    if (workerThread_.joinable()) {
        if (isRunning_.load()) {
            return false; // Already running
        }
        workerThread_.join();
    }
    cancelRequested_ = false;
    isRunning_ = true;
    workerThread_ = std::thread([this, func]() {
        struct ResetGuard { std::atomic<bool>& f; ~ResetGuard() { f = false; } };
        ResetGuard _rg{isRunning_};
        func();
    });
    return true;
}

void UiOperationRunner::cancel() {
    cancelRequested_ = true;
}

bool UiOperationRunner::isCancelled() const {
    return cancelRequested_.load();
}

bool UiOperationRunner::isRunning() const {
    return isRunning_.load();
}

} // namespace ui