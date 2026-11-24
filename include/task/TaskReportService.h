#pragma once

#include "Logger.h"
#include "task/TaskType.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace task {

// A single log line captured during a task's execution window. `level` is
// retained for fidelity; `line` is the raw line emitted verbatim into the
// report's log excerpt (task-report-hover Task 2).
struct LogLine {
    ::LogLevel level;
    std::string line;
};

// In-memory index entry for one published task report. `content` is the full
// UTF-8 report text; `filePath` is the absolute `logDir/report_<taskId>.txt`.
struct TaskReportInfo {
    std::string taskId;
    std::string filePath;
    std::string content;
};

// Formats a task report (metadata header + statistics section + log excerpt),
// writes it to `logDir/report_<taskId>.txt` and keeps an in-memory index so the
// monitoring UI can read a finished task's report instantly without re-reading
// the file (task-report-hover Task 2).
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
                             const std::vector<task::LogLine>& lines,
                             const task::TaskStats& stats) const;

    // Format + write `logDir/report_<taskId>.txt` (overwrite). Returns the file
    // path on success and updates the index; returns an empty string on write
    // failure WITHOUT updating the index and logs LogLevel::WARN.
    std::string publish(const task::TaskHandle& handle,
                        const std::vector<task::LogLine>& lines,
                        const task::TaskStats& stats);

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
