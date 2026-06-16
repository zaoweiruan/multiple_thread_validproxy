#include "AutoTaskManager.h"
#include "NetworkMonitor.h"
#include "SubitemUpdaterV2.h"
#include "ProxyBatchTester.h"
#include "Logger.h"
#include "ShareLink.h"
#include "Profileitem.h"
#include "ProfileExItem.h"
#include "Utils.h"

#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <random>
#include <filesystem>
#include <boost/json.hpp>

AutoTaskManager::AutoTaskManager(sqlite3* db,
                                 const config::AppConfig& config,
                                 const std::string& baseDir,
                                 std::atomic<bool>* externalCancel,
                                 const NetworkMonitor* netMon)
    : db_(db)
    , config_(config)
    , baseDir_(baseDir)
    , externalCancel_(externalCancel)
    , netMon_(netMon)
{
    if (!config_.auto_task.state_file.empty()) {
        stateFilePath_ = config_.auto_task.state_file;
    } else {
        stateFilePath_ = baseDir_ + "/worker/autotask_state.json";
    }
}

AutoTaskManager::~AutoTaskManager() {
    cancel();
}

bool AutoTaskManager::run(const std::vector<std::string>& stepNames) {
    if (running_.exchange(true)) {
        Logger::write("AutoTask: already running", LogLevel::WARN);
        return false;
    }

    std::string taskId = getCurrentTimestamp() + "_" + std::to_string(std::random_device{}());

    state_.task_id = taskId;
    state_.created_at = getCurrentTimestamp();
    state_.current_step_index = 0;
    state_.steps = createStepList(stepNames);
    state_.completed = false;
    state_.cancelled = false;

    writeState();
    Logger::write("AutoTask: starting with " + std::to_string(stepNames.size()) + " steps", LogLevel::REPORT);

    bool result = runSteps();

    Logger::write("AutoTask: " + std::string(result ? "completed" : (state_.cancelled ? "cancelled" : "failed")),
        result ? LogLevel::REPORT : LogLevel::ERR);

    return result;
}

bool AutoTaskManager::resume() {
    AutoTaskState saved = loadStateFile(stateFilePath_);
    if (saved.task_id.empty()) {
        Logger::write("AutoTask: no saved state to resume", LogLevel::WARN);
        return false;
    }
    if (saved.completed) {
        Logger::write("AutoTask: previous task already completed, nothing to resume", LogLevel::INFO);
        return false;
    }
    if (!saved.cancelled) {
        Logger::write("AutoTask: previous task was not cancelled, starting fresh from saved steps", LogLevel::INFO);
        std::vector<std::string> stepNames;
        for (const AutoTaskStepInfo& s : saved.steps) {
            stepNames.push_back(s.name);
        }
        return run(stepNames);
    }

    if (saved.current_step_index < 0 || saved.current_step_index >= static_cast<int>(saved.steps.size())) {
        Logger::write("AutoTask: saved state has invalid step index, starting fresh", LogLevel::WARN);
        std::vector<std::string> stepNames;
        for (const AutoTaskStepInfo& s : saved.steps) {
            stepNames.push_back(s.name);
        }
        return run(stepNames);
    }

    Logger::write("AutoTask: resuming from step " + std::to_string(saved.current_step_index)
        + " [" + saved.steps[saved.current_step_index].name + "]", LogLevel::REPORT);

    state_ = saved;
    state_.cancelled = false;

    if (running_.exchange(true)) {
        Logger::write("AutoTask: already running", LogLevel::WARN);
        return false;
    }

    bool result = runSteps();

    Logger::write("AutoTask: " + std::string(result ? "resumed and completed" : "resume failed"),
        result ? LogLevel::REPORT : LogLevel::ERR);

    return result;
}

