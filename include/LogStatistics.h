#ifndef LOG_STATISTICS_H
#define LOG_STATISTICS_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------
// LogStatistics — 解析日志文件并统计各级别出现次数；
// 对 WARN / ERROR 按消息内容（原因）聚合计数。
//
// 解析输入契约（LoggerInstance::write 写盘格式，每行 flush）：
//     [YYYY-MM-DD HH:MM:SS] [LEVEL] message
// 级别 token 位于第二个方括号内：TRACE/DEBUG/INFO/REPORT/WARN/ERROR。
//
// 纯标准库模块，无 wx 依赖，可在 tests/ 独立单测。
// ---------------------------------------------------------------

struct LogLevelCounts {
    std::size_t trace = 0;
    std::size_t debug = 0;
    std::size_t info = 0;
    std::size_t report = 0;
    std::size_t warn = 0;
    std::size_t error = 0;
    std::size_t total = 0;   // 有效日志行数（级别 token 可识别）
};

// 原因聚合项：归一化原因（去时间戳/级别前缀，并去除 "SKIP: <host> - " 等
// 具体 host 前缀，使同一原因的不同 host 聚合为一类）+ 统计值（出现次数）
struct ReasonCount {
    std::string reason;
    std::size_t count = 0;
};

struct LogStatisticsResult {
    LogLevelCounts counts;
    std::vector<ReasonCount> warnReasons;   // 按次数降序（同次数保持字典序）
    std::vector<ReasonCount> errorReasons;  // 按次数降序（同次数保持字典序）
    bool fileOpened = false;                // 文件是否成功打开
};

// 解析日志文件；文件不存在/无法打开时 fileOpened == false，其余字段为零值。
// 无法识别级别 token 的行（空行/截断行/非日志行）不计入任何计数。
LogStatisticsResult parseLogFile(const std::string& filePath);

#endif // LOG_STATISTICS_H
