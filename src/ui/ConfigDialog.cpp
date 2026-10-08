#include "ConfigDialog.h"

#include <wx/sizer.h>
#include <wx/tokenzr.h>
#include <wx/display.h>
#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <wx/notebook.h>
#include <wx/listctrl.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>

#include "Utils.h"
#include "FullConfigTemplateDAO.h"

#include "BundleChainEditor.h"
#include "ComponentListPanel.h"
#include "config/BundleChainResolver.h"

namespace {

// boost::json 无内置 pretty-print，手写缩进式序列化（2 空格/层）。
std::string prettyJson(const boost::json::value& value, int indent) {
    const std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
    const std::string padIn(static_cast<std::size_t>(indent + 1) * 2, ' ');

    if (value.is_object()) {
        const boost::json::object& obj = value.as_object();
        if (obj.empty()) {
            return "{}";
        }
        std::string out = "{\n";
        std::size_t index = 0;
        for (const boost::json::key_value_pair& kv : obj) {
            out += padIn + "\"" + std::string(kv.key()) + "\": "
                 + prettyJson(kv.value(), indent + 1);
            if (++index < obj.size()) {
                out += ",";
            }
            out += "\n";
        }
        out += pad + "}";
        return out;
    }

    if (value.is_array()) {
        const boost::json::array& arr = value.as_array();
        if (arr.empty()) {
            return "[]";
        }
        std::string out = "[\n";
        for (std::size_t i = 0; i < arr.size(); ++i) {
            out += padIn + prettyJson(arr[i], indent + 1);
            if (i + 1 < arr.size()) {
                out += ",";
            }
            out += "\n";
        }
        out += pad + "]";
        return out;
    }

    return boost::json::serialize(value);
}

} // namespace

// -------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(ConfigDialog, wxDialog)
    EVT_BUTTON(wxID_OK, ConfigDialog::onOk)
    EVT_BUTTON(wxID_CANCEL, ConfigDialog::onCancel)
    EVT_PG_CHANGED(wxID_ANY, ConfigDialog::onPropertyChanged)
    EVT_LIST_ITEM_SELECTED(wxID_ANY, ConfigDialog::onTemplateSelection)
wxEND_EVENT_TABLE()

