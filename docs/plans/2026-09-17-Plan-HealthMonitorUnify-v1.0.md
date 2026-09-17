# 方案 B「配置层聚合 + 控制层解耦」实施计划（HealthMonitorUnify）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新增 `independent_probe.enabled` 独立开关将 silent 独立代理探活从 `proxy_process_monitor.enabled` 解耦，并按 2026-09-17 评审层级树重组 ConfigDialog 分类（监控悬浮窗 / 健康度评估父分类 + 独立代理评估 + 代理池评估 / 代理池配置 / 代理后端选择），同时暴露 `proxy.scoring_*_weight` 三权重（补全序列化缺口）。

**Architecture:** 数据面保持独立引擎（silent 探活线程 + ProxyScorer 纯函数 + 池 evaluatorLoop 均不动），仅在配置层与控制层做三项改动：(1) `AppConfig` 新增 `independent_probe.enabled` 字段并经 Parser/Serializer 全链路贯通（默认 `true` 保持现有行为，旧 config.json 无此键仍兼容）；(2) MainFrame 的监控 timer 生命周期改为 `proxy_process_monitor.enabled || independent_probe.enabled` 驱动，timer 回调内 silent 探活单独受 `isIndependentProbeEnabled()` 门控；(3) ConfigDialog 按评审树重建分类。

**Tech Stack:** C++17 / wxWidgets 3.2+ (wxPropertyGrid 嵌套 wxPropertyCategory) / boost::json / Google Test、Catch2 UI 自动化测试。

**规格来源:** `docs/reports/2026-09-16-Report-StandalonePool-ConfigSemantics-v1.0.md` v1.2 §7.4.4（方案 B 详细设计）+ §7.3 v1.2 评审层级树；评审依据 `docs/review/2026-09-17-Review-results-of-configuration-item-reengineering.md`。

---

## 文件结构总览

| 文件 | 职责 | 操作 |
| :--- | :--- | :--- |
| `include/ConfigReader.h` | `AppConfig` 增 `independent_probe` 段 | Modify |
| `include/config/sections/IndependentProbeConfigParser.h` | 新增 parser（镜像 ProxyProcessMonitorConfigParser 模式） | Create |
| `src/ConfigReader.cpp` | 注册新 parser 调用 | Modify |
| `src/config/ConfigJsonSerializer.cpp` | 序列化 `independent_probe` + 补写 `scoring_*_weight` | Modify |
| `include/config/sections/ProxyConfigParser.h` | **已**解析 scoring 权重（无需改） | — |
| `src/ui/AppController.h` / `.cpp` | `isIndependentProbeEnabled()` / `setIndependentProbeEnabled()` 访问器 | Modify |
| `src/ui/MainFrame.cpp` | timer 生命周期双开关驱动 + timer 回调内探活门控 | Modify |
| `src/ui/StandaloneFloatingWidget.h` / `.cpp` | `probeChk_` 控件 + onToggleProbe + applySettings + Orb/Panel Hide-Show | Modify |
| `src/ui/ConfigDialog.cpp` | 分类树重构（监控悬浮窗/健康度评估/代理池配置/代理后端选择）+ scoring 属性 + 校验 | Modify |
| `tests/test_config_reader.cpp` | independent_probe + scoring 往返序列化测试 | Modify |
| `bin/config.json` | 对齐新增键 `independent_probe.enabled: true` | Modify |
| `docs/INDEX.md` | 登记本 Plan | Modify |

> **禁止 `auto`**（AGENTS.md 核心约束）；所有类型显式声明。

---

## Task 1: Config 层 — `independent_probe` 字段全链路 + scoring 序列化缺口补全

**Files:**
- Modify: `include/ConfigReader.h:100`（在 `proxy_process_monitor` 块后插入）
- Create: `include/config/sections/IndependentProbeConfigParser.h`
- Modify: `src/ConfigReader.cpp:28`（include）、`src/ConfigReader.cpp:80-81`（parser 调用）
- Modify: `src/config/ConfigJsonSerializer.cpp:125`（proxyObj 补 scoring）、`:152`（新增 independent_probe 块）
- Test: `tests/test_config_reader.cpp`（在 `SaveRoundTrip_FieldCompleteness` 结束 `}` 之后追加 1 个用例）

- [ ] **Step 1: 写失败测试（往返）**

在 `tests/test_config_reader.cpp` 的 `SaveRoundTrip_FieldCompleteness`（L165 `}`）之后追加：

