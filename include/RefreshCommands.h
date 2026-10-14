// include/RefreshCommands.h
#pragma once

#include <string>
#include <string_view>

// 数据变更后的刷新命令协议。
// 既有命令（DEDUP_OK / DEDUP_FAIL / REGION_RESOLVE_DONE / RESOLVE_REGION_START）
// 的字符串值保持不变，避免破坏已实现路径。
//
// 约定：所有刷新命令以 "DATA_" 前缀开头（新增命令）或为已知既有常量。
// 已枚举全仓 43+ 个 StatusUpdateEvent 载荷，无一项以 "DATA_" 开头，
// 因此 DATA_ 前缀可作为「未识别即协议漂移」的早警依据。
namespace ui {
namespace refresh_cmd {

// ── 既有命令 ─────────────────────────────────────────────
inline constexpr std::string_view kStatusResolved    = "RESOLVE_REGION_START";
inline constexpr std::string_view kRegionResolveDone = "REGION_RESOLVE_DONE";
inline constexpr std::string_view kDedupOk           = "DEDUP_OK";
inline constexpr std::string_view kDedupFail         = "DEDUP_FAIL";

// ── 新增命令（P0）───────────────────────────────────────
// ProfileItem 变更（Region / SubId / remarks）→ 全量重载 proxies_
inline constexpr std::string_view kRowsChanged    = "DATA_ROWS_CHANGED";
// ProfileExItem 变更（Delay / Failures / Runtime / Health）→ 全量重载 exItems_
inline constexpr std::string_view kResultsChanged = "DATA_RESULTS_CHANGED";

// 订阅面刷新复用既有 SubscriptionRefreshEvent（专门事件），
// 不再新增字符串命令 —— 见规格 §11.4。

// 命令 → 状态栏人读文案。未命中返回空串（调用方据此跳过状态栏写入）。
// P0 期间 kDedupOk 映射留空（评审建议 #8），P1 去模态框时再启用。
std::string statusTextFor(std::string_view command);

}  // namespace refresh_cmd
}  // namespace ui
