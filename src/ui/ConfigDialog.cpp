#include "ConfigDialog.h"

#include <wx/sizer.h>
#include <wx/tokenzr.h>
#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>
#include <wx/msgdlg.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "Utils.h"

// -------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(ConfigDialog, wxDialog)
    EVT_BUTTON(wxID_OK, ConfigDialog::onOk)
    EVT_BUTTON(wxID_CANCEL, ConfigDialog::onCancel)
    EVT_PG_CHANGED(wxID_ANY, ConfigDialog::onPropertyChanged)
wxEND_EVENT_TABLE()

// -------------------------------------------------------------------
ConfigDialog::ConfigDialog(wxWindow* parent, const config::AppConfig& cfg)
    : wxDialog(parent, wxID_ANY, L"配置编辑器",
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    // Property grid (single page, categories for grouping)
    propGrid_ = new wxPropertyGrid(this, wxID_ANY,
                                   wxDefaultPosition, wxDefaultSize,
                                   wxPG_DEFAULT_STYLE | wxPG_BOLD_MODIFIED);

    // --- 数据库 配置 ---
    propGrid_->Append(new wxPropertyCategory(L"数据库"));
    wxFileProperty* dbPathProp = new wxFileProperty(L"路径", "database_path", cfg.database_path);
    propGrid_->Append(dbPathProp);
    propGrid_->SetPropertyAttribute("database_path", wxPG_FILE_SHOW_FULL_PATH, (long)1);
    propGrid_->Append(new wxStringProperty(L"SQL 查询", "sql_query", cfg.sql_query));
    propGrid_->Append(new wxStringProperty(L"按 SubId 查询", "sql_by_subid", cfg.sql_by_subid));

    // --- Xray 配置 ---
    propGrid_->Append(new wxPropertyCategory(L"Xray"));
    wxFileProperty* xrayExecProp = new wxFileProperty(L"执行文件", "xray_executable", cfg.xray_executable);
    propGrid_->Append(xrayExecProp);
    propGrid_->SetPropertyAttribute("xray_executable", wxPG_FILE_SHOW_FULL_PATH, (long)1);
    propGrid_->Append(new wxIntProperty(L"工作数", "xray_workers", cfg.xray_workers));
    propGrid_->Append(new wxIntProperty(L"起始端口", "xray_start_port", cfg.xray_start_port));
    propGrid_->Append(new wxIntProperty(L"API 端口", "xray_api_port", cfg.xray_api_port));

    // --- 测试 配置 ---
    propGrid_->Append(new wxPropertyCategory(L"测试"));
    propGrid_->Append(new wxStringProperty(L"测试 URL", "test_url", cfg.test_url));
    propGrid_->Append(new wxIntProperty(L"超时(毫秒)", "test_timeout_ms", cfg.test_timeout_ms));

    // --- 日志 配置 ---
    propGrid_->Append(new wxPropertyCategory(L"日志"));
    // log_enabled removed - always true (default)
    propGrid_->Append(new wxBoolProperty(L"网络错误日志", "log_network_failures", cfg.log_network_failures));
    wxArrayString levelChoices;
    levelChoices.Add("TRACE"); levelChoices.Add("DEBUG"); levelChoices.Add("INFO");
    levelChoices.Add("REPORT"); levelChoices.Add("WARN"); levelChoices.Add("ERROR");
    propGrid_->Append(new wxEnumProperty(L"控制台级别", "log_console_level", levelChoices));
    propGrid_->Append(new wxEnumProperty(L"文件级别", "log_file_level", levelChoices));

    // --- 订阅 配置 ---
    propGrid_->Append(new wxPropertyCategory(L"订阅"));
    propGrid_->Append(new wxStringProperty(L"加速器URL", "accelerator_url", cfg.accelerator_url));
    propGrid_->Append(new wxBoolProperty(L"加速器", "update_method_accelerator", false));
    propGrid_->Append(new wxBoolProperty(L"代理", "update_method_proxy", false));
    propGrid_->Append(new wxBoolProperty(L"直连", "update_method_direct", false));
    propGrid_->Append(new wxStringProperty(L"更新方式", "update_method_display", L""));
    propGrid_->SetPropertyReadOnly("update_method_display");
    propGrid_->Append(new wxBoolProperty(L"检查自动更新", "check_auto_update_interval",
                                         cfg.check_auto_update_interval));
    propGrid_->Append(new wxIntProperty(L"连接超时(毫秒)", "subscription_connect_timeout_ms",
                                       cfg.subscription_connect_timeout_ms));
    propGrid_->Append(new wxIntProperty(L"请求超时(毫秒)", "subscription_timeout_ms",
                                       cfg.subscription_timeout_ms));

    // --- 去重 配置 ---
    propGrid_->Append(new wxPropertyCategory(L"去重"));
    propGrid_->Append(new wxBoolProperty(L"更新后去重", "dedup_after_update", cfg.dedup_after_update));
    propGrid_->Append(new wxBoolProperty(L"启用黑名单", "blacklist_enabled", cfg.blacklist_enabled));
    propGrid_->Append(new wxStringProperty(L"黑名单订阅ID", "blacklist_subid", cfg.blacklist_subid));
    propGrid_->Append(new wxIntProperty(L"黑名单阈值", "blacklist_threshold",
                                       cfg.blacklist_threshold));
    // dedup_subids: 逗号分隔的订阅ID列表
    {
        wxString subids;
        for (size_t i = 0; i < cfg.dedup_subids.size(); ++i) {
            if (i > 0) subids += ",";
            subids += cfg.dedup_subids[i];
        }
        propGrid_->Append(new wxStringProperty(L"保护订阅ID列表(逗号分隔)", "dedup_subids", subids));
    }

    // --- 同步 配置 ---
    propGrid_->Append(new wxPropertyCategory(L"同步"));
    wxFileProperty* srcDbProp = new wxFileProperty(L"源数据库", "sync_source_db", cfg.sync.source_db);
    wxFileProperty* tgtDbProp = new wxFileProperty(L"目标数据库", "sync_target_db", cfg.sync.target_db);
    propGrid_->Append(srcDbProp);
    propGrid_->Append(tgtDbProp);
    propGrid_->SetPropertyAttribute("sync_source_db", wxPG_FILE_SHOW_FULL_PATH, (long)1);
    propGrid_->SetPropertyAttribute("sync_target_db", wxPG_FILE_SHOW_FULL_PATH, (long)1);
    propGrid_->Append(new wxBoolProperty(L"跳过保护订阅", "sync_skip_subids", cfg.sync.sync_skip_subids));

    // --- 通知 配置 ---
    propGrid_->Append(new wxPropertyCategory(L"通知"));
    propGrid_->Append(new wxBoolProperty(L"启用通知", "notification_enabled", cfg.notification_enabled));
    propGrid_->Append(new wxBoolProperty(L"更新时通知", "notification_on_update", cfg.notification_on_update));
    propGrid_->Append(new wxBoolProperty(L"测试时通知", "notification_on_test", cfg.notification_on_test));

    propGrid_->SetPropertyAttributeAll(wxPG_BOOL_USE_CHECKBOX, true);

    topSizer->Add(propGrid_, 1, wxEXPAND | wxALL, 8);

    // Buttons
    wxSizer* btnSizer = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
    if (btnSizer) topSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 8);

    SetSizer(topSizer);
    Layout();  // Ensure layout before setting splitter
    // Set splitter at 0.6x of previous width (450)
    propGrid_->SetSplitterPosition(450);
    topSizer->Fit(this);
    topSizer->SetSizeHints(this);
    SetMinSize(wxSize(1000, 600));
    SetSize(wxSize(1000, 600));
    CentreOnScreen();  // Center dialog on screen

    loadConfig(cfg);
}

