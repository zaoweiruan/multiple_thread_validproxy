#ifndef I18N_I18N_H
#define I18N_I18N_H

#include <initializer_list>
#include <string>

#include "i18n/Language.h"
#include "i18n/StringId.h"

// 纯 C++ 查询与格式化（不依赖 wxWidgets），可被 UITests / 业务层 / 日志层直接复用。
// UI 层的 wxString 封装见 i18n/I18nWx.h。
namespace i18n {

// 仅启动路径与单测调用：运行期不切换语言（切换后重启生效）。
void setLanguage(Language lang);
Language language();

// 返回静态存储的指针，永不解引用悬垂；越界或空项返回 L"(missing translation)"。
const wchar_t* w(StrId id);
const char* u8(StrId id);

// 按顺序替换 `{}`：参数不足保留剩余 `{}`，多余参数忽略，参数内 `{}` 不二次替换。
std::string u8f(StrId id, std::initializer_list<std::string> args);

}  // namespace i18n

#endif  // I18N_I18N_H