```cpp
// ============================================================
// independent_probe + scoring weights 往返 — 方案 B 新字段
// ============================================================
TEST_F(ConfigReaderTest, SaveRoundTrip_IndependentProbeAndScoring) {
    std::string tmpDirGeneric = std::filesystem::path(tempDir_.path()).generic_string();

    AppConfig original;
    original.database_path = tmpDirGeneric + "/fc_db.db";
    original.proxy.xray_executable = tmpDirGeneric + "/fc_xray.exe";
    original.independent_probe.enabled = false;
    original.proxy.scoring_delay_weight = 0.4;
    original.proxy.scoring_stability_weight = 0.35;
    original.proxy.scoring_history_weight = 0.25;

    touchFile(original.database_path);
    touchFile(original.proxy.xray_executable);

    ASSERT_TRUE(ConfigReader::save(configPath("fc_indprobe.json"), original));

    std::optional<AppConfig> loaded = ConfigReader::load(configPath("fc_indprobe.json"));
    ASSERT_TRUE(loaded.has_value());

    EXPECT_EQ(loaded->independent_probe.enabled, false);
    EXPECT_DOUBLE_EQ(loaded->proxy.scoring_delay_weight, 0.4);
    EXPECT_DOUBLE_EQ(loaded->proxy.scoring_stability_weight, 0.35);
    EXPECT_DOUBLE_EQ(loaded->proxy.scoring_history_weight, 0.25);
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cmake --build build --parallel 8 --target test_config_reader && ctest -R ConfigReaderTest -V --test-dir build`

Expected: 编译失败 `error: 'AppConfig' has no member named 'independent_probe'`（RED）。

- [ ] **Step 3: ConfigReader.h 新增字段**

在 `include/ConfigReader.h:100` 的 `} proxy_process_monitor;` 之后插入：

```cpp
    // Independent proxy periodic silent probe configuration.
    // interval reuses proxy_process_monitor.checkIntervalMs;
    // failure threshold reuses standalone_pool.evaluate.pruneFailStreak.
    struct {
        bool enabled = true;
    } independent_probe;
```

- [ ] **Step 4: 新建 IndependentProbeConfigParser.h**

Create `include/config/sections/IndependentProbeConfigParser.h`（镜像 `ProxyProcessMonitorConfigParser.h` 结构）：

```cpp
#ifndef CONFIG_SECTIONS_INDEPENDENT_PROBE_H
#define CONFIG_SECTIONS_INDEPENDENT_PROBE_H

#include <boost/json.hpp>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class IndependentProbeConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config);
};

inline void IndependentProbeConfigParser::parse(const boost::json::value& root, AppConfig& config) {
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();

    if (obj.contains("independent_probe") && obj.at("independent_probe").is_object()) {
        const boost::json::object& ip = obj.at("independent_probe").as_object();

        if (ip.contains("enabled") && ip.at("enabled").is_bool()) {
            config.independent_probe.enabled = ip.at("enabled").as_bool();
        } else if (ip.contains("enabled")) {
            Logger::write("WARNING: config.independent_probe.enabled has wrong type (expected bool), using default", LogLevel::WARN);
        }
    } else if (obj.contains("independent_probe")) {
        Logger::write("WARNING: config.independent_probe has wrong type (expected object), using default", LogLevel::WARN);
    }
}

} // namespace config
#endif
```

- [ ] **Step 5: ConfigReader.cpp 注册 parser**

`src/ConfigReader.cpp:28` 的 `#include "config/sections/ProxyProcessMonitorConfigParser.h"` 之后加一行：

```cpp
#include "config/sections/IndependentProbeConfigParser.h"
```

`src/ConfigReader.cpp:80` 的 `ProxyProcessMonitorConfigParser().parse(jv, config);` 之后加一行：

```cpp
    IndependentProbeConfigParser().parse(jv, config);
```

- [ ] **Step 6: ConfigJsonSerializer.cpp 补 scoring 序列化 + 新增 independent_probe 块**

`src/config/ConfigJsonSerializer.cpp` proxyObj 收尾处（`root["proxy"] = proxyObj;` 之前，L125 附近）追加：

```cpp
    proxyObj["scoring_delay_weight"] = config.proxy.scoring_delay_weight;
    proxyObj["scoring_stability_weight"] = config.proxy.scoring_stability_weight;
    proxyObj["scoring_history_weight"] = config.proxy.scoring_history_weight;
```

`root["proxy_process_monitor"] = pmObj;`（L152）之后追加：

```cpp
    // independent_probe
    boost::json::object ipObj;
    ipObj["enabled"] = config.independent_probe.enabled;
    root["independent_probe"] = ipObj;
```

> 现状缺口说明：`proxy.scoring_*_weight` 虽被 `ProxyConfigParser.h:58-69` 解析，但序列化器从未写出——手工编辑 config.json 的权重会在下次保存时丢失。本步骤一并补全，是 ConfigDialog 暴露权重（Task 4）的前置。

- [ ] **Step 7: 运行测试确认通过**

Run: `cmake --build build --parallel 8`（预期 `test_config_reader.exe` 编译链接成功）

Run: `ctest -R ConfigReaderTest -V --test-dir build`

Expected: 原 43/43 全绿 + 新用例 GREEN（GREEN）。

- [ ] **Step 8: 提交**

```bash
git add include/ConfigReader.h include/config/sections/IndependentProbeConfigParser.h src/ConfigReader.cpp src/config/ConfigJsonSerializer.cpp tests/test_config_reader.cpp
git commit -m "feat(config): add independent_probe.enabled + serialize scoring weights (plan Task 1)"
```

---

## Task 2: AppController 探活状态访问器