// -------------------------------------------------------------------
ConfigDialog::ConfigDialog(wxWindow* parent, const config::AppConfig& cfg, sqlite3* db,
                           const std::string& configPath)
    : wxDialog(parent, wxID_ANY, L"配置编辑器",
                wxDefaultPosition, wxSize(1500, 950),
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , db_(db)
    , configPath_(configPath)
{
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    // Notebook with tabs
    notebook_ = new wxNotebook(this, wxID_ANY);

    // === Tab 1: 应用配置 (existing PropertyGrid) ===
    wxPanel* configTab = new wxPanel(notebook_);
    wxBoxSizer* configTabSizer = new wxBoxSizer(wxVERTICAL);

    propGrid_ = new wxPropertyGrid(configTab, wxID_ANY,
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

    // --- 代理后端选择（原「代理配置」，解决 P3：后端路径条件化可见） ---
    propGrid_->Append(new wxPropertyCategory(L"代理后端选择"));
    {
        wxBoolProperty* useSbProp = new wxBoolProperty(L"使用sing-box为代理终端", "proxy_use_singbox", cfg.proxy.use_singbox);
        propGrid_->Append(useSbProp);
    }
    propGrid_->Append(new wxIntProperty(L"SOCKS 监听端口", "proxy_socks_base_port", cfg.proxy.socks_base_port));

    // Xray 后端路径组（use_singbox=false 时可见）——指针存成员供 applyBackendVisibility 使用
    xrayBackendCat_ = new wxPropertyCategory(L"Xray 后端路径");
    propGrid_->Append(xrayBackendCat_);
    {
        wxFileProperty* xrayExecProp = new wxFileProperty(L"xray执行文件", "proxy_xray_executable", cfg.proxy.xray_executable);
        propGrid_->AppendIn(xrayBackendCat_, xrayExecProp);
        propGrid_->SetPropertyAttribute("proxy_xray_executable", wxPG_FILE_SHOW_FULL_PATH, (long)1);
        xrayExecProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
    }
    {
        wxFileProperty* assetDirProp = new wxFileProperty(L"xray_location_asset 目录", "proxy_xray_asset_dir", cfg.proxy.xray_asset_dir);
        propGrid_->AppendIn(xrayBackendCat_, assetDirProp);
        propGrid_->SetPropertyAttribute("proxy_xray_asset_dir", wxPG_FILE_SHOW_FULL_PATH, (long)1);
        assetDirProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
        propGrid_->SetPropertyAttribute("proxy_xray_asset_dir", wxPG_DIALOG_TITLE, L"选择 Xray 资源目录");
    }

    // sing-box 后端路径组（use_singbox=true 时可见）——指针存成员供 applyBackendVisibility 使用
    sbBackendCat_ = new wxPropertyCategory(L"sing-box 后端路径");
    propGrid_->Append(sbBackendCat_);
    {
        wxFileProperty* sbExecProp = new wxFileProperty(L"Sing-box执行文件", "proxy_singbox_executable", cfg.proxy.singbox_executable);
        propGrid_->AppendIn(sbBackendCat_, sbExecProp);
        propGrid_->SetPropertyAttribute("proxy_singbox_executable", wxPG_FILE_SHOW_FULL_PATH, (long)1);
        sbExecProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
        propGrid_->SetPropertyAttribute("proxy_singbox_executable", wxPG_DIALOG_TITLE, L"选择 Sing-box 可执行文件");
    }
    {
        wxFileProperty* sbAssetProp = new wxFileProperty(L"Sing-box资源目录", "proxy_singbox_asset_dir", cfg.proxy.singbox_asset_dir);
        propGrid_->AppendIn(sbBackendCat_, sbAssetProp);
        propGrid_->SetPropertyAttribute("proxy_singbox_asset_dir", wxPG_FILE_SHOW_FULL_PATH, (long)1);
        sbAssetProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
        propGrid_->SetPropertyAttribute("proxy_singbox_asset_dir", wxPG_DIALOG_TITLE, L"选择 Sing-box 资源目录");
    }

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

    // --- 监控悬浮窗 配置（原「监控代理进程」，解决 P5） ---
    propGrid_->Append(new wxPropertyCategory(L"监控悬浮窗"));
    propGrid_->Append(new wxBoolProperty(L"启用", "proxy_process_monitor_enabled", cfg.proxy_process_monitor.enabled));
    propGrid_->Append(new wxIntProperty(L"检测间隔(毫秒)", "proxy_process_monitor_check_interval_ms", cfg.proxy_process_monitor.checkIntervalMs));

    // --- 健康度评估 配置（新增父分类，2026-09-17 评审层级树） ---
    wxPropertyCategory* healthCat = new wxPropertyCategory(L"健康度评估");
    propGrid_->Append(healthCat);

    // 独立代理评估 子分类（解决 P2）：周期探活开关 + ProxyScorer 三权重
    wxPropertyCategory* indepEvalCat = new wxPropertyCategory(L"独立代理评估");
    propGrid_->AppendIn(healthCat, indepEvalCat);
    propGrid_->AppendIn(indepEvalCat, new wxBoolProperty(L"周期探活", "independent_probe_enabled", cfg.independent_probe.enabled));
    propGrid_->AppendIn(indepEvalCat, new wxFloatProperty(L"延迟权重(0.0-1.0)", "scoring_delay_weight", cfg.proxy.scoring_delay_weight));
    propGrid_->AppendIn(indepEvalCat, new wxFloatProperty(L"稳定性权重(0.0-1.0)", "scoring_stability_weight", cfg.proxy.scoring_stability_weight));
    propGrid_->AppendIn(indepEvalCat, new wxFloatProperty(L"历史权重(0.0-1.0)", "scoring_history_weight", cfg.proxy.scoring_history_weight));

    // 代理池评估 子分类（解决 P1/P4/P6）：evaluate.* 六项
    wxPropertyCategory* poolEvalCat = new wxPropertyCategory(L"代理池评估");
    propGrid_->AppendIn(healthCat, poolEvalCat);
    propGrid_->AppendIn(poolEvalCat, new wxIntProperty(L"评估间隔(秒)", "pool_eval_interval", cfg.standalone_pool.evaluate.intervalSec));
    propGrid_->AppendIn(poolEvalCat, new wxBoolProperty(L"报告健康结果", "pool_eval_report_health", cfg.standalone_pool.evaluate.reportHealth));
    propGrid_->AppendIn(poolEvalCat, new wxBoolProperty(L"自动剔除失效成员", "pool_eval_auto_prune", cfg.standalone_pool.evaluate.autoPruneDead));
    propGrid_->AppendIn(poolEvalCat, new wxIntProperty(L"剔除阈值(连续失败次数)", "pool_eval_prune_streak", cfg.standalone_pool.evaluate.pruneFailStreak));
    propGrid_->AppendIn(poolEvalCat, new wxBoolProperty(L"自动优化(预留记录式)", "pool_eval_auto_optimize", cfg.standalone_pool.evaluate.autoOptimize));
    // 探针 worker 数：0 = 禁用常驻探针池（ProxyProbePool::start 的 workerCount<=0 分支），
    // 1-N = 常驻 Xray 探针 worker 数（spec PoolConfigDialogAdjust）。
    // 注意：worker 数在池启动时一次性读取（StandaloneProxyPool.cpp:46 构造探针池），
    // 运行期修改需重启池后才生效。
    propGrid_->AppendIn(poolEvalCat, new wxIntProperty(L"探针 worker 数(0=禁用, 重启池后生效)", "pool_eval_probe_workers", cfg.standalone_pool.evaluate.probeWorkers));

    // --- 代理池配置（拆分，解决 P1/P4/P6：pool 本体 + observatory） ---
    // 方案甲：mode / observatory.type / observatory.samplingCount 三个预留字段（零运行时
    // 消费方）不在 UI 暴露，后端往返由 loadConfig() 起底的 editedConfig_ 原值透传。
    // 端口为期望值：实际监听端口由 PortManager 在池启动时分配（见 spec §4.1 脚注）。
    // 观测探测地址：恒被 test.url 覆盖（AppController::startProxyPool 用 test_url
    // 填充 probeUrl），后端字段保留作极端兜底但不暴露 UI（spec PoolConfigDialogAdjust）。
    propGrid_->Append(new wxPropertyCategory(L"代理池配置"));
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
    propGrid_->Append(new wxIntProperty(L"观测间隔(秒)", "pool_obs_interval", cfg.standalone_pool.observatory.intervalSec));
    propGrid_->Append(new wxIntProperty(L"观测超时(秒)", "pool_obs_timeout", cfg.standalone_pool.observatory.timeoutSec));

    propGrid_->SetPropertyAttributeAll(wxPG_BOOL_USE_CHECKBOX, true);

    configTabSizer->Add(propGrid_, 1, wxEXPAND | wxALL, 8);
    configTab->SetSizer(configTabSizer);

    notebook_->AddPage(configTab, L"应用配置");

    // === Tab 2: 模板配置 ===
    createTemplateTab();

    topSizer->Add(notebook_, 1, wxEXPAND | wxALL, 8);

    // Buttons
    wxSizer* btnSizer = CreateSeparatedButtonSizer(wxOK | wxCANCEL);
    if (btnSizer) topSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 8);

    SetSizer(topSizer);
    Layout();
    topSizer->Fit(this);
    topSizer->SetSizeHints(this);
    // 尺寸：宽度满足「模板列表 + 组件链编辑（Bundle 列表 | 组件表）+
    // 配置 JSON 预览」同屏布局；高度按屏幕可用区域自适应，下限 780
    // （此前硬编码 1100x650 会把「组件链编辑」Tab 的 JSON 预览框压成 0 高，
    //  只剩上方标签可见）。
    int screenW = 0;
    int screenH = 0;
    wxDisplaySize(&screenW, &screenH);
    int dialogH = 780;
    if (screenH > 0) {
        const int usable = static_cast<int>(screenH * 0.85);
        if (usable > dialogH) {
            dialogH = (usable > 1200) ? 1200 : usable;
        }
    }
    SetMinSize(wxSize(1100, 780));
    SetSize(wxSize(1440, dialogH));
    CentreOnScreen();

    // Load templates after layout
    loadTemplates();
    loadPipelineConfig();

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
    propGrid_->SetPropertyValue("proxy_singbox_executable", wxString(cfg.proxy.singbox_executable));
    propGrid_->SetPropertyValue("proxy_singbox_asset_dir", wxString(cfg.proxy.singbox_asset_dir));

    // ProxyProcessMonitor fields
    propGrid_->SetPropertyValue("proxy_process_monitor_enabled", cfg.proxy_process_monitor.enabled);
    propGrid_->SetPropertyValue("proxy_process_monitor_check_interval_ms", cfg.proxy_process_monitor.checkIntervalMs);

    // StandalonePool fields (方案甲：12 项；mode/type/samplingCount/destination 不经 UI，
    // 由 editedConfig_ = cfg 起底原值透传)
    propGrid_->SetPropertyValue("pool_enabled", cfg.standalone_pool.enabled);
    propGrid_->SetPropertyValue("pool_socks_port", cfg.standalone_pool.socksPort);
    propGrid_->SetPropertyValue("pool_api_port", cfg.standalone_pool.apiPort);
    propGrid_->SetPropertyValue("pool_balancer_strategy", wxString(cfg.standalone_pool.balancerStrategy));
    propGrid_->SetPropertyValue("pool_obs_interval", cfg.standalone_pool.observatory.intervalSec);
    propGrid_->SetPropertyValue("pool_obs_timeout", cfg.standalone_pool.observatory.timeoutSec);
    propGrid_->SetPropertyValue("pool_eval_interval", cfg.standalone_pool.evaluate.intervalSec);
    propGrid_->SetPropertyValue("pool_eval_report_health", cfg.standalone_pool.evaluate.reportHealth);
    propGrid_->SetPropertyValue("pool_eval_auto_prune", cfg.standalone_pool.evaluate.autoPruneDead);
    propGrid_->SetPropertyValue("pool_eval_prune_streak", cfg.standalone_pool.evaluate.pruneFailStreak);
    propGrid_->SetPropertyValue("pool_eval_auto_optimize", cfg.standalone_pool.evaluate.autoOptimize);
    propGrid_->SetPropertyValue("pool_eval_probe_workers", cfg.standalone_pool.evaluate.probeWorkers);

    // 独立代理评估 子分类回填（方案 B：周期探活 + ProxyScorer 三权重）
    propGrid_->SetPropertyValue("independent_probe_enabled", cfg.independent_probe.enabled);
    propGrid_->SetPropertyValue("scoring_delay_weight", cfg.proxy.scoring_delay_weight);
    propGrid_->SetPropertyValue("scoring_stability_weight", cfg.proxy.scoring_stability_weight);
    propGrid_->SetPropertyValue("scoring_history_weight", cfg.proxy.scoring_history_weight);
    applyBackendVisibility();
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
    editedConfig_.proxy.singbox_executable = propGrid_->GetPropertyValueAsString("proxy_singbox_executable").ToStdString();
    editedConfig_.proxy.singbox_asset_dir = propGrid_->GetPropertyValueAsString("proxy_singbox_asset_dir").ToStdString();

    // ProxyProcessMonitor fields
    editedConfig_.proxy_process_monitor.enabled = propGrid_->GetPropertyValueAsBool("proxy_process_monitor_enabled");
    editedConfig_.proxy_process_monitor.checkIntervalMs = propGrid_->GetPropertyValueAsInt("proxy_process_monitor_check_interval_ms");
    // Clamp to valid range
    if (editedConfig_.proxy_process_monitor.checkIntervalMs < 5000) editedConfig_.proxy_process_monitor.checkIntervalMs = 5000;
    if (editedConfig_.proxy_process_monitor.checkIntervalMs > 300000) editedConfig_.proxy_process_monitor.checkIntervalMs = 300000;

    // StandalonePool fields (方案甲：12 项读回；mode/type/samplingCount/destination 不经 UI，
    // 保留 loadConfig() 起底的原值；probeUrl 为运行期派生值，不落盘不读回)
    editedConfig_.standalone_pool.enabled = propGrid_->GetPropertyValueAsBool("pool_enabled");
    editedConfig_.standalone_pool.socksPort = static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_socks_port"));
    editedConfig_.standalone_pool.apiPort = static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_api_port"));
    editedConfig_.standalone_pool.balancerStrategy = propGrid_->GetPropertyValueAsString("pool_balancer_strategy").ToStdString();
    editedConfig_.standalone_pool.observatory.intervalSec = static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_obs_interval"));
    editedConfig_.standalone_pool.observatory.timeoutSec = static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_obs_timeout"));
    editedConfig_.standalone_pool.evaluate.intervalSec = static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_eval_interval"));
    editedConfig_.standalone_pool.evaluate.reportHealth = propGrid_->GetPropertyValueAsBool("pool_eval_report_health");
    editedConfig_.standalone_pool.evaluate.autoPruneDead = propGrid_->GetPropertyValueAsBool("pool_eval_auto_prune");
    editedConfig_.standalone_pool.evaluate.pruneFailStreak = static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_eval_prune_streak"));
    editedConfig_.standalone_pool.evaluate.autoOptimize = propGrid_->GetPropertyValueAsBool("pool_eval_auto_optimize");
    {
        int pw = static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_eval_probe_workers"));
        if (pw < 0) pw = 0;           // 0 = 禁用探针池
        if (pw > 64) pw = 64;         // 上限保护
        editedConfig_.standalone_pool.evaluate.probeWorkers = pw;
    }

    // 独立代理评估 子分类读回（方案 B）
    editedConfig_.independent_probe.enabled = propGrid_->GetPropertyValueAsBool("independent_probe_enabled");
    editedConfig_.proxy.scoring_delay_weight = propGrid_->GetPropertyValueAsDouble("scoring_delay_weight");
    editedConfig_.proxy.scoring_stability_weight = propGrid_->GetPropertyValueAsDouble("scoring_stability_weight");
    editedConfig_.proxy.scoring_history_weight = propGrid_->GetPropertyValueAsDouble("scoring_history_weight");

    // AutoTask fields
    editedConfig_.auto_task.steps = stepOrder_;
    editedConfig_.auto_task.notify_on_complete = propGrid_->GetPropertyValueAsBool("autotask_notify");

    return validateConfig();
}

void ConfigDialog::applyBackendVisibility() {
    if (!propGrid_) return;
    const bool useSingbox = propGrid_->GetPropertyValueAsBool("proxy_use_singbox");
    if (xrayBackendCat_) { xrayBackendCat_->Hide(useSingbox); }
    if (sbBackendCat_)   { sbBackendCat_->Hide(!useSingbox); }
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
    // Template file validation removed - using ConfigBuilder from database

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
        // observatory.destination 不暴露 UI：恒被 test.url 覆盖（spec PoolConfigDialogAdjust），
        // 后端保留透传，不再校验格式。
        if (editedConfig_.standalone_pool.evaluate.intervalSec < 1 || editedConfig_.standalone_pool.evaluate.intervalSec > 600) {
            wxMessageBox("评估间隔必须在 1 到 600 秒之间", "验证错误", wxOK | wxICON_ERROR);
            return false;
        }
        if (editedConfig_.standalone_pool.evaluate.pruneFailStreak < 1) {
            wxMessageBox("剔除阈值必须大于等于 1", "验证错误", wxOK | wxICON_ERROR);
            return false;
        }
        if (editedConfig_.standalone_pool.evaluate.probeWorkers < 0 ||
            editedConfig_.standalone_pool.evaluate.probeWorkers > 64) {
            wxMessageBox("探针 worker 数必须在 0 到 64 之间（0=禁用探针池）", "验证错误",
                         wxOK | wxICON_ERROR);
            return false;
        }
    }

    // Scoring weights 校验（0.0-1.0）
    {
        const double dw = editedConfig_.proxy.scoring_delay_weight;
        const double sw = editedConfig_.proxy.scoring_stability_weight;
        const double hw = editedConfig_.proxy.scoring_history_weight;
        if (dw < 0.0 || dw > 1.0 || sw < 0.0 || sw > 1.0 || hw < 0.0 || hw > 1.0) {
            wxMessageBox("代理评分权重必须在 0.0 到 1.0 之间", "验证错误", wxOK | wxICON_ERROR);
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
    if (propName == "proxy_use_singbox") {
        applyBackendVisibility();
    }
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

// ================================================================
// 模板配置 Tab 实现
// ================================================================

void ConfigDialog::createTemplateTab() {
    wxPanel* templateTab = new wxPanel(notebook_);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // 子 Notebook：模板配置 + 配置管线
    templateSubNotebook_ = new wxNotebook(templateTab, wxID_ANY);

    // === 子 Tab 1: 模板配置（原有功能）===
    wxPanel* templateConfigPanel = new wxPanel(templateSubNotebook_);
    wxBoxSizer* templateConfigSizer = new wxBoxSizer(wxVERTICAL);

    // 工具栏
    wxBoxSizer* toolSizer = new wxBoxSizer(wxHORIZONTAL);
    toolSizer->Add(new wxStaticText(templateConfigPanel, wxID_ANY, L"模板列表:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    
    templateList_ = new wxListCtrl(templateConfigPanel, wxID_ANY,
                                    wxDefaultPosition, wxDefaultSize,
                                    wxLC_REPORT);
    templateList_->InsertColumn(0, L"ID", wxLIST_FORMAT_LEFT, 140);
    templateList_->InsertColumn(1, L"类型", wxLIST_FORMAT_CENTER, 80);
    templateList_->InsertColumn(2, L"父模板", wxLIST_FORMAT_LEFT, 120);
    templateList_->InsertColumn(3, L"备注", wxLIST_FORMAT_LEFT, 250);
    templateList_->InsertColumn(4, L"启用", wxLIST_FORMAT_CENTER, 50);
    templateList_->InsertColumn(5, L"状态", wxLIST_FORMAT_CENTER, 60);
    toolSizer->Add(templateList_, 1, wxEXPAND | wxRIGHT, 8);

    wxBoxSizer* btnSizer = new wxBoxSizer(wxVERTICAL);
    wxButton* btnSave = new wxButton(templateConfigPanel, wxID_ANY, L"保存选中");
    btnSave->Bind(wxEVT_BUTTON, &ConfigDialog::onTemplateSave, this);
    wxButton* btnRefresh = new wxButton(templateConfigPanel, wxID_ANY, L"刷新");
    btnRefresh->Bind(wxEVT_BUTTON, &ConfigDialog::onTemplateRefresh, this);
    btnSizer->Add(btnSave, 0, wxEXPAND | wxBOTTOM, 4);
    btnSizer->Add(btnRefresh, 0, wxEXPAND);
    toolSizer->Add(btnSizer, 0, wxEXPAND);

    templateConfigSizer->Add(toolSizer, 0, wxEXPAND | wxALL, 8);

    // 编辑器
    templateConfigSizer->Add(new wxStaticText(templateConfigPanel, wxID_ANY, L"JSON 配置:"), 0, wxLEFT | wxBOTTOM, 8);
    templateEditor_ = new wxTextCtrl(templateConfigPanel, wxID_ANY, L"",
                                      wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE | wxTE_WORDWRAP | wxTE_DONTWRAP);
    templateEditor_->SetFont(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    templateConfigSizer->Add(templateEditor_, 1, wxEXPAND | wxALL, 8);

    templateConfigPanel->SetSizer(templateConfigSizer);
    templateSubNotebook_->AddPage(templateConfigPanel, L"模板配置");

    // === 子 Tab 2: 配置管线（新增）===
    createPipelineTab();

    // === 子 Tab 3: 组件包配置（新增）===
    createBundleTab();

    // === 子 Tab 4: 组件链编辑（阶段 8）===
    createChainEditTab();

    mainSizer->Add(templateSubNotebook_, 1, wxEXPAND | wxALL, 8);

    templateTab->SetSizer(mainSizer);
    notebook_->AddPage(templateTab, L"模板配置");
}

void ConfigDialog::loadTemplates() {
    if (!db_ || !templateList_ || !templateEditor_) return;

    db::FullConfigTemplateDAO dao(db_);
    dao.createTableIfNotExists();
    templates_ = dao.getAll();
    dao.seedDefaultTemplates();  // Ensure default templates exist
    templates_ = dao.getAll();

    templateList_->DeleteAllItems();
    for (const auto& item : templates_) {
        int row = templateList_->InsertItem(templates_.size() - 1, wxString(item.id));
        wxString coreTypeStr = (item.coreType == 2) ? L"Xray" : (item.coreType == 24) ? L"SingBox" : wxString::Format(L"%d", item.coreType);
        templateList_->SetItem(row, 1, coreTypeStr);
        templateList_->SetItem(row, 2, item.parentId.empty() ? L"(基础)" : wxString(item.parentId));
        templateList_->SetItem(row, 3, wxString(item.remarks));
        templateList_->SetItem(row, 4, item.enabled ? L"✓" : L"✗");
        wxString syncStr = dao.isTemplateInSync(item.id) ? L"✅" : (item.bundleChain.empty() ? L"✅" : L"⚠️");
        templateList_->SetItem(row, 5, syncStr);
    }

    // Select first item
    if (!templates_.empty()) {
        templateList_->SetItemState(0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        templateEditor_->ChangeValue(wxString(templates_[0].config));
    }

    // 同时加载组件包
    loadBundles();
}

void ConfigDialog::onTemplateSelection(wxListEvent& event) {
    long sel = templateList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= static_cast<long>(templates_.size())) return;

    const auto& item = templates_[sel];
    templateEditor_->ChangeValue(wxString(item.config));
    syncChainEditorFromSelection();
}

void ConfigDialog::onTemplateSave(wxCommandEvent& event) {
    if (!templateList_ || !templateEditor_ || !db_) return;

    long sel = templateList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= static_cast<long>(templates_.size())) {
        wxMessageBox(L"请先选择要保存的模板", L"提示", wxOK | wxICON_INFORMATION);
        return;
    }

    db::FullConfigTemplateDAO dao(db_);
    templates_[sel].config = templateEditor_->GetValue().ToStdString();
    
    if (dao.save(templates_[sel])) {
        wxMessageBox(L"模板已保存", L"成功", wxOK | wxICON_INFORMATION);
        loadTemplates();
    } else {
        wxMessageBox(L"保存失败，请查看日志", L"错误", wxOK | wxICON_ERROR);
    }
}

void ConfigDialog::onTemplateRefresh(wxCommandEvent& event) {
    loadTemplates();
}

// ================================================================
// 配置管线 Tab 实现
// ================================================================

void ConfigDialog::createPipelineTab() {
    wxPanel* pipelineTab = new wxPanel(templateSubNotebook_);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // === 左侧：管线列表 ===
    wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
    leftSizer->Add(new wxStaticText(pipelineTab, wxID_ANY, L"管线列表:"), 0, wxLEFT | wxBOTTOM, 8);
    
    pipelineList_ = new wxListCtrl(pipelineTab, wxID_ANY,
                                    wxDefaultPosition, wxSize(200, -1),
                                    wxLC_REPORT);
    pipelineList_->InsertColumn(0, L"管线名", wxLIST_FORMAT_LEFT, 120);
    pipelineList_->InsertColumn(1, L"模板", wxLIST_FORMAT_LEFT, 120);
    pipelineList_->InsertColumn(2, L"步骤数", wxLIST_FORMAT_CENTER, 60);
    pipelineList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& e) { onPipelineSelection(e); });
    leftSizer->Add(pipelineList_, 1, wxEXPAND | wxALL, 8);

    wxBoxSizer* leftBtnSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* btnDeletePipeline = new wxButton(pipelineTab, wxID_ANY, L"删除");
    btnDeletePipeline->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { onPipelineDelete(e); });
    leftBtnSizer->Add(btnDeletePipeline, 0, wxRIGHT, 4);
    wxButton* btnRefreshPipeline = new wxButton(pipelineTab, wxID_ANY, L"刷新");
    btnRefreshPipeline->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { loadPipelineConfig(); });
    leftBtnSizer->Add(btnRefreshPipeline, 0);
    leftSizer->Add(leftBtnSizer, 0, wxEXPAND | wxALL, 8);

    mainSizer->Add(leftSizer, 0, wxEXPAND);

    // === 中间：步骤编辑器 ===
    wxBoxSizer* centerSizer = new wxBoxSizer(wxVERTICAL);
    
    // 模板选择器
    wxBoxSizer* templateSizer = new wxBoxSizer(wxHORIZONTAL);
    templateSizer->Add(new wxStaticText(pipelineTab, wxID_ANY, L"模板:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    templateChoice_ = new wxChoice(pipelineTab, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr);
    templateChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent& e) { onTemplateChoice(e); });
    templateSizer->Add(templateChoice_, 1, wxEXPAND);
    centerSizer->Add(templateSizer, 0, wxEXPAND | wxALL, 8);

    // 步骤列表
    centerSizer->Add(new wxStaticText(pipelineTab, wxID_ANY, L"步骤列表:"), 0, wxLEFT | wxBOTTOM, 8);
    stepList_ = new wxListCtrl(pipelineTab, wxID_ANY,
                                wxDefaultPosition, wxSize(300, 150),
                                wxLC_REPORT);
    stepList_->InsertColumn(0, L"#", wxLIST_FORMAT_CENTER, 40);
    stepList_->InsertColumn(1, L"步骤类型", wxLIST_FORMAT_LEFT, 140);
    stepList_->InsertColumn(2, L"错误策略", wxLIST_FORMAT_CENTER, 80);
    stepList_->InsertColumn(3, L"参数摘要", wxLIST_FORMAT_LEFT, 200);
    stepList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& e) { onStepSelection(e); });
    centerSizer->Add(stepList_, 0, wxEXPAND | wxALL, 8);

    // 参数 JSON 编辑器
    centerSizer->Add(new wxStaticText(pipelineTab, wxID_ANY, L"参数 JSON:"), 0, wxLEFT | wxBOTTOM, 8);
    stepParamEditor_ = new wxTextCtrl(pipelineTab, wxID_ANY, L"",
                                       wxDefaultPosition, wxDefaultSize,
                                       wxTE_MULTILINE | wxTE_DONTWRAP);
    stepParamEditor_->SetFont(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    centerSizer->Add(stepParamEditor_, 1, wxEXPAND | wxALL, 8);

    // 步骤按钮
    wxBoxSizer* stepBtnSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* btnMoveUp = new wxButton(pipelineTab, wxID_ANY, L"上移");
    btnMoveUp->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { onStepMoveUp(e); });
    stepBtnSizer->Add(btnMoveUp, 0, wxRIGHT, 4);
    wxButton* btnMoveDown = new wxButton(pipelineTab, wxID_ANY, L"下移");
    btnMoveDown->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { onStepMoveDown(e); });
    stepBtnSizer->Add(btnMoveDown, 0, wxRIGHT, 4);
    wxButton* btnAddStep = new wxButton(pipelineTab, wxID_ANY, L"添加");
    btnAddStep->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { onStepAdd(e); });
    stepBtnSizer->Add(btnAddStep, 0, wxRIGHT, 4);
    wxButton* btnDeleteStep = new wxButton(pipelineTab, wxID_ANY, L"删除");
    btnDeleteStep->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { onStepDelete(e); });
    stepBtnSizer->Add(btnDeleteStep, 0, wxRIGHT, 4);
    wxButton* btnSavePipeline = new wxButton(pipelineTab, wxID_ANY, L"保存管线");
    btnSavePipeline->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { onPipelineSave(e); });
    stepBtnSizer->Add(btnSavePipeline, 0);
    centerSizer->Add(stepBtnSizer, 0, wxEXPAND | wxALL, 8);

    mainSizer->Add(centerSizer, 1, wxEXPAND);

    // === 右侧：模板预览 ===
    wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
    rightSizer->Add(new wxStaticText(pipelineTab, wxID_ANY, L"模板预览:"), 0, wxLEFT | wxBOTTOM, 8);

    wxBoxSizer* previewBtnSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* btnGenerate = new wxButton(pipelineTab, wxID_ANY, L"生成预览");
    btnGenerate->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { onGeneratePreview(e); });
    previewBtnSizer->Add(btnGenerate, 0, wxRIGHT, 4);
    wxButton* btnClear = new wxButton(pipelineTab, wxID_ANY, L"清空");
    btnClear->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { onClearPreview(e); });
    previewBtnSizer->Add(btnClear, 0);
    rightSizer->Add(previewBtnSizer, 0, wxEXPAND | wxBOTTOM, 8);

    previewEditor_ = new wxTextCtrl(pipelineTab, wxID_ANY, L"",
                                     wxDefaultPosition, wxDefaultSize,
                                     wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    previewEditor_->SetFont(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    rightSizer->Add(previewEditor_, 1, wxEXPAND | wxBOTTOM, 8);

    previewStatus_ = new wxStaticText(pipelineTab, wxID_ANY, L"");
    rightSizer->Add(previewStatus_, 0, wxEXPAND | wxALL, 8);

    mainSizer->Add(rightSizer, 0, wxEXPAND);

    pipelineTab->SetSizer(mainSizer);
    templateSubNotebook_->AddPage(pipelineTab, L"配置管线");
}

void ConfigDialog::loadPipelineConfig() {
    if (!pipelineList_ || !templateChoice_) return;

    // 读取 config.json
    boost::json::value root;
    if (!readConfigJson(root)) {
        wxLogWarning("加载 config.json 失败，使用默认空管线");
        // 初始化默认空管线
        initializeDefaultPipelines();
        refreshPipelineList();
        fillTemplateChoice();
        return;
    }

    // 解析 config_pipeline 段
    pipelines_.clear();
    bool hasPipelineConfig = false;
    if (root.is_object() && root.as_object().contains("config_pipeline")) {
        const auto& pipelineObj = root.as_object().at("config_pipeline");
        if (pipelineObj.is_object() && !pipelineObj.as_object().empty()) {
            hasPipelineConfig = true;
            for (const auto& [key, val] : pipelineObj.as_object()) {
                if (!val.is_object()) continue;
                config::PipelineDefinition def;
                const auto& obj = val.as_object();
                if (obj.contains("templateId") && obj.at("templateId").is_string()) {
                    def.templateId = obj.at("templateId").as_string();
                }
                if (obj.contains("steps") && obj.at("steps").is_array()) {
                    for (const auto& stepVal : obj.at("steps").as_array()) {
                        if (!stepVal.is_object()) continue;
                        config::StepConfig step;
                        const auto& stepObj = stepVal.as_object();
                        if (stepObj.contains("type") && stepObj.at("type").is_string()) {
                            step.type = stepObj.at("type").as_string();
                        }
                        if (stepObj.contains("params") && stepObj.at("params").is_object()) {
                            step.params = stepObj.at("params").as_object();
                        }
                        if (stepObj.contains("onError") && stepObj.at("onError").is_string()) {
                            step.onError = stepObj.at("onError").as_string();
                        }
                        def.steps.push_back(step);
                    }
                }
                pipelines_[key] = def;
            }
        }
    }

    // 如果没有管线配置，初始化默认空管线
    if (!hasPipelineConfig) {
        initializeDefaultPipelines();
    }

    // 填充管线列表
    refreshPipelineList();

    // 填充模板下拉框
    fillTemplateChoice();
}

void ConfigDialog::initializeDefaultPipelines() {
    // 6 个固定管线名
    static const std::vector<std::string> defaultNames = {
        "test", "test_singbox",
        "standalone", "standalone_singbox",
        "pool", "pool_singbox"
    };
    for (const auto& name : defaultNames) {
        if (pipelines_.find(name) == pipelines_.end()) {
            config::PipelineDefinition def;
            def.templateId = "";
            def.steps.clear();
            pipelines_[name] = def;
        }
    }
}

void ConfigDialog::fillTemplateChoice() {
    templateChoice_->Clear();
    std::vector<std::string> templateIds = getAvailableTemplateIds();
    for (const auto& id : templateIds) {
        templateChoice_->Append(wxString(id.c_str(), wxConvUTF8));
    }
}

void ConfigDialog::savePipelineConfig() {
    boost::json::value root;
    if (!readConfigJson(root)) {
        wxMessageBox(L"无法读取 config.json", L"错误", wxOK | wxICON_ERROR);
        return;
    }

    // 更新 config_pipeline 段
    boost::json::object pipelineObj;
    for (const auto& [name, def] : pipelines_) {
        boost::json::object defObj;
        defObj["templateId"] = def.templateId;
        
        boost::json::array stepsArr;
        for (const auto& step : def.steps) {
            boost::json::object stepObj;
            stepObj["type"] = step.type;
            stepObj["params"] = step.params;
            stepObj["onError"] = step.onError;
            stepsArr.push_back(stepObj);
        }
        defObj["steps"] = stepsArr;
        pipelineObj[name] = defObj;
    }

    root.as_object()["config_pipeline"] = pipelineObj;

    if (!writeConfigJson(root)) {
        wxMessageBox(L"保存 config.json 失败", L"错误", wxOK | wxICON_ERROR);
        return;
    }

    wxMessageBox(L"管线保存成功，重启应用后生效", L"成功", wxOK | wxICON_INFORMATION);
}

bool ConfigDialog::readConfigJson(boost::json::value& root) const {
    try {
        std::ifstream file(configPath_);
        if (!file.is_open()) return false;
        root = boost::json::parse(file);
        return true;
    } catch (const std::exception& e) {
        wxLogError("config.json 解析失败: %s", e.what());
        return false;
    }
}

bool ConfigDialog::writeConfigJson(const boost::json::value& root) const {
    try {
        // 事务性写入：先写临时文件再 rename
        std::string tempPath = configPath_ + ".tmp";
        std::ofstream file(tempPath);
        if (!file.is_open()) return false;
        
        std::string json = boost::json::serialize(root);
        file << json;
        file.close();

        // 备份原文件
        std::string backupPath = configPath_ + ".bak";
        std::filesystem::copy_file(configPath_, backupPath, 
                                     std::filesystem::copy_options::overwrite_existing);
        
        // 替换原文件
        std::filesystem::rename(tempPath, configPath_);
        return true;
    } catch (const std::exception& e) {
        wxLogError("写入 config.json 失败: %s", e.what());
        return false;
    }
}

void ConfigDialog::onPipelineSelection(wxListEvent& event) {
    long sel = pipelineList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0) return;

    // 获取管线名（从 list item 文本）
    wxString nameStr = pipelineList_->GetItemText(sel, 0);
    currentPipelineName_ = nameStr.ToStdString();
    currentStepIndex_ = -1;

    // 填充模板选择器
    auto it = pipelines_.find(currentPipelineName_);
    if (it != pipelines_.end() && !it->second.templateId.empty()) {
        for (int i = 0; i < templateChoice_->GetCount(); i++) {
            if (templateChoice_->GetString(i) == wxString(it->second.templateId.c_str(), wxConvUTF8)) {
                templateChoice_->SetSelection(i);
                break;
            }
        }
    }

    refreshStepList();
}

