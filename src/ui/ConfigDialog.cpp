#include "ConfigDialog.h"

#include <wx/sizer.h>
#include <wx/tokenzr.h>
#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

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
    // wxWidgets 3.3.3 bug: wxPGPropertyFlags_ShowFullFileName maps to Reserved_1,
    // but ValueToString checks wxPGFlags::ShowFullFileName — set the correct flag directly.
    dbPathProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
    propGrid_->Append(new wxStringProperty(L"SQL 查询", "sql_query", cfg.sql_query));
    propGrid_->Append(new wxStringProperty(L"按 SubId 查询", "sql_by_subid", cfg.sql_by_subid));

    // --- 工作线程配置 (原 Xray) ---
    propGrid_->Append(new wxPropertyCategory(L"工作线程配置"));
    propGrid_->Append(new wxIntProperty(L"工作数", "xray_workers", cfg.xray_workers));
    propGrid_->Append(new wxIntProperty(L"起始端口", "xray_start_port", cfg.xray_start_port));
    propGrid_->Append(new wxIntProperty(L"API 端口", "xray_api_port", cfg.xray_api_port));

    // --- 代理配置 ---
    propGrid_->Append(new wxPropertyCategory(L"代理配置"));
    {
        wxFileProperty* xrayExecProp = new wxFileProperty(L"xray执行文件", "proxy_xray_executable", cfg.proxy.xray_executable);
        propGrid_->Append(xrayExecProp);
        propGrid_->SetPropertyAttribute("proxy_xray_executable", wxPG_FILE_SHOW_FULL_PATH, (long)1);
        xrayExecProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
    }
    {
        wxFileProperty* assetDirProp = new wxFileProperty(L"xray_location_asset 目录", "proxy_xray_asset_dir", cfg.proxy.xray_asset_dir);
        propGrid_->Append(assetDirProp);
        propGrid_->SetPropertyAttribute("proxy_xray_asset_dir", wxPG_FILE_SHOW_FULL_PATH, (long)1);
        assetDirProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
        propGrid_->SetPropertyAttribute("proxy_xray_asset_dir", wxPG_DIALOG_TITLE, L"选择 Xray 资源目录");
    }
    {
        wxFileProperty* tmplProp = new wxFileProperty(L"xray配置模板", "proxy_template_config_path", cfg.proxy.template_config_path);
        propGrid_->Append(tmplProp);
        propGrid_->SetPropertyAttribute("proxy_template_config_path", wxPG_FILE_SHOW_FULL_PATH, (long)1);
        tmplProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
        propGrid_->SetPropertyAttribute("proxy_template_config_path", wxPG_DIALOG_TITLE, L"选择 Xray 启动配置模板文件");
    }
    {
        wxFileProperty* sbExecProp = new wxFileProperty(L"Sing-box执行文件", "proxy_singbox_executable", cfg.proxy.singbox_executable);
        propGrid_->Append(sbExecProp);
        propGrid_->SetPropertyAttribute("proxy_singbox_executable", wxPG_FILE_SHOW_FULL_PATH, (long)1);
        sbExecProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
        propGrid_->SetPropertyAttribute("proxy_singbox_executable", wxPG_DIALOG_TITLE, L"选择 Sing-box 可执行文件");
    }
    {
        // singbox_asset_dir：geoip.dat / geosite.dat 等 Sing-box 资源目录（与
        // proxy.xray_asset_dir 对应；此前仅存在于 struct/序列化器，无 UI 属性）
        wxFileProperty* sbAssetProp = new wxFileProperty(L"Sing-box资源目录", "proxy_singbox_asset_dir", cfg.proxy.singbox_asset_dir);
        propGrid_->Append(sbAssetProp);
        propGrid_->SetPropertyAttribute("proxy_singbox_asset_dir", wxPG_FILE_SHOW_FULL_PATH, (long)1);
        sbAssetProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
        propGrid_->SetPropertyAttribute("proxy_singbox_asset_dir", wxPG_DIALOG_TITLE, L"选择 Sing-box 资源目录");
    }
    {
        wxFileProperty* sbTmplProp = new wxFileProperty(L"Sing-box配置模板", "proxy_singbox_template_config_path", cfg.proxy.singbox_template_config_path);
        propGrid_->Append(sbTmplProp);
        propGrid_->SetPropertyAttribute("proxy_singbox_template_config_path", wxPG_FILE_SHOW_FULL_PATH, (long)1);
        sbTmplProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
        propGrid_->SetPropertyAttribute("proxy_singbox_template_config_path", wxPG_DIALOG_TITLE, L"选择 Sing-box 启动配置模板文件");
    }
    propGrid_->Append(new wxBoolProperty(L"使用sing-box为代理终端", "proxy_use_singbox", cfg.proxy.use_singbox));
    propGrid_->Append(new wxIntProperty(L"SOCKS 监听端口", "proxy_socks_base_port", cfg.proxy.socks_base_port));

    // --- 测试 配置 ---
    propGrid_->Append(new wxPropertyCategory(L"测试"));
    propGrid_->Append(new wxStringProperty(L"测试 URL", "test_url", cfg.test_url));
    propGrid_->Append(new wxIntProperty(L"超时(毫秒)", "test_timeout_ms", cfg.test_timeout_ms));

    // --- 日志 配置 ---
    propGrid_->Append(new wxPropertyCategory(L"日志"));
    // log_enabled removed - always true (default)
    // log_network_failures removed (Spec 附录A): the flag only gated 3 batch-test
    // startup INFO lines; output is now controlled purely by log levels (DEBUG).
    wxArrayString levelChoices;
    levelChoices.Add("TRACE"); levelChoices.Add("DEBUG"); levelChoices.Add("INFO");
    levelChoices.Add("REPORT"); levelChoices.Add("WARN"); levelChoices.Add("ERROR");
    propGrid_->Append(new wxEnumProperty(L"控制台级别", "log_console_level", levelChoices));
    propGrid_->Append(new wxEnumProperty(L"文件级别", "log_file_level", levelChoices));

    // --- 网络监控 配置 ---
    propGrid_->Append(new wxPropertyCategory(L"网络监控"));
    propGrid_->Append(new wxBoolProperty(L"启用", "network_monitor_enabled", cfg.network_monitor.enabled));
    propGrid_->Append(new wxStringProperty(L"检测URL(逗号分隔)", "network_monitor_checkUrls", ""));
    propGrid_->Append(new wxIntProperty(L"检测间隔(毫秒)", "network_monitor_interval", cfg.network_monitor.checkIntervalMs));
    propGrid_->Append(new wxIntProperty(L"检测超时(毫秒)", "network_monitor_timeout", cfg.network_monitor.checkTimeoutMs));
    propGrid_->Append(new wxIntProperty(L"最大探测次数", "network_monitor_maxProbes", cfg.network_monitor.maxProbes));
    // Note: maxProbes=0 means immediate cancel (legacy behavior), >0 enables probe grace window

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
    srcDbProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
    propGrid_->SetPropertyAttribute("sync_target_db", wxPG_FILE_SHOW_FULL_PATH, (long)1);
    tgtDbProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
    propGrid_->Append(new wxBoolProperty(L"跳过保护订阅", "sync_skip_subids", cfg.sync.sync_skip_subids));

    // --- 自动任务 配置 ---
    propGrid_->Append(new wxPropertyCategory(L"自动任务"));
    propGrid_->Append(new wxBoolProperty(L"全部更新", "autotask_step_update_all", false));
    propGrid_->Append(new wxBoolProperty(L"全部测试", "autotask_step_test_all", false));
    propGrid_->Append(new wxBoolProperty(L"去重", "autotask_step_dedup", false));
    propGrid_->Append(new wxBoolProperty(L"同步", "autotask_step_sync", false));
    propGrid_->Append(new wxBoolProperty(L"导出", "autotask_step_export", false));
    propGrid_->Append(new wxBoolProperty(L"解析地区", "autotask_step_resolve_region", false));
    propGrid_->Append(new wxStringProperty(L"任务链", "autotask_chain_display", L""));
    propGrid_->SetPropertyReadOnly("autotask_chain_display");
    // --- 通知 配置 ---
    propGrid_->Append(new wxPropertyCategory(L"通知"));
    propGrid_->Append(new wxBoolProperty(L"启用通知", "notification_enabled", cfg.notification_enabled));
    propGrid_->Append(new wxBoolProperty(L"更新时通知", "notification_on_update", cfg.notification_on_update));
    propGrid_->Append(new wxBoolProperty(L"测试时通知", "notification_on_test", cfg.notification_on_test));
    propGrid_->Append(new wxBoolProperty(L"任务完成通知", "autotask_notify", cfg.auto_task.notify_on_complete));

    // --- 监控代理进程 配置 ---
    propGrid_->Append(new wxPropertyCategory(L"监控代理进程"));
    propGrid_->Append(new wxBoolProperty(L"启用", "proxy_process_monitor_enabled", cfg.proxy_process_monitor.enabled));
    propGrid_->Append(new wxIntProperty(L"检测间隔(毫秒)", "proxy_process_monitor_check_interval_ms", cfg.proxy_process_monitor.checkIntervalMs));

    // --- 独立代理池 配置 ---
    // 方案甲：mode / observatory.type / observatory.samplingCount 三个预留字段（零运行时
    // 消费方）不在 UI 暴露，后端往返由 loadConfig() 起底的 editedConfig_ 原值透传。
    // 端口为期望值：实际监听端口由 PortManager 在池启动时分配（见 spec §4.1 脚注）。
    propGrid_->Append(new wxPropertyCategory(L"独立代理池"));
    propGrid_->Append(new wxBoolProperty(L"启用", "pool_enabled", cfg.standalone_pool.enabled));
    propGrid_->Append(new wxIntProperty(L"SOCKS 端口(期望值)", "pool_socks_port", cfg.standalone_pool.socksPort));
    propGrid_->Append(new wxIntProperty(L"API 端口(期望值)", "pool_api_port", cfg.standalone_pool.apiPort));
    {
        // balancerStrategy 白名单必须与 StandalonePoolConfigParser.h 一致
        wxArrayString strategyChoices;
        strategyChoices.Add(L"random");
        strategyChoices.Add(L"leastPing");
        strategyChoices.Add(L"leastLoad");
        propGrid_->Append(new wxEnumProperty(L"均衡策略", "pool_balancer_strategy", strategyChoices));
        propGrid_->SetPropertyValue("pool_balancer_strategy", wxString(cfg.standalone_pool.balancerStrategy));
    }
    // 观测探测地址：仅当 test.url 为空时作为探测 URL 兜底（AppController.cpp:1806 用
    // test_url 覆盖 probeUrl）
    propGrid_->Append(new wxStringProperty(L"观测探测地址(兜底)", "pool_obs_destination", wxString(cfg.standalone_pool.observatory.destination)));
    propGrid_->Append(new wxIntProperty(L"观测间隔(秒)", "pool_obs_interval", cfg.standalone_pool.observatory.intervalSec));
    propGrid_->Append(new wxIntProperty(L"观测超时(秒)", "pool_obs_timeout", cfg.standalone_pool.observatory.timeoutSec));
    propGrid_->Append(new wxIntProperty(L"评估间隔(秒)", "pool_eval_interval", cfg.standalone_pool.evaluate.intervalSec));
    propGrid_->Append(new wxBoolProperty(L"报告健康结果", "pool_eval_report_health", cfg.standalone_pool.evaluate.reportHealth));
    propGrid_->Append(new wxBoolProperty(L"自动剔除失效成员", "pool_eval_auto_prune", cfg.standalone_pool.evaluate.autoPruneDead));
    propGrid_->Append(new wxIntProperty(L"剔除阈值(连续失败次数)", "pool_eval_prune_streak", cfg.standalone_pool.evaluate.pruneFailStreak));
    propGrid_->Append(new wxBoolProperty(L"自动优化(预留记录式)", "pool_eval_auto_optimize", cfg.standalone_pool.evaluate.autoOptimize));

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
    // Network monitor fields
    propGrid_->SetPropertyValue("network_monitor_enabled", cfg.network_monitor.enabled);
    {
        wxString urls;
        for (size_t i = 0; i < cfg.network_monitor.checkUrls.size(); ++i) {
            if (i > 0) urls += ",";
            urls += wxString(cfg.network_monitor.checkUrls[i]);
        }
        propGrid_->SetPropertyValue("network_monitor_checkUrls", urls);
    }
    propGrid_->SetPropertyValue("network_monitor_interval", cfg.network_monitor.checkIntervalMs);
    propGrid_->SetPropertyValue("network_monitor_timeout", cfg.network_monitor.checkTimeoutMs);
    propGrid_->SetPropertyValue("network_monitor_maxProbes", cfg.network_monitor.maxProbes);
    // Set accelerator_url
    propGrid_->SetPropertyValue("accelerator_url", wxString(cfg.accelerator_url));
    // Set update_methods checkboxes
    bool hasAccel = false, hasProxy = false, hasDirect = false;
    for (const std::string& m : cfg.update_methods) {
        if (m == "accelerator") hasAccel = true;
        else if (m == "proxy") hasProxy = true;
        else if (m == "direct") hasDirect = true;
    }
    propGrid_->SetPropertyValue("update_method_accelerator", hasAccel);
    propGrid_->SetPropertyValue("update_method_proxy", hasProxy);
    propGrid_->SetPropertyValue("update_method_direct", hasDirect);
    refreshUpdateMethodDisplay();

    // AutoTask step checkboxes
    bool hasUpdate = false, hasTest = false, hasDedup = false, hasSync = false, hasExport = false, hasResolveRegion = false;
    for (const std::string& s : cfg.auto_task.steps) {
        if (s == "update_all") hasUpdate = true;
        else if (s == "test_all") hasTest = true;
        else if (s == "dedup") hasDedup = true;
        else if (s == "sync") hasSync = true;
        else if (s == "export") hasExport = true;
        else if (s == "resolve_region") hasResolveRegion = true;
    }
    propGrid_->SetPropertyValue("autotask_step_update_all", hasUpdate);
    propGrid_->SetPropertyValue("autotask_step_test_all", hasTest);
    propGrid_->SetPropertyValue("autotask_step_dedup", hasDedup);
    propGrid_->SetPropertyValue("autotask_step_sync", hasSync);
    propGrid_->SetPropertyValue("autotask_step_export", hasExport);
    propGrid_->SetPropertyValue("autotask_step_resolve_region", hasResolveRegion);
    stepOrder_ = cfg.auto_task.steps;
    refreshAutoTaskChainDisplay();

    // Proxy fields
    propGrid_->SetPropertyValue("proxy_xray_executable", wxString(cfg.proxy.xray_executable));
    propGrid_->SetPropertyValue("proxy_use_singbox", cfg.proxy.use_singbox);
    propGrid_->SetPropertyValue("proxy_socks_base_port", cfg.proxy.socks_base_port);
    propGrid_->SetPropertyValue("proxy_xray_asset_dir", wxString(cfg.proxy.xray_asset_dir));
    propGrid_->SetPropertyValue("proxy_template_config_path", wxString(cfg.proxy.template_config_path));
    propGrid_->SetPropertyValue("proxy_singbox_executable", wxString(cfg.proxy.singbox_executable));
    propGrid_->SetPropertyValue("proxy_singbox_asset_dir", wxString(cfg.proxy.singbox_asset_dir));

    propGrid_->SetPropertyValue("proxy_singbox_template_config_path", wxString(cfg.proxy.singbox_template_config_path));

    // ProxyProcessMonitor fields
    propGrid_->SetPropertyValue("proxy_process_monitor_enabled", cfg.proxy_process_monitor.enabled);
    propGrid_->SetPropertyValue("proxy_process_monitor_check_interval_ms", cfg.proxy_process_monitor.checkIntervalMs);

    // StandalonePool fields (方案甲：12 项；mode/type/samplingCount 不经 UI，由
    // editedConfig_ = cfg 起底原值透传)
    propGrid_->SetPropertyValue("pool_enabled", cfg.standalone_pool.enabled);
    propGrid_->SetPropertyValue("pool_socks_port", cfg.standalone_pool.socksPort);
    propGrid_->SetPropertyValue("pool_api_port", cfg.standalone_pool.apiPort);
    propGrid_->SetPropertyValue("pool_balancer_strategy", wxString(cfg.standalone_pool.balancerStrategy));
    propGrid_->SetPropertyValue("pool_obs_destination", wxString(cfg.standalone_pool.observatory.destination));
    propGrid_->SetPropertyValue("pool_obs_interval", cfg.standalone_pool.observatory.intervalSec);
    propGrid_->SetPropertyValue("pool_obs_timeout", cfg.standalone_pool.observatory.timeoutSec);
    propGrid_->SetPropertyValue("pool_eval_interval", cfg.standalone_pool.evaluate.intervalSec);
    propGrid_->SetPropertyValue("pool_eval_report_health", cfg.standalone_pool.evaluate.reportHealth);
    propGrid_->SetPropertyValue("pool_eval_auto_prune", cfg.standalone_pool.evaluate.autoPruneDead);
    propGrid_->SetPropertyValue("pool_eval_prune_streak", cfg.standalone_pool.evaluate.pruneFailStreak);
    propGrid_->SetPropertyValue("pool_eval_auto_optimize", cfg.standalone_pool.evaluate.autoOptimize);
}

