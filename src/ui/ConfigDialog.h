#ifndef UI_CONFIG_DIALOG_H
#define UI_CONFIG_DIALOG_H

#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/propgrid/manager.h>
#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>

#include "ConfigReader.h"

// ---------------------------------------------------------------
// ConfigDialog — wxPropertyGrid-based config editor
// ---------------------------------------------------------------
class ConfigDialog : public wxDialog {
public:
    ConfigDialog(wxWindow* parent, const config::AppConfig& cfg);

    config::AppConfig getConfig() const { return editedConfig_; }
    bool isModified() const { return modified_; }

private:
    void loadConfig(const config::AppConfig& cfg);
    bool saveConfig();
    void onOk(wxCommandEvent& event);
    void onCancel(wxCommandEvent& event);
    void onPropertyChanged(wxPropertyGridEvent& event);
    bool validateConfig();

    config::AppConfig editedConfig_;
    bool modified_{false};

    wxPropertyGridManager* propGridManager_;
    wxPropertyGridPage* dbPage_;
    wxPropertyGridPage* xrayPage_;
    wxPropertyGridPage* testPage_;
    wxPropertyGridPage* logPage_;
    wxPropertyGridPage* subPage_;
    wxPropertyGridPage* dedupPage_;
    wxPropertyGridPage* syncPage_;

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_CONFIG_DIALOG_H
