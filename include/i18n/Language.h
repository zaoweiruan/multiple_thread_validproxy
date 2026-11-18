#ifndef I18N_LANGUAGE_H
#define I18N_LANGUAGE_H

#include <string>

namespace i18n {

// 界面语言。语言只在启动时确定一次（切换后重启生效），运行期只读。
enum class Language {
    ZhCN = 0,
    EnUS = 1
};

// 内置默认语言：zh-CN（不探测 Windows 界面语言，见 Spec §5 / D4）。
Language defaultLanguage();

// 解析配置值或日志语言码，大小写不敏感，兼容 zh / zh-cn / zh_CN / en / en-us / en_US。
// 成功返回 true 并写回 out；失败返回 false 且不改动 out（调用方决定回退策略）。
bool parseLanguage(const std::string& text, Language& out);

// "zh-CN" / "en-US"（配置值与日志语言横幅都用它）。
const char* languageCode(Language lang);

}  // namespace i18n

#endif  // I18N_LANGUAGE_H