bool ConfigDialog::saveConfig() {
    // Database fields
    editedConfig_.database_path = propGrid_->GetPropertyValueAsString("database_path").ToStdString();
    editedConfig_.sql_query = propGrid_->GetPropertyValueAsString("sql_query").ToStdString();
    editedConfig_.sql_by_subid = propGrid_->GetPropertyValueAsString("sql_by_subid").ToStdString();

    // Xray (worker thread) fields - executable moved to proxy section
    editedConfig_.xray_workers = propGrid_->GetPropertyValueAsInt("xray_workers");
    editedConfig_.xray_start_port = propGrid_->GetPropertyValueAsInt("xray_start_port");
    editedConfig_.xray_api_port = propGrid_->GetPropertyValueAsInt("xray_api_port");

    // Test fields
    editedConfig_.test_url = propGrid_->GetPropertyValueAsString("test_url").ToStdString();
    editedConfig_.test_timeout_ms = propGrid_->GetPropertyValueAsInt("test_timeout_ms");

    // Log fields - log_enabled always true (removed from UI)
    editedConfig_.log_enabled = true;
    editedConfig_.log_console_level = propGrid_->GetPropertyValueAsString("log_console_level").ToStdString();
    editedConfig_.log_file_level = propGrid_->GetPropertyValueAsString("log_file_level").ToStdString();

    // Network monitor fields
    editedConfig_.network_monitor.enabled = propGrid_->GetPropertyValueAsBool("network_monitor_enabled");
    editedConfig_.network_monitor.checkUrls.clear();
    {
        wxString raw = propGrid_->GetPropertyValueAsString("network_monitor_checkUrls");
        wxStringTokenizer tok(raw, ",");
        while (tok.HasMoreTokens()) {
            wxString url = tok.GetNextToken().Trim(true).Trim(false);
            if (!url.empty()) {
                editedConfig_.network_monitor.checkUrls.push_back(url.ToStdString());
            }
        }
    }
    editedConfig_.network_monitor.checkIntervalMs = propGrid_->GetPropertyValueAsInt("network_monitor_interval");
    editedConfig_.network_monitor.checkTimeoutMs = propGrid_->GetPropertyValueAsInt("network_monitor_timeout");
    editedConfig_.network_monitor.maxProbes = propGrid_->GetPropertyValueAsInt("network_monitor_maxProbes");
    // Clamp maxProbes to valid range
    if (editedConfig_.network_monitor.maxProbes < 0) editedConfig_.network_monitor.maxProbes = 0;

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

    // Proxy fields
    editedConfig_.proxy.xray_executable = propGrid_->GetPropertyValueAsString("proxy_xray_executable").ToStdString();
    editedConfig_.proxy.use_singbox = propGrid_->GetPropertyValueAsBool("proxy_use_singbox");
    editedConfig_.proxy.socks_base_port = propGrid_->GetPropertyValueAsInt("proxy_socks_base_port");
    editedConfig_.proxy.xray_asset_dir = propGrid_->GetPropertyValueAsString("proxy_xray_asset_dir").ToStdString();
    editedConfig_.proxy.template_config_path = propGrid_->GetPropertyValueAsString("proxy_template_config_path").ToStdString();
    editedConfig_.proxy.singbox_executable = propGrid_->GetPropertyValueAsString("proxy_singbox_executable").ToStdString();
    editedConfig_.proxy.singbox_asset_dir = propGrid_->GetPropertyValueAsString("proxy_singbox_asset_dir").ToStdString();

    editedConfig_.proxy.singbox_template_config_path = propGrid_->GetPropertyValueAsString("proxy_singbox_template_config_path").ToStdString();

    // ProxyProcessMonitor fields
    editedConfig_.proxy_process_monitor.enabled = propGrid_->GetPropertyValueAsBool("proxy_process_monitor_enabled");
    editedConfig_.proxy_process_monitor.checkIntervalMs = propGrid_->GetPropertyValueAsInt("proxy_process_monitor_check_interval_ms");
    // Clamp to valid range
    if (editedConfig_.proxy_process_monitor.checkIntervalMs < 5000) editedConfig_.proxy_process_monitor.checkIntervalMs = 5000;
    if (editedConfig_.proxy_process_monitor.checkIntervalMs > 300000) editedConfig_.proxy_process_monitor.checkIntervalMs = 300000;

    // StandalonePool fields (方案甲：12 项读回；mode/type/samplingCount 不经 UI，
    // 保留 loadConfig() 起底的原值；probeUrl 为运行期派生值，不落盘不读回)
    editedConfig_.standalone_pool.enabled = propGrid_->GetPropertyValueAsBool("pool_enabled");
    editedConfig_.standalone_pool.socksPort = static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_socks_port"));
    editedConfig_.standalone_pool.apiPort = static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_api_port"));
    editedConfig_.standalone_pool.balancerStrategy = propGrid_->GetPropertyValueAsString("pool_balancer_strategy").ToStdString();
    editedConfig_.standalone_pool.observatory.destination = propGrid_->GetPropertyValueAsString("pool_obs_destination").ToStdString();
    editedConfig_.standalone_pool.observatory.intervalSec = static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_obs_interval"));
    editedConfig_.standalone_pool.observatory.timeoutSec = static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_obs_timeout"));
    editedConfig_.standalone_pool.evaluate.intervalSec = static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_eval_interval"));
    editedConfig_.standalone_pool.evaluate.reportHealth = propGrid_->GetPropertyValueAsBool("pool_eval_report_health");
    editedConfig_.standalone_pool.evaluate.autoPruneDead = propGrid_->GetPropertyValueAsBool("pool_eval_auto_prune");
    editedConfig_.standalone_pool.evaluate.pruneFailStreak = static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_eval_prune_streak"));
    editedConfig_.standalone_pool.evaluate.autoOptimize = propGrid_->GetPropertyValueAsBool("pool_eval_auto_optimize");

    // AutoTask fields
    editedConfig_.auto_task.steps = stepOrder_;
    editedConfig_.auto_task.notify_on_complete = propGrid_->GetPropertyValueAsBool("autotask_notify");

    return validateConfig();
}

