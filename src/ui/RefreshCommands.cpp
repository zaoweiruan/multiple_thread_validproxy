// src/ui/RefreshCommands.cpp
#include "RefreshCommands.h"
#include "i18n/I18n.h"

#include <string>
#include <string_view>

namespace ui {
namespace refresh_cmd {

std::string statusTextFor(std::string_view command) {
    if (command == kRowsChanged) {
        return i18n::u8(i18n::StrId::Refresh_RowsChanged);
    }
    if (command == kResultsChanged) {
        return i18n::u8(i18n::StrId::Refresh_ResultsChanged);
    }
    if (command == kRegionResolveDone) {
        return i18n::u8(i18n::StrId::Refresh_RegionResolved);
    }
    if (command == kDedupOk) {
        // 2026-11-11: DEDUP_OK 去模态框（评审建议 #8 的 P1 落地），
        // 成功反馈改状态栏文案；kDedupFail 仍留空（失败走模态框 + ERR）。
        return i18n::u8(i18n::StrId::Refresh_DedupDone);
    }
    // kDedupFail / kStatusResolved / 未知命令 → 空串
    return "";
}

}  // namespace refresh_cmd
}  // namespace ui