**Files:**
- Modify: `src/ui/AppController.h:209-225`（`isProxyPoolEnabled`/`setPool*` 附近新增公有方法）
- Modify: `src/ui/AppController.cpp`（在 `setPoolReportHealth` L2030 附近实现）

> 设计决策：镜像 `setPoolReportHealth` 的「仅内存热切换、不落盘」模式——悬浮窗勾选为运行期开关，持久值由 ConfigDialog 保存（与池三开关一致）。`getConfig()` 返回副本，MainFrame 启动时读取持久值。

- [ ] **Step 1: AppController.h 声明**

在 `src/ui/AppController.h` 中 `void setPoolAutoOptimize(bool on);`（L224）之后追加：

```cpp
     // Independent-proxy periodic silent probe (decoupled from the floating
     // widget switch per 方案 B). isIndependentProbeEnabled() is read by the
     // monitor timer each tick; setIndependentProbeEnabled() is the live
     // runtime toggle from StandaloneFloatingWidget (persisted via ConfigDialog).
     bool isIndependentProbeEnabled() const;
     void setIndependentProbeEnabled(bool on);
```

- [ ] **Step 2: AppController.cpp 实现**

在 `src/ui/AppController.cpp:2030` 的 `void AppController::setPoolReportHealth(bool on) {` 之前插入（注意保留原 4 空格缩进风格，文件顶部已 include `Logger.h`）：

```cpp
bool AppController::isIndependentProbeEnabled() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return config_.independent_probe.enabled;
}

void AppController::setIndependentProbeEnabled(bool on) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.independent_probe.enabled = on;
    Logger::write(std::string("[Config] independent_probe.enabled -> ") +
                  (on ? "true" : "false"), LogLevel::INFO);
}
```

- [ ] **Step 3: 编译验证**

Run: `cmake --build build --parallel 8`

Expected: 0 error（既有 warning 可忽略）。

> **接受项（评审修订 P4）**：AppController 访问器不设独立单测——构造 AppController 需完整 db/config 夹具，成本高于收益；`independent_probe.enabled` 默认 `true` 语义已由 Task 1 的 config 往返测试（`SaveRoundTrip_IndependentProbeAndScoring`）覆盖。

- [ ] **Step 4: 提交**

```bash
git add src/ui/AppController.h src/ui/AppController.cpp
git commit -m "feat(controller): add independent probe enabled accessors (plan Task 2)"
```

---

## Task 3: MainFrame — silent 探活解耦（timer 生命周期 + 回调门控）

**Files:**
- Modify: `src/ui/MainFrame.cpp:547-551`（启动门控）
- Modify: `src/ui/MainFrame.cpp:964-971`（`onProxyMonTimer` 内探活门控）
- Modify: `src/ui/MainFrame.cpp:1204-1215`（热改门控）

> 设计决策：timer 需在 `proxy_process_monitor.enabled || independent_probe.enabled` 时运行（timer 同时驱动悬浮窗刷新、悬垂纳管、silent 探活）；timer 回调内 silent 探活单独受 `isIndependentProbeEnabled()` 门控。当仅探活启用（监控关闭）时，状态栏圆点保持灰色（`updateProxyMonStatus(false, 0)`）。

- [ ] **Step 1: 修改启动门控（L547-551）**

> **前置确认（评审修订 P3）**：确认 `MainFrame.cpp` 可解析 `config::AppConfig`（经 `AppController.h` 间接包含 `ConfigReader.h`）。若编译报 `AppConfig` 未定义，在 `MainFrame.cpp` include 区补 `#include "ConfigReader.h"`。

现状：

```cpp
    if (controller_ && controller_->getConfig().proxy_process_monitor.enabled) {
        startProxyMonitor(controller_->getConfig().proxy_process_monitor.checkIntervalMs);
    } else {
        updateProxyMonStatus(false, 0);
    }
```

改为：

```cpp
    const config::AppConfig curCfg = controller_ ? controller_->getConfig() : config::AppConfig();
    const bool monEnabled = curCfg.proxy_process_monitor.enabled;
    const bool probeEnabled = curCfg.independent_probe.enabled;
    if (controller_ && (monEnabled || probeEnabled)) {
        startProxyMonitor(curCfg.proxy_process_monitor.checkIntervalMs);
        if (!monEnabled) {
            // 仅探活启用：timer 运行但悬浮窗关闭，状态圆点保持灰色。
            updateProxyMonStatus(false, 0);
        }
    } else {
        updateProxyMonStatus(false, 0);
    }
```

> 注：若 L547 现用 `controller_->getConfig()` 链式调用取 `enabled` 与 `checkIntervalMs`，本步骤统一为 `curCfg` 副本，避免两次调用取到不一致快照。

- [ ] **Step 2: 修改 onProxyMonTimer 探活门控（L971）**

现状（L971）：

```cpp
    controller_->testOnlineProxiesAsync(this, true);
```

改为：

```cpp
    // 方案 B：silent 探活独立于悬浮窗开关，由 independent_probe.enabled 门控。
    if (controller_->isIndependentProbeEnabled()) {
        controller_->testOnlineProxiesAsync(this, true);
    }
```