bool AutoTaskManager::runSteps() {
    bool allOk = true;
    for (; state_.current_step_index < static_cast<int>(state_.steps.size()); ++state_.current_step_index) {
        if (isCancelled()) {
            state_.cancelled = true;
            Logger::write("AutoTask: cancelled at step " + std::to_string(state_.current_step_index), LogLevel::REPORT);
            writeState();
            break;
        }

        if (netMon_ && !netMon_->IsConnected()) {
            Logger::write("AutoTask: aborted at step " + std::to_string(state_.current_step_index)
                + " [" + state_.steps[state_.current_step_index].name + "] - network disconnected", LogLevel::ERR);
            state_.cancelled = true;
            state_.steps[state_.current_step_index].status = StepStatus::CANCELLED;
            allOk = false;
            writeState();
            break;
        }

        AutoTaskStepInfo& step = state_.steps[state_.current_step_index];
        if (step.status == StepStatus::COMPLETED) {
            Logger::write("AutoTask: skipping already completed step [" + step.name + "]", LogLevel::INFO);
            if (progressCb_) {
                AutoTaskProgress p;
                p.current_step = state_.current_step_index;
                p.total_steps = static_cast<int>(state_.steps.size());
                p.step_name = step.name;
                p.step_status = StepStatus::COMPLETED;
                p.percent = (state_.current_step_index * 100) / static_cast<int>(state_.steps.size());
                progressCb_(p);
            }
            continue;
        }

        step.status = StepStatus::RUNNING;
        step.started_at = getCurrentTimestamp();
        step.error.clear();
        writeState();

        if (progressCb_) {
            AutoTaskProgress p;
            p.current_step = state_.current_step_index;
            p.total_steps = static_cast<int>(state_.steps.size());
            p.step_name = step.name;
            p.step_status = StepStatus::RUNNING;
            p.percent = (state_.current_step_index * 100) / static_cast<int>(state_.steps.size());
            progressCb_(p);
        }

        Logger::write("AutoTask: executing step " + std::to_string(state_.current_step_index + 1)
            + "/" + std::to_string(state_.steps.size()) + " [" + step.name + "]", LogLevel::REPORT);

        bool ok = executeStep(step, state_.current_step_index);

        step.completed_at = getCurrentTimestamp();
        if (isCancelled()) {
            step.status = StepStatus::CANCELLED;
            state_.cancelled = true;
            allOk = false;
            Logger::write("AutoTask: step [" + step.name + "] cancelled", LogLevel::REPORT);
            writeState();
            break;
        } else if (ok) {
            step.status = StepStatus::COMPLETED;
            Logger::write("AutoTask: step [" + step.name + "] completed", LogLevel::REPORT);
        } else {
            step.status = StepStatus::FAILED;
            allOk = false;
            Logger::write("AutoTask: step [" + step.name + "] failed", LogLevel::ERR);
            writeState();
            break;
        }
        writeState();
    }

    state_.completed = !state_.cancelled && allOk;
    running_ = false;
    writeState();

    if (state_.completed && config_.auto_task.notify_on_complete) {
        utils::sendNotification("AutoTask Complete", "All steps completed successfully");
    }

    return state_.completed;
}

bool AutoTaskManager::executeStep(const AutoTaskStepInfo& step, int /*stepIndex*/) {
    switch (step.type) {
        case AutoTaskStepType::UPDATE_ALL: return stepUpdateAll();
        case AutoTaskStepType::TEST_ALL:   return stepTestAll();
        case AutoTaskStepType::DEDUP:      return stepDedup();
        case AutoTaskStepType::SYNC:       return stepSync();
        case AutoTaskStepType::EXPORT:     return stepExport();
    }
    return false;
}

bool AutoTaskManager::stepUpdateAll() {
    std::atomic<bool>* cancelPtr = externalCancel_ ? externalCancel_ : &cancelRequested_;
    update::SubitemUpdaterV2 updater(db_, config_.xray_executable, config_, nullptr, baseDir_, cancelPtr, netMon_);
    bool result = updater.run();
    if (isCancelled() && !result) {
        Logger::write("AutoTask: update step was cancelled", LogLevel::REPORT);
    }
    return result;
}

bool AutoTaskManager::stepTestAll() {
    std::atomic<bool>* cancelPtr = externalCancel_ ? externalCancel_ : &cancelRequested_;
    ProxyBatchTester tester(db_, config_, baseDir_, cancelPtr, netMon_);
    bool result = tester.run();
    if (isCancelled() && !result) {
        Logger::write("AutoTask: test step was cancelled", LogLevel::REPORT);
    }
    return result;
}

bool AutoTaskManager::stepDedup() {
    std::atomic<bool>* cancelPtr = externalCancel_ ? externalCancel_ : &cancelRequested_;
    update::SubitemUpdaterV2 updater(db_, config_.xray_executable, config_, nullptr, baseDir_, cancelPtr, netMon_);
    bool result = updater.deduplicate();
    if (isCancelled() && !result) {
        Logger::write("AutoTask: dedup step was cancelled", LogLevel::REPORT);
    }
    return result;
}

bool AutoTaskManager::stepSync() {
    std::string sourceDb = config_.sync.source_db;
    std::string targetDb = config_.sync.target_db;

    if (sourceDb.empty() || targetDb.empty()) {
        Logger::write("AutoTask: sync step skipped — source or target DB not configured", LogLevel::WARN);
        return true;
    }

    update::SubitemUpdaterV2 updater(nullptr, "", config_, nullptr, baseDir_, nullptr, netMon_);
    bool result = updater.syncDatabases(sourceDb, targetDb);
    if (isCancelled() && !result) {
        Logger::write("AutoTask: sync step was cancelled", LogLevel::REPORT);
    }
    return result;
}

