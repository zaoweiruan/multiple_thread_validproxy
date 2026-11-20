#include "TaskChainText.h"

#include "i18n/I18nWx.h"

namespace ui {

wxString taskChainStepName(const std::string& step) {
    if (step == "update_all")     return i18n::wx(i18n::StrId::ConfigDialog_UpdateAll);
    if (step == "test_all")       return i18n::wx(i18n::StrId::ConfigDialog_TestAll);
    if (step == "dedup")          return i18n::wx(i18n::StrId::ConfigDialog_Cleanup);
    if (step == "sync")           return i18n::wx(i18n::StrId::ConfigDialog_Sync);
    if (step == "export")         return i18n::wx(i18n::StrId::ConfigDialog_Export);
    if (step == "resolve_region") return i18n::wx(i18n::StrId::ConfigDialog_ResolveRegions);
    return wxString();
}

wxString taskChainDisplayText(const std::vector<std::string>& steps) {
    wxString display;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (i > 0) {
            display += L" \u2192 ";   // " → "
        }
        display += taskChainStepName(steps[i]);
    }
    return display;
}

}  // namespace ui