> 悬垂纳管（L955-963）保持无条件随 timer 运行（探活启用时纳管仍然需要；探活关闭时纳管照旧，本就是维护性任务）。

- [ ] **Step 3: 修改热改门控（L1204-1215）**

现状（`onMenuConfig` 保存回调内）：

```cpp
        // Detect proxy process monitor changes
        bool oldProxyMonEnabled = config_.proxy_process_monitor.enabled;
        int oldProxyMonInterval = config_.proxy_process_monitor.checkIntervalMs;
        bool proxyMonEnabledChanged = (cfg.proxy_process_monitor.enabled != oldProxyMonEnabled);
        bool proxyMonIntervalChanged = (cfg.proxy_process_monitor.checkIntervalMs != oldProxyMonInterval);
        if (proxyMonEnabledChanged || proxyMonIntervalChanged) {
            if (cfg.proxy_process_monitor.enabled) {
                startProxyMonitor(cfg.proxy_process_monitor.checkIntervalMs);
            } else {
                stopProxyMonitor();
            }
        }
```

改为：

```cpp
        // Detect proxy process monitor / independent probe changes.
        // Timer stays alive when EITHER switch is on (it drives the widget
        // refresh, dangling adoption AND the silent probe cadence).
        bool oldProxyMonEnabled = config_.proxy_process_monitor.enabled;
        int oldProxyMonInterval = config_.proxy_process_monitor.checkIntervalMs;
        bool oldProbeEnabled = config_.independent_probe.enabled;
        bool proxyMonEnabledChanged = (cfg.proxy_process_monitor.enabled != oldProxyMonEnabled);
        bool proxyMonIntervalChanged = (cfg.proxy_process_monitor.checkIntervalMs != oldProxyMonInterval);
        bool probeEnabledChanged = (cfg.independent_probe.enabled != oldProbeEnabled);
        bool needProxyMonTimer = cfg.proxy_process_monitor.enabled || cfg.independent_probe.enabled;
        if (proxyMonEnabledChanged || proxyMonIntervalChanged || probeEnabledChanged) {
            if (needProxyMonTimer) {
                startProxyMonitor(cfg.proxy_process_monitor.checkIntervalMs);
                if (!cfg.proxy_process_monitor.enabled) {
                    updateProxyMonStatus(false, 0);   // 保持灰点（仅探活模式）
                }
            } else {
                stopProxyMonitor();
            }
        }
```

- [ ] **Step 4: 编译验证**

Run: `cmake --build build --parallel 8`

Expected: 0 error。

- [ ] **Step 5: UI 测试回归**

Run: `ctest -R "UI_FLOATINGWIDGET|UI_POOL|UI_PORTCLOSE" -V --test-dir build`

Expected: 3/3 Passed（timer 生命周期改动不影响悬浮窗功能）。

- [ ] **Step 6: 提交**

```bash
git add src/ui/MainFrame.cpp
git commit -m "feat(mainframe): decouple silent probe from floating widget switch (plan Task 3)"
```

---

## Task 4: StandaloneFloatingWidget — 新增「独立代理探活」勾选框

**Files:**
- Modify: `src/ui/StandaloneFloatingWidget.h:129-134`（新增 `probeChk_` 成员）
- Modify: `src/ui/StandaloneFloatingWidget.cpp`（ctor 控件创建 L278-296、绑定 L328、applySettings L372-386、onToggleProbe 仿 L773、Orb Hide L1038-1040 / Panel Show L1061-1063）

> 设计决策（对报告 §7.4.4 #4 的微调）：报告原文「控件灰化条件 `poolEnabled || independentProbeEnabled`」会导致探活开关在 `independent_probe.enabled=false` 时被灰化而无法再次打开。本计划采用：`probeChk_` 在悬浮窗存在期间始终可勾选（悬浮窗本身由 `proxy_process_monitor.enabled` 门控），与池三开关（按 `standalone_pool.enabled` 灰化）正交。

- [ ] **Step 1: StandaloneFloatingWidget.h 新增成员**

在 `src/ui/StandaloneFloatingWidget.h` 的 `wxCheckBox* optimizeChk_{nullptr};`（L133）之后追加：

```cpp
    wxCheckBox* probeChk_{nullptr};   // 独立代理周期探活开关（方案 B）
```

- [ ] **Step 2: ctor 创建控件 + 布局**

在 `src/ui/StandaloneFloatingWidget.cpp` 的 `optimizeChk_ = new wxCheckBox(...);` 之后（L282 附近）追加创建与初值：

```cpp
    probeChk_ = new wxCheckBox(this, wxID_ANY, L"独立代理探活");
    probeChk_->SetBackgroundColour(wxColour(245, 246, 247));
    probeChk_->SetValue(cfg_.independent_probe.enabled);
```

在 `poolChkRow` 组装处（L294-296）追加：

```cpp
    poolChkRow->Add(probeChk_, 0, wxRIGHT, 12);
```

- [ ] **Step 3: 绑定 onToggleProbe**

