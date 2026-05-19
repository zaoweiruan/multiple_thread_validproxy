#include "ConfigDialog.h"

#include <wx/sizer.h>
#include <wx/propgrid/manager.h>
#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>
#include <wx/msgdlg.h>

#include <fstream>
#include <sstream>

// -------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(ConfigDialog, wxDialog)
    EVT_BUTTON(wxID_OK, ConfigDialog::onOk)
    EVT_BUTTON(wxID_CANCEL, ConfigDialog::onCancel)
    EVT_PG_CHANGED(wxID_ANY, ConfigDialog::onPropertyChanged)
wxEND_EVENT_TABLE()

// -------------------------------------------------------------------
ConfigDialog::ConfigDialog(wxWindow* parent, const config::AppConfig& cfg)
    : wxDialog(parent, wxID_ANY, "Configuration Editor",
               wxDefaultPosition, wxSize(600, 500),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    // Property grid manager
    propGridManager_ = new wxPropertyGridManager(this, wxID_ANY,
                                                   wxDefaultPosition, wxDefaultSize,
                                                   wxPGMAN_DEFAULT_STYLE | wxPG_BOLD_MODIFIED);

    // --- Database page ---
    dbPage_ = propGridManager_->AddPage("Database");
    dbPage_->Append(new wxStringProperty("Path", "database_path", cfg.database_path));
    dbPage_->Append(new wxStringProperty("SQL Query", "sql_query", cfg.sql_query));
    dbPage_->Append(new wxStringProperty("SQL By SubId", "sql_by_subid", cfg.sql_by_subid));

    // --- Xray page ---
    xrayPage_ = propGridManager_->AddPage("Xray");
    xrayPage_->Append(new wxFileProperty("Executable", "xray_executable", cfg.xray_executable));
    xrayPage_->Append(new wxIntProperty("Workers", "xray_workers", cfg.xray_workers));
    xrayPage_->Append(new wxIntProperty("Start Port", "xray_start_port", cfg.xray_start_port));
    xrayPage_->Append(new wxIntProperty("API Port", "xray_api_port", cfg.xray_api_port));

    // --- Test page ---
    testPage_ = propGridManager_->AddPage("Test");
    testPage_->Append(new wxStringProperty("Test URL", "test_url", cfg.test_url));
    testPage_->Append(new wxIntProperty("Timeout (ms)", "test_timeout_ms", cfg.test_timeout_ms));

    // --- Log page ---
    logPage_ = propGridManager_->AddPage("Log");
    logPage_->Append(new wxBoolProperty("Enabled", "log_enabled", cfg.log_enabled));
    logPage_->Append(new wxBoolProperty("Network Failures", "log_network_failures", cfg.log_network_failures));
    wxArrayString levelChoices;
    levelChoices.Add("TRACE"); levelChoices.Add("DEBUG"); levelChoices.Add("INFO");
    levelChoices.Add("REPORT"); levelChoices.Add("WARN"); levelChoices.Add("ERROR");
    logPage_->Append(new wxEnumProperty("Console Level", "log_console_level", levelChoices));
    logPage_->Append(new wxEnumProperty("File Level", "log_file_level", levelChoices));

    // --- Subscription page ---
    subPage_ = propGridManager_->AddPage("Subscription");
    wxArrayString modeChoices;
    modeChoices.Add("proxy_first"); modeChoices.Add("direct_first"); modeChoices.Add("direct_only");
    subPage_->Append(new wxEnumProperty("Priority Mode", "priority_mode", modeChoices));
    subPage_->Append(new wxBoolProperty("Check Auto Update", "check_auto_update_interval",
                                         cfg.check_auto_update_interval));

    // --- Dedup page ---
    dedupPage_ = propGridManager_->AddPage("Dedup");
    dedupPage_->Append(new wxBoolProperty("Enabled", "dedup_enabled", cfg.dedup_enabled));
    dedupPage_->Append(new wxBoolProperty("After Update", "dedup_after_update", cfg.dedup_after_update));
    dedupPage_->Append(new wxIntProperty("Blacklist Threshold", "blacklist_threshold",
                                          cfg.blacklist_threshold));

    // --- Sync page ---
    syncPage_ = propGridManager_->AddPage("Sync");
    syncPage_->Append(new wxFileProperty("Source DB", "sync_source_db", cfg.sync.source_db));
    syncPage_->Append(new wxFileProperty("Target DB", "sync_target_db", cfg.sync.target_db));
    syncPage_->Append(new wxBoolProperty("Skip SubIDs", "sync_skip_subids", cfg.sync.sync_skip_subids));

    propGridManager_->SetPropertyAttributeAll(wxPG_BOOL_USE_CHECKBOX, (long)1);

    topSizer->Add(propGridManager_, 1, wxEXPAND | wxALL, 8);

    // Buttons
    wxSizer* btnSizer = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
    if (btnSizer) topSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 8);

    SetSizer(topSizer);
    topSizer->Fit(this);

    loadConfig(cfg);
}

