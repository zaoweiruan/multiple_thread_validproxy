// test_task_chain_text.cpp
//
// 回归（2026-11-20）：配置对话框「自动任务」页的「任务链」显示乱码。
//
// 根因不是编码，而是对象生命周期：旧实现
//   static const wchar_t* stepDisplayName(const std::string& step) {
//       return i18n::wx(...);   // 临时 wxString 的缓冲区，语句结束即释放
//   }
// 返回悬垂指针，属性网格读到堆垃圾（随机字节 → 乱码）。
//
// 修复后组合逻辑集中在 ui::taskChainDisplayText()，按值返回 wxString。
// 本测试固定中英文完整文本，并确认返回的字符串独立于任何临时对象。
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "TaskChainText.h"
#include "i18n/I18n.h"

namespace {

std::vector<std::string> fullChain() {
    std::vector<std::string> steps;
    steps.push_back("update_all");
    steps.push_back("test_all");
    steps.push_back("dedup");
    steps.push_back("sync");
    steps.push_back("export");
    steps.push_back("resolve_region");
    return steps;
}

}  // namespace

TEST(TaskChainTextTest, ChineseChainMatchesStringTable) {
    i18n::setLanguage(i18n::Language::ZhCN);
    const wxString text = ui::taskChainDisplayText(fullChain());
    EXPECT_EQ(std::wstring(L"\u5168\u90e8\u66f4\u65b0 \u2192 \u5168\u90e8\u6d4b\u8bd5 "
                           L"\u2192 \u6e05\u6d17 \u2192 \u540c\u6b65 "
                           L"\u2192 \u5bfc\u51fa \u2192 \u89e3\u6790\u5730\u533a"),
              std::wstring(text.wc_str()));
}

TEST(TaskChainTextTest, EnglishChainMatchesStringTable) {
    i18n::setLanguage(i18n::Language::EnUS);
    const wxString text = ui::taskChainDisplayText(fullChain());
    EXPECT_EQ(std::wstring(L"Update all \u2192 Test all \u2192 Cleanup \u2192 Sync "
                           L"\u2192 Export \u2192 Resolve regions"),
              std::wstring(text.wc_str()));
    i18n::setLanguage(i18n::Language::ZhCN);
}

TEST(TaskChainTextTest, EmptyAndUnknownSteps) {
    i18n::setLanguage(i18n::Language::ZhCN);
    EXPECT_TRUE(ui::taskChainDisplayText(std::vector<std::string>()).empty());
    EXPECT_TRUE(ui::taskChainStepName("no_such_step").empty());
    // 未知步骤不发散：保留箭头但名称为空。
    std::vector<std::string> one;
    one.push_back("no_such_step");
    EXPECT_TRUE(ui::taskChainDisplayText(one).empty());
}

// 值语义：返回的 wxString 拥有自己的缓冲区，重复调用结果稳定。
// 旧实现每次读悬垂指针，重复调用会得到不同的垃圾字节。
TEST(TaskChainTextTest, ResultOwnsItsBufferAndIsStable) {
    i18n::setLanguage(i18n::Language::ZhCN);
    wxString first = ui::taskChainDisplayText(fullChain());
    const wxString copy(first);
    for (int i = 0; i < 200; ++i) {
        const wxString again = ui::taskChainDisplayText(fullChain());
        ASSERT_EQ(copy, again) << "iteration " << i;
    }
    ASSERT_EQ(copy, first);
}
