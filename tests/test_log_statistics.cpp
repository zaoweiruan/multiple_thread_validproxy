#include <gtest/gtest.h>
#include "LogStatistics.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string makeTempLogPath(const std::string& suffix) {
    std::filesystem::path dir = std::filesystem::temp_directory_path();
    std::string name = "logstat_test_" + suffix + ".log";
    return (dir / name).string();
}

bool writeLogFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }
    out << content;
    return out.good();
}

void removeFile(const std::string& path) {
    std::remove(path.c_str());
}

} // namespace

// 1) 各级别计数与 total（有效日志行数）
TEST(LogStatisticsTest, LevelCounts) {
    const std::string path = makeTempLogPath("levels");
    ASSERT_TRUE(writeLogFile(path,
        "[2026-08-12 10:00:00] [TRACE] t1\n"
        "[2026-08-12 10:00:01] [DEBUG] d1\n"
        "[2026-08-12 10:00:02] [INFO] i1\n"
        "[2026-08-12 10:00:03] [REPORT] r1\n"
        "[2026-08-12 10:00:04] [WARN] w1\n"
        "[2026-08-12 10:00:05] [ERROR] e1\n"
        "[2026-08-12 10:00:06] [INFO] i2\n"));

    LogStatisticsResult result = parseLogFile(path);

    EXPECT_TRUE(result.fileOpened);
    EXPECT_EQ(1u, result.counts.trace);
    EXPECT_EQ(1u, result.counts.debug);
    EXPECT_EQ(2u, result.counts.info);
    EXPECT_EQ(1u, result.counts.report);
    EXPECT_EQ(1u, result.counts.warn);
    EXPECT_EQ(1u, result.counts.error);
    EXPECT_EQ(7u, result.counts.total);

    removeFile(path);
}

// 2) WARN / ERROR 按消息原文聚合计数
TEST(LogStatisticsTest, WarnErrorAggregation) {
    const std::string path = makeTempLogPath("agg");
    ASSERT_TRUE(writeLogFile(path,
        "[2026-08-12 10:00:00] [WARN] proxy timeout\n"
        "[2026-08-12 10:00:01] [WARN] proxy timeout\n"
        "[2026-08-12 10:00:02] [WARN] dns fail\n"
        "[2026-08-12 10:00:03] [ERROR] connect refused\n"
        "[2026-08-12 10:00:04] [ERROR] connect refused\n"));

    LogStatisticsResult result = parseLogFile(path);

    ASSERT_EQ(2u, result.warnReasons.size());
    EXPECT_EQ("proxy timeout", result.warnReasons[0].reason);
    EXPECT_EQ(2u, result.warnReasons[0].count);
    EXPECT_EQ("dns fail", result.warnReasons[1].reason);
    EXPECT_EQ(1u, result.warnReasons[1].count);

    ASSERT_EQ(1u, result.errorReasons.size());
    EXPECT_EQ("connect refused", result.errorReasons[0].reason);
    EXPECT_EQ(2u, result.errorReasons[0].count);

    removeFile(path);
}

// 3) 畸形行（空行/无 '[' 开头/截断）不计入任何统计
TEST(LogStatisticsTest, MalformedLinesIgnored) {
    const std::string path = makeTempLogPath("malformed");
    ASSERT_TRUE(writeLogFile(path,
        "[2026-08-12 10:00:00] [INFO] ok\n"
        "not a log line\n"
        "\n"
        "[2026-08-12 10:00:01] [TRACE] t\n"
        "[truncated without level\n"
        "garbage] [INFO] not at line start\n"));

    LogStatisticsResult result = parseLogFile(path);

    EXPECT_EQ(2u, result.counts.total);
    EXPECT_EQ(1u, result.counts.info);
    EXPECT_EQ(1u, result.counts.trace);
    EXPECT_EQ(0u, result.counts.warn);
    EXPECT_EQ(0u, result.counts.error);

    removeFile(path);
}

// 4) 原因按次数降序；同次数保持字典序
TEST(LogStatisticsTest, ReasonsSortedByCountDesc) {
    const std::string path = makeTempLogPath("sort");
    ASSERT_TRUE(writeLogFile(path,
        "[2026-08-12 10:00:00] [WARN] aaa\n"
        "[2026-08-12 10:00:01] [WARN] bbb\n"
        "[2026-08-12 10:00:02] [WARN] bbb\n"
        "[2026-08-12 10:00:03] [WARN] bbb\n"
        "[2026-08-12 10:00:04] [WARN] ccc\n"
        "[2026-08-12 10:00:05] [WARN] ccc\n"
        "[2026-08-12 10:00:06] [WARN] ccc\n"
        "[2026-08-12 10:00:07] [WARN] ddd\n"
        "[2026-08-12 10:00:08] [WARN] ddd\n"));

    LogStatisticsResult result = parseLogFile(path);

    ASSERT_EQ(4u, result.warnReasons.size());
    EXPECT_EQ("bbb", result.warnReasons[0].reason);
    EXPECT_EQ(3u, result.warnReasons[0].count);
    EXPECT_EQ("ccc", result.warnReasons[1].reason);
    EXPECT_EQ(3u, result.warnReasons[1].count);
    EXPECT_EQ("ddd", result.warnReasons[2].reason);
    EXPECT_EQ(2u, result.warnReasons[2].count);
    EXPECT_EQ("aaa", result.warnReasons[3].reason);
    EXPECT_EQ(1u, result.warnReasons[3].count);

    removeFile(path);
}