static const char* stepNameForProp(const wxString& propName) {
    if (propName == "autotask_step_update_all") return "update_all";
    if (propName == "autotask_step_test_all")  return "test_all";
    if (propName == "autotask_step_dedup")     return "dedup";
    if (propName == "autotask_step_sync")      return "sync";
    if (propName == "autotask_step_export")          return "export";
    if (propName == "autotask_step_resolve_region")  return "resolve_region";
    return "";
}

static const wchar_t* stepDisplayName(const std::string& step) {
    if (step == "update_all") return L"全部更新";
    if (step == "test_all")   return L"全部测试";
    if (step == "dedup")      return L"去重";
    if (step == "sync")       return L"同步";
    if (step == "export")            return L"导出";
    if (step == "resolve_region")    return L"解析地区";
    return L"";
}

void ConfigDialog::refreshAutoTaskChainDisplay() {
    wxString display;
    for (size_t i = 0; i < stepOrder_.size(); ++i) {
        if (i > 0) display += L" → ";
        display += stepDisplayName(stepOrder_[i]);
    }
    propGrid_->SetPropertyValue("autotask_chain_display", display);
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
    if (!editedConfig_.proxy.use_singbox && editedConfig_.proxy.xray_executable.empty()) {
        wxMessageBox("Xray executable path cannot be empty", "Validation Error", wxOK | wxICON_ERROR);
        return false;
    }
    if (!editedConfig_.proxy.use_singbox && !std::filesystem::exists(editedConfig_.proxy.xray_executable)) {
        wxMessageBox("Xray executable file not found.\n\nPath:\n" + editedConfig_.proxy.xray_executable,
                     "Validation Error", wxOK | wxICON_ERROR);
        return false;
    }
    if (!editedConfig_.proxy.use_singbox) {
        std::filesystem::path xrayPath(editedConfig_.proxy.xray_executable);
        std::string ext = xrayPath.extension().string();
        if (!ext.empty() && ext != ".exe") {
            wxMessageBox("Xray executable should have .exe extension.\n\nCurrent:\n" + editedConfig_.proxy.xray_executable,
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
    // Validate template config path: must be non-empty and file must exist if configured
    if (!editedConfig_.proxy.template_config_path.empty() &&
        !std::filesystem::exists(editedConfig_.proxy.template_config_path)) {
        wxMessageBox("启动配置模板文件不存在。\n\n路径:\n" + editedConfig_.proxy.template_config_path,
                     "验证错误", wxOK | wxICON_ERROR);
        return false;
    }
    // Validate sing-box template config path (only if Xray template is empty - sing-box mode)
    if (editedConfig_.proxy.singbox_executable.empty() && !editedConfig_.proxy.singbox_template_config_path.empty() &&
        !std::filesystem::exists(editedConfig_.proxy.singbox_template_config_path)) {
        wxMessageBox("Sing-box 配置模板文件不存在。\n\n路径:\n" + editedConfig_.proxy.singbox_template_config_path,
                     "验证错误", wxOK | wxICON_ERROR);
        return false;
    }

    // Network monitor validation
    if (editedConfig_.network_monitor.checkIntervalMs < 5000 || editedConfig_.network_monitor.checkIntervalMs > 300000) {
        wxMessageBox("Network monitor interval must be between 5000 and 300000 ms", "Validation Error", wxOK | wxICON_ERROR);
        return false;
    }
    if (editedConfig_.network_monitor.checkTimeoutMs < 1000 || editedConfig_.network_monitor.checkTimeoutMs > 30000) {
        wxMessageBox("Network monitor timeout must be between 1000 and 30000 ms", "Validation Error", wxOK | wxICON_ERROR);
        return false;
    }
    // Validate checkUrls: each must be valid URL format, and at least one required when enabled
    if (editedConfig_.network_monitor.enabled) {
        for (const std::string& url : editedConfig_.network_monitor.checkUrls) {
            if (!utils::isValidUrlFormat(url)) {
                wxMessageBox("网络监控检测URL格式无效: " + wxString(url), "URL格式错误", wxOK | wxICON_WARNING);
                return false;
            }
        }
        if (editedConfig_.network_monitor.checkUrls.empty()) {
            wxMessageBox("网络监控启用时必须配置至少一个检测URL", "验证错误", wxOK | wxICON_ERROR);
            return false;
        }
    }
    // ProxyProcessMonitor validation
    if (editedConfig_.proxy_process_monitor.checkIntervalMs < 5000 ||
        editedConfig_.proxy_process_monitor.checkIntervalMs > 300000) {
        wxMessageBox("代理进程监控检测间隔必须在 5000 到 300000 毫秒之间", "验证错误", wxOK | wxICON_ERROR);
        return false;
    }

    // StandalonePool validation — 仅在启用时强制（关闭时不校验数值细节）
    if (editedConfig_.standalone_pool.enabled) {
        if (editedConfig_.standalone_pool.socksPort < 1024 || editedConfig_.standalone_pool.socksPort > 65535) {
            wxMessageBox("代理池 SOCKS 端口必须在 1024 到 65535 之间", "验证错误", wxOK | wxICON_ERROR);
            return false;
        }
        if (editedConfig_.standalone_pool.apiPort < 1024 || editedConfig_.standalone_pool.apiPort > 65535) {
            wxMessageBox("代理池 API 端口必须在 1024 到 65535 之间", "验证错误", wxOK | wxICON_ERROR);
            return false;
        }
        if (editedConfig_.standalone_pool.observatory.intervalSec < 1 || editedConfig_.standalone_pool.observatory.intervalSec > 300) {
            wxMessageBox("观测间隔必须在 1 到 300 秒之间", "验证错误", wxOK | wxICON_ERROR);
            return false;
        }
        if (editedConfig_.standalone_pool.observatory.timeoutSec < 1 || editedConfig_.standalone_pool.observatory.timeoutSec > 60) {
            wxMessageBox("观测超时必须在 1 到 60 秒之间", "验证错误", wxOK | wxICON_ERROR);
            return false;
        }
        const std::string& poolDest = editedConfig_.standalone_pool.observatory.destination;
        if (!poolDest.empty() && !utils::isValidUrlFormat(poolDest)) {
            wxMessageBox("代理池观测探测地址格式无效: " + wxString(poolDest), "URL格式错误", wxOK | wxICON_WARNING);
            return false;
        }
        if (poolDest.empty() && editedConfig_.test_url.empty()) {
            // 兜底探测地址与 test.url 双空：探测将无 URL 可用，警告但不阻断
            wxMessageBox("代理池观测探测地址与测试URL均为空，池启动后探测可能失败", "配置警告", wxOK | wxICON_WARNING);
        }
        if (editedConfig_.standalone_pool.evaluate.intervalSec < 1 || editedConfig_.standalone_pool.evaluate.intervalSec > 600) {
            wxMessageBox("评估间隔必须在 1 到 600 秒之间", "验证错误", wxOK | wxICON_ERROR);
            return false;
        }
        if (editedConfig_.standalone_pool.evaluate.pruneFailStreak < 1) {
            wxMessageBox("剔除阈值必须大于等于 1", "验证错误", wxOK | wxICON_ERROR);
            return false;
        }
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
    if (propName.StartsWith("autotask_step_")) {
        const char* step = stepNameForProp(propName);
        if (step && step[0]) {
            bool checked = propGrid_->GetPropertyValueAsBool(propName);
            if (checked) {
                if (std::find(stepOrder_.begin(), stepOrder_.end(), step) == stepOrder_.end()) {
                    stepOrder_.push_back(step);
                }
            } else {
                stepOrder_.erase(std::remove(stepOrder_.begin(), stepOrder_.end(), step), stepOrder_.end());
            }
        }
        refreshAutoTaskChainDisplay();
    }
}