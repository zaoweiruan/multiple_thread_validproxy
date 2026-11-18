// tests/test_i18n.cpp — 双语言字符串表与语言解析
// （Spec 2026-11-18-Spec-BilingualUI-v1.2 §7.1 / §7.2 / §7.3 / §10.1）
#include <gtest/gtest.h>

#include <cstddef>
#include <string>

#include "i18n/I18n.h"
#include "i18n/Language.h"
#include "i18n/StringId.h"

namespace {

class I18nTest : public ::testing::Test {
protected:
    void SetUp() override {
        i18n::setLanguage(i18n::Language::ZhCN);
    }

    void TearDown() override {
        i18n::setLanguage(i18n::Language::ZhCN);
    }

    static std::string narrow(i18n::StrId id) {
        return std::string(i18n::u8(id));
    }

    static std::wstring wide(i18n::StrId id) {
        return std::wstring(i18n::w(id));
    }
};

// ---------------------------------------------------------------- 表完整性

TEST_F(I18nTest, EveryEntryHasBothLanguages) {
    for (int index = 0; index < i18n::kStrIdCount; ++index) {
        const i18n::StrId id = static_cast<i18n::StrId>(index);

        i18n::setLanguage(i18n::Language::ZhCN);
        EXPECT_FALSE(narrow(id).empty()) << "zh u8 empty at index " << index;
        EXPECT_FALSE(wide(id).empty()) << "zh w empty at index " << index;

        i18n::setLanguage(i18n::Language::EnUS);
        EXPECT_FALSE(narrow(id).empty()) << "en u8 empty at index " << index;
        EXPECT_FALSE(wide(id).empty()) << "en w empty at index " << index;
    }
}

// ---------------------------------------------------------------- 已知文案

TEST_F(I18nTest, ChineseMenuTitlesMatchCurrentUi) {
    // FR-7：中文模式必须与当前版本逐字一致。
    EXPECT_EQ(wide(i18n::StrId::MainFrame_Menu_File), L"&文件");
    EXPECT_EQ(wide(i18n::StrId::MainFrame_Menu_Proxy), L"&代理");
    EXPECT_EQ(wide(i18n::StrId::MainFrame_Menu_Task), L"&任务");
    EXPECT_EQ(wide(i18n::StrId::MainFrame_Menu_Settings), L"&设置");
    EXPECT_EQ(wide(i18n::StrId::MainFrame_Menu_Help), L"&帮助");
    EXPECT_EQ(wide(i18n::StrId::MainFrame_Menu_Language), L"语言 / Language");
}

TEST_F(I18nTest, EnglishMenuTitles) {
    i18n::setLanguage(i18n::Language::EnUS);
    EXPECT_EQ(wide(i18n::StrId::MainFrame_Menu_File), L"&File");
    EXPECT_EQ(wide(i18n::StrId::MainFrame_Menu_Proxy), L"&Proxy");
    EXPECT_EQ(wide(i18n::StrId::MainFrame_Menu_Task), L"&Task");
    EXPECT_EQ(wide(i18n::StrId::MainFrame_Menu_Settings), L"&Settings");
    EXPECT_EQ(wide(i18n::StrId::MainFrame_Menu_Help), L"&Help");
    EXPECT_EQ(wide(i18n::StrId::MainFrame_Menu_Language), L"Language");
}

TEST_F(I18nTest, LanguageItemsAreFixedInBothModes) {
    // 语言项自身不翻译：两个模式下都是 "中文 (Chinese)" / "English (English)"，
    // 否则默认中文界面里的英文用户连可点的入口都认不出来（D4）。
    const std::wstring langZh = wide(i18n::StrId::MainFrame_Menu_LangZh);
    const std::wstring langEn = wide(i18n::StrId::MainFrame_Menu_LangEn);
    EXPECT_EQ(langZh, std::wstring(L"中文 (Chinese)"));
    EXPECT_EQ(langEn, std::wstring(L"English (English)"));

    i18n::setLanguage(i18n::Language::EnUS);
    EXPECT_EQ(langZh, wide(i18n::StrId::MainFrame_Menu_LangZh));
    EXPECT_EQ(langEn, wide(i18n::StrId::MainFrame_Menu_LangEn));
}

TEST_F(I18nTest, SubscriptionColumnNamesFollowD5) {
    // D5：名称/有效/代理数/更新/ID → Name/ValidProxies/TotalProxies/UpdatedTime/ID（保留 ↕）。
    EXPECT_EQ(wide(i18n::StrId::Subscription_Column_Enabled), L"启用");
    EXPECT_EQ(wide(i18n::StrId::Subscription_Column_Name), L"名称 ↕");
    EXPECT_EQ(wide(i18n::StrId::Subscription_Column_Valid), L"有效 ↕");
    EXPECT_EQ(wide(i18n::StrId::Subscription_Column_Total), L"代理数 ↕");
    EXPECT_EQ(wide(i18n::StrId::Subscription_Column_Updated), L"更新 ↕");
    EXPECT_EQ(wide(i18n::StrId::Subscription_Column_Id), L"ID ↕");

    i18n::setLanguage(i18n::Language::EnUS);
    EXPECT_EQ(wide(i18n::StrId::Subscription_Column_Enabled), L"On");
    EXPECT_EQ(wide(i18n::StrId::Subscription_Column_Name), L"Name ↕");
    EXPECT_EQ(wide(i18n::StrId::Subscription_Column_Valid), L"ValidProxies ↕");
    EXPECT_EQ(wide(i18n::StrId::Subscription_Column_Total), L"TotalProxies ↕");
    EXPECT_EQ(wide(i18n::StrId::Subscription_Column_Updated), L"UpdatedTime ↕");
    EXPECT_EQ(wide(i18n::StrId::Subscription_Column_Id), L"ID ↕");
}

TEST_F(I18nTest, ToolbarLabelsArePresent) {
    // 工具栏标签无法用 UIA 断言（wxAuiToolBar 是单个自绘窗口），只能靠表断言兜住。
    const i18n::StrId ids[] = {
        i18n::StrId::MainFrame_Toolbar_UpdateAll,
        i18n::StrId::MainFrame_Toolbar_Test,
        i18n::StrId::MainFrame_Toolbar_StopTest,
        i18n::StrId::MainFrame_Toolbar_Dedup,
        i18n::StrId::MainFrame_Toolbar_Region,
        i18n::StrId::MainFrame_Toolbar_Sync,
        i18n::StrId::MainFrame_Toolbar_Import,
        i18n::StrId::MainFrame_Toolbar_AutoTask,
        i18n::StrId::MainFrame_Toolbar_StandaloneMon,
        i18n::StrId::MainFrame_Toolbar_Config,
        i18n::StrId::MainFrame_Toolbar_DetailToggle,
    };
    for (std::size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        i18n::setLanguage(i18n::Language::ZhCN);
        const std::wstring zh = wide(ids[i]);
        i18n::setLanguage(i18n::Language::EnUS);
        const std::wstring en = wide(ids[i]);
        EXPECT_FALSE(zh.empty());
        EXPECT_FALSE(en.empty());
        EXPECT_NE(zh, en) << "toolbar label not translated at slot " << i;
    }
}

// ---------------------------------------------------------------- 加速键

TEST_F(I18nTest, AcceleratorsMatchAcrossLanguages) {
    // 加速键属于表值（不是拼接），两语言必须逐字一致，否则英文模式快捷键会变。
    for (int index = 0; index < i18n::kStrIdCount; ++index) {
        const i18n::StrId id = static_cast<i18n::StrId>(index);
        i18n::setLanguage(i18n::Language::ZhCN);
        const std::string zh = narrow(id);
        i18n::setLanguage(i18n::Language::EnUS);
        const std::string en = narrow(id);

        const std::size_t zhTab = zh.find('\t');
        const std::size_t enTab = en.find('\t');
        EXPECT_EQ(zhTab == std::string::npos, enTab == std::string::npos)
            << "tab marker differs at index " << index;
        if (zhTab != std::string::npos && enTab != std::string::npos) {
            EXPECT_EQ(zh.substr(zhTab), en.substr(enTab)) << "accelerator differs at index " << index;
        }
    }
}

// ---------------------------------------------------------------- 语言解析

TEST_F(I18nTest, DefaultLanguageIsZhCn) {
    EXPECT_EQ(i18n::defaultLanguage(), i18n::Language::ZhCN);
}

TEST_F(I18nTest, ParseLanguageAliases) {
    const char* zhValues[] = { "zh", "zh-CN", "ZH-cn", "zh_CN", " zh-cn ", "zh-Hans" };
    for (std::size_t i = 0; i < sizeof(zhValues) / sizeof(zhValues[0]); ++i) {
        i18n::Language out = i18n::Language::EnUS;
        ASSERT_TRUE(i18n::parseLanguage(zhValues[i], out)) << zhValues[i];
        EXPECT_EQ(out, i18n::Language::ZhCN) << zhValues[i];
    }

    const char* enValues[] = { "en", "en-US", "EN_us", "en-GB", "en-us\t" };
    for (std::size_t i = 0; i < sizeof(enValues) / sizeof(enValues[0]); ++i) {
        i18n::Language out = i18n::Language::ZhCN;
        ASSERT_TRUE(i18n::parseLanguage(enValues[i], out)) << enValues[i];
        EXPECT_EQ(out, i18n::Language::EnUS) << enValues[i];
    }
}

TEST_F(I18nTest, ParseLanguageRejectsUnknownAndKeepsOut) {
    const char* badValues[] = { "", "fr", "ja-JP", "zh-CN-extra", "english", "z" };
    for (std::size_t i = 0; i < sizeof(badValues) / sizeof(badValues[0]); ++i) {
        i18n::Language out = i18n::Language::EnUS;
        EXPECT_FALSE(i18n::parseLanguage(badValues[i], out)) << badValues[i];
        EXPECT_EQ(out, i18n::Language::EnUS) << badValues[i];
    }
}

TEST_F(I18nTest, LanguageCodeRoundTrip) {
    i18n::Language out = i18n::Language::ZhCN;
    ASSERT_TRUE(i18n::parseLanguage(i18n::languageCode(i18n::Language::ZhCN), out));
    EXPECT_EQ(out, i18n::Language::ZhCN);
    ASSERT_TRUE(i18n::parseLanguage(i18n::languageCode(i18n::Language::EnUS), out));
    EXPECT_EQ(out, i18n::Language::EnUS);
}

// ---------------------------------------------------------------- 格式化

TEST_F(I18nTest, U8fWithoutHolesReturnsPattern) {
    EXPECT_EQ(i18n::u8f(i18n::StrId::MainFrame_Message_RestartToApply, {}),
              i18n::u8(i18n::StrId::MainFrame_Message_RestartToApply));
}

TEST_F(I18nTest, U8fReplacesHolesInOrder) {
    i18n::setLanguage(i18n::Language::EnUS);
    EXPECT_EQ(i18n::u8f(i18n::StrId::MainFrame_Status_LanguagePendingRestart, { "en-US" }),
              std::string("UI language set to en-US; restart to apply"));
}

TEST_F(I18nTest, U8fKeepsHolesWhenArgumentsRunOut) {
    i18n::setLanguage(i18n::Language::EnUS);
    EXPECT_EQ(i18n::u8f(i18n::StrId::MainFrame_Status_LanguagePendingRestart, {}),
              std::string("UI language set to {}; restart to apply"));
}

TEST_F(I18nTest, U8fIgnoresExtraArguments) {
    i18n::setLanguage(i18n::Language::EnUS);
    EXPECT_EQ(i18n::u8f(i18n::StrId::MainFrame_Status_LanguagePendingRestart, { "en-US", "extra" }),
              std::string("UI language set to en-US; restart to apply"));
}

TEST_F(I18nTest, U8fDoesNotRescanInsertedArgument) {
    // 参数内容里的 {} 不得被二次替换（否则用户数据里的花括号会吞掉后面的参数）。
    i18n::setLanguage(i18n::Language::EnUS);
    EXPECT_EQ(i18n::u8f(i18n::StrId::MainFrame_Status_LanguagePendingRestart, { "{}{}" }),
              std::string("UI language set to {}{}; restart to apply"));
}

}  // namespace