void ConfigDialog::onPipelineDelete(wxCommandEvent& event) {
    long sel = pipelineList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0) {
        wxMessageBox(L"请先选择要删除的管线", L"提示", wxOK | wxICON_INFORMATION);
        return;
    }

    wxString nameStr = pipelineList_->GetItemText(sel, 0);
    std::string name = nameStr.ToStdString();

    if (wxMessageBox(L"确定删除管线「" + nameStr + L"」？", L"确认删除", 
                      wxYES_NO | wxICON_QUESTION) == wxYES) {
        pipelines_.erase(name);
        refreshPipelineList();
    }
}

void ConfigDialog::onPipelineSave(wxCommandEvent& event) {
    // 验证管线名
    static const std::set<std::string> validNames = {
        "test", "test_singbox",
        "standalone", "standalone_singbox",
        "pool", "pool_singbox"
    };
    if (validNames.find(currentPipelineName_) == validNames.end()) {
        wxMessageBox(L"管线名无效：仅支持 test/standalone/pool × xray/singbox", 
                      L"验证错误", wxOK | wxICON_ERROR);
        return;
    }

    // 收集当前编辑的管线定义
    config::PipelineDefinition def;
    if (templateChoice_->GetSelection() >= 0) {
        def.templateId = templateChoice_->GetStringSelection().ToStdString();
    }

    // 从步骤列表收集步骤
    for (int i = 0; i < stepList_->GetItemCount(); i++) {
        config::StepConfig step;
        step.type = stepList_->GetItemText(i, 1).ToStdString();
        step.onError = stepList_->GetItemText(i, 2).ToStdString();

        // 解析参数摘要（从 stepParamEditor_ 获取当前编辑的参数）
        if (i == currentStepIndex_) {
            std::string paramStr = stepParamEditor_->GetValue().ToStdString();
            if (!paramStr.empty()) {
                try {
                    boost::json::value paramVal = boost::json::parse(paramStr);
                    if (paramVal.is_object()) {
                        step.params = paramVal.as_object();
                    }
                } catch (...) {
                    // 忽略解析错误
                }
            }
        }
        def.steps.push_back(step);
    }

    pipelines_[currentPipelineName_] = def;
    savePipelineConfig();
}