在 `src/ui/StandaloneFloatingWidget.cpp` 的 `optimizeChk_->Bind(wxEVT_CHECKBOX, ...)`（L330）之后追加：

```cpp
    probeChk_->Bind(wxEVT_CHECKBOX, &StandaloneFloatingWidget::onToggleProbe, this);
```

- [ ] **Step 4: applySettings 回填 + 灰化**

在 `src/ui/StandaloneFloatingWidget.cpp` applySettings 的 `if (optimizeChk_) { optimizeChk_->SetValue(...); }`（L384）之后追加：

```cpp
    // 独立代理探活开关：不随池启用状态灰化（独立于池，见方案 B）。
    if (probeChk_) { probeChk_->SetValue(cfg_.independent_probe.enabled); }
```

> 池灰化块（L376-379 `poolEnabled`）保持只作用于 6 个池控件，不新增 probeChk_ 灰化。

- [ ] **Step 5: 实现 onToggleProbe**

在 `src/ui/StandaloneFloatingWidget.cpp` 的 `onToggleOptimize`（L789-793）之后追加：

```cpp
void StandaloneFloatingWidget::onToggleProbe(wxCommandEvent& event) {
    if (controller_) {
        controller_->setIndependentProbeEnabled(probeChk_->GetValue());
    }
    event.Skip();
}
```

- [ ] **Step 6: Orb / Panel 分支 Hide-Show 补 probeChk_（防 bugfix #84 回归）**

Orb 分支（`applyShape` 内 `if (mode_==Orb)` 附近 L1038-1040）在 `if (optimizeChk_) optimizeChk_->Hide();` 之后追加：

```cpp
        if (probeChk_) probeChk_->Hide();
```

Panel 分支（L1061-1063）在 `if (optimizeChk_) optimizeChk_->Show();` 之后追加：

```cpp
        if (probeChk_) probeChk_->Show();
```

> 必须同步 Hide/Show：Orb 模式下未隐藏的子控件 HWND 会横亘球体上部截获鼠标命中（2026-09-14 bugfix #84 OrbHitZone 同类缺陷）。

- [ ] **Step 7: UI 测试回归 + 编译**

Run: `cmake --build build --parallel 8 && ctest -R "UI_FLOATINGWIDGET|UI_POOL" -V --test-dir build`

Expected: 构建 0 error；UI_FLOATINGWIDGET（含 Orb hit-test 用例）5/5 + UI_POOL 全绿。

- [ ] **Step 8: 提交**

```bash
git add src/ui/StandaloneFloatingWidget.h src/ui/StandaloneFloatingWidget.cpp
git commit -m "feat(widget): add independent probe toggle checkbox (plan Task 4)"
```---

## Task 5: ConfigDialog — 分类树重构（2026-09-17 评审层级树）

**Files:**
- Modify: `src/ui/ConfigDialog.cpp`（构建树 L55-101/L188-221、回填 L307-323、保存 L430-453、校验 L593-631）
- Modify: `src/ui/ConfigDialog.h`（新增 `applyBackendVisibility()` 声明 + `xrayBackendCat_`/`sbBackendCat_` 成员）

> 目标树（仅动 4 个分类，其余保持原位，避免大范围移动引入回归）：
>
> ```
> ├── 数据库
> ├── 工作线程配置 (Xray 全局，不动)
> ├── 代理后端选择         ← 重命名原「代理配置」并重组（use_singbox 前置驱动条件可见性，解决 P3）
> ├── 测试 → 日志 → 网络监控 → 订阅 → 去重 → 同步 → 自动任务 → 通知（不动）
> ├── 监控悬浮窗           ← 重命名原「监控代理进程」（解决 P5）
> ├── 健康度评估           ← 新增父分类
> │   ├── 独立代理评估     ← 新增子分类（解决 P2）：independent_probe.enabled + scoring_*_weight×3
> │   └── 代理池评估       ← 拆分子分类（解决 P1/P4/P6）：pool_eval_*×6
> └── 代理池配置           ← 拆分（解决 P1/P4/P6）：pool_enabled/socks/api/balancer/obs×2
> ```
>
> 评审树中「日志 / Xray 全局 / 测试」等顺序调整属纯视觉重排，本计划不执行（非功能改动，降低回归面）。**有意识取舍，已经用户确认**（评审修订 P5）：仅动 4 个分类（代理后端选择/监控悬浮窗/健康度评估/代理池配置），其余分类保持原位。

- [ ] **Step 1: 「代理配置」→「代理后端选择」+ 条件可见性**

将 `src/ui/ConfigDialog.cpp:54-101` 段整体替换为（属性名全部保持现值，仅重组顺序与可见性）：

