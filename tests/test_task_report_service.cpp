// tests/test_task_report_service.cpp — task::TaskReportService 报告格式化(含统计节+updateMethods)+落盘+内存索引
// (task-report-format Task 2; brief: 报告格式 v2 —— 墙钟时间/durationMinutes/去日志摘录/updateMethods 字段)
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <string>

#include "task/TaskReportService.h"
#include "task/TaskType.h"
#include "test_utils.h"

namespace {

bool FileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

// A steady-clock stamp `seconds` in the past, rendered as the decimal tick
// count the queue stores in TaskHandle.createdAt/startedAt/completedAt. Used to
// build deterministic pairs whose wall-clock and durationMinutes are valid.
std::string stampSecondsAgo(long long seconds) {
    const long long ticksPerSecond =
        static_cast<long long>(std::chrono::steady_clock::period::den) /
        static_cast<long long>(std::chrono::steady_clock::period::num);
    const long long nowTicks =
        static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count());
    return std::to_string(nowTicks - seconds * ticksPerSecond);
}

// True when `v` (the field value, already trimmed to the wall-clock string)
// looks like a local "yyyy-mm-dd hh:mm:ss" (19 chars with the fixed
// separators). The exact instant is non-deterministic (formatReport captures
// now() internally); only the FORMAT is asserted here (spec D1: 测试只断言
// 格式合法/省略，不断言精确时刻).
bool LooksLikeWallClock(const std::string& v) {
    if (v.size() != 19u) return false;
    if (v[4] != '-' || v[7] != '-' || v[10] != ' ' || v[13] != ':' || v[16] != ':') return false;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16) continue;
        if (v[i] < '0' || v[i] > '9') return false;
    }
    return true;
}

// Returns the full line that STARTS with `prefix` (empty when none). Used to
// assert the horizontal field layout (two or more fields sharing one line).
std::string findLineStartingWith(const std::string& report, const std::string& prefix) {
    std::size_t pos = 0;
    while (pos <= report.size()) {
        std::size_t end = report.find('\n', pos);
        if (end == std::string::npos) end = report.size();
        if (report.compare(pos, prefix.size(), prefix) == 0) {
            return report.substr(pos, end - pos);
        }
        if (end == report.size()) break;
        pos = end + 1;
    }
    return "";
}

// Fixture providing a service bound to a fresh temp directory per test
// (TEST_F per project brief). dir_ is declared before svc_ so the directory
// exists before the service captures it.
class TaskReportServiceTest : public ::testing::Test {
protected:
    TempDir dir_;
    task::TaskReportService svc_{dir_.path()};
};

TEST_F(TaskReportServiceTest, FormatReportContainsMetadataStatsAndNoLogExcerpt) {
    task::TaskHandle h; h.taskId = "t1"; h.type = task::TaskType::TEST_ALL;
    h.status = task::TaskStatus::COMPLETED;
    h.startedAt = stampSecondsAgo(3600);
    h.completedAt = stampSecondsAgo(1800);
    task::TaskStats st{20, 17, 2, 1};
    std::string r = svc_.formatReport(h, st, "");
    EXPECT_NE(r.find("taskId: t1"), std::string::npos);
    EXPECT_NE(r.find("TEST_ALL"), std::string::npos);
    EXPECT_NE(r.find("total: 20"), std::string::npos);
    EXPECT_NE(r.find("success: 17"), std::string::npos);
    // durationMinutes: (3600-1800)s = 30 whole minutes.
    EXPECT_NE(r.find("durationMinutes: 30"), std::string::npos);
    // startedAt is rendered as a wall-clock date (valid format, not raw ticks).
    const std::size_t startPos = r.find("startedAt: ");
    ASSERT_NE(startPos, std::string::npos);
    const std::string startedValue = r.substr(startPos + std::string("startedAt: ").size(), 19);
    EXPECT_TRUE(LooksLikeWallClock(startedValue)) << "startedAt value: '" << startedValue << "'";
    // The log-excerpt section is gone (report format v2).
    EXPECT_EQ(r.find("---- log excerpt ----"), std::string::npos);
}

TEST_F(TaskReportServiceTest, FormatReportOmitsUpdateMethodsWhenEmpty) {
    task::TaskHandle h; h.taskId = "t4"; h.status = task::TaskStatus::COMPLETED; h.parentTaskId = "p1";
    task::TaskStats st{0, 0, 0, 0};
    std::string r = svc_.formatReport(h, st, "");
    EXPECT_EQ(r.find("updateMethods:"), std::string::npos);
}

TEST_F(TaskReportServiceTest, FormatReportIncludesUpdateMethodsAfterParentBeforeStats) {
    task::TaskHandle h; h.taskId = "t5"; h.status = task::TaskStatus::COMPLETED; h.parentTaskId = "p1";
    task::TaskStats st{0, 0, 0, 0};
    std::string r = svc_.formatReport(h, st, "accelerator, proxy");
    const std::size_t umPos = r.find("updateMethods: accelerator, proxy");
    EXPECT_NE(umPos, std::string::npos);
    const std::size_t parentPos = r.find("parentTaskId: p1");
    const std::size_t statsPos = r.find("---- statistics ----");
    ASSERT_NE(parentPos, std::string::npos);
    ASSERT_NE(statsPos, std::string::npos);
    EXPECT_GT(umPos, parentPos);
    EXPECT_LT(umPos, statsPos);
}

TEST_F(TaskReportServiceTest, FormatReportUsesHorizontalFieldLayout) {
    task::TaskHandle h; h.taskId = "t6"; h.type = task::TaskType::TEST_SUBSCRIPTION;
    h.status = task::TaskStatus::COMPLETED; h.targetId = "target-1";
    h.createdAt = stampSecondsAgo(3700);
    h.startedAt = stampSecondsAgo(3600);
    h.completedAt = stampSecondsAgo(1800);
    task::TaskStats st{3, 2, 1, 0};
    const std::string r = svc_.formatReport(h, st, "");

    // type + targetId share one line.
    const std::string typeLine = findLineStartingWith(r, "type: ");
    EXPECT_NE(typeLine.find("TEST_SUBSCRIPTION"), std::string::npos) << typeLine;
    EXPECT_NE(typeLine.find("targetId: target-1"), std::string::npos) << typeLine;
    // status + durationMinutes share one line (3600-1800s = 30 min).
    const std::string statusLine = findLineStartingWith(r, "status: ");
    EXPECT_NE(statusLine.find("status: COMPLETED"), std::string::npos) << statusLine;
    EXPECT_NE(statusLine.find("durationMinutes: 30"), std::string::npos) << statusLine;
    // createdAt/startedAt/completedAt share one line.
    const std::string timesLine = findLineStartingWith(r, "createdAt: ");
    EXPECT_NE(timesLine.find("createdAt: "), std::string::npos) << timesLine;
    EXPECT_NE(timesLine.find("startedAt: "), std::string::npos) << timesLine;
    EXPECT_NE(timesLine.find("completedAt: "), std::string::npos) << timesLine;
    // The old per-field lines are gone.
    EXPECT_TRUE(findLineStartingWith(r, "targetId: ").empty());
    EXPECT_TRUE(findLineStartingWith(r, "startedAt: ").empty());
    EXPECT_TRUE(findLineStartingWith(r, "completedAt: ").empty());
    // Statistics collapse onto one pipe-separated line.
    EXPECT_EQ(findLineStartingWith(r, "total: "),
              "total: 3 | success: 2 | failed: 1 | skipped: 0");
}

TEST_F(TaskReportServiceTest, FormatReportListsFailedSubscriptions) {
    task::TaskHandle h; h.taskId = "t7"; h.type = task::TaskType::UPDATE_ALL;
    h.status = task::TaskStatus::COMPLETED;
    task::TaskStats st{3, 1, 2, 0};
    st.failures.push_back(task::TaskFailure{"订阅甲", "sub-1"});
    st.failures.push_back(task::TaskFailure{"", "sub-2"});
    const std::string r = svc_.formatReport(h, st, "");

    const std::size_t header = r.find("---- failed subscriptions ----");
    ASSERT_NE(header, std::string::npos);
    // Section comes after the statistics section.
    EXPECT_GT(header, r.find("---- statistics ----"));
    EXPECT_NE(r.find("订阅甲 (sub-1)"), std::string::npos);
    // Empty name falls back to the bare ID.
    EXPECT_NE(r.find("sub-2"), std::string::npos);
    EXPECT_EQ(r.find(" (sub-2)"), std::string::npos);
}

TEST_F(TaskReportServiceTest, FormatReportOmitsFailedSectionWhenNoFailures) {
    task::TaskHandle h; h.taskId = "t8"; h.status = task::TaskStatus::COMPLETED;
    const std::string r = svc_.formatReport(h, task::TaskStats{}, "");
    EXPECT_EQ(r.find("---- failed subscriptions ----"), std::string::npos);
}

TEST_F(TaskReportServiceTest, PublishWritesFileAndIndexes) {
    task::TaskHandle h; h.taskId = "t2"; h.status = task::TaskStatus::FAILED; h.error = "boom";
    task::TaskStats st{5, 3, 2, 0};
    std::string p = svc_.publish(h, st, "");
    EXPECT_FALSE(p.empty());
    EXPECT_TRUE(FileExists(p));
    task::TaskReportInfo info;
    EXPECT_TRUE(svc_.find("t2", info));
    EXPECT_NE(info.content.find("boom"), std::string::npos);
    EXPECT_NE(info.content.find("failed: 2"), std::string::npos);
}

TEST_F(TaskReportServiceTest, EmptyStatsStillPublishes) {
    task::TaskHandle h; h.taskId = "t3"; h.status = task::TaskStatus::COMPLETED;
    std::string p = svc_.publish(h, {}, "");
    EXPECT_FALSE(p.empty());
    task::TaskReportInfo info;
    EXPECT_TRUE(svc_.find("t3", info));
}

TEST_F(TaskReportServiceTest, MissingTaskIdFindReturnsFalseWithoutInserting) {
    task::TaskReportInfo info;
    EXPECT_FALSE(svc_.find("nope", info));   // 缺键：false，不插空条目
    EXPECT_FALSE(svc_.find("nope", info));   // 再次：仍 false（索引未被污染）
}

TEST_F(TaskReportServiceTest, PublishOverwritesSameTaskId) {
    task::TaskHandle h; h.taskId = "dup"; h.status = task::TaskStatus::COMPLETED;
    std::string p1 = svc_.publish(h, {}, "");
    EXPECT_FALSE(p1.empty());
    h.status = task::TaskStatus::FAILED; h.error = "second";
    std::string p2 = svc_.publish(h, {}, "");
    EXPECT_EQ(p1, p2);                        // 同一路径覆盖，不产生重复文件
    task::TaskReportInfo info;
    EXPECT_TRUE(svc_.find("dup", info));
    EXPECT_NE(info.content.find("second"), std::string::npos);
}

TEST_F(TaskReportServiceTest, PublishWriteFailureReturnsEmptyAndNoIndex) {
    const std::string badDir = (std::filesystem::temp_directory_path() / "vp_no_such_dir_zz").string();
    std::error_code ec;
    std::filesystem::remove_all(badDir, ec);  // 保证不存在（可移植：非固定 Z: 盘）
    task::TaskReportService svcBad(badDir);
    task::TaskHandle h; h.taskId = "f1"; h.status = task::TaskStatus::COMPLETED;
    std::string p = svcBad.publish(h, {}, "");
    EXPECT_TRUE(p.empty());
    task::TaskReportInfo info;
    EXPECT_FALSE(svcBad.find("f1", info));   // 写失败不更新索引
}

} // namespace