void ConfigDialog::onStepSelection(wxListEvent& event) {
    long sel = stepList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0) return;

    currentStepIndex_ = static_cast<int>(sel);
    refreshStepParamEditor();
}

void ConfigDialog::onStepAdd(wxCommandEvent& event) {
    std::vector<std::string> stepTypes = getAvailableStepTypes();
    wxArrayString names;
    for (const auto& t : stepTypes) {
        names.Add(wxString(getStepDisplayName(t).c_str(), wxConvUTF8));
    }

    wxSingleChoiceDialog dlg(this, L"选择步骤类型:", L"添加步骤", names);
    if (dlg.ShowModal() == wxID_OK) {
        int sel = dlg.GetSelection();
        if (sel >= 0 && sel < static_cast<int>(stepTypes.size())) {
            config::PipelineDefinition& def = pipelines_[currentPipelineName_];
            config::StepConfig step;
            step.type = stepTypes[sel];
            step.onError = "continue";
            def.steps.push_back(step);
            refreshStepList();
        }
    }
}

void ConfigDialog::onStepDelete(wxCommandEvent& event) {
    if (currentStepIndex_ < 0) {
        wxMessageBox(L"请先选择要删除的步骤", L"提示", wxOK | wxICON_INFORMATION);
        return;
    }

    if (wxMessageBox(L"确定删除当前步骤？", L"确认删除", 
                      wxYES_NO | wxICON_QUESTION) == wxYES) {
        long sel = currentStepIndex_;
        stepList_->DeleteItem(sel);
        currentStepIndex_ = -1;
        refreshStepList();
    }
}

