#include "task/TaskReportService.h"
#include "task/TaskFormat.h"

#include <chrono>
#include <fstream>
#include <sstream>

namespace {

// Status label for the metadata header (TaskType.h exports taskTypeToString but
// no status equivalent; kept file-local, matching the UI's local helper).
const char* taskStatusToString(task::TaskStatus s) {
    switch (s) {
    case task::TaskStatus::PENDING:   return "PENDING";
    case task::TaskStatus::QUEUED:    return "QUEUED";
    case task::TaskStatus::RUNNING:   return "RUNNING";
    case task::TaskStatus::COMPLETED: return "COMPLETED";
    case task::TaskStatus::FAILED:    return "FAILED";
    case task::TaskStatus::CANCELLED: return "CANCELLED";
    case task::TaskStatus::REJECTED:  return "REJECTED";
    case task::TaskStatus::SKIPPED:   return "SKIPPED";
    }
    return "UNKNOWN";
}

} // namespace

namespace task {

TaskReportService::TaskReportService(std::string logDir)
    : logDir_(std::move(logDir)) {
}

std::string TaskReportService::formatReport(const task::TaskHandle& handle,
                                            const task::TaskStats& stats,
                                            const std::string& updateMethods) const {
    std::ostringstream out;

    // A single clock snapshot so all three time fields share one reference
    // instant (the wall clock is derived from the steady stamps relative to
    // now; capturing now() per field could straddle a second boundary).
    const std::chrono::steady_clock::time_point steadyNow =
        std::chrono::steady_clock::now();
    const std::chrono::system_clock::time_point wallNow =
        std::chrono::system_clock::now();

    // ── metadata header: horizontal layout, present fields only (spec R3) ──
    // 2026-11-26 layout: related fields share one line to keep the hover popup
    // short (taskId / type+targetId / status+durationMinutes / all timestamps).
    out << "taskId: " << handle.taskId << "\n";

    out << "type: " << taskTypeToString(handle.type);
    if (!handle.targetId.empty()) out << " targetId: " << handle.targetId;
    out << "\n";

    // durationMinutes: floor minutes between the two steady stamps; -1 (either
    // stamp invalid) → the field is omitted.
    const long long durationMinutes =
        task::durationMinutesBetween(handle.startedAt, handle.completedAt);
    out << "status: " << taskStatusToString(handle.status);
    if (durationMinutes >= 0) out << " durationMinutes: " << durationMinutes;
    out << "\n";

    // Format v2: steady stamps rendered as local wall-clock; invalid/empty
    // stamps produce the empty string → the field is omitted from the line.
    const std::string createdAt =
        task::wallClockDateTime(handle.createdAt, steadyNow, wallNow);
    const std::string startedAt =
        task::wallClockDateTime(handle.startedAt, steadyNow, wallNow);
    const std::string completedAt =
        task::wallClockDateTime(handle.completedAt, steadyNow, wallNow);
    bool wroteTime = false;
    if (!createdAt.empty()) { out << "createdAt: " << createdAt; wroteTime = true; }
    if (!startedAt.empty()) {
        if (wroteTime) out << ' ';
        out << "startedAt: " << startedAt;
        wroteTime = true;
    }
    if (!completedAt.empty()) {
        if (wroteTime) out << ' ';
        out << "completedAt: " << completedAt;
        wroteTime = true;
    }
    if (wroteTime) out << "\n";

    if (!handle.error.empty()) out << "error: " << handle.error << "\n";
    if (handle.progressTotal != 0 || handle.progressCurrent != 0) {
        out << "progressCurrent: " << handle.progressCurrent
            << " progressTotal: " << handle.progressTotal << "\n";
    }
    if (!handle.parentTaskId.empty()) out << "parentTaskId: " << handle.parentTaskId << "\n";
    // Format v2: updateMethods emitted only when non-empty, after parentTaskId
    // and before the statistics section.
    if (!updateMethods.empty()) out << "updateMethods: " << updateMethods << "\n";

    // ── statistics section (structured, spec R3 / D6) ──
    // 2026-11-26: one pipe-separated line instead of four.
    out << "---- statistics ----\n";
    out << "total: " << stats.total << " | success: " << stats.success
        << " | failed: " << stats.failed << " | skipped: " << stats.skipped << "\n";

    // ── failed subscription identities (update tasks, 2026-11-26) ──
    if (!stats.failures.empty()) {
        out << "---- failed subscriptions ----\n";
        for (const task::TaskFailure& f : stats.failures) {
            if (!f.name.empty()) {
                out << f.name << " (" << f.id << ")\n";
            } else {
                out << f.id << "\n";
            }
        }
    }

    return out.str();
}

std::string TaskReportService::publish(const task::TaskHandle& handle,
                                       const task::TaskStats& stats,
                                       const std::string& updateMethods) {
    const std::string content = formatReport(handle, stats, updateMethods);
    const std::string filePath = logDir_ + "/report_" + handle.taskId + ".txt";

    std::lock_guard<std::mutex> lock(mutex_);

    {
        std::ofstream file(filePath, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!file.is_open()) {
            Logger::write("TaskReportService: failed to open report file for task " +
                          handle.taskId + " at " + filePath, LogLevel::WARN);
            return "";
        }
        file << content;
        file.flush();
        if (!file.good()) {
            Logger::write("TaskReportService: failed to write report file for task " +
                          handle.taskId + " at " + filePath, LogLevel::WARN);
            return "";
        }
    }

    task::TaskReportInfo info;
    info.taskId = handle.taskId;
    info.filePath = filePath;
    info.content = content;
    index_[handle.taskId] = info;
    return filePath;
}

bool TaskReportService::find(const std::string& taskId, task::TaskReportInfo& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<std::string, task::TaskReportInfo>::const_iterator it = index_.find(taskId);
    if (it == index_.end()) {
        return false; // no empty entry inserted; `out` untouched
    }
    out = it->second; // copy under the lock — no pointer escapes
    return true;
}

} // namespace task
