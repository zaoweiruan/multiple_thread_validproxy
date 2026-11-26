#pragma once

#include "Logger.h"
#include "task/TaskType.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace task {

// In-memory index entry for one published task report. `content` is the full
// UTF-8 report text; `filePath` is the absolute `logDir/report_<taskId>.txt`.
struct TaskReportInfo {
    std::string taskId;
    std::string filePath;
    std::string content;
};

// Formats a task report (metadata header + statistics section, report format
// v2), writes it to `logDir/report_<taskId>.txt` and keeps an in-memory index
// so the monitoring UI can read a finished task's report instantly without
// re-reading the file (task-report-hover Task 2; format v2 = task-report-format
// Task 2).
//
// Format v2: time fields are rendered as local wall-clock `yyyy-mm-dd
// hh:mm:ss` (via task::wallClockDateTime; invalid/empty stamps omit the line),
// `durationMinutes` is derived via task::durationMinutesBetween(startedAt,
// completedAt) (-1 omits the line), there is NO `---- log excerpt ----`
// section, and `updateMethods` is emitted only when non-empty (positioned after
// parentTaskId, before `---- statistics ----`).
//
// Format v3 (2026-11-26): horizontal layout — `type` + `targetId` share a line,
// `status` + `durationMinutes` share a line, and all present timestamps share
// one line; the statistics section is a single pipe-separated line
// (`total: N | success: N | failed: N | skipped: N`). When
// `stats.failures` is non-empty, a `---- failed subscriptions ----` section
// follows the statistics with one `name (id)` entry per failed item (bare id
// when the name is empty).
//
// Thread safety: a single std::mutex guards the index and the disk write.
// `find` performs the lookup AND copies the content to `out` while holding the
// lock, so no pointer escapes the lock; it never inserts an empty entry for a
// missing key. `publish` updates the index only after the disk write succeeds.
class TaskReportService {
public:
    explicit TaskReportService(std::string logDir);

    // Build the report text without touching disk or shared state.
    std::string formatReport(const task::TaskHandle& handle,
                             const task::TaskStats& stats,
                             const std::string& updateMethods) const;

    // Format + write `logDir/report_<taskId>.txt` (overwrite). Returns the file
    // path on success and updates the index; returns an empty string on write
    // failure WITHOUT updating the index and logs LogLevel::WARN.
    std::string publish(const task::TaskHandle& handle,
                        const task::TaskStats& stats,
                        const std::string& updateMethods);

    // Copy the report for `taskId` into `out` under the lock. Returns false
    // (and leaves `out` untouched) when the key is absent — no empty entry is
    // inserted.
    bool find(const std::string& taskId, task::TaskReportInfo& out) const;

private:
    std::string logDir_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, task::TaskReportInfo> index_;
};

} // namespace task