void ConfigDialog::onStepMoveUp(wxCommandEvent& event) {
    if (currentStepIndex_ <= 0) return;

    // 交换步骤位置
    config::PipelineDefinition& def = pipelines_[currentPipelineName_];
    if (currentStepIndex_ >= static_cast<int>(def.steps.size())) return;

    std::swap(def.steps[currentStepIndex_ - 1], def.steps[currentStepIndex_]);
    currentStepIndex_--;
    refreshStepList();
}

void ConfigDialog::onStepMoveDown(wxCommandEvent& event) {
    if (currentStepIndex_ < 0) return;

    config::PipelineDefinition& def = pipelines_[currentPipelineName_];
    if (currentStepIndex_ >= static_cast<int>(def.steps.size()) - 1) return;

    std::swap(def.steps[currentStepIndex_], def.steps[currentStepIndex_ + 1]);
    currentStepIndex_++;
    refreshStepList();
}

void ConfigDialog::onTemplateChoice(wxCommandEvent& event) {
    if (currentPipelineName_.empty()) return;

    auto it = pipelines_.find(currentPipelineName_);
    if (it != pipelines_.end()) {
        it->second.templateId = templateChoice_->GetStringSelection().ToStdString();
        refreshPipelineList();
    }
}

void ConfigDialog::onGeneratePreview(wxCommandEvent& event) {
    boost::json::value preview;
    if (generatePreviewConfig(preview)) {
        std::string formatted = formatJson(preview);
        previewEditor_->ChangeValue(wxString(formatted.c_str(), wxConvUTF8));
        previewStatus_->SetLabel(L"✅ 生成成功");
    }
}