```cpp
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
    {
        wxFileProperty* tmplProp = new wxFileProperty(L"xray配置模板", "proxy_template_config_path", cfg.proxy.template_config_path);
        propGrid_->AppendIn(xrayBackendCat_, tmplProp);
        propGrid_->SetPropertyAttribute("proxy_template_config_path", wxPG_FILE_SHOW_FULL_PATH, (long)1);
        tmplProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
        propGrid_->SetPropertyAttribute("proxy_template_config_path", wxPG_DIALOG_TITLE, L"选择 Xray 启动配置模板文件");
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
    {
        wxFileProperty* sbTmplProp = new wxFileProperty(L"Sing-box配置模板", "proxy_singbox_template_config_path", cfg.proxy.singbox_template_config_path);
        propGrid_->AppendIn(sbBackendCat_, sbTmplProp);
        propGrid_->SetPropertyAttribute("proxy_singbox_template_config_path", wxPG_FILE_SHOW_FULL_PATH, (long)1);
        sbTmplProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
        propGrid_->SetPropertyAttribute("proxy_singbox_template_config_path", wxPG_DIALOG_TITLE, L"选择 Sing-box 启动配置模板文件");
    }
```

- [ ] **Step 2: 「监控代理进程」→「监控悬浮窗」+ 新增「健康度评估」父分类 +「代理池配置」**

将 `src/ui/ConfigDialog.cpp:188-221` 段（`--- 监控代理进程 配置 ---` 至 `pool_eval_probe_workers` 结束）整体替换为：

```cpp
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
    propGrid_->AppendIn(indepEvalCat, new wxDoubleProperty(L"延迟权重(0.0-1.0)", "scoring_delay_weight", cfg.proxy.scoring_delay_weight));
    propGrid_->AppendIn(indepEvalCat, new wxDoubleProperty(L"稳定性权重(0.0-1.0)", "scoring_stability_weight", cfg.proxy.scoring_stability_weight));
    propGrid_->AppendIn(indepEvalCat, new wxDoubleProperty(L"历史权重(0.0-1.0)", "scoring_history_weight", cfg.proxy.scoring_history_weight));

    // 代理池评估 子分类（解决 P1/P4/P6）：evaluate.* 六项
    wxPropertyCategory* poolEvalCat = new wxPropertyCategory(L"代理池评估");
    propGrid_->AppendIn(healthCat, poolEvalCat);
    propGrid_->AppendIn(poolEvalCat, new wxIntProperty(L"评估间隔(秒)", "pool_eval_interval", cfg.standalone_pool.evaluate.intervalSec));
    propGrid_->AppendIn(poolEvalCat, new wxBoolProperty(L"报告健康结果", "pool_eval_report_health", cfg.standalone_pool.evaluate.reportHealth));
    propGrid_->AppendIn(poolEvalCat, new wxBoolProperty(L"自动剔除失效成员", "pool_eval_auto_prune", cfg.standalone_pool.evaluate.autoPruneDead));
    propGrid_->AppendIn(poolEvalCat, new wxIntProperty(L"剔除阈值(连续失败次数)", "pool_eval_prune_streak", cfg.standalone_pool.evaluate.pruneFailStreak));
    propGrid_->AppendIn(poolEvalCat, new wxBoolProperty(L"自动优化(预留记录式)", "pool_eval_auto_optimize", cfg.standalone_pool.evaluate.autoOptimize));
    // 探针 worker 数：0 = 禁用常驻探针池（ProxyProbePool::start 的 workerCount<=0 分支），1-N = 常驻 Xray 探针 worker 数。
    propGrid_->AppendIn(poolEvalCat, new wxIntProperty(L"探针 worker 数(0=禁用)", "pool_eval_probe_workers", cfg.standalone_pool.evaluate.probeWorkers));

    // --- 代理池配置（拆分，解决 P1/P4/P6：pool 本体 + observatory） ---
    propGrid_->Append(new wxPropertyCategory(L"代理池配置"));
    propGrid_->Append(new wxBoolProperty(L"启用", "pool_enabled", cfg.standalone_pool.enabled));
    propGrid_->Append(new wxIntProperty(L"SOCKS 端口(期望值)", "pool_socks_port", cfg.standalone_pool.socksPort));
    propGrid_->Append(new wxIntProperty(L"API 端口(期望值)", "pool_api_port", cfg.standalone_pool.apiPort));
    {
        wxArrayString strategyChoices;
        strategyChoices.Add("random");
        strategyChoices.Add("leastPing");
        strategyChoices.Add("leastLoad");
        propGrid_->Append(new wxEnumProperty(L"均衡策略", "pool_balancer_strategy", strategyChoices));
        propGrid_->SetPropertyValue("pool_balancer_strategy", wxString(cfg.standalone_pool.balancerStrategy));
    }
    propGrid_->Append(new wxIntProperty(L"观测间隔(秒)", "pool_obs_interval", cfg.standalone_pool.observatory.intervalSec));
    propGrid_->Append(new wxIntProperty(L"观测超时(秒)", "pool_obs_timeout", cfg.standalone_pool.observatory.timeoutSec));
```

> 保留原注释要点（方案甲 mode/observatory.type/samplingCount 预留、destination 恒被 test.url 覆盖、probeUrl 内存注入）到合适位置；属性名 `pool_*`/`proxy_*`/`independent_probe_enabled`/`scoring_*_weight` 全部与后续回填/保存段一致。