bool AutoTaskManager::stepExport() {
    db::models::ProfileitemDAO profileDao(db_);
    db::models::ProfileExItemDAO exDao(db_);

    std::string sql = R"(
        SELECT p.*, COALESCE(pe.Delay, 0) as ExDelay
        FROM ProfileItem p
        LEFT JOIN ProfileExItem pe ON p.IndexId = pe.IndexId
        WHERE CAST(COALESCE(pe.Delay, 0) AS INTEGER) > 0
        ORDER BY CAST(pe.Delay AS INTEGER) ASC
    )";

    std::vector<db::models::Profileitem> profiles = profileDao.getAll(sql);
    if (profiles.empty()) {
        Logger::write("AutoTask: export step — no valid proxies to export", LogLevel::INFO);
        return true;
    }

    char timestamp[32];
    time_t now = time(nullptr);
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", localtime(&now));

    std::filesystem::path outPath = std::filesystem::path(baseDir_)
        / "proxies" / ("proxies_" + std::string(timestamp) + ".txt");
    std::filesystem::create_directories(outPath.parent_path());

    std::ofstream outFile(outPath, std::ios::binary);
    if (!outFile.is_open()) {
        Logger::write("AutoTask: export step — failed to open output file", LogLevel::ERR);
        return false;
    }

    for (const db::models::Profileitem& profile : profiles) {
        std::string link = share::ShareLink::toShareUri(
            profile.configtype, profile.address, profile.port,
            profile.id, profile.security, profile.network,
            profile.flow, profile.sni, profile.alpn,
            profile.fingerprint, profile.allowinsecure, profile.path,
            profile.requesthost, profile.headertype, profile.streamsecurity,
            profile.remarks, profile.echconfiglist, profile.publickey,
            profile.shortid
        );
        if (!link.empty()) {
            outFile << link << "\n";
        }
    }

    outFile.close();

    Logger::write("AutoTask: exported " + std::to_string(profiles.size())
        + " proxies to " + outPath.string(), LogLevel::REPORT);

    return true;
}

void AutoTaskManager::cancel() {
    cancelRequested_ = true;
}

bool AutoTaskManager::isCancelled() const {
    if (externalCancel_ && externalCancel_->load()) return true;
    return cancelRequested_.load();
}

bool AutoTaskManager::isRunning() const {
    return running_.load();
}

AutoTaskState AutoTaskManager::getState() const {
    return state_;
}

std::string AutoTaskManager::getStateFilePath() const {
    return stateFilePath_;
}

void AutoTaskManager::setProgressCallback(ProgressCallback cb) {
    progressCb_ = std::move(cb);
}

// ---- static utility methods ----

AutoTaskStepType AutoTaskManager::stepNameToType(const std::string& name) {
    if (name == "update" || name == "update_all") return AutoTaskStepType::UPDATE_ALL;
    if (name == "test" || name == "test_all")   return AutoTaskStepType::TEST_ALL;
    if (name == "dedup")  return AutoTaskStepType::DEDUP;
    if (name == "sync")   return AutoTaskStepType::SYNC;
    if (name == "export") return AutoTaskStepType::EXPORT;
    return AutoTaskStepType::UPDATE_ALL;
}

std::string AutoTaskManager::stepTypeToName(AutoTaskStepType type) {
    switch (type) {
        case AutoTaskStepType::UPDATE_ALL: return "update";
        case AutoTaskStepType::TEST_ALL:   return "test";
        case AutoTaskStepType::DEDUP:      return "dedup";
        case AutoTaskStepType::SYNC:       return "sync";
        case AutoTaskStepType::EXPORT:     return "export";
    }
    return "unknown";
}

std::vector<AutoTaskStepInfo> AutoTaskManager::createStepList(const std::vector<std::string>& stepNames) {
    std::vector<AutoTaskStepInfo> steps;
    for (const std::string& name : stepNames) {
        AutoTaskStepInfo info;
        info.type = stepNameToType(name);
        if (info.type == AutoTaskStepType::UPDATE_ALL && name != "update" && name != "update_all") {
            Logger::write("AutoTask: unknown step name \"" + name + "\" treated as update", LogLevel::WARN);
        }
        info.name = name;
        info.status = StepStatus::PENDING;
        steps.push_back(info);
    }
    return steps;
}

// ---- state persistence ----