// -------------------------------------------------------------------
void ConfigDialog::loadConfig(const config::AppConfig& cfg) {
    editedConfig_ = cfg;
    propGrid_->SetPropertyValue("log_console_level", wxString(cfg.log_console_level));
    propGrid_->SetPropertyValue("log_file_level", wxString(cfg.log_file_level));
    // Set accelerator_url
    propGrid_->SetPropertyValue("accelerator_url", wxString(cfg.accelerator_url));
    // Set update_methods checkboxes
    bool hasAccel = false, hasProxy = false, hasDirect = false;
    for (const auto& m : cfg.update_methods) {
        if (m == "accelerator") hasAccel = true;
        else if (m == "proxy") hasProxy = true;
        else if (m == "direct") hasDirect = true;
    }
    propGrid_->SetPropertyValue("update_method_accelerator", hasAccel);
    propGrid_->SetPropertyValue("update_method_proxy", hasProxy);
    propGrid_->SetPropertyValue("update_method_direct", hasDirect);
    refreshUpdateMethodDisplay();
}

bool ConfigDialog::saveConfig() {
    // Database fields
    editedConfig_.database_path = propGrid_->GetPropertyValueAsString("database_path").ToStdString();
    editedConfig_.sql_query = propGrid_->GetPropertyValueAsString("sql_query").ToStdString();
    editedConfig_.sql_by_subid = propGrid_->GetPropertyValueAsString("sql_by_subid").ToStdString();

    // Xray fields
    editedConfig_.xray_executable = propGrid_->GetPropertyValueAsString("xray_executable").ToStdString();
    editedConfig_.xray_workers = propGrid_->GetPropertyValueAsInt("xray_workers");
    editedConfig_.xray_start_port = propGrid_->GetPropertyValueAsInt("xray_start_port");
    editedConfig_.xray_api_port = propGrid_->GetPropertyValueAsInt("xray_api_port");

    // Test fields
    editedConfig_.test_url = propGrid_->GetPropertyValueAsString("test_url").ToStdString();
    editedConfig_.test_timeout_ms = propGrid_->GetPropertyValueAsInt("test_timeout_ms");

    // Log fields - log_enabled always true (removed from UI)
    editedConfig_.log_enabled = true;
    editedConfig_.log_network_failures = propGrid_->GetPropertyValueAsBool("log_network_failures");
    editedConfig_.log_console_level = propGrid_->GetPropertyValueAsString("log_console_level").ToStdString();
    editedConfig_.log_file_level = propGrid_->GetPropertyValueAsString("log_file_level").ToStdString();

    // Subscription fields
    {
        std::string accelUrl = propGrid_->GetPropertyValueAsString("accelerator_url").ToStdString();
        if (!accelUrl.empty() && !utils::isValidUrlFormat(accelUrl)) {
            wxMessageBox("加速器 URL 格式无效，请确认包含 http:// 或 https:// 开头且域名有效",
                         "URL 格式错误", wxOK | wxICON_WARNING);
            accelUrl.clear();
            propGrid_->SetPropertyValue("accelerator_url", "");
        }
        editedConfig_.accelerator_url = accelUrl;
    }
    editedConfig_.update_methods.clear();
    if (propGrid_->GetPropertyValueAsBool("update_method_accelerator"))
        editedConfig_.update_methods.push_back("accelerator");
    if (propGrid_->GetPropertyValueAsBool("update_method_proxy"))
        editedConfig_.update_methods.push_back("proxy");
    if (propGrid_->GetPropertyValueAsBool("update_method_direct"))
        editedConfig_.update_methods.push_back("direct");
    if (editedConfig_.update_methods.empty())
        editedConfig_.update_methods.push_back("accelerator");
    editedConfig_.check_auto_update_interval = propGrid_->GetPropertyValueAsBool("check_auto_update_interval");
    editedConfig_.subscription_connect_timeout_ms = propGrid_->GetPropertyValueAsInt("subscription_connect_timeout_ms");
    editedConfig_.subscription_timeout_ms = propGrid_->GetPropertyValueAsInt("subscription_timeout_ms");

    // Dedup fields - dedup_enabled is always true now (removed from UI)
    editedConfig_.dedup_enabled = true;
    editedConfig_.dedup_after_update = propGrid_->GetPropertyValueAsBool("dedup_after_update");
    editedConfig_.blacklist_enabled = propGrid_->GetPropertyValueAsBool("blacklist_enabled");
    editedConfig_.blacklist_subid = propGrid_->GetPropertyValueAsString("blacklist_subid").ToStdString();
    editedConfig_.blacklist_threshold = propGrid_->GetPropertyValueAsInt("blacklist_threshold");
    // Parse comma-separated dedup_subids
    {
        editedConfig_.dedup_subids.clear();
        wxString raw = propGrid_->GetPropertyValueAsString("dedup_subids");
        wxStringTokenizer tok(raw, ",");
        while (tok.HasMoreTokens()) {
            wxString id = tok.GetNextToken().Trim(true).Trim(false);
            if (!id.empty()) {
                editedConfig_.dedup_subids.push_back(id.ToStdString());
            }
        }
    }

    // Sync fields
    editedConfig_.sync.source_db = propGrid_->GetPropertyValueAsString("sync_source_db").ToStdString();
    editedConfig_.sync.target_db = propGrid_->GetPropertyValueAsString("sync_target_db").ToStdString();
    editedConfig_.sync.sync_skip_subids = propGrid_->GetPropertyValueAsBool("sync_skip_subids");

    // Notification fields
    editedConfig_.notification_enabled = propGrid_->GetPropertyValueAsBool("notification_enabled");
    editedConfig_.notification_on_update = propGrid_->GetPropertyValueAsBool("notification_on_update");
    editedConfig_.notification_on_test = propGrid_->GetPropertyValueAsBool("notification_on_test");

    return validateConfig();
}

