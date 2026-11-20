#ifndef UI_TASK_CHAIN_TEXT_H
#define UI_TASK_CHAIN_TEXT_H

#include <string>
#include <vector>

#include <wx/string.h>

// 自动任务步骤链的显示文本。
//
// 2026-11-20 根因记录：旧实现在 ConfigDialog.cpp 内联为
//   static const wchar_t* stepDisplayName(const std::string& step) {
//       return i18n::wx(i18n::StrId::ConfigDialog_UpdateAll);
//   }
// 返回的是**临时 wxString 的内部缓冲区**，完整表达式结束即销毁 —— 悬垂指针。
// 属性网格随后读到的是堆上的垃圾字节，界面上的「任务链」因此显示乱码
// （不是编码问题，而是对象生命周期问题）。
//
// 组合逻辑集中到本文件，并一律按值返回 wxString，从接口上消除同类错误。
namespace ui {

// 单个步骤的本地化名称；未知步骤返回空串。
wxString taskChainStepName(const std::string& step);

// 以 " → " 连接整条链；空链返回空串。
wxString taskChainDisplayText(const std::vector<std::string>& steps);

}  // namespace ui

#endif  // UI_TASK_CHAIN_TEXT_H