static std::string stepStatusToString(StepStatus s) {
    switch (s) {
        case StepStatus::PENDING:   return "pending";
        case StepStatus::RUNNING:   return "running";
        case StepStatus::COMPLETED: return "completed";
        case StepStatus::FAILED:    return "failed";
        case StepStatus::CANCELLED: return "cancelled";
    }
    return "unknown";
}

static StepStatus stringToStepStatus(const std::string& s) {
    if (s == "completed") return StepStatus::COMPLETED;
    if (s == "failed")    return StepStatus::FAILED;
    if (s == "running")   return StepStatus::RUNNING;
    if (s == "cancelled") return StepStatus::CANCELLED;
    return StepStatus::PENDING;
}

void AutoTaskManager::writeState() {
    saveStateFile(stateFilePath_, state_);
}

bool AutoTaskManager::saveStateFile(const std::string& filePath, const AutoTaskState& state) {
    try {
        boost::json::object root;
        root["version"] = 1;
        root["task_id"] = state.task_id;
        root["created_at"] = state.created_at;
        root["current_step_index"] = state.current_step_index;
        root["completed"] = state.completed;
        root["cancelled"] = state.cancelled;

        boost::json::array stepsArr;
        for (const AutoTaskStepInfo& step : state.steps) {
            boost::json::object stepObj;
            stepObj["name"] = step.name;
            stepObj["status"] = stepStatusToString(step.status);
            stepObj["error"] = step.error;
            stepObj["started_at"] = step.started_at;
            stepObj["completed_at"] = step.completed_at;
            stepsArr.push_back(stepObj);
        }
        root["steps"] = stepsArr;

        // Ensure parent directory exists
        std::filesystem::path parent = std::filesystem::path(filePath).parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            std::filesystem::create_directories(parent);
        }

        std::ofstream file(filePath);
        if (!file.is_open()) {
            Logger::write("AutoTask: failed to write state file: " + filePath, LogLevel::ERR);
            return false;
        }
        file << boost::json::serialize(root);
        file.close();
        return true;
    } catch (const std::exception& e) {
        Logger::write("AutoTask: exception writing state file: " + std::string(e.what()), LogLevel::ERR);
        return false;
    }
}

AutoTaskState AutoTaskManager::loadStateFile(const std::string& filePath) {
    AutoTaskState state;
    state.current_step_index = 0;
    state.completed = false;
    state.cancelled = false;

    std::ifstream file(filePath);
    if (!file.is_open()) {
        return state;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    try {
        boost::json::value jv = boost::json::parse(content);
        if (!jv.is_object()) return state;

        boost::json::object& root = jv.as_object();

        if (root.contains("task_id") && root["task_id"].is_string())
            state.task_id = root["task_id"].as_string().c_str();
        if (root.contains("created_at") && root["created_at"].is_string())
            state.created_at = root["created_at"].as_string().c_str();
        if (root.contains("current_step_index") && root["current_step_index"].is_int64())
            state.current_step_index = static_cast<int>(root["current_step_index"].as_int64());
        if (root.contains("completed") && root["completed"].is_bool())
            state.completed = root["completed"].as_bool();
        if (root.contains("cancelled") && root["cancelled"].is_bool())
            state.cancelled = root["cancelled"].as_bool();

        if (root.contains("steps") && root["steps"].is_array()) {
            for (const boost::json::value& sv : root["steps"].as_array()) {
                if (!sv.is_object()) continue;
                const boost::json::object& so = sv.as_object();
                AutoTaskStepInfo step;
                step.type = AutoTaskStepType::UPDATE_ALL;
                const boost::json::value* nameVal = so.if_contains("name");
                if (nameVal && nameVal->is_string()) {
                    step.name = nameVal->as_string().c_str();
                    step.type = stepNameToType(step.name);
                }
                const boost::json::value* statusVal = so.if_contains("status");
                if (statusVal && statusVal->is_string())
                    step.status = stringToStepStatus(statusVal->as_string().c_str());
                const boost::json::value* errorVal = so.if_contains("error");
                if (errorVal && errorVal->is_string())
                    step.error = errorVal->as_string().c_str();
                const boost::json::value* startedVal = so.if_contains("started_at");
                if (startedVal && startedVal->is_string())
                    step.started_at = startedVal->as_string().c_str();
                const boost::json::value* completedVal = so.if_contains("completed_at");
                if (completedVal && completedVal->is_string())
                    step.completed_at = completedVal->as_string().c_str();
                state.steps.push_back(step);
            }
        }
    } catch (const std::exception& e) {
        Logger::write("AutoTask: exception loading state file: " + std::string(e.what()), LogLevel::ERR);
    }

    return state;
}

std::string AutoTaskManager::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", localtime(&t));
    return std::string(buf);
}
