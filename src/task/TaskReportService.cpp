#include "task/TaskReportService.h"

#include <cstdlib>
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

// Parse a whole string as a base-10 integer; returns -1 when the string is
// empty or contains any non-digit junk. Used to derive durationMs from the
// startedAt/completedAt tick-count strings (TaskQueue stores steady_clock tick
// counts). A datetime string ("yyyy-MM-dd ...") fails and is treated as absent.
long long parseLong(const std::string& s) {
    if (s.empty()) return -1;
    const char* c = s.c_str();
    char* endp = nullptr;
    const long long v = std::strtoll(c, &endp, 10);
    if (endp == c || *endp != '\0') return -1;
    return v;
}

} // namespace

namespace task {

TaskReportService::TaskReportService(std::string logDir)
    : logDir_(std::move(logDir)) {
}

std::string TaskReportService::formatReport(const task::TaskHandle& handle,
                                            const std::vector<task::LogLine>& lines,
                                            const task::TaskStats& stats) const {
    std::ostringstream out;

    // ── metadata header: present fields only (spec R3) ──
    out << "taskId: " << handle.taskId << "\n";
    out << "type: " << taskTypeToString(handle.type) << "\n";
    out << "status: " << taskStatusToString(handle.status) << "\n";
    if (!handle.createdAt.empty()) out << "createdAt: " << handle.createdAt << "\n";
    if (!handle.startedAt.empty()) out << "startedAt: " << handle.startedAt << "\n";
    if (!handle.completedAt.empty()) out << "completedAt: " << handle.completedAt << "\n";
    // durationMs: derived when both endpoints parse as integers (orchestrator
    // decision 2026-11-24: integer delta from startedAt/completedAt).
    const long long start = parseLong(handle.startedAt);
    const long long end = parseLong(handle.completedAt);
    if (start >= 0 && end >= 0) {
        out << "durationMs: " << (end - start) << "\n";
    }
    if (!handle.targetId.empty()) out << "targetId: " << handle.targetId << "\n";
    if (!handle.error.empty()) out << "error: " << handle.error << "\n";
    if (handle.progressTotal != 0 || handle.progressCurrent != 0) {
        out << "progressCurrent: " << handle.progressCurrent << "\n";
        out << "progressTotal: " << handle.progressTotal << "\n";
    }
    if (!handle.parentTaskId.empty()) out << "parentTaskId: " << handle.parentTaskId << "\n";

    // ── statistics section (structured, spec R3 / D6) ──
    out << "---- statistics ----\n";
    out << "total: " << stats.total << "\n";
    out << "success: " << stats.success << "\n";
    out << "failed: " << stats.failed << "\n";
    out << "skipped: " << stats.skipped << "\n";

    // ── log excerpt: captured task-window lines, verbatim ──
    out << "---- log excerpt ----\n";
    for (std::vector<task::LogLine>::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        out << it->line << "\n";
    }

    return out.str();
}

std::string TaskReportService::publish(const task::TaskHandle& handle,
                                       const std::vector<task::LogLine>& lines,
                                       const task::TaskStats& stats) {
    const std::string content = formatReport(handle, lines, stats);
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