void ConfigDialog::refreshUpdateMethodDisplay() {
    wxString display;
    if (propGrid_->GetPropertyValueAsBool("update_method_accelerator")) {
        if (!display.empty()) display += " → ";
        display += L"加速器";
    }
    if (propGrid_->GetPropertyValueAsBool("update_method_proxy")) {
        if (!display.empty()) display += " → ";
        display += L"代理";
    }
    if (propGrid_->GetPropertyValueAsBool("update_method_direct")) {
        if (!display.empty()) display += " → ";
        display += L"直连";
    }
    if (display.empty()) {
        display = L"(无)";
    }
    propGrid_->SetPropertyValue("update_method_display", display);
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
    if (!std::filesystem::exists(editedConfig_.xray_executable)) {
        wxMessageBox("Xray executable file not found.\n\nPath:\n" + editedConfig_.xray_executable,
                     "Validation Error", wxOK | wxICON_ERROR);
        return false;
    }
    {
        std::filesystem::path xrayPath(editedConfig_.xray_executable);
        std::string ext = xrayPath.extension().string();
        if (!ext.empty() && ext != ".exe") {
            wxMessageBox("Xray executable should have .exe extension.\n\nCurrent:\n" + editedConfig_.xray_executable,
                         "Validation Warning", wxOK | wxICON_WARNING);
            // Continue — allow non-standard extensions
        }
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
    if (editedConfig_.subscription_connect_timeout_ms < 1000 || editedConfig_.subscription_connect_timeout_ms > 120000) {
        wxMessageBox("Subscription connect timeout must be between 1000 and 120000 ms", "Validation Error", wxOK | wxICON_ERROR);
        return false;
    }
    if (editedConfig_.subscription_timeout_ms < 1000 || editedConfig_.subscription_timeout_ms > 120000) {
        wxMessageBox("Subscription timeout must be between 1000 and 120000 ms", "Validation Error", wxOK | wxICON_ERROR);
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

void ConfigDialog::onPropertyChanged(wxPropertyGridEvent& event) {
    modified_ = true;
    wxString propName = event.GetPropertyName();
    if (propName == "update_method_accelerator" ||
        propName == "update_method_proxy" ||
        propName == "update_method_direct") {
        refreshUpdateMethodDisplay();
    }
}