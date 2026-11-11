// src/ui/RefreshCommands.cpp
#include "RefreshCommands.h"

#include <string>
#include <string_view>

namespace ui {
namespace refresh_cmd {

std::string statusTextFor(std::string_view command) {
    if (command == kRowsChanged) {
        return "数据已更新，代理列表已刷新";
    }
    if (command == kResultsChanged) {
        return "测试结果已更新";
    }
    if (command == kRegionResolveDone) {
        return "地区解析完成，代理列表已刷新";
    }
    if (command == kDedupOk) {
        // 2026-11-11: DEDUP_OK 去模态框（评审建议 #8 的 P1 落地），
        // 成功反馈改状态栏文案；kDedupFail 仍留空（失败走模态框 + ERR）。
        return "去重完成，代理列表已刷新";
    }
    // kDedupFail / kStatusResolved / 未知命令 → 空串
    return "";
}

}  // namespace refresh_cmd
}  // namespace ui
