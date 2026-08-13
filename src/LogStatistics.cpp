#include "LogStatistics.h"

#include <algorithm>
#include <fstream>
#include <map>

namespace {

// 从日志行提取级别 token 与消息原文。
// 行格式: [YYYY-MM-DD HH:MM:SS] [LEVEL] message
// 仅解析前两个方括号：第一个为时间戳，第二个为级别；第二个 ']' 后跳过
// 空白即为消息原文。消息内容中出现 "[ERROR]" 之类文本不影响判定。
// 返回 false 表示该行无法识别（空行/截断/非日志行），调用方直接忽略。
bool extractLevelAndMessage(const std::string& line, std::string& levelOut, std::string& messageOut) {
    if (line.empty() || line[0] != '[') {
        return false;
    }
    std::size_t firstClose = line.find(']');
    if (firstClose == std::string::npos) {
        return false;
    }
    std::size_t secondOpen = line.find('[', firstClose);
    if (secondOpen == std::string::npos) {
        return false;
    }
    std::size_t secondClose = line.find(']', secondOpen);
    if (secondClose == std::string::npos) {
        return false;
    }
    levelOut = line.substr(secondOpen + 1, secondClose - secondOpen - 1);
    std::size_t msgStart = secondClose + 1;
    while (msgStart < line.size() && line[msgStart] == ' ') {
        ++msgStart;
    }
    messageOut = line.substr(msgStart);
    return true;
}

// 从日志消息提取归一化原因（聚合键），去除具体 host 等易变前缀，
// 使同一原因（不同 host）聚合为一类。
// 规则：消息以 "SKIP: " 开头且含 " - " 分隔符时，取分隔符之后部分
//   "SKIP: <host>[:port] - <reason>"  ->  "<reason>"
//   （如 "SKIP: 140.248.186.45:443 - invalid UUID format" -> "invalid UUID format"）；
// 其余消息保留原文（如 "Failed to parse vmess: not JSON"、"proxy timeout"）。
std::string extractReason(const std::string& message) {
    const std::string prefix = "SKIP: ";
    if (message.compare(0, prefix.size(), prefix) != 0) {
        return message;
    }
    std::size_t sep = message.find(" - ");
    if (sep == std::string::npos) {
        return message;
    }
    std::size_t reasonStart = sep + 3;
    while (reasonStart < message.size() && message[reasonStart] == ' ') {
        ++reasonStart;
    }
    std::size_t reasonEnd = message.size();
    while (reasonEnd > reasonStart &&
           (message[reasonEnd - 1] == ' ' || message[reasonEnd - 1] == '\r')) {
        --reasonEnd;
    }
    return message.substr(reasonStart, reasonEnd - reasonStart);
}

// 将聚合 map（按键字典序）转为按计数降序的 vector；
// std::stable_sort 保证同次数保持字典序。
std::vector<ReasonCount> toSortedReasons(const std::map<std::string, std::size_t>& agg) {
    std::vector<ReasonCount> reasons;
    reasons.reserve(agg.size());
    for (std::map<std::string, std::size_t>::const_iterator it = agg.begin(); it != agg.end(); ++it) {
        ReasonCount rc;
        rc.reason = it->first;
        rc.count = it->second;
        reasons.push_back(rc);
    }
    std::stable_sort(reasons.begin(), reasons.end(),
        [](const ReasonCount& lhs, const ReasonCount& rhs) {
            return lhs.count > rhs.count;
        });
    return reasons;
}

} // namespace

LogStatisticsResult parseLogFile(const std::string& filePath) {
    LogStatisticsResult result;

    std::ifstream file(filePath, std::ios::in);
    if (!file.is_open()) {
        return result; // fileOpened 保持 false
    }
    result.fileOpened = true;

    std::map<std::string, std::size_t> warnAgg;
    std::map<std::string, std::size_t> errorAgg;

    std::string line;
    while (std::getline(file, line)) {
        std::string level;
        std::string message;
        if (!extractLevelAndMessage(line, level, message)) {
            continue;
        }
        ++result.counts.total;
        if (level == "TRACE") {
            ++result.counts.trace;
        } else if (level == "DEBUG") {
            ++result.counts.debug;
        } else if (level == "INFO") {
            ++result.counts.info;
        } else if (level == "REPORT") {
            ++result.counts.report;
        } else if (level == "WARN") {
            ++result.counts.warn;
            ++warnAgg[extractReason(message)];
        } else if (level == "ERROR") {
            ++result.counts.error;
            ++errorAgg[extractReason(message)];
        }
        // 其他无法识别的级别 token 不计入任何计数
    }

    result.warnReasons = toSortedReasons(warnAgg);
    result.errorReasons = toSortedReasons(errorAgg);
    return result;
}
