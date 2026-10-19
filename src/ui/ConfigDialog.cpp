#include "ConfigDialog.h"

#include <windows.h>

#include <wx/sizer.h>
#include <wx/tokenzr.h>
#include <wx/display.h>
#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <wx/notebook.h>
#include <wx/listctrl.h>
#include <wx/textdlg.h>
#include <wx/choicdlg.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>

#include "Utils.h"
#include "FullConfigTemplateDAO.h"

#include "BundleChainEditor.h"
#include "ComponentListPanel.h"
#include "config/TemplateDecomposer.h"
#include "config/SchemaValidator.h"
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

/// 组件类型的枚举名。与「组件包配置」页速查表同源，避免两处枚举名不一致。
const wchar_t* componentTypeName(config::ComponentType type) {
    switch (type) {
        case config::ComponentType::ENV:               return L"ENV";
        case config::ComponentType::LOG:               return L"LOG";
        case config::ComponentType::API:               return L"API";
        case config::ComponentType::DNS:               return L"DNS";
        case config::ComponentType::ROUTING:           return L"ROUTING";
        case config::ComponentType::POLICY:            return L"POLICY";
        case config::ComponentType::INBOUND:           return L"INBOUND";
        case config::ComponentType::OUTBOUND:          return L"OUTBOUND";
        case config::ComponentType::STATS:             return L"STATS";
        case config::ComponentType::FAKEDNS:           return L"FAKEDNS";
        case config::ComponentType::METRICS:           return L"METRICS";
        case config::ComponentType::OBSERVATORY:       return L"OBSERVATORY";
        case config::ComponentType::BURST_OBSERVATORY: return L"BURST_OBSERVATORY";
        case config::ComponentType::GEODATA:           return L"GEODATA";
        case config::ComponentType::VERSION:           return L"VERSION";
        default:                                        return L"?";
    }
}

/// 解析失败或空串时原样返回，保证只读预览框恒有可见内容，
/// 不会因美化失败而显示空白。
std::string prettyJsonOrRaw(const std::string& json) {
    if (json.empty()) {
        return json;
    }
    boost::system::error_code ec;
    const boost::json::value parsed = boost::json::parse(json, ec);
    if (ec) {
        return json;
    }
    return prettyJson(parsed, 0);
}

} // namespace

// -------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(ConfigDialog, wxDialog)
    EVT_BUTTON(wxID_OK, ConfigDialog::onOk)
    EVT_BUTTON(wxID_CANCEL, ConfigDialog::onCancel)
    EVT_PG_CHANGED(wxID_ANY, ConfigDialog::onPropertyChanged)
    // 注意：不要用 EVT_LIST_ITEM_SELECTED(wxID_ANY, onTemplateSelection)。
    // wxID_ANY 会捕获对话框内**所有** wxListCtrl 的选中事件，经事件冒泡上溯到
    // ConfigDialog，于是单击「组件包列表」/「组件类型列表」也会走进
    // onTemplateSelection → syncChainEditorFromSelection() → componentListPanel_->clear()，
    // 把刚解析出来的组件列表清空（用户报「解析后单击组件类型显示框会清空」）。
    // 改为在 createTemplateTab() 里对 templateList_ 单独 Bind。
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
      // TIME_WAIT 是否算作端口占用（2026-10-12 Spec StandalonePortImmediateRebind，Task 1 / P0-1）
      // true = 恢复旧行为：TIME_WAIT 残留也算占用（会触发顺延）；false = 新行为（默认）
      propGrid_->Append(new wxBoolProperty(L"TIME_WAIT 视为端口占用（回滚开关）", "port_time_wait_blocking", cfg.port.time_wait_blocking));
      // 独立代理 API 端口（2026-10-12 Spec StandalonePortImmediateRebind）
      // 原为硬编码 10085 且不校验可用性；被占用时启动会自动向后顺延 32 个端口
      propGrid_->Append(new wxIntProperty(L"独立代理 API 端口", "proxy_api_base_port", cfg.proxy.api_base_port));
      // 是否启用 ECH（2026-10-14 Spec EchSwitch）：
      //   false = 生成配置时不输出 echConfigList；true = 输出并把值中空格替换为 '+'
      propGrid_->Append(new wxBoolProperty(L"是否启用 ECH", "proxy_ech_enabled", cfg.proxy.ech_enabled));

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
    // The "最大探测次数" property was removed on 2026-10-19 (D2): the network
    // monitor no longer cancels anything, so there is no grace window to tune.

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

    // 尺寸策略：以屏幕为硬上限，绝不让对话框超出屏幕（否则顶部 Tab 栏会被
    // 顶出可视区，表现为「无法切换 Tab」）。
    //
    // 坑（2026-10-08）：①topSizer->SetSizeHints(this) 把「内容理想尺寸」
    // 写进 size hint（应用配置的属性网格几十行 → 1170 高），随后 SetSize
    // 被 wx 拒绝（不能缩到 hint 之下）→ 对话框 1950x1170、顶部 T=-90；
    // ②改用 SetSizeHints(0,0) + SetSize 后仍超屏（1860x1110），因为
    // 对「有 sizer 的窗口」SetSize 会被 sizer 布局再次拉回内容尺寸。
    // 最终方案：SetMaxSize 卡死上限 + 不调用 Fit()，让窗口停在上限内。
    // 上限取「本对话框所在显示器的工作区」而非 wxDisplaySize()（后者返回
    // 默认主显示器尺寸，多显示器 / 低分辨率主屏时会算出比当前屏幕更大的值
    // ——UI 测试沙箱 1280x720 上就曾因此得到 740 > 720 而超屏）。
    int maxW = 1440;
    int maxH = 900;
    {
        // 取本窗口所在显示器的客户区（Win32：任务栏不占客户区）。
        // 不用 wxDisplaySize()：它返回默认主显示器尺寸，多显示器或低分辨率
        // 主屏时会算出比当前屏幕更大的上限（UI 测试沙箱 1280x720 上曾因此
        // 得到 740 > 720 而超屏）。
        // 对话框尚未显示，GetHwnd() 不可用；用主显示器（与 CentreOnScreen
        // 的落点一致）的工作区作为上限。
        HMONITOR mon = ::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (mon != nullptr && ::GetMonitorInfoW(mon, &mi)) {
            const int cw = mi.rcWork.right - mi.rcWork.left;
            const int ch = mi.rcWork.bottom - mi.rcWork.top;
            if (cw > 0) maxW = cw;
            if (ch > 0) maxH = ch;
        }
    }
    const int margin = 20;              // 留边，避免贴边被任务栏/边框吃掉
    maxW -= margin;
    maxH -= margin;
    if (maxW < 640)  maxW = 640;
    if (maxH < 480)  maxH = 480;

    const int minW = 1100;
    const int minH = 780;
    int wantW = 1440;
    int wantH = 918;
    if (wantW > maxW) wantW = maxW;
    if (wantH > maxH) wantH = maxH;
    // 屏幕比 min 还小时以屏幕为准：SetMinSize 不得大于 SetMaxSize，
    // 否则 wx 会把窗口撑到 min 高度而超屏（低分辨率屏必现）。
    const int effMinW = (minW < maxW) ? minW : maxW;
    const int effMinH = (minH < maxH) ? minH : maxH;
    if (wantW < effMinW) wantW = effMinW;
    if (wantH < effMinH) wantH = effMinH;

    SetMinSize(wxSize(effMinW, effMinH));
    SetMaxSize(wxSize(maxW, maxH));
    SetSizeHints(0, 0);
    SetSize(wxSize(wantW, wantH));
    CentreOnScreen();

    // Load templates after layout
    loadTemplates();

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
    propGrid_->SetPropertyValue("proxy_ech_enabled", cfg.proxy.ech_enabled);
    propGrid_->SetPropertyValue("proxy_socks_base_port", cfg.proxy.socks_base_port);
      propGrid_->SetPropertyValue("port_time_wait_blocking", cfg.port.time_wait_blocking);
      propGrid_->SetPropertyValue("proxy_api_base_port", cfg.proxy.api_base_port);
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
    editedConfig_.proxy.ech_enabled = propGrid_->GetPropertyValueAsBool("proxy_ech_enabled");
    editedConfig_.proxy.socks_base_port = propGrid_->GetPropertyValueAsInt("proxy_socks_base_port");
      editedConfig_.port.time_wait_blocking = propGrid_->GetPropertyValueAsBool("port_time_wait_blocking");
      editedConfig_.proxy.api_base_port = propGrid_->GetPropertyValueAsInt("proxy_api_base_port");
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

