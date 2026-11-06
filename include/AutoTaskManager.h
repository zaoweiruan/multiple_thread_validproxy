#ifndef AUTO_TASK_MANAGER_H
#define AUTO_TASK_MANAGER_H

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>
#include <sqlite3.h>
#include "ConfigReader.h"
#include "SubitemUpdaterV2.h"   // UpdateProgressObserver snapshot type (v1.2.6 §14.1)

class NetworkMonitor;
class ProxyBatchTester;

enum class AutoTaskStepType {
    UPDATE_ALL,
    TEST_ALL,
    DEDUP,
    SYNC,
    EXPORT,
    RESOLVE_REGION
};

enum class StepStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    CANCELLED
};

struct AutoTaskStepInfo {
    AutoTaskStepType type;
    std::string name;
    StepStatus status;
    std::string error;
    std::string started_at;
    std::string completed_at;
};

struct AutoTaskState {
    std::string task_id;
    std::string created_at;
    int current_step_index;
    std::vector<AutoTaskStepInfo> steps;
    bool completed;
    bool cancelled;
    int sql_query_id{0};       // SQL scheme id for sql_query (0 = use config string)
    int sql_by_subid_id{0};    // SQL scheme id for sql_by_subid (0 = use config string)
};

struct AutoTaskProgress {
    int current_step;
    int total_steps;
    std::string step_name;
    StepStatus step_status;
    int percent;
    std::string message;
};

class AutoTaskManager {
public:
    using ProgressCallback = std::function<void(const AutoTaskProgress&)>;

    AutoTaskManager(sqlite3* db,
                    const config::AppConfig& config,
                    const std::string& baseDir,
                    std::atomic<bool>* externalCancel = nullptr,
                    const NetworkMonitor* netMon = nullptr);

    ~AutoTaskManager();

    bool run(const std::vector<std::string>& stepNames);
    bool resume();

    void cancel();
    bool isCancelled() const;
    bool isRunning() const;

    AutoTaskState getState() const;
    std::string getStateFilePath() const;

    static AutoTaskState loadStateFile(const std::string& filePath);
    static bool saveStateFile(const std::string& filePath, const AutoTaskState& state);

    static AutoTaskStepType stepNameToType(const std::string& name);
    static std::string stepTypeToName(AutoTaskStepType type);
    static std::vector<AutoTaskStepInfo> createStepList(const std::vector<std::string>& stepNames);

    static std::string defaultStateFilePath();

    void setProgressCallback(ProgressCallback cb);

    /// Observe the ProxyBatchTester used by the auto-task "test" step, so the
    /// UI can register it as the live pool and insert high-priority proxies.
    /// Called with the tester before the step runs and with nullptr after.
    /// The tester is shared_ptr-owned (the step keeps it alive through run());
    /// the UI stores its own copy so a concurrent poll can never observe a
    /// dangling pointer.
    using PoolObserver = std::function<void(const std::shared_ptr<ProxyBatchTester>&)>;
    void setPoolObserver(PoolObserver cb);

    /// Observe the full step list (the auto-task breakdown) so the UI can show
    /// every sub-task and its live status in the task monitor. Invoked from the
    /// worker thread at run/resume start and on every step transition, with a
    /// consistent snapshot of state_.steps (PENDING/RUNNING/COMPLETED/FAILED/
    /// CANCELLED). The receiver must be thread-safe and non-blocking (owner
    /// request 2026-11-04: 自动任务子任务拆解纳入任务监控).
    using StepObserver = std::function<void(const std::vector<AutoTaskStepInfo>&)>;
    void setStepObserver(StepObserver cb);

    /// Subscription-level snapshots INSIDE the update_all step (owner
    /// 2026-11-06: 更新订阅只显示整体进度): while stepUpdateAll runs,
    /// SubitemUpdaterV2 publishes one entry per enabled subscription; those
    /// snapshots are forwarded here and AppController reduces them to overall
    /// counters (no per-subscription rows). Fires on the same worker thread as
    /// StepObserver — callbacks never interleave, the receiver must be
    /// thread-safe and non-blocking.
    using UpdateProgressObserver =
        std::function<void(const std::vector<update::SubitemUpdaterV2::UpdateStepInfo>&)>;
    void setUpdateProgressObserver(UpdateProgressObserver cb);

private:
    bool runSteps();
    bool executeStep(const AutoTaskStepInfo& step, int stepIndex);
    void notifyStepObserver();
    bool stepUpdateAll();
    bool stepTestAll();
    bool stepDedup();
    bool stepSync();
    bool stepExport();
    bool stepResolveRegions();

    void writeState();
    std::string getCurrentTimestamp();

    sqlite3* db_;
    config::AppConfig config_;
    std::string baseDir_;
    std::atomic<bool>* externalCancel_;
    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool> running_{false};

    AutoTaskState state_;
    ProgressCallback progressCb_;
    PoolObserver poolObserver_;
    StepObserver stepObserver_;
    // Forwards every SubitemUpdaterV2 publish inside stepUpdateAll; AppController
    // turns the snapshots into overall progress counters (owner 2026-11-06).
    UpdateProgressObserver updateProgressObserver_;

    std::string stateFilePath_;
    const NetworkMonitor* netMon_{nullptr};
};

#endif // AUTO_TASK_MANAGER_H
