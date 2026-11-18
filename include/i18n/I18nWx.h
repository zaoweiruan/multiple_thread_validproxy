#ifndef I18N_I18N_WX_H
#define I18N_I18N_WX_H

#include <cstddef>
#include <initializer_list>

#include <wx/string.h>

#include "i18n/I18n.h"

// wxWidgets 便捷封装：仅 UI 层（src/ui/**、main_gui.cpp）包含。
// 非 UI 层请直接用 i18n::u8 / u8f，避免 core 代码依赖 wx。
namespace i18n {

inline wxString wx(StrId id) {
    return wxString(i18n::w(id));
}

// 与 u8f 同语义：按顺序替换 `{}`，参数不足保留 `{}`，多余参数忽略。
inline wxString wxf(StrId id, std::initializer_list<wxString> args) {
    const wxString pattern = i18n::wx(id);
    wxString out;
    out.reserve(pattern.length() + 16);

    std::initializer_list<wxString>::const_iterator next = args.begin();
    std::size_t pos = 0;
    while (pos < pattern.length()) {
        const std::size_t hole = pattern.find(wxS("{}"), static_cast<wxString::size_type>(pos));
        if (hole == wxString::npos) {
            out += pattern.Mid(static_cast<wxString::size_type>(pos));
            break;
        }
        out += pattern.Mid(static_cast<wxString::size_type>(pos),
                           static_cast<wxString::size_type>(hole) - static_cast<wxString::size_type>(pos));
        if (next == args.end()) {
            out += wxS("{}");
        } else {
            out += *next;
            ++next;
        }
        pos = hole + 2;
    }
    return out;
}

}  // namespace i18n

#endif  // I18N_I18N_WX_H