// 子 Notebook：模板配置 + 组件包配置 + 组件链编辑
    templateSubNotebook_ = new wxNotebook(templateTab, wxID_ANY);

    // === 子 Tab 1: 模板配置（原有功能）===
    wxPanel* templateConfigPanel = new wxPanel(templateSubNotebook_);
    // 水平并列：左=模板列表（含标题+按钮头部栏），右=生成的配置预览
    wxBoxSizer* templateConfigSizer = new wxBoxSizer(wxHORIZONTAL);

    // --- 左列：顶部标题+按钮栏，模板列表在其下方 ---
    wxBoxSizer* leftColSizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* toolSizer = new wxBoxSizer(wxHORIZONTAL);
    toolSizer->Add(new wxStaticText(templateConfigPanel, wxID_ANY, L"模板列表:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

    wxButton* btnSave = new wxButton(templateConfigPanel, wxID_ANY, L"重新生成");
    btnSave->Bind(wxEVT_BUTTON, &ConfigDialog::onTemplateSave, this);
    wxButton* btnRefresh = new wxButton(templateConfigPanel, wxID_ANY, L"刷新");
    btnRefresh->Bind(wxEVT_BUTTON, &ConfigDialog::onTemplateRefresh, this);
    // 标题在左、按钮靠右
    toolSizer->AddStretchSpacer(1);
    toolSizer->Add(btnSave, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    toolSizer->Add(btnRefresh, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);

    leftColSizer->Add(toolSizer, 0, wxEXPAND | wxALL, 8);

    templateList_ = new wxListCtrl(templateConfigPanel, wxID_ANY,
                                    wxDefaultPosition, wxDefaultSize,
                                    wxLC_REPORT);
    templateList_->InsertColumn(0, L"ID", wxLIST_FORMAT_LEFT, 120);
    templateList_->InsertColumn(1, L"类型", wxLIST_FORMAT_CENTER, 60);
    templateList_->InsertColumn(2, L"父模板", wxLIST_FORMAT_LEFT, 100);
    templateList_->InsertColumn(3, L"备注", wxLIST_FORMAT_LEFT, 220);
    templateList_->InsertColumn(4, L"启用", wxLIST_FORMAT_CENTER, 45);
    templateList_->InsertColumn(5, L"状态", wxLIST_FORMAT_CENTER, 50);
    // 选中事件只绑定到本控件（不能走事件表的 wxID_ANY，否则会捕获其它列表的选中事件）
    templateList_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& e) { onTemplateSelection(e); });
    leftColSizer->Add(templateList_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    templateConfigSizer->Add(leftColSizer, 1, wxEXPAND);

    // --- 右列：生成的配置预览（只读）---
    // Config 是配置生成的唯一来源，但它本身不可独立手改：
    // 只能通过「组件包配置」改包 + 「组件链编辑」改链，再解析生成。
    wxBoxSizer* rightColSizer = new wxBoxSizer(wxVERTICAL);
    rightColSizer->Add(
        new wxStaticText(templateConfigPanel, wxID_ANY,
                         L"生成的配置（只读，由组件链解析产生；"
                         L"请到「组件包配置」/「组件链编辑」修改后重新解析）:"),
        0, wxLEFT | wxBOTTOM, 8);
    templateEditor_ = new wxTextCtrl(templateConfigPanel, wxID_ANY, L"",
                                      wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    templateEditor_->SetFont(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    rightColSizer->Add(templateEditor_, 1, wxEXPAND | wxALL, 8);
    templateConfigSizer->Add(rightColSizer, 1, wxEXPAND);

    templateConfigPanel->SetSizer(templateConfigSizer);
    templateSubNotebook_->AddPage(templateConfigPanel, L"模板配置");

    // === 子 Tab 2: 组件链编辑（阶段 8）===
    // 置于「组件包配置」之前：链编辑是「改包→串链→看结果」的上游操作，
    // 放在前面更符合使用顺序。页面顺序即 create 调用顺序（wxNotebook 按添加序排列）。
    createChainEditTab();

    // === 子 Tab 3: 组件包配置 ===
    createBundleTab();

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
        templateEditor_->ChangeValue(
            wxString::FromUTF8(prettyJsonOrRaw(templates_[0].config).c_str()));
    }

    // 同时加载组件包
    loadBundles();
}

void ConfigDialog::onTemplateSelection(wxListEvent& event) {
    long sel = templateList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= static_cast<long>(templates_.size())) return;

    const auto& item = templates_[sel];
    templateEditor_->ChangeValue(
        wxString::FromUTF8(prettyJsonOrRaw(item.config).c_str()));
    syncChainEditorFromSelection();
}

void ConfigDialog::onTemplateSave(wxCommandEvent& event) {
    (void)event;
    // Config 不可独立手改：这里不再把编辑器内容写回 Config，
    // 而是按当前组件链重新解析并物化（组件包在别处被改动后用它刷新）。
    if (templateList_ == nullptr || db_ == nullptr) return;

    long sel = templateList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= static_cast<long>(templates_.size())) {
        wxMessageBox(L"请先选择要重新生成的模板", L"提示", wxOK | wxICON_INFORMATION);
        return;
    }

    const std::string templateId = templates_[sel].id;
    config::TemplateDecomposer decomposer(db_);
    if (decomposer.syncConfigFromChain(templateId)) {
        wxMessageBox(L"已按组件链重新生成 " + wxString(templateId) + L" 的配置",
                     L"成功", wxOK | wxICON_INFORMATION);
    } else {
        wxMessageBox(L"该模板没有可用的组件链，无法生成配置。"
                     L"请先到「组件链编辑」为其添加 Bundle。",
                     L"提示", wxOK | wxICON_INFORMATION);
    }
    loadTemplates();
}