// 5) 消息文本中出现的 [ERROR] 字样不误判为 ERROR 级别
TEST(LogStatisticsTest, BracketTextNotMisparsed) {
    const std::string path = makeTempLogPath("bracket");
    ASSERT_TRUE(writeLogFile(path,
        "[2026-08-12 10:00:00] [INFO] check [ERROR] inside text\n"
        "[2026-08-12 10:00:01] [INFO] plain\n"));

    LogStatisticsResult result = parseLogFile(path);

    EXPECT_EQ(2u, result.counts.info);
    EXPECT_EQ(0u, result.counts.error);
    EXPECT_EQ(2u, result.counts.total);
    EXPECT_TRUE(result.errorReasons.empty());

    removeFile(path);
}

// 6) 文件不存在/无法打开：fileOpened == false，其余字段零值
TEST(LogStatisticsTest, MissingFile) {
    std::filesystem::path dir = std::filesystem::temp_directory_path();
    std::string missing = (dir / "logstat_test_missing_does_not_exist.log").string();

    LogStatisticsResult result = parseLogFile(missing);

    EXPECT_FALSE(result.fileOpened);
    EXPECT_EQ(0u, result.counts.total);
    EXPECT_TRUE(result.warnReasons.empty());
    EXPECT_TRUE(result.errorReasons.empty());
}

// 7) 空文件：fileOpened == true，total == 0
TEST(LogStatisticsTest, EmptyFile) {
    const std::string path = makeTempLogPath("empty");
    ASSERT_TRUE(writeLogFile(path, ""));

    LogStatisticsResult result = parseLogFile(path);

    EXPECT_TRUE(result.fileOpened);
    EXPECT_EQ(0u, result.counts.total);
    EXPECT_EQ(0u, result.counts.warn);
    EXPECT_EQ(0u, result.counts.error);
    EXPECT_TRUE(result.warnReasons.empty());
    EXPECT_TRUE(result.errorReasons.empty());

    removeFile(path);
}

// 8) WARN 与 ERROR 各自独立聚合，消息相同也不混淆
TEST(LogStatisticsTest, ErrorReasonsSeparate) {
    const std::string path = makeTempLogPath("separate");
    ASSERT_TRUE(writeLogFile(path,
        "[2026-08-12 10:00:00] [WARN] shared msg\n"
        "[2026-08-12 10:00:01] [ERROR] shared msg\n"
        "[2026-08-12 10:00:02] [WARN] shared msg\n"));

    LogStatisticsResult result = parseLogFile(path);

    ASSERT_EQ(1u, result.warnReasons.size());
    EXPECT_EQ("shared msg", result.warnReasons[0].reason);
    EXPECT_EQ(2u, result.warnReasons[0].count);

    ASSERT_EQ(1u, result.errorReasons.size());
    EXPECT_EQ("shared msg", result.errorReasons[0].reason);
    EXPECT_EQ(1u, result.errorReasons[0].count);

    EXPECT_EQ(2u, result.counts.warn);
    EXPECT_EQ(1u, result.counts.error);

    removeFile(path);
}

// 9) SKIP: <host> - <reason> 归一化聚合：不同 host 同原因合并为一类，reason 不含 host
TEST(LogStatisticsTest, SkipReasonNormalization) {
    const std::string path = makeTempLogPath("skip_norm");
    ASSERT_TRUE(writeLogFile(path,
        "[2026-08-12 10:00:00] [WARN] SKIP: 140.248.186.45:443 - invalid UUID format\n"
        "[2026-08-12 10:00:01] [WARN] SKIP: www.speedtest.net:443 - invalid UUID format\n"
        "[2026-08-12 10:00:02] [WARN] SKIP: 188.114.97.6:443 - invalid UUID format\n"));

    LogStatisticsResult result = parseLogFile(path);

    ASSERT_EQ(1u, result.warnReasons.size());
    EXPECT_EQ("invalid UUID format", result.warnReasons[0].reason);
    EXPECT_EQ(3u, result.warnReasons[0].count);
    EXPECT_EQ(3u, result.counts.warn);

    removeFile(path);
}

// 10) 归一化仅作用于 "SKIP: <host> - <reason>" 形态：
//     无分隔符的 SKIP 与普通消息均保留原文；ERROR 侧同样归一化
TEST(LogStatisticsTest, ReasonNormalizationBoundaries) {
    const std::string path = makeTempLogPath("skip_bound");
    ASSERT_TRUE(writeLogFile(path,
        "[2026-08-12 10:00:00] [WARN] SKIP: host-only.no-sep\n"
        "[2026-08-12 10:00:01] [WARN] Failed to parse vmess: not JSON\n"
        "[2026-08-12 10:00:02] [ERROR] SKIP: 1.2.3.4:443 - unsupported SS cipher: 'aes-256-cfb'\n"
        "[2026-08-12 10:00:03] [ERROR] SKIP: 5.6.7.8:443 - unsupported SS cipher: 'aes-256-cfb'\n"));

    LogStatisticsResult result = parseLogFile(path);

    ASSERT_EQ(2u, result.warnReasons.size());
    EXPECT_EQ("Failed to parse vmess: not JSON", result.warnReasons[0].reason);
    EXPECT_EQ(1u, result.warnReasons[0].count);
    EXPECT_EQ("SKIP: host-only.no-sep", result.warnReasons[1].reason);
    EXPECT_EQ(1u, result.warnReasons[1].count);

    ASSERT_EQ(1u, result.errorReasons.size());
    EXPECT_EQ("unsupported SS cipher: 'aes-256-cfb'", result.errorReasons[0].reason);
    EXPECT_EQ(2u, result.errorReasons[0].count);

    removeFile(path);
}