void ConfigDialog::onClearPreview(wxCommandEvent& event) {
    previewEditor_->ChangeValue(L"");
    previewStatus_->SetLabel(L"");
}

bool ConfigDialog::generatePreviewConfig(boost::json::value& config) const {
    if (currentPipelineName_.empty()) {
        previewStatus_->SetLabel(L"错误: 未选择管线");
        return false;
    }

    auto it = pipelines_.find(currentPipelineName_);
    if (it == pipelines_.end()) {
        previewStatus_->SetLabel(L"错误: 管线不存在");
        return false;
    }

    const auto& pipeline = it->second;

    // 1. 从 DB 加载模板配置
    if (pipeline.templateId.empty()) {
        previewStatus_->SetLabel(L"错误: 未选择模板");
        return false;
    }

    db::FullConfigTemplateDAO dao(db_);
    auto templateOpt = dao.getById(pipeline.templateId);
    if (!templateOpt.has_value()) {
        previewStatus_->SetLabel(L"错误: 模板加载失败");
        return false;
    }

    // 2. 解析模板 JSON
    boost::json::value configVal;
    try {
        configVal = boost::json::parse(templateOpt->config);
    } catch (const std::exception& e) {
        previewStatus_->SetLabel(L"错误: 模板 JSON 解析失败");
        return false;
    }

    // 3. 构建 PipelineContext
    config::PipelineContext ctx;
    ctx.db = db_;
    // 判断是否为 SingBox 管线
    if (currentPipelineName_.find("singbox") != std::string::npos) {
        ctx.proxyType = config::ProxyType::SingBox;
    } else {
        ctx.proxyType = config::ProxyType::Xray;
    }

    // 4. 执行各步骤
    const auto& registry = config::StepRegistry::instance();
    for (const auto& step : pipeline.steps) {
        auto handler = registry.getHandler(step.type);
        if (!handler) continue;

        try {
            handler->execute(configVal.as_object(), step.params, ctx);
        } catch (const std::exception& e) {
            previewStatus_->SetLabel(
                wxString::Format(L"步骤执行错误 [%s]: %s", 
                                 wxString(step.type.c_str(), wxConvUTF8),
                                 wxString(e.what(), wxConvUTF8)));
            return false;
        }
    }

    config = configVal;
    return true;
}

bool ConfigDialog::validateStepParams(
    const std::string& type, 
    const boost::json::object& params) const {
    
    if (type == "add_outbound") {
        if (!params.contains("tag")) return false;
    } else if (type == "merge_routing") {
        if (!params.contains("inboundTag")) return false;
    } else if (type == "add_inbound") {
        if (!params.contains("tag")) return false;
    } else if (type == "add_observatory") {
        if (!params.contains("subjectSelector")) return false;
    } else if (type == "add_balancer") {
        if (!params.contains("tag")) return false;
    } else if (type == "add_api") {
        if (!params.contains("tag")) return false;
    } else if (type == "modify_field") {
        if (!params.contains("path") || !params.contains("value")) return false;
    }
    return true;
}

void ConfigDialog::refreshStepList() {
    if (!stepList_) return;

    stepList_->DeleteAllItems();
    if (currentPipelineName_.empty()) return;

    auto it = pipelines_.find(currentPipelineName_);
    if (it == pipelines_.end()) return;

    const auto& steps = it->second.steps;
    for (size_t i = 0; i < steps.size(); i++) {
        int row = stepList_->InsertItem(i, wxString::Format(L"%d", static_cast<int>(i + 1)));
        stepList_->SetItem(row, 1, wxString(steps[i].type.c_str(), wxConvUTF8));
        stepList_->SetItem(row, 2, wxString(steps[i].onError.c_str(), wxConvUTF8));
        
        // 参数摘要（前 50 字符）
        std::string paramStr = formatJson(boost::json::value(steps[i].params));
        if (paramStr.length() > 50) paramStr = paramStr.substr(0, 50) + "...";
        stepList_->SetItem(row, 3, wxString(paramStr.c_str(), wxConvUTF8));
    }

    // 重新选中之前选中的步骤
    if (currentStepIndex_ >= 0 && currentStepIndex_ < static_cast<int>(steps.size())) {
        stepList_->SetItemState(currentStepIndex_, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    }
}

void ConfigDialog::refreshStepParamEditor() {
    if (!stepParamEditor_ || currentStepIndex_ < 0) return;

    auto it = pipelines_.find(currentPipelineName_);
    if (it == pipelines_.end() || currentStepIndex_ >= static_cast<int>(it->second.steps.size())) {
        stepParamEditor_->ChangeValue(L"{}");
        return;
    }

    std::string paramStr = formatJson(boost::json::value(it->second.steps[currentStepIndex_].params));
    stepParamEditor_->ChangeValue(wxString(paramStr.c_str(), wxConvUTF8));
}

void ConfigDialog::refreshPipelineList() {
    if (!pipelineList_) return;

    pipelineList_->DeleteAllItems();
    int row = 0;
    for (const auto& [name, def] : pipelines_) {
        pipelineList_->InsertItem(row, wxString(name.c_str(), wxConvUTF8));
        pipelineList_->SetItem(row, 1, wxString(def.templateId.c_str(), wxConvUTF8));
        pipelineList_->SetItem(row, 2, wxString::Format(L"%d", static_cast<int>(def.steps.size())));
        row++;
    }
}

std::vector<std::string> ConfigDialog::getAvailableTemplateIds() const {
    std::vector<std::string> ids;
    db::FullConfigTemplateDAO dao(db_);
    auto allTemplates = dao.getAll();
    for (const auto& t : allTemplates) {
        ids.push_back(t.id);
    }
    return ids;
}

std::vector<std::string> ConfigDialog::getAvailableStepTypes() const {
    const auto& registry = config::StepRegistry::instance();
    return registry.getRegisteredTypes();
}

std::string ConfigDialog::getStepDisplayName(const std::string& type) const {
    static const std::map<std::string, std::string> displayNames = {
        {"replace_variables", "变量替换"},
        {"add_outbound", "添加出站"},
        {"merge_routing", "合并路由"},
        {"add_inbound", "添加入站"},
        {"add_observatory", "添加观测器"},
        {"add_balancer", "添加负载均衡"},
        {"add_stats", "启用统计"},
        {"add_policy", "添加策略"},
        {"add_api", "添加 API"},
        {"modify_field", "修改字段"}
    };
    auto it = displayNames.find(type);
    return (it != displayNames.end()) ? it->second : type;
}

std::string ConfigDialog::formatJson(const boost::json::value& value) const {
    return boost::json::serialize(value);
}

// ================================================================
// 组件包配置 tab
// ================================================================

void ConfigDialog::createBundleTab() {
    wxPanel* bundleTab = new wxPanel(templateSubNotebook_);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    // === 左侧：组件包列表 ===
    wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
    leftSizer->Add(new wxStaticText(bundleTab, wxID_ANY, L"组件包列表:"), 0, wxLEFT | wxBOTTOM, 8);

    bundleList_ = new wxListCtrl(bundleTab, wxID_ANY,
                                  wxDefaultPosition, wxSize(280, -1),
                                  wxLC_REPORT);
    bundleList_->InsertColumn(0, L"ID", wxLIST_FORMAT_LEFT, 140);
    bundleList_->InsertColumn(1, L"核心", wxLIST_FORMAT_CENTER, 60);
    bundleList_->InsertColumn(2, L"类型", wxLIST_FORMAT_CENTER, 80);
    bundleList_->InsertColumn(3, L"组件数", wxLIST_FORMAT_CENTER, 60);
    bundleList_->InsertColumn(4, L"版本", wxLIST_FORMAT_CENTER, 50);
    bundleList_->InsertColumn(5, L"启用", wxLIST_FORMAT_CENTER, 50);
    bundleList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& e) { onBundleSelection(e); });
    leftSizer->Add(bundleList_, 1, wxEXPAND | wxALL, 8);

    // 按钮栏
    wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* btnRefresh = new wxButton(bundleTab, wxID_ANY, L"刷新");
    btnRefresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { onBundleRefresh(e); });
    btnSizer->Add(btnRefresh, 0, wxRIGHT, 4);
    wxButton* btnDelete = new wxButton(bundleTab, wxID_ANY, L"删除");
    btnDelete->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { onBundleDelete(e); });
    btnSizer->Add(btnDelete, 0, wxRIGHT, 4);
    wxButton* btnSave = new wxButton(bundleTab, wxID_ANY, L"保存");
    btnSave->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { onBundleSave(e); });
    btnSizer->Add(btnSave, 0);
    leftSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 8);

    mainSizer->Add(leftSizer, 0, wxEXPAND);

    // === 右侧：JSON 编辑器 ===
    wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
    rightSizer->Add(new wxStaticText(bundleTab, wxID_ANY, L"JSON 配置:"), 0, wxLEFT | wxBOTTOM, 8);
    bundleEditor_ = new wxTextCtrl(bundleTab, wxID_ANY, L"",
                                    wxDefaultPosition, wxDefaultSize,
                                    wxTE_MULTILINE | wxTE_WORDWRAP | wxTE_DONTWRAP);
    bundleEditor_->SetFont(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    rightSizer->Add(bundleEditor_, 1, wxEXPAND | wxALL, 8);

    mainSizer->Add(rightSizer, 1, wxEXPAND);

    bundleTab->SetSizer(mainSizer);
    templateSubNotebook_->AddPage(bundleTab, L"组件包配置");
}

