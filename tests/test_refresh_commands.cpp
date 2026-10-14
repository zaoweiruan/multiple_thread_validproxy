// tests/test_refresh_commands.cpp
#include <gtest/gtest.h>

#include "RefreshCommands.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::string_view> AllCommands() {
    return {
        ui::refresh_cmd::kStatusResolved,
        ui::refresh_cmd::kRegionResolveDone,
        ui::refresh_cmd::kDedupOk,
        ui::refresh_cmd::kDedupFail,
        ui::refresh_cmd::kRowsChanged,
        ui::refresh_cmd::kResultsChanged,
    };
}

}  // namespace

TEST(RefreshCommandsTest, AllCommandsAreNonEmpty) {
    for (std::string_view cmd : AllCommands()) {
        EXPECT_FALSE(cmd.empty()) << "空命令常量";
    }
}

TEST(RefreshCommandsTest, AllCommandsAreUnique) {
    std::vector<std::string_view> cmds = AllCommands();
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        for (std::size_t j = i + 1; j < cmds.size(); ++j) {
            EXPECT_NE(cmds[i], cmds[j])
                << "命令字面量重复: " << cmds[i];
        }
    }
}

TEST(RefreshCommandsTest, RowsChangedMapsToStatusText) {
    EXPECT_EQ("数据已更新，代理列表已刷新",
              ui::refresh_cmd::statusTextFor(ui::refresh_cmd::kRowsChanged));
}

TEST(RefreshCommandsTest, ResultsChangedMapsToStatusText) {
    EXPECT_EQ("测试结果已更新",
              ui::refresh_cmd::statusTextFor(ui::refresh_cmd::kResultsChanged));
}

TEST(RefreshCommandsTest, RegionResolveDoneMapsToStatusText) {
    EXPECT_EQ("地区解析完成，代理列表已刷新",
              ui::refresh_cmd::statusTextFor(ui::refresh_cmd::kRegionResolveDone));
}

TEST(RefreshCommandsTest, DedupOkMapsToStatusText) {
    // P0 期间 kDedupOk 映射留空（评审建议 #8），P1 去模态框时再启用
    EXPECT_TRUE(ui::refresh_cmd::statusTextFor(ui::refresh_cmd::kDedupOk).empty());
}

TEST(RefreshCommandsTest, DedupFailHasNoStatusText) {
    // 失败路径走模态框 + ERR 日志，不写状态栏成功文案，避免误导
    EXPECT_TRUE(ui::refresh_cmd::statusTextFor(ui::refresh_cmd::kDedupFail).empty());
}

TEST(RefreshCommandsTest, UnknownCommandReturnsEmpty) {
    EXPECT_TRUE(ui::refresh_cmd::statusTextFor("SOMETHING_ELSE").empty());
}

TEST(RefreshCommandsTest, ExistingCommandsUnchanged) {
    // 既有路径依赖这些字符串值，改动会破坏已实现行为
    EXPECT_EQ(std::string_view("DEDUP_OK"), ui::refresh_cmd::kDedupOk);
    EXPECT_EQ(std::string_view("DEDUP_FAIL"), ui::refresh_cmd::kDedupFail);
    EXPECT_EQ(std::string_view("REGION_RESOLVE_DONE"),
              ui::refresh_cmd::kRegionResolveDone);
}