void ConfigDialog::onTemplateRefresh(wxCommandEvent& event) {
    loadTemplates();
}

// ================================================================
// 「配置管线」Tab 已于 2026-10-10 移除
// 原因：ConfigBuilder::setPipeline() 全仓库无调用点 → pipeline_ 恒 nullptr →
//       build() 永远走 legacy 分支（loadTemplate 读 Config 列），管线永不执行；
//       config.json 亦无 config_pipeline 段。属未启用的遗留半成品，
//       其「可编排」能力已由组件链（组件包配置 / 组件链编辑）覆盖且正在使用。
// 底层 ConfigPipeline / StepRegistry / StepHandler 类及其单测全部保留未动。
// ================================================================

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
    wxButton* btnNew = new wxButton(bundleTab, wxID_ANY, L"新增");
    btnNew->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) { onBundleNew(e); });
    btnSizer->Add(btnNew, 0, wxRIGHT, 4);
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

    // === 右侧：JSON 编辑器 + 速查表（水平并排，两表上下排列）===
    wxBoxSizer* editorRowSizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
    rightSizer->Add(new wxStaticText(bundleTab, wxID_ANY, L"JSON 配置:"), 0, wxLEFT | wxBOTTOM, 8);
    bundleEditor_ = new wxTextCtrl(bundleTab, wxID_ANY, L"",
                                    wxDefaultPosition, wxDefaultSize,
                                    wxTE_MULTILINE | wxTE_WORDWRAP | wxTE_DONTWRAP);
    bundleEditor_->SetFont(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    rightSizer->Add(bundleEditor_, 1, wxEXPAND | wxALL, 8);
    editorRowSizer->Add(rightSizer, 1, wxEXPAND);

    // --- 速查表组：组件类型 + 占位符，上下排列 ---
    wxBoxSizer* tableSizer = new wxBoxSizer(wxVERTICAL);

    // 组件类型表：内容由 ComponentRegistry 运行时生成，避免与代码漂移
    wxStaticText* typeLabel = new wxStaticText(bundleTab, wxID_ANY,
                                               L"组件类型（ComponentType）:");
    typeLabel->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    tableSizer->Add(typeLabel, 0, wxLEFT | wxTOP, 8);

    // 初始高度用 wxSize(360, -1) 让 sizer 按 proportion 均分纵向空间；
    // 若给死高度（如 380/220），wxListCtrl 的 best-size 会让组件类型表
    // 抢占大部分纵向空间，占位符表被挤到只剩两三行。
    bundleTypeTable_ = new wxListCtrl(bundleTab, wxID_ANY, wxDefaultPosition, wxSize(360, -1),
                                      wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    bundleTypeTable_->InsertColumn(0, L"枚举", wxLIST_FORMAT_LEFT, 150);
    bundleTypeTable_->InsertColumn(1, L"值", wxLIST_FORMAT_CENTER, 40);
    bundleTypeTable_->InsertColumn(2, L"Xray 节", wxLIST_FORMAT_LEFT, 110);
    bundleTypeTable_->InsertColumn(3, L"SingBox 节", wxLIST_FORMAT_LEFT, 110);
    bundleTypeTable_->InsertColumn(4, L"策略", wxLIST_FORMAT_CENTER, 70);
    tableSizer->Add(bundleTypeTable_, 1, wxEXPAND | wxALL, 8);

    // 占位符表：与 ConfigBuilder::variablesToMap() 一一对应
    wxStaticText* varLabel = new wxStaticText(bundleTab, wxID_ANY,
                                              L"占位符（运行时由 ConfigBuilder 替换）:");
    varLabel->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
    tableSizer->Add(varLabel, 0, wxLEFT | wxTOP, 8);

    // 同样用 wxSize(360, -1)，与组件类型表等分纵向空间
    bundleVarTable_ = new wxListCtrl(bundleTab, wxID_ANY, wxDefaultPosition, wxSize(360, -1),
                                     wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    bundleVarTable_->InsertColumn(0, L"占位符", wxLIST_FORMAT_LEFT, 200);
    bundleVarTable_->InsertColumn(1, L"说明", wxLIST_FORMAT_LEFT, 300);
    tableSizer->Add(bundleVarTable_, 1, wxEXPAND | wxALL, 8);

    editorRowSizer->Add(tableSizer, 0, wxEXPAND);

    mainSizer->Add(editorRowSizer, 1, wxEXPAND);

    populateBundleTypeTable();
    populateBundleVarTable();

    bundleTab->SetSizer(mainSizer);
    templateSubNotebook_->AddPage(bundleTab, L"组件包配置");
}

void ConfigDialog::populateBundleTypeTable() {
    if (bundleTypeTable_ == nullptr) return;

    config::ComponentRegistry& registry = config::ComponentRegistry::instance();
    const std::vector<config::ComponentType>& types = registry.getAllTypes();

    long row = 0;
    for (std::vector<config::ComponentType>::const_iterator it = types.begin();
         it != types.end(); ++it, ++row) {
        const config::ComponentMetadata* meta = registry.getMetadata(*it);
        if (meta == nullptr) continue;

        const wxString enumName = componentTypeName(*it);

        const wxString xraySection = meta->configSection.empty()
                                         ? wxString(L"（无）")
                                         : wxString::FromUTF8(meta->configSection.c_str());
        const wxString sbSection = meta->singboxSection.empty()
                                       ? wxString(L"（无）")
                                       : wxString::FromUTF8(meta->singboxSection.c_str());

        wxString strategy;
        switch (meta->strategy) {
            case config::CombinationStrategy::OVERRIDE: strategy = L"OVERRIDE"; break;
            case config::CombinationStrategy::APPEND:  strategy = L"APPEND"; break;
            case config::CombinationStrategy::MERGE:   strategy = L"MERGE"; break;
            default:                                   strategy = L"?"; break;
        }

        bundleTypeTable_->InsertItem(row, enumName);
        bundleTypeTable_->SetItem(row, 1, wxString::Format(L"%d", static_cast<int>(*it)));
        bundleTypeTable_->SetItem(row, 2, xraySection);
        bundleTypeTable_->SetItem(row, 3, sbSection);
        bundleTypeTable_->SetItem(row, 4, strategy);
    }
}

void ConfigDialog::populateBundleVarTable() {
    if (bundleVarTable_ == nullptr) return;

    // 顺序与说明对齐 ConfigBuilder::variablesToMap()（src/config/ConfigBuilder.cpp:35-46）
    struct VarRow { const wchar_t* name; const wchar_t* desc; };
    static const VarRow kVars[] = {
        { L"${socksPort}",          L"SOCKS 入站端口（int）" },
        { L"${apiPort}",             L"API 端口（int，仅 Xray）" },
        { L"${probeUrl}",            L"探测 URL（string）" },
        { L"${observatoryInterval}", L"Observatory 间隔（int，毫秒）" },
        { L"${balancerStrategy}",    L"负载均衡策略（string）" },
        { L"${enableObservatory}",   L"启用 observatory（\"true\"/\"false\"）" },
        { L"${enableCnDirect}",      L"启用 CN 直连分流（\"true\"/\"false\"）" },
        { L"${blockQuic}",           L"阻断 QUIC（\"true\"/\"false\"）" }
    };
    const int count = static_cast<int>(sizeof(kVars) / sizeof(kVars[0]));

    for (int i = 0; i < count; ++i) {
        bundleVarTable_->InsertItem(i, kVars[i].name);
        bundleVarTable_->SetItem(i, 1, kVars[i].desc);
    }
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

    // 选中第一项。
    // DeleteAllItems 后行状态本应被清空，但为稳妥仍显式清除所有行的选中位：
    // SetItemState 只按掩码设置**本行**状态，不会清除其它行；
    // 一旦出现多行同时选中，onBundleSave 的 GetNextItem 会取到错误的行。
    if (!bundles_.empty()) {
        for (long r = 0; r < bundleList_->GetItemCount(); ++r) {
            bundleList_->SetItemState(r, 0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        }
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

    // 多行美化：boost::json::serialize 产出单行压缩 JSON，编辑器是
    // DONTWRAP 的，只能横向滚动才看得到结构。改用 prettyJsonOrRaw
    // （解析失败/空串时原样返回，保证恒有可见内容）。
    bundleEditor_->ChangeValue(
        wxString::FromUTF8(prettyJsonOrRaw(boost::json::serialize(root)).c_str()));
}

void ConfigDialog::onBundleNew(wxCommandEvent& event) {
    (void)event;
    if (!bundleEditor_ || !db_) return;

    // 1. 输入 ID
    wxTextEntryDialog idDlg(this, L"请输入新组件包的 ID：", L"新增组件包", L"xray_CUSTOM");
    if (idDlg.ShowModal() != wxID_OK) return;
    const std::string newId = idDlg.GetValue().Trim(true).Trim(false).ToStdString();
    if (newId.empty()) {
        wxMessageBox(L"组件包 ID 不能为空", L"提示", wxOK | wxICON_INFORMATION);
        return;
    }

    config::ComponentBundleDAO dao(db_);

    // 2. 唯一性校验
    if (dao.getById(newId).has_value()) {
        wxMessageBox(L"组件包 '" + wxString(newId) + L"' 已存在，请换一个 ID",
                     L"提示", wxOK | wxICON_INFORMATION);
        return;
    }

    // 3. 选择核心类型
    wxArrayString coreChoices;
    coreChoices.Add(L"Xray");
    coreChoices.Add(L"SingBox");
    wxSingleChoiceDialog coreDlg(this, L"请选择该组件包适用的核心：", L"核心类型", coreChoices);
    coreDlg.SetSelection(0);
    if (coreDlg.ShowModal() != wxID_OK) return;
    const bool isSingBox = (coreDlg.GetSelection() == 1);
    const wxString coreName = coreChoices[coreDlg.GetSelection()];

    // 3.5 选择初始组件类型。
    // 组件的 `type`（ComponentType 枚举值）是**必填**字段——保存前 SchemaValidator
    // 会校验 `components[i].type` 必须是 0..14 的整数，缺失即报
    // 「ComponentBundle 'components[0]' missing integer 'type'」并拒绝保存。
    // 手工往骨架里补组件极易漏写该字段，故这里让用户选一个类型，
    // 新建时直接把 {"type": N, "params": {}} 预置进 components。
    const std::vector<config::ComponentType>& allTypes =
        config::ComponentRegistry::instance().getAllTypes();
    wxArrayString typeChoices;
    typeChoices.Add(L"暂不添加组件（components 为空数组）");
    for (std::vector<config::ComponentType>::const_iterator it = allTypes.begin();
         it != allTypes.end(); ++it) {
        const config::ComponentMetadata* meta =
            config::ComponentRegistry::instance().getMetadata(*it);
        const wxString section = (meta != nullptr && !meta->configSection.empty())
                                     ? wxString::FromUTF8(meta->configSection.c_str())
                                     : wxString(L"（无配置节）");
        typeChoices.Add(wxString::Format(L"%s  —  %s", componentTypeName(*it), section));
    }
    wxSingleChoiceDialog typeDlg(this,
                                 L"请选择要预置的组件类型（会自动填好 type 字段）：",
                                 L"初始组件类型", typeChoices);
    typeDlg.SetSelection(0);
    if (typeDlg.ShowModal() != wxID_OK) return;
    const bool addInitialComponent = (typeDlg.GetSelection() > 0);
    const config::ComponentType initialType =
        addInitialComponent
            ? allTypes[static_cast<std::size_t>(typeDlg.GetSelection() - 1)]
            : config::ComponentType::ENV;

    // 4. 以骨架落库。
    //    立即落库而非「草稿态」，是因为 onBundleSave 要求先选中列表行，
    //    只填编辑器不落库会让后续保存被前置条件挡住。
    //    components 视用户选择：预置一个带 type 的组件，或保持空数组。
    config::ComponentBundle bundle;
    bundle.id = newId;
    bundle.name = newId;
    bundle.description = "Manually created bundle (" + coreName.ToStdString() + ")";
    bundle.coreType = isSingBox ? config::CoreType::SINGBOX : config::CoreType::XRAY;
    bundle.version = 1;
    bundle.enabled = true;
    bundle.components.clear();
    if (addInitialComponent) {
        config::ComponentConfig comp;
        comp.type = initialType;
        comp.params.clear();
        comp.id.clear();
        bundle.components.push_back(comp);
    }

    if (!dao.save(bundle)) {
        wxMessageBox(L"创建失败，请查看日志", L"错误", wxOK | wxICON_ERROR);
        return;
    }

    // 5. 刷新并选中新建行。
    //    必须先清空所有行的选中位再选中目标行：wxListCtrl::SetItemState 是
    //    「按掩码设置本行状态」，**不会**清除其它行（msw/listctrl.cpp 走
    //    LVM_SETITEMSTATE 逐行下发）。若不先清空，新增前已选中的行（如 xray_LOG）
    //    会与新行**同时**处于选中态；onBundleSave 用 GetNextItem 取的是
    //    **第一个**选中行，会拿到旧行而非新行 → 与编辑器内容不匹配 →
    //    误报「组件包 ID 已存在」（截图 bin/log/18487d7e-*.png）。
    loadBundles();
    if (bundleList_) {
        for (long r = 0; r < bundleList_->GetItemCount(); ++r) {
            bundleList_->SetItemState(r, 0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        }
        for (std::size_t i = 0; i < bundles_.size(); ++i) {
            if (bundles_[i].id == newId) {
                bundleList_->SetItemState(static_cast<long>(i),
                                          wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                          wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
                bundleList_->EnsureVisible(static_cast<long>(i));
                break;
            }
        }
    }

    // 6. 编辑器展示骨架供继续编辑（不伪造 wxListEvent 去调 onBundleSelection，
    //    它只从列表读选中行，直接构造编辑内容更安全）
    {
        boost::json::object root;
        root["id"] = bundle.id;
        root["name"] = bundle.name;
        root["description"] = bundle.description;
        root["coreType"] = static_cast<int>(bundle.coreType);
        root["version"] = bundle.version;
        root["enabled"] = bundle.enabled;
        // 与落库内容保持一致：预置组件时把 {"type": N, "params": {}} 展示出来，
        // 用户只需在 params 里填内容，不必自己补 type 字段。
        boost::json::array compsJson;
        for (std::size_t i = 0; i < bundle.components.size(); ++i) {
            boost::json::object compObj;
            compObj["type"] = static_cast<int>(bundle.components[i].type);
            compObj["params"] = boost::json::object();
            compsJson.push_back(compObj);
        }
        root["components"] = compsJson;
        bundleEditor_->ChangeValue(
            wxString::FromUTF8(prettyJsonOrRaw(boost::json::serialize(root)).c_str()));
    }

    // 7. 新建包尚未被任何链引用，同步预期为 0（与保存/删除路径保持一致）
    config::TemplateDecomposer decomposer(db_);
    decomposer.syncConfigsFromChains(newId);

    wxMessageBox(L"已创建组件包 '" + wxString(newId) + L"'，请在右侧编辑 JSON 后点「保存」",
                 L"成功", wxOK | wxICON_INFORMATION);
}

void ConfigDialog::onBundleSave(wxCommandEvent& event) {
    if (!bundleList_ || !bundleEditor_ || !db_) return;

    long sel = bundleList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0 || sel >= static_cast<long>(bundles_.size())) {
        wxMessageBox(L"请先选择要保存的组件包", L"提示", wxOK | wxICON_INFORMATION);
        return;
    }

// 解析 + 校验组件包 JSON。
    // 统一走 SchemaValidator::parseAndValidateComponentBundle：它用 error_code 重载，
    // 任何输入都不抛异常（单参 boost::json::parse 会抛，异常逃逸出 wx 事件处理器
    // 会终止进程 → 用户所见闪崩；且落库在解析之后，故配置也一并丢失）。
    // 该函数同时完成「语法 → 对象判型 → 结构校验」，非法即弹窗且不落库。
    boost::json::value parseResult;
    std::string schemaError;
    if (!config::SchemaValidator::parseAndValidateComponentBundle(
            bundleEditor_->GetValue().ToStdString(), parseResult, schemaError)) {
        wxMessageBox(wxString::FromUTF8(schemaError.c_str())
                         + L"\n\n未保存，请修正后重试。",
                     L"校验失败", wxOK | wxICON_ERROR);
        return;
    }

    // 防御：wxListCtrl 默认允许多选（Ctrl+点击），而下面按 sel 取 bundles_[sel]
    // 与编辑器内容比对。若多行同时选中，GetNextItem 只返回**第一个**选中行，
    // 会与编辑器里显示的包不一致，导致误报「ID 已存在」或保存错对象。
    int selectedCount = 0;
    for (long r = bundleList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
         r != -1;
         r = bundleList_->GetNextItem(r, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) {
        ++selectedCount;
    }
    if (selectedCount != 1) {
        wxMessageBox(wxString::Format(
            L"当前有 %d 个组件包处于选中状态，请只选中一个再保存。\n"
            L"（列表支持 Ctrl/Shift 多选，会造成保存对象与编辑器内容不一致）",
            selectedCount),
            L"提示", wxOK | wxICON_INFORMATION);
        return;
    }

    const boost::json::object& root = parseResult.as_object();

    config::ComponentBundle bundle = bundles_[sel];

    if (auto it = root.find("id"); it != root.end() && it->value().is_string()) {
        bundle.id = std::string(it->value().as_string());
        // 改名会静默覆盖另一个组件包（DAO 以 id 为主键 upsert），必须拦下
        if (bundle.id != bundles_[sel].id) {
            for (std::size_t i = 0; i < bundles_.size(); ++i) {
                if (bundles_[i].id == bundle.id) {
                    wxMessageBox(L"组件包 ID 已存在：" + wxString::FromUTF8(bundle.id.c_str())
                                     + L"\n请换一个未被占用的 ID。\n\n未保存。",
                                 L"校验失败", wxOK | wxICON_ERROR);
                    return;
                }
            }
        }
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
        // 组件包变更 → 重新物化所有引用它的模板的 Config（配置生成的唯一来源）。
        const std::string oldId = bundles_[sel].id;
        const bool renamed = !oldId.empty() && oldId != bundle.id;

        config::TemplateDecomposer decomposer(db_);
        int synced = decomposer.syncConfigsFromChains(bundle.id);

        wxString msg;
        if (renamed) {
            // 改名：依赖「旧 ID」的模板引用已悬空，按新 ID 同步找不到它们。
            // 必须显式提示——它们的链解析会以「Bundle 不存在」失败，
            // Config 停在旧值，需要用户到「组件链编辑」里改引用。
            const std::vector<db::FullConfigTemplateItem> dependents =
                db::FullConfigTemplateDAO(db_).getByBundleId(oldId);
            msg = wxString::Format(L"组件包 ID 已由 '%s' 改为 '%s'。\n\n",
                                   wxString::FromUTF8(oldId.c_str()),
                                   wxString::FromUTF8(bundle.id.c_str()));
            if (dependents.empty()) {
                msg += L"没有模板引用旧 ID，已同步 "
                     + wxString::Format(L"%d", synced) + L" 个模板。";
            } else {
                msg += wxString::Format(
                    L"⚠ 以下 %d 个模板仍引用旧 ID，其组件链已无法解析，需到「组件链编辑」更新引用：\n",
                    static_cast<int>(dependents.size()));
                for (std::size_t i = 0; i < dependents.size(); ++i) {
                    msg += L"  · " + wxString::FromUTF8(dependents[i].id.c_str()) + L"\n";
                }
            }
        } else {
            msg = wxString::Format(L"组件包已保存，已同步 %d 个模板的 Config。", synced);
        }

        loadBundles();
        // 模板列表的 ✅/⚠️ 状态列由 loadTemplates() 计算（isTemplateInSync），
        // 组件包版本变化后必须刷新，否则状态指示停留在旧值。
        loadTemplates();
        wxMessageBox(msg, L"成功", wxOK | (renamed ? wxICON_WARNING : wxICON_INFORMATION));
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
    const std::string deletedId = bundles_[sel].id;
    if (dao.deleteById(deletedId)) {
        // 删除组件包 → 引用它的模板将无法解析，需同步重物化 Config。
        // 先在删除前定位依赖者：这些模板的链已悬空，物化会失败，
        // 必须如实告知而不是让「已同步 N 个」掩盖问题。
        const std::vector<db::FullConfigTemplateItem> dependents =
            db::FullConfigTemplateDAO(db_).getByBundleId(deletedId);
        config::TemplateDecomposer decomposer(db_);
        const int synced = decomposer.syncConfigsFromChains(deletedId);
        loadBundles();
        loadTemplates();

        wxString msg = wxString::Format(L"已删除，已同步 %d 个模板的 Config。", synced);
        if (!dependents.empty()) {
            msg += wxString::Format(
                L"\n\n⚠ 以下 %d 个模板的组件链仍引用已删除的 '%s'，其链已无法解析，"
                L"需到「组件链编辑」移除该 Bundle：\n",
                static_cast<int>(dependents.size()),
                wxString::FromUTF8(deletedId.c_str()));
            for (std::size_t i = 0; i < dependents.size(); ++i) {
                msg += L"  · " + wxString::FromUTF8(dependents[i].id.c_str()) + L"\n";
            }
        }
        wxMessageBox(msg, L"成功",
                     wxOK | (dependents.empty() ? wxICON_INFORMATION : wxICON_WARNING));
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

    // 编辑区：控件一律以 editPanel 为 parent（不可用 chainTab）。
    // 父子关系约束：wxSizer::SetContainingWindow 在 Debug 下断言「sizer 管理的
    // 所有控件的 parent 必须是该 sizer 所属窗口」，且会**递归**改写嵌套 sizer 的
    // containing window（wxWidgets/src/common/sizer.cpp:988-995），
    // 详见 docs/bugfix/2026-10-08-Bugfix-ChainTab-ParentAssert-v1.0.md §6。
    // 因此本函数不套任何「带自有 sizer 的中间 panel」，所有控件直接挂 editPanel，
    // 只用**不挂窗口的**纯布局 sizer 分栏。
    wxPanel* editPanel = new wxPanel(chainTab);

    // ---------- 三个显示框的内容（parent 均为 editPanel）----------
    // 第 1 栏：Bundle 列表（不要内置按钮条，按钮移到共享表头行，见下）
    BundleChainEditor* chainEditor = new BundleChainEditor(editPanel, db_, /*showButtons=*/false);
    chainEditor_ = chainEditor;

    // 第 2 栏：组件列表
    ComponentListPanel* componentPanel = new ComponentListPanel(editPanel, db_);
    componentListPanel_ = componentPanel;

    // 注：解析状态文字（chainStatus_）在下方第 2 栏表头面板内创建——
    //     它必须与组件列表同栏才能左对齐，且 parent 须是该面板本身。
    // 解析回调：填充组件列表 + 状态文字 + 配置预览
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

    // 第 3 栏：配置预览框（只读）
    wxTextCtrl* previewBox = new wxTextCtrl(editPanel, wxID_ANY, wxEmptyString,
                                           wxDefaultPosition, wxSize(-1, -1),
                                           wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    previewBox->SetFont(wxFont(9, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL,
                               wxFONTWEIGHT_NORMAL));
    previewBox->SetMinSize(wxSize(200, 160));
    chainPreview_ = previewBox;

    // ---------- 三栏：每栏 = [固定高度表头面板, 显示框] ----------
    // 两个诉求原本互相矛盾：
    //   · 左对齐要求「标签」与「框」在**同一栏**（否则是两条独立 sizer 行，
    //     只能靠 wxLEFT 手调像素——实测连续五轮 0→50→100→120 仍未对齐，
    //     因为 wxStaticText 与 wxTextCtrl 各有客户区边距，不体现在 sizer 计算里）；
    //   · 顶对齐要求三栏表头**等高**。
    // 两者可同时满足：每栏用**同栏内的**表头面板（min height 相同），
    // 面板左边缘与该栏框的左边缘天然相同（同一纵向 sizer、均无左边距）。
    //
    // ⚠ 表头面板内的控件**必须以该面板为 parent**（先建面板、再以它建控件）。
    //   若控件 parent 是 editPanel 却被塞进面板的 sizer，会触发
    //   wxSizer::SetContainingWindow 的 parent 断言
    //   （见 docs/bugfix/2026-10-08-Bugfix-ChainTab-ParentAssert-v1.0.md §6）。
    const int kHeaderHeight = 30;

    // 第 1 栏表头：添加 / 移除 / 解析
    wxPanel* headerPanel1 = new wxPanel(editPanel);
    headerPanel1->SetMinSize(wxSize(-1, kHeaderHeight));
    {
        wxButton* btnAddBundle = new wxButton(headerPanel1, wxID_ANY, L"添加 Bundle");
        btnAddBundle->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { chainEditor_->triggerAddBundle(); });
        wxButton* btnRemoveBundle = new wxButton(headerPanel1, wxID_ANY, L"移除");
        btnRemoveBundle->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { chainEditor_->triggerRemoveBundle(); });
        wxButton* btnResolve = new wxButton(headerPanel1, wxID_ANY, L"解析");
        btnResolve->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { chainEditor_->triggerResolve(); });
        wxBoxSizer* hs = new wxBoxSizer(wxHORIZONTAL);
        hs->Add(btnAddBundle, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        hs->Add(btnRemoveBundle, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        hs->Add(btnResolve, 0, wxALIGN_CENTER_VERTICAL, 0);
        headerPanel1->SetSizer(hs);
    }

    // 第 2 栏表头：解析状态（需求 2：置于组件类型显示框上方）
    wxPanel* headerPanel2 = new wxPanel(editPanel);
    headerPanel2->SetMinSize(wxSize(-1, kHeaderHeight));
    {
        wxStaticText* statusText = new wxStaticText(headerPanel2, wxID_ANY, L"");
        chainStatus_ = statusText;
        wxBoxSizer* hs = new wxBoxSizer(wxHORIZONTAL);
        hs->Add(statusText, 1, wxALIGN_CENTER_VERTICAL, 0);
        headerPanel2->SetSizer(hs);
    }

    // 第 3 栏表头：「配置预览:」+「保存链」并列（需求 3 + 4）
    wxPanel* headerPanel3 = new wxPanel(editPanel);
    headerPanel3->SetMinSize(wxSize(-1, kHeaderHeight));
    {
        wxStaticText* previewLabel = new wxStaticText(headerPanel3, wxID_ANY, L"配置预览:");
        wxButton* editSaveBtn = new wxButton(headerPanel3, wxID_ANY, L"保存链");
        editSaveBtn->Bind(wxEVT_BUTTON, &ConfigDialog::onChainSave, this);
        wxBoxSizer* hs = new wxBoxSizer(wxHORIZONTAL);
        hs->Add(previewLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        hs->AddStretchSpacer(1);
        hs->Add(editSaveBtn, 0, wxALIGN_CENTER_VERTICAL, 0);
        headerPanel3->SetSizer(hs);
    }

    // 宽度比例 2 : 3 : 4（Bundle : 组件 : 预览），三栏间距与表头一致
    wxBoxSizer* col1 = new wxBoxSizer(wxVERTICAL);
    col1->Add(headerPanel1, 0, wxEXPAND | wxBOTTOM, 6);
    col1->Add(chainEditor, 1, wxEXPAND, 0);

    wxBoxSizer* col2 = new wxBoxSizer(wxVERTICAL);
    col2->Add(headerPanel2, 0, wxEXPAND | wxBOTTOM, 6);
    col2->Add(componentPanel, 1, wxEXPAND, 0);

    wxBoxSizer* col3 = new wxBoxSizer(wxVERTICAL);
    col3->Add(headerPanel3, 0, wxEXPAND | wxBOTTOM, 6);
    col3->Add(previewBox, 1, wxEXPAND, 0);

    wxBoxSizer* editBodySizer = new wxBoxSizer(wxHORIZONTAL);
    editBodySizer->Add(col1, 2, wxEXPAND | wxRIGHT, 6);
    editBodySizer->Add(col2, 3, wxEXPAND | wxRIGHT, 6);
    editBodySizer->Add(col3, 4, wxEXPAND, 0);

    // ---------- 「当前模板」行（需求 3：置于三个表头之上）----------
    wxStaticText* templateLabel = new wxStaticText(editPanel, wxID_ANY,
        L"当前模板：（未选择）");
    templateLabel->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
                                  wxFONTWEIGHT_BOLD));
    chainTemplateLabel_ = templateLabel;

    wxBoxSizer* editSizer = new wxBoxSizer(wxVERTICAL);
    editSizer->Add(templateLabel, 0, wxLEFT | wxRIGHT | wxTOP, 10);
    editSizer->Add(editBodySizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    editPanel->SetSizer(editSizer);

    // editPanel 直接挂在 chainTab 上（不可再套 topPanel：那会使 editPanel 成为
    // topSizer 的子项而其 parent 是 chainTab，违反 sizer 的 parent 断言）。
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    mainSizer->Add(editPanel, 1, wxEXPAND | wxALL, 4);
    chainTab->SetSizer(mainSizer);

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
        // 「当前模板」标签随选中项刷新：id + 备注（备注为空时只显示 id）
        if (chainTemplateLabel_ != nullptr) {
            wxString label = wxString::Format(L"当前模板：%s",
                                              wxString::FromUTF8(item.id.c_str()));
            if (!item.remarks.empty()) {
                label += wxString::Format(L"（%s）",
                                          wxString::FromUTF8(item.remarks.c_str()));
            }
            chainTemplateLabel_->SetLabel(label);
        }
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
        if (chainTemplateLabel_ != nullptr) {
            chainTemplateLabel_->SetLabel(L"当前模板：（未选择）");
        }
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
    // 必须先判 success 再判 conflicts：resolveBundleChain 遇到不存在的 Bundle 会
    // 直接 return，此时 conflicts 仍是空 vector。只看 hasErrors() 会把「引用了已删除
    // 组件包」的坏链当成无冲突放行，随后物化拿不到任何组件却报成功。
    if (!result.success) {
        wxMessageBox(wxString::FromUTF8(result.errorMessage.c_str())
                         + L"\n\n请在「组件链编辑」中移除该 Bundle 后再保存。",
                     L"校验失败", wxOK | wxICON_ERROR);
        return;
    }
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
        // 链变了 → 重新物化本模板的 Config（配置生成的唯一来源）。
        // 只同步当前模板，避免覆盖其它模板可能存在的手工编辑。
        config::TemplateDecomposer decomposer(db_);
        const bool synced = decomposer.syncConfigFromChain(templates_[sel].id);

        loadTemplates();
        // 物化失败时必须如实告知：链已落库但 Config 未更新，
        // 此时「模板与链解析结果不同」的状态会持续存在，不能报「成功」误导用户。
        if (synced) {
            wxMessageBox(L"组件链已保存到模板 " + wxString(templates_[sel].id)
                         + L"，并已同步该模板的 Config",
                         L"成功", wxOK | wxICON_INFORMATION);
        } else {
            wxMessageBox(L"组件链已保存到模板 " + wxString(templates_[sel].id)
                         + L"，但该模板的 Config 未能重新生成（请查看日志）。",
                         L"部分成功", wxOK | wxICON_WARNING);
        }
        syncChainEditorFromSelection();
    } else {
        wxMessageBox(L"保存失败，请查看日志", L"错误", wxOK | wxICON_ERROR);
    }
}
