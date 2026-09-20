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
    // kDedupOk / kDedupFail / kStatusResolved / 未知命令 → 空串
    return "";
}

}  // namespace refresh_cmd
}  // namespace ui
