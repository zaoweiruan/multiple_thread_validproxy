// ---------------------------------------------------------------------------
// test_autotask_state_path.cpp
//
// 回归：自动任务断点文件路径必须是可移植的（2026-11-18 现场问题）
//   `bin/config.json` 的 `auto_task.state_file` 存的是上一台机器的绝对路径，
//   拷到别的电脑后 `E:\...` 不存在 → 每个步骤刷
//   `[ERROR] AutoTask: failed to write state file: E:\...`。
//
// 约定（见 include/AutoTaskStatePath.h）：
//   空串 → <exe 目录>/worker/autotask_state.json
//   相对路径 → 以 exe 目录为基准
//   绝对路径（盘符/UNC/根相对）→ 原样
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "AutoTaskStatePath.h"

namespace {

class AutoTaskStatePathTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        base_ = std::filesystem::temp_directory_path() / "validproxy_autotask_path_test";
        std::error_code ec;
        std::filesystem::create_directories(base_, ec);
    }

    static void TearDownTestSuite() {
        std::error_code ec;
        std::filesystem::remove_all(base_, ec);
    }

    static std::filesystem::path base_;
};

std::filesystem::path AutoTaskStatePathTest::base_;

}  // namespace

TEST_F(AutoTaskStatePathTest, EmptyConfigResolvesToDefaultNextToExe) {
    EXPECT_EQ(autotask::resolveStateFilePath("", "D:/apps/validproxy"),
              "D:/apps/validproxy/worker/autotask_state.json");
}

TEST_F(AutoTaskStatePathTest, WhitespaceOnlyConfigResolvesToDefault) {
    EXPECT_EQ(autotask::resolveStateFilePath("   \t ", "D:/apps/validproxy"),
              "D:/apps/validproxy/worker/autotask_state.json");
}

TEST_F(AutoTaskStatePathTest, RelativePathResolvesAgainstExeDir) {
    EXPECT_EQ(autotask::resolveStateFilePath("worker/state.json", "D:/apps/validproxy"),
              "D:/apps/validproxy/worker/state.json");
    EXPECT_EQ(autotask::resolveStateFilePath("state.json", "D:/apps/validproxy/"),
              "D:/apps/validproxy/state.json");
}

TEST_F(AutoTaskStatePathTest, DriveAbsolutePathIsKeptVerbatim) {
    const std::string configured = "E:\\eclipse_workspace\\multiple_thread_validproxy\\bin/worker/autotask_state.json";
    EXPECT_EQ(autotask::resolveStateFilePath(configured, "D:/apps/validproxy"), configured);
}

TEST_F(AutoTaskStatePathTest, UncAndRootedPathsAreAbsolute) {
    EXPECT_TRUE(autotask::isAbsolutePath("\\\\server\\share\\state.json"));
    EXPECT_TRUE(autotask::isAbsolutePath("C:/tmp/state.json"));
    EXPECT_TRUE(autotask::isAbsolutePath("\\rooted\\state.json"));
    EXPECT_FALSE(autotask::isAbsolutePath("worker/state.json"));
    EXPECT_FALSE(autotask::isAbsolutePath("./state.json"));
}

TEST_F(AutoTaskStatePathTest, TrailingSeparatorsOnExeDirAreNormalized) {
    EXPECT_EQ(autotask::defaultStateFilePath("D:/apps/validproxy///"),
              "D:/apps/validproxy/worker/autotask_state.json");
    EXPECT_EQ(autotask::resolveStateFilePath("worker/x.json", "D:/apps/validproxy\\"),
              "D:/apps/validproxy/worker/x.json");
}

TEST_F(AutoTaskStatePathTest, MissingParentDirectoryIsCreated) {
    const std::filesystem::path target = base_ / "nested" / "deeper" / "state.json";
    EXPECT_TRUE(autotask::ensureParentDirectory(target.string()));
    EXPECT_TRUE(std::filesystem::is_directory(target.parent_path()));
}

TEST_F(AutoTaskStatePathTest, ParentThatIsAFileIsRejected) {
    const std::filesystem::path blocker = base_ / "blocker.txt";
    {
        std::ofstream out(blocker, std::ios::binary | std::ios::trunc);
        out << "not a directory";
    }
    const std::filesystem::path target = blocker / "state.json";
    EXPECT_FALSE(autotask::ensureParentDirectory(target.string()));
}

TEST_F(AutoTaskStatePathTest, EmptyPathIsRejected) {
    EXPECT_FALSE(autotask::ensureParentDirectory(""));
}
