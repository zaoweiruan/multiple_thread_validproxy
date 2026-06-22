#ifndef UI_UI_OPERATION_RUNNER_H
#define UI_UI_OPERATION_RUNNER_H

#include <atomic>
#include <thread>
#include <functional>

namespace ui {

class UiOperationRunner {
public:
    UiOperationRunner();
    ~UiOperationRunner();

    bool runAsync(std::function<void()> func);
    void cancel();
    bool isCancelled() const;
    bool isRunning() const;

private:
    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool> isRunning_{false};
    std::thread workerThread_;
};

} // namespace ui

#endif // UI_UI_OPERATION_RUNNER_H