void ConfigDialog::loadBundles() {
    if (!db_ || !bundleList_ || !bundleEditor_) return;

    config::ComponentBundleDAO dao(db_);
    dao.createTableIfNotExists();
    bundles_ = dao.getAll();

    bundleList_->DeleteAllItems();
    for (const auto& bundle : bundles_) {
        int row = bundleList_->InsertItem(static_cast<long>(bundles_.size()) - 1, wxString(bundle.id));
        wxString coreTypeStr = (bundle.coreType == config::CoreType::XRAY) ? L"Xray"
                              : (bundle.coreType == config::CoreType::SINGBOX) ? L"SingBox"
                              : wxString::Format(L"%d", static_cast<int>(bundle.coreType));
        bundleList_->SetItem(row, 1, coreTypeStr);

        wxString typeStr = (bundle.templateType == config::TemplateType::BASE) ? L"基础"
                          : (bundle.templateType == config::TemplateType::TEST) ? L"测试"
                          : (bundle.templateType == config::TemplateType::STANDALONE) ? L"独立"
                          : (bundle.templateType == config::TemplateType::POOL) ? L"池"
                          : wxString::Format(L"%d", static_cast<int>(bundle.templateType));
        bundleList_->SetItem(row, 2, typeStr);

        bundleList_->SetItem(row, 3, wxString::Format(L"%d", static_cast<int>(bundle.components.size())));
        bundleList_->SetItem(row, 4, wxString::Format(L"%d", bundle.version));
        bundleList_->SetItem(row, 5, bundle.enabled ? L"✓" : L"✗");
    }

    // 选中第一项
    if (!bundles_.empty()) {
        bundleList_->SetItemState(0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                  wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        wxListEvent selEvent;
        onBundleSelection(selEvent);
    } else {
        bundleEditor_->ChangeValue(L"");
    }
}

void ConfigDialog::onBundleSelection(wxListEvent& event) {
    if (!bundleList_ || !bundleEditor_) return;
    long sel = bundleList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= static_cast<long>(bundles_.size())) return;

    const auto& bundle = bundles_[sel];

    // 构建可编辑 JSON
    boost::json::object root;
    root["id"] = bundle.id;
    root["name"] = bundle.name;
    root["description"] = bundle.description;
    root["coreType"] = static_cast<int>(bundle.coreType);
    root["templateType"] = static_cast<int>(bundle.templateType);
    root["version"] = bundle.version;
    root["enabled"] = bundle.enabled;

    boost::json::array comps;
    for (const auto& comp : bundle.components) {
        boost::json::object compObj;
        compObj["type"] = static_cast<int>(comp.type);

        boost::json::object paramsObj;
        for (const auto& [key, value] : comp.params) {
            paramsObj[std::string(key)] = value;
        }
        compObj["params"] = std::move(paramsObj);

        if (!comp.id.empty()) {
            compObj["id"] = comp.id;
        }
        comps.push_back(std::move(compObj));
    }
    root["components"] = std::move(comps);

    bundleEditor_->ChangeValue(wxString(boost::json::serialize(root)));
}

void ConfigDialog::onBundleSave(wxCommandEvent& event) {
    if (!bundleList_ || !bundleEditor_ || !db_) return;

    long sel = bundleList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= static_cast<long>(bundles_.size())) {
        wxMessageBox(L"请先选择要保存的组件包", L"提示", wxOK | wxICON_INFORMATION);
        return;
    }

    // 解析 JSON
    auto parseResult = boost::json::parse(bundleEditor_->GetValue().ToStdString());
    if (parseResult.is_null()) {
        wxMessageBox(L"JSON 解析失败", L"错误", wxOK | wxICON_ERROR);
        return;
    }

    auto& root = parseResult.as_object();

    config::ComponentBundle bundle = bundles_[sel];

    if (auto it = root.find("id"); it != root.end() && it->value().is_string()) {
        bundle.id = std::string(it->value().as_string());
    }
    if (auto it = root.find("name"); it != root.end() && it->value().is_string()) {
        bundle.name = std::string(it->value().as_string());
    }
    if (auto it = root.find("description"); it != root.end() && it->value().is_string()) {
        bundle.description = std::string(it->value().as_string());
    }
    if (auto it = root.find("coreType"); it != root.end() && it->value().is_number()) {
        bundle.coreType = static_cast<config::CoreType>(static_cast<int>(it->value().as_int64()));
    }
    if (auto it = root.find("templateType"); it != root.end() && it->value().is_number()) {
        bundle.templateType = static_cast<config::TemplateType>(static_cast<int>(it->value().as_int64()));
    }
    if (auto it = root.find("version"); it != root.end() && it->value().is_number()) {
        bundle.version = static_cast<int>(it->value().as_int64());
    }
    if (auto it = root.find("enabled"); it != root.end() && it->value().is_bool()) {
        bundle.enabled = it->value().as_bool();
    }

    // 解析组件列表
    bundle.components.clear();
    if (auto it = root.find("components"); it != root.end() && it->value().is_array()) {
        for (const auto& compVal : it->value().as_array()) {
            if (!compVal.is_object()) continue;
            auto& compObj = compVal.as_object();
            config::ComponentConfig comp;

            if (auto typeIt = compObj.find("type"); typeIt != compObj.end() && typeIt->value().is_number()) {
                comp.type = static_cast<config::ComponentType>(static_cast<int>(typeIt->value().as_int64()));
            }
            if (auto paramsIt = compObj.find("params"); paramsIt != compObj.end() && paramsIt->value().is_object()) {
                for (const auto& [key, value] : paramsIt->value().as_object()) {
                    comp.params[std::string(key)] = value;
                }
            }
            if (auto idIt = compObj.find("id"); idIt != compObj.end() && idIt->value().is_string()) {
                comp.id = std::string(idIt->value().as_string());
            }

            bundle.components.push_back(std::move(comp));
        }
    }

    config::ComponentBundleDAO dao(db_);
    if (dao.save(bundle)) {
        wxMessageBox(L"组件包已保存", L"成功", wxOK | wxICON_INFORMATION);
        loadBundles();
    } else {
        wxMessageBox(L"保存失败", L"错误", wxOK | wxICON_ERROR);
    }
}

void ConfigDialog::onBundleDelete(wxCommandEvent& event) {
    if (!bundleList_ || !db_) return;

    long sel = bundleList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= static_cast<long>(bundles_.size())) {
        wxMessageBox(L"请先选择要删除的组件包", L"提示", wxOK | wxICON_INFORMATION);
        return;
    }

    wxString confirmMsg = wxString::Format(L"确定要删除组件包 '%s' 吗？", bundles_[sel].id.c_str());
    if (wxMessageBox(confirmMsg, L"确认删除", wxYES_NO | wxICON_QUESTION) != wxYES) {
        return;
    }

    config::ComponentBundleDAO dao(db_);
    if (dao.deleteById(bundles_[sel].id)) {
        wxMessageBox(L"已删除", L"成功", wxOK | wxICON_INFORMATION);
        loadBundles();
    } else {
        wxMessageBox(L"删除失败", L"错误", wxOK | wxICON_ERROR);
    }
}

void ConfigDialog::onBundleRefresh(wxCommandEvent& event) {
    loadBundles();
}

// ================================================================
// 组件链编辑 Tab（阶段 8.2 Bundle 选择器 + 参数编辑 / 8.4 CoreType 适配
// / 8.5 冲突提示）—— 编辑当前模板的 bundleChain
// ================================================================