// -------------------------------------------------------------------
void ConfigDialog::loadConfig(const config::AppConfig& cfg) {
    editedConfig_ = cfg;
    // Properties are already populated in constructor
}

bool ConfigDialog::saveConfig() {
    // Read back all properties
    editedConfig_.database_path = dbPage_->GetPropertyValueAsString("database_path").ToStdString();
    editedConfig_.sql_query = dbPage_->GetPropertyValueAsString("sql_query").ToStdString();
    editedConfig_.sql_by_subid = dbPage_->GetPropertyValueAsString("sql_by_subid").ToStdString();

    editedConfig_.xray_executable = xrayPage_->GetPropertyValueAsString("xray_executable").ToStdString();
    editedConfig_.xray_workers = xrayPage_->GetPropertyValueAsInt("xray_workers");
    editedConfig_.xray_start_port = xrayPage_->GetPropertyValueAsInt("xray_start_port");
    editedConfig_.xray_api_port = xrayPage_->GetPropertyValueAsInt("xray_api_port");

    editedConfig_.test_url = testPage_->GetPropertyValueAsString("test_url").ToStdString();
    editedConfig_.test_timeout_ms = testPage_->GetPropertyValueAsInt("test_timeout_ms");

    editedConfig_.log_enabled = logPage_->GetPropertyValueAsBool("log_enabled");
    editedConfig_.log_network_failures = logPage_->GetPropertyValueAsBool("log_network_failures");
    editedConfig_.log_console_level = logPage_->GetPropertyValueAsString("log_console_level").ToStdString();
    editedConfig_.log_file_level = logPage_->GetPropertyValueAsString("log_file_level").ToStdString();

    std::string mode = subPage_->GetPropertyValueAsString("priority_mode").ToStdString();
    editedConfig_.priority_mode = mode;
    editedConfig_.check_auto_update_interval = subPage_->GetPropertyValueAsBool("check_auto_update_interval");

    editedConfig_.dedup_enabled = dedupPage_->GetPropertyValueAsBool("dedup_enabled");
    editedConfig_.dedup_after_update = dedupPage_->GetPropertyValueAsBool("dedup_after_update");
    editedConfig_.blacklist_threshold = dedupPage_->GetPropertyValueAsInt("blacklist_threshold");

    editedConfig_.sync.source_db = syncPage_->GetPropertyValueAsString("sync_source_db").ToStdString();
    editedConfig_.sync.target_db = syncPage_->GetPropertyValueAsString("sync_target_db").ToStdString();
    editedConfig_.sync.sync_skip_subids = syncPage_->GetPropertyValueAsBool("sync_skip_subids");

    return validateConfig();
}

bool ConfigDialog::validateConfig() {
    if (editedConfig_.database_path.empty()) {
        wxMessageBox("Database path cannot be empty", "Validation Error", wxOK | wxICON_ERROR);
        return false;
    }
    if (editedConfig_.xray_executable.empty()) {
        wxMessageBox("Xray executable path cannot be empty", "Validation Error", wxOK | wxICON_ERROR);
        return false;
    }
    if (editedConfig_.xray_workers < 1 || editedConfig_.xray_workers > 64) {
        wxMessageBox("Workers must be between 1 and 64", "Validation Error", wxOK | wxICON_ERROR);
        return false;
    }
    if (editedConfig_.xray_start_port < 1024 || editedConfig_.xray_start_port > 65535) {
        wxMessageBox("Start port must be between 1024 and 65535", "Validation Error", wxOK | wxICON_ERROR);
        return false;
    }
    if (editedConfig_.test_timeout_ms < 1000 || editedConfig_.test_timeout_ms > 120000) {
        wxMessageBox("Timeout must be between 1000 and 120000 ms", "Validation Error", wxOK | wxICON_ERROR);
        return false;
    }
    return true;
}

// -------------------------------------------------------------------
void ConfigDialog::onOk(wxCommandEvent&) {
    if (saveConfig()) {
        modified_ = true;
        EndModal(wxID_OK);
    }
}

void ConfigDialog::onCancel(wxCommandEvent&) {
    EndModal(wxID_CANCEL);
}

void ConfigDialog::onPropertyChanged(wxPropertyGridEvent&) {
    modified_ = true;
}