- [ ] **Step 3: 条件可见性函数 + use_singbox 联动（成员指针版）**

先在 `ConfigDialog.h` 新增私有声明与成员（成员指针避免按标签查询的脆弱性，评审修订 P1）：

```cpp
    // 「代理后端选择」两个后端路径组（条件可见，解决 P3）——指针在构建树时捕获
    wxPropertyGridProperty* xrayBackendCat_{nullptr};
    wxPropertyGridProperty* sbBackendCat_{nullptr};
    void applyBackendVisibility();
```

在 `ConfigDialog.cpp` 保存回调（`onSave`/`onOk` 附近）之前实现：

```cpp
void ConfigDialog::applyBackendVisibility() {
    if (!propGrid_) return;
    const bool useSingbox = propGrid_->GetPropertyValueAsBool("proxy_use_singbox");
    if (xrayBackendCat_) { xrayBackendCat_->SetHidden(useSingbox); }
    if (sbBackendCat_)   { sbBackendCat_->SetHidden(!useSingbox); }
}
```

在对话框构造末尾（`LoadConfig` 回填段后）追加 `applyBackendVisibility();`，并绑定 `use_singbox` 属性变化。**绑定前先 grep 确认 ConfigDialog.cpp 是否已有 `wxEVT_PG_CHANGED` 处理器**（Bind 允许多 handler，但避免与既有逻辑重复触发）：

```cpp
    propGrid_->Bind(wxEVT_PG_CHANGED, [this](wxPropertyGridEvent& event) {
        if (event.GetPropertyName() == "proxy_use_singbox") {
            applyBackendVisibility();
        }
        event.Skip();
    });
```

- [ ] **Step 4: 回填段（loadValues）补 independent_probe + scoring**

在 `ConfigDialog.cpp` 回填块（L307-323 `pool_eval_probe_workers` 之后）追加：

```cpp
    propGrid_->SetPropertyValue("independent_probe_enabled", cfg.independent_probe.enabled);
    propGrid_->SetPropertyValue("scoring_delay_weight", cfg.proxy.scoring_delay_weight);
    propGrid_->SetPropertyValue("scoring_stability_weight", cfg.proxy.scoring_stability_weight);
    propGrid_->SetPropertyValue("scoring_history_weight", cfg.proxy.scoring_history_weight);
    applyBackendVisibility();
```

- [ ] **Step 5: 保存段（editedConfig 装配）补 independent_probe + scoring**

在 `ConfigDialog.cpp` 保存块（L448-453 `probeWorkers` 之后）追加：

```cpp
    editedConfig_.independent_probe.enabled = propGrid_->GetPropertyValueAsBool("independent_probe_enabled");
    editedConfig_.proxy.scoring_delay_weight = propGrid_->GetPropertyValueAsDouble("scoring_delay_weight");
    editedConfig_.proxy.scoring_stability_weight = propGrid_->GetPropertyValueAsDouble("scoring_stability_weight");
    editedConfig_.proxy.scoring_history_weight = propGrid_->GetPropertyValueAsDouble("scoring_history_weight");
```

- [ ] **Step 6: 校验段（validateConfig）补 scoring 区间**

在 `ConfigDialog.cpp` 校验区（L593-631，`standalone_pool` 校验块附近）追加：

```cpp
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
```

> 校验块需确认 `validateConfig()` 现有 `return false` 路径位置后插入（放在 `pool_eval_probe_workers` 校验 L627-631 之后、最终 `return true;` 之前）。

- [ ] **Step 7: 编译 + 配置测试回归**

Run: `cmake --build build --parallel 8 && ctest -R "ConfigReaderTest|UI_MAINWINDOW" -V --test-dir build`

Expected: 构建 0 error；ConfigReaderTest 全绿（含 Task 1 新用例）；UI_MAINWINDOW Passed（对话框可正常弹出）。

- [ ] **Step 8: 提交**

```bash
git add src/ui/ConfigDialog.cpp src/ui/ConfigDialog.h
git commit -m "feat(dialog): restructure categories per 2026-09-17 review + expose scoring weights (plan Task 5)"
```

---

## Task 6: 全量验证 + 生产配置对齐 + 文档登记

**Files:**
- Modify: `bin/config.json`（新增 `independent_probe.enabled: true`）
- Modify: `docs/INDEX.md`（登记本 Plan + 更新报告条目状态）
- Modify: `docs/plans/project-plans-tracker.md`（AGENTS.md §6.2 要求更新状态跟踪器）

- [ ] **Step 1: 全量构建 + 全部测试**

Run: `cmake --build build --parallel 8 && ctest -V --test-dir build`

Expected: 非 UI 全绿 + UI 全绿（UI_POOL / UI_FLOATINGWIDGET / UI_PORTCLOSE 至少 3/3）。

- [ ] **Step 2: 生产配置对齐**

在 `bin/config.json` 的 `proxy_process_monitor` 块之后追加：

```json
"independent_probe": {
  "enabled": true
},
```