void ConfigDialog::createChainEditTab() {
    wxPanel* chainTab = new wxPanel(templateSubNotebook_);

    // 配置 JSON 预览（resolveToConfig 的输出，占位符已替换）。
    // 上下两个独立 panel（编辑区 / 预览区），预览框有 220px 初始高度与
    // 160px 最小高度——对话框高度不足时 sizer 曾把预览框压成 0 高
    // （只剩上方标签可见）。
    //
    // 父子关系约束：wxSizer::SetContainingWindow 会在 Debug 下断言
    // 「sizer 管理的所有控件的 parent 必须是该 sizer 所属窗口」。因此
    // editPanel 的 sizer 里不能放 parent 为 chainTab 的控件——必须先建
    // editPanel、再以它为 parent 建控件。同理 previewPanel。
    wxPanel* previewPanel = new wxPanel(chainTab);
    wxStaticText* previewLabel = new wxStaticText(previewPanel, wxID_ANY,
        L"配置 JSON 预览（resolveToConfig 输出）:");
    wxTextCtrl* previewBox = new wxTextCtrl(previewPanel, wxID_ANY, wxEmptyString,
                                           wxDefaultPosition, wxSize(-1, 220),
                                           wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    previewBox->SetFont(wxFont(9, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL,
                               wxFONTWEIGHT_NORMAL));
    // 硬下限：对话框高度不足时 sizer 会把可扩展控件压到 min size，
    // 不设 min size 时 wxTextCtrl 可被压成 0 高（只剩上方标签可见）。
    previewBox->SetMinSize(wxSize(200, 160));
    wxBoxSizer* previewSizer = new wxBoxSizer(wxVERTICAL);
    previewSizer->Add(previewLabel, 0, wxEXPAND | wxALL, 4);
    previewSizer->Add(previewBox, 1, wxEXPAND | wxALL, 4);
    previewPanel->SetSizer(previewSizer);

    // 编辑区：控件一律以 editPanel 为 parent（不可用 chainTab）。
    wxPanel* editPanel = new wxPanel(chainTab);
    wxStaticText* editHint = new wxStaticText(editPanel, wxID_ANY,
        L"左侧编辑组件链（顺序添加 Bundle + 参数 JSON），「解析」后右侧显示合并组件并生成下方配置预览；"
        L"「保存」会把链写入当前模板（templateType=bundleChain）。");
    wxButton* editSaveBtn = new wxButton(editPanel, wxID_ANY, L"保存链到当前模板");
    editSaveBtn->Bind(wxEVT_BUTTON, &ConfigDialog::onChainSave, this);
    wxStaticText* editStatus = new wxStaticText(editPanel, wxID_ANY, L"");
    chainStatus_ = editStatus;

    BundleChainEditor* chainEditor = new BundleChainEditor(editPanel, db_);
    chainEditor_ = chainEditor;
    chainEditor_->onResolved = [this](const config::BundleChainResolveResult& result) {
        componentListPanel_->setComponents(result.components, result.sources);
        componentListPanel_->setConflictStatus(result.conflicts);
        wxString statusText;
        if (result.success) {
            statusText = wxString::Format(L"解析成功：%d 个组件，%d 个冲突",
                static_cast<int>(result.components.size()),
                static_cast<int>(result.conflicts.size()));
        } else {
            statusText = L"解析失败：" + wxString::FromUTF8(result.errorMessage);
        }
        chainStatus_->SetLabel(statusText);

        // 配置预览（resolveToConfig 填充 result.config）
        if (!result.success) {
            showChainPreview(std::string("// 解析失败：") + result.errorMessage);
        } else if (result.config.is_null()) {
            showChainPreview(std::string("// 解析成功，但生成的配置为空"));
        } else {
            showChainPreview(prettyJson(result.config, 0));
        }
    };

    ComponentListPanel* componentPanel = new ComponentListPanel(editPanel, db_);
    componentListPanel_ = componentPanel;

    wxBoxSizer* editTitleSizer = new wxBoxSizer(wxHORIZONTAL);
    editTitleSizer->Add(editHint, 1, wxEXPAND | wxRIGHT, 8);
    editTitleSizer->Add(editSaveBtn, 0, 0, 0);

    wxBoxSizer* editBodySizer = new wxBoxSizer(wxHORIZONTAL);
    editBodySizer->Add(chainEditor, 1, wxEXPAND | wxRIGHT, 8);
    editBodySizer->Add(componentPanel, 1, wxEXPAND, 0);

    wxBoxSizer* editSizer = new wxBoxSizer(wxVERTICAL);
    editSizer->Add(editTitleSizer, 0, wxEXPAND | wxALL, 6);
    editSizer->Add(editBodySizer, 1, wxEXPAND | wxLEFT | wxRIGHT, 6);
    editSizer->Add(editStatus, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    editPanel->SetSizer(editSizer);

    wxPanel* topPanel = new wxPanel(chainTab);
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
    topSizer->Add(editPanel, 1, wxEXPAND | wxALL, 4);
    topPanel->SetSizer(topSizer);

    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    mainSizer->Add(topPanel, 3, wxEXPAND | wxALL, 4);
    mainSizer->Add(previewPanel, 2, wxEXPAND | wxALL, 4);
    chainTab->SetSizer(mainSizer);

    // 预览框成员指针（供 showChainPreview 写入）
    chainPreview_ = previewBox;

    templateSubNotebook_->AddPage(chainTab, L"组件链编辑");
}

void ConfigDialog::showChainPreview(const std::string& jsonText) {
    if (chainPreview_ != nullptr) {
        chainPreview_->ChangeValue(wxString::FromUTF8(jsonText.c_str()));
    }
}

void ConfigDialog::syncChainEditorFromSelection() {
    if (chainEditor_ == nullptr || componentListPanel_ == nullptr) {
        return;
    }
    long sel = templateList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

    config::BundleChain chain;
    if (sel >= 0 && sel < static_cast<long>(templates_.size())) {
        const db::FullConfigTemplateItem& item = templates_[sel];
        chainEditor_->setCoreType(item.coreType == 2
                                      ? config::CoreType::XRAY
                                      : config::CoreType::SINGBOX);
        if (!item.bundleChain.empty()) {
            boost::system::error_code ec;
            boost::json::value v = boost::json::parse(item.bundleChain, ec);
            if (!ec && v.is_object()) {
                const boost::json::object& obj = v.as_object();
                if (obj.contains("bundleIds") && obj.at("bundleIds").is_array()) {
                    for (const boost::json::value& idVal : obj.at("bundleIds").as_array()) {
                        if (idVal.is_string()) {
                            chain.bundleIds.push_back(std::string(idVal.get_string().c_str()));
                        }
                    }
                }
                if (obj.contains("params") && obj.at("params").is_object()) {
                    for (const boost::json::key_value_pair& kv : obj.at("params").as_object()) {
                        chain.params[std::string(kv.key())] = kv.value();
                    }
                }
            }
        }
        chainEditor_->setChain(chain);
    } else {
        chainEditor_->clear();
    }
    componentListPanel_->clear();
    if (chainStatus_ != nullptr) {
        chainStatus_->SetLabel(L"");
    }
}

void ConfigDialog::onChainSave(wxCommandEvent& event) {
    (void)event;
    if (chainEditor_ == nullptr || componentListPanel_ == nullptr || db_ == nullptr) {
        return;
    }
    long sel = templateList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= static_cast<long>(templates_.size())) {
        wxMessageBox(L"请先在左侧模板列表中选择要保存的模板", L"提示",
                     wxOK | wxICON_INFORMATION);
        return;
    }

    const config::BundleChain chain = chainEditor_->getChain();
    if (chain.bundleIds.empty()) {
        wxMessageBox(L"组件链为空（未添加任何 Bundle），无法保存", L"提示",
                     wxOK | wxICON_INFORMATION);
        return;
    }

    // 重解析校验（ERROR 级冲突阻止保存，WARN 允许但提示）
    config::BundleChainResolver resolver(db_);
    const config::BundleChainResolveResult result = resolver.resolveBundleChain(chain);
    if (ComponentListPanel::hasErrors(result.conflicts)) {
        wxMessageBox(L"组件链存在 ERROR 级冲突，请先修复后再保存", L"错误",
                     wxOK | wxICON_ERROR);
        return;
    }

    // 序列化 bundleChain = {"bundleIds":[...],"params":{...}}
    boost::json::object root;
    boost::json::array idsArr;
    for (std::size_t i = 0; i < chain.bundleIds.size(); ++i) {
        idsArr.push_back(boost::json::value(chain.bundleIds[i]));
    }
    root["bundleIds"] = idsArr;
    boost::json::object paramsObj;
    for (std::map<std::string, boost::json::value>::const_iterator it = chain.params.begin();
         it != chain.params.end(); ++it) {
        paramsObj[it->first] = it->second;
    }
    root["params"] = paramsObj;

    // 同步记录 Bundle 版本快照（维护模板列表 ✅/⚠️ 同步状态）
    const std::vector<config::BundleVersionInfo> versions = resolver.getCurrentVersions(chain);
    boost::json::array verArr;
    for (std::size_t i = 0; i < versions.size(); ++i) {
        boost::json::object verObj;
        verObj["bundleId"] = versions[i].bundleId;
        verObj["version"] = versions[i].version;
        verArr.push_back(verObj);
    }

    templates_[sel].bundleChain = boost::json::serialize(root);
    templates_[sel].bundleVersion = boost::json::serialize(verArr);
    templates_[sel].templateType = "bundleChain";

    db::FullConfigTemplateDAO dao(db_);
    if (dao.save(templates_[sel])) {
        wxMessageBox(L"组件链已保存到模板 " + wxString(templates_[sel].id),
                     L"成功", wxOK | wxICON_INFORMATION);
        loadTemplates();
    } else {
        wxMessageBox(L"保存失败，请查看日志", L"错误", wxOK | wxICON_ERROR);
    }
}