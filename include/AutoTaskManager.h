#ifndef AUTO_TASK_MANAGER_H
#define AUTO_TASK_MANAGER_H

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <sqlite3.h>
#include "ConfigReader.h"

class NetworkMonitor;

enum class AutoTaskStepType {
    UPDATE_ALL,
    TEST_ALL,
    DEDUP,
    SYNC,
    EXPORT
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
};

struct AutoTaskProgress {
    int current_step;
    int total_steps;
    std::string step_name;
    StepStatus step_status;
    int percent;
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

    void setProgressCallback(ProgressCallback cb);

private:
    bool runSteps();
    bool executeStep(const AutoTaskStepInfo& step, int stepIndex);
    bool stepUpdateAll();
    bool stepTestAll();
    bool stepDedup();
    bool stepSync();
    bool stepExport();

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

    std::string stateFilePath_;
    const NetworkMonitor* netMon_{nullptr};
};

#endif // AUTO_TASK_MANAGER_H