> 对齐后可通过 `validproxy-cli.exe -c bin/config.json` 启动验证配置可解析（CLI 无参启动走默认批量测试，可用 `-c` 指定配置做冒烟）。

> **JSON 格式注意（评审修订 P6）**：手工加键时确保上一块（`proxy_process_monitor`）末行补逗号；键名与 serializer 写出一致（`independent_probe` / `enabled`），避免下次保存时产生重复键。

- [ ] **Step 3: 文档登记**

`docs/INDEX.md` §8.2 实施计划/规格表追加一行：

```markdown
| 2026-09-17 | feat | [`2026-09-17-Plan-HealthMonitorUnify-v1.0.md`](./plans/2026-09-17-Plan-HealthMonitorUnify-v1.0.md) | 方案 B 配置层聚合+控制层解耦 — independent_probe.enabled 独立开关解耦 silent 探活；ConfigDialog 按评审层级树重组（监控悬浮窗/健康度评估/独立代理评估/代理池评估/代理池配置/代理后端选择）；暴露 proxy.scoring_*_weight 三权重（补序列化缺口）；悬浮窗新增「独立代理探活」勾选框；规格=Report v1.2 §7.4.4 + §7.3 评审树 | ⏳ executing |
```

并更新 `docs/plans/project-plans-tracker.md` 本计划条目状态为 `executing`。

- [ ] **Step 4: 提交**

```bash
git add bin/config.json docs/INDEX.md docs/plans/project-plans-tracker.md docs/plans/2026-09-17-Plan-HealthMonitorUnify-v1.0.md
git commit -m "docs(plan): register HealthMonitorUnify plan + align production config (plan Task 6)"
```

---

## 评审修订记录（v1.0 → v1.1, 2026-09-17）

按 Report v1.2 × Plan v1.0 交叉评审意见修订（评审输出见会话记录）：

| # | 评审问题 | 修订内容 | 位置 |
| :---: | :--- | :--- | :--- |
| P1 | 「按标签查询」脆弱 | `applyBackendVisibility()` 改用 `xrayBackendCat_`/`sbBackendCat_` 成员指针（Step 1 构建时捕获，Step 3 用指针）；Task 5 Files 增补 `ConfigDialog.h` | Task 5 Step 1/Step 3 |
| P2 | PG_CHANGED 绑定潜在冲突 | Step 3 增「绑定前 grep 确认既有 wxEVT_PG_CHANGED 处理器」 | Task 5 Step 3 |
| P3 | `config::AppConfig` 可见性未证实 | Task 3 Step 1 增「前置确认 include」子步 | Task 3 Step 1 |
| P4 | Task 2/3/4 无单测 | Task 2 Step 3 增「接受项」注记（config 侧已由 Task 1 覆盖） | Task 2 Step 3 |
| P5 | 评审树视觉顺序部分执行需授权 | Task 5 目标树注记增「有意识取舍，已经用户确认」 | Task 5 目标树 |
| P6 | 手工加键 JSON 合规 | Task 6 Step 2 增「JSON 格式注意」 | Task 6 Step 2 |

伴生修订：报告 v1.2 同步 3 处（§7.4.3 方案 B 行「1 个控件」、§7.4.4 #4 灰化条件、§7.3 observatory 归属注记 + §6 结论 #4 文档关联）。

---

## 自检（Self-Review）

**规格覆盖：**
- §7.4.4 #1 ConfigReader 新字段 → Task 1 ✅
- §7.4.4 #2 Serializer + Parser → Task 1 ✅（含解析侧已有 scoring 的确认）
- §7.4.4 #3 ConfigDialog 分类重构 → Task 5 ✅（评审层级树完整落地）
- §7.4.4 #4 悬浮窗 probeChk_ → Task 4 ✅
- §7.4.4 #5 AppController silent 探活条件 → Task 2 + Task 3 ✅
- 报告 §6 结论 #3 scoring 暴露 + [0.0,1.0] 校验 → Task 1（序列化）+ Task 5（UI + 校验）✅

**占位符扫描：** 无 TBD/TODO；每个 Task 均含精确路径、代码、命令、预期输出、提交。

**类型一致性：** `independent_probe.enabled`（bool）在 ConfigReader.h / Parser / Serializer / AppController / MainFrame / Widget / ConfigDialog 全程一致；`scoring_delay_weight` 等（double）在 test（`EXPECT_DOUBLE_EQ`）/ ConfigDialog（`GetPropertyValueAsDouble`）/ Serializer（`as_double` via boost json）一致。

**风险注记：**
- `config::AppConfig` 若尚未被 MainFrame.cpp include，Task 3 Step 1 的 `config::AppConfig curCfg` 需加 `#include "ConfigReader.h"`（MainFrame.cpp 已通过 AppController.h 间接引用，编译期可确认）。
- Task 5 Step 2 中 `wxDoubleProperty` 需 `<wx/propgrid/props.h>`（ConfigDialog.cpp 已含 propgrid 头）。
- UI 自动化测试不覆盖 ConfigDialog 树结构细节，Task 5 交付需人工打开配置窗口目视确认层级（见 Task 5 手工验证项）。

---