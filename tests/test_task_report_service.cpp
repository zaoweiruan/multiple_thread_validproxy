// tests/test_task_report_service.cpp — task::TaskReportService 报告格式化(含统计节)+落盘+内存索引
// (task-report-hover Task 2; brief: TaskReportService 报告格式化+落盘+内存索引)
#include <gtest/gtest.h>
#include <filesystem>

#include "task/TaskReportService.h"
#include "test_utils.h"

namespace {

bool FileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

// Fixture providing a service bound to a fresh temp directory per test
// (TEST_F per project brief). dir_ is declared before svc_ so the directory
// exists before the service captures it.
class TaskReportServiceTest : public ::testing::Test {
protected:
    TempDir dir_;
    task::TaskReportService svc_{dir_.path()};
};

TEST_F(TaskReportServiceTest, FormatReportContainsMetadataStatsAndLogLines) {
    task::TaskHandle h; h.taskId = "t1"; h.type = task::TaskType::TEST_ALL;
    h.status = task::TaskStatus::COMPLETED; h.startedAt = "100"; h.completedAt = "1000";
    task::TaskStats st{20, 17, 2, 1};
    std::vector<task::LogLine> lines{{::LogLevel::INFO, "[2026-11-24 10:00:00] [INFO] batch done"}};
    std::string r = svc_.formatReport(h, lines, st);
    EXPECT_NE(r.find("taskId: t1"), std::string::npos);
    EXPECT_NE(r.find("TEST_ALL"), std::string::npos);
    EXPECT_NE(r.find("total: 20"), std::string::npos);
    EXPECT_NE(r.find("success: 17"), std::string::npos);
    EXPECT_NE(r.find("batch done"), std::string::npos);
}

TEST_F(TaskReportServiceTest, PublishWritesFileAndIndexes) {
    task::TaskHandle h; h.taskId = "t2"; h.status = task::TaskStatus::FAILED; h.error = "boom";
    task::TaskStats st{5, 3, 2, 0};
    std::string p = svc_.publish(h, {}, st);
    EXPECT_FALSE(p.empty());
    EXPECT_TRUE(FileExists(p));
    task::TaskReportInfo info;
    EXPECT_TRUE(svc_.find("t2", info));
    EXPECT_NE(info.content.find("boom"), std::string::npos);
    EXPECT_NE(info.content.find("failed: 2"), std::string::npos);
}

TEST_F(TaskReportServiceTest, EmptyLogLinesAndZeroStatsStillPublishes) {
    task::TaskHandle h; h.taskId = "t3"; h.status = task::TaskStatus::COMPLETED;
    std::string p = svc_.publish(h, {}, {});
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
    std::string p1 = svc_.publish(h, {}, {});
    EXPECT_FALSE(p1.empty());
    h.status = task::TaskStatus::FAILED; h.error = "second";
    std::string p2 = svc_.publish(h, {}, {});
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
    std::string p = svcBad.publish(h, {}, {});
    EXPECT_TRUE(p.empty());
    task::TaskReportInfo info;
    EXPECT_FALSE(svcBad.find("f1", info));   // 写失败不更新索引
}

} // namespace
