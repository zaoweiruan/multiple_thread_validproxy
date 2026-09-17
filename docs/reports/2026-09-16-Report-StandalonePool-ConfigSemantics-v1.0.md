# Report: `proxy_process_monitor` / `standalone_pool` 配置语义与「独立代理健康度评估」UI 暴露清单

- 日期: 2026-09-16
- 类型: Report（配置语义说明 + UI 暴露面盘点）
- 模块: `config.proxy_process_monitor` / `config.standalone_pool`（含 `StandaloneFloatingWidget`、`ConfigDialog`、`AppController::startProxyPool`）
- 版本: v1.2（附录：配置项架构评估 + 架构级合并方案评估 + §7.3 分类重构方案评审修订）
- 代码基线: `include/ConfigReader.h`、`include/config/sections/ProxyProcessMonitorConfigParser.h`、`include/config/sections/StandalonePoolConfigParser.h`、`include/StandaloneProxyPool.h`、`src/ui/MainFrame.cpp`、`src/ui/ConfigDialog.cpp`、`src/ui/StandaloneFloatingWidget.cpp`、`src/ui/AppController.cpp`、`bin/config.json`

> **触发**: 用户咨询「两个开关的作用」与「独立代理健康度评估配置是否暴露」。本报告将代码级证据与当前生产配置对齐，作为长期文档沉淀，避免每次口头解释。

> **修订记录（v1.0 → v1.1, 2026-09-16）**：新增 §7 配置项架构评估章节，覆盖 4 子节：§7.1 `standalone_pool.enabled` 必要性、§7.2 健康度评估与 `proxy_process_monitor.enabled` 合并可行性、§7.3 配置窗口分类标识问题（P1-P6）、§7.4 架构级合并方案（A/B/C 三档评估 + 推荐方案 B 详细设计）。§6 结论 #3 同步修正为「独立到新增「独立代理评分」分类」，与 §7.3 P2、§7.4 方案 B 建议一致。附录证据索引补充 v1.1 依据。

> **修订记录（v1.1 → v1.2, 2026-09-17）**：按 `docs/review/2026-09-17-Review-results-of-configuration-item-reengineering.md` 评审意见修订 §7.3「建议分类重构方案」——由「平铺 13 分类」改为「层级式」：「健康度评估」新增为父分类，下挂「独立代理评估」+「代理池评估」两个子分类；「代理池配置」承载 pool 本体；「监控悬浮窗」重命名；「代理后端选择」独立分类。§6 结论 #3 同步改为「独立到新增「独立代理评估」子分类」；§7.4.4 方案 B 的 ConfigDialog 分类树对齐评审层级。附录证据索引补充 v1.2 依据。*（评审修订补记：§7.3 补 `observatory.intervalSec/timeoutSec` 归属注记；§7.4.3/§7.4.4 控件口径与灰化条件按 Plan v1.1 评审统一——「悬浮窗新增 1 个 probeChk_」且「不随池/探活状态灰化」，原稿 3 控件/灰化公式为遗留笔误。)*

---

## 1. 概述

`config.json` 中与独立代理监控/代理池相关的两组开关位于不同层级：

| 配置段 | 默认值 | 生产 `bin/config.json` | 门禁对象 |
| :--- | :--- | :--- | :--- |
| `proxy_process_monitor.enabled` | `false` | `true` | UI 悬浮监控面板 `StandaloneFloatingWidget` 与状态栏 timer |
| `standalone_pool.enabled` | `false` | `true` | 运行时代理池 `StandaloneProxyPool`（单 Xray 进程 + 动态成员注入） |

两个开关**独立**：前者只控制监控 UI 是否可用，后者只控制池本体是否可运行。用户当前生产配置两者同时启用，属预期组合。

「独立代理健康度评估」相关配置集中在 `config.standalone_pool.evaluate.*`，共 6 字段；另有两个「健康度权重」字段 `config.proxy.scoring_*_weight` 用于 ProxyScorer，本报告一并纳入暴露面盘点。

---

## 2. `proxy_process_monitor.enabled` 作用

### 2.1 字段定义

`include/ConfigReader.h:96-100`：

```cpp
// ProxyProcessMonitor configuration
struct {
    bool enabled{false};
    int checkIntervalMs{30000};
} proxy_process_monitor;
```

### 2.2 解析器行为

`include/config/sections/ProxyProcessMonitorConfigParser.h:15-42`：

- `enabled` 类型错误时 `WARN` 并保留默认 `false`。
- `check_interval_ms` 强制夹逼 **[5000, 300000]** ms（防止高频刷 UI / 低频失效），类型错误 `WARN` 并保留默认 `30000`。

### 2.3 消费点（三处 UI 触点）

| 消费位置 | 代码位置 | 作用 |
| :--- | :--- | :--- |
| 启动路径 | `src/ui/MainFrame.cpp:537-542` | `enabled=true` → `startProxyMonitor(checkIntervalMs)`；否则 `updateProxyMonStatus(false, 0)`（状态栏圆点灰） |
| 悬浮窗创建 | `src/ui/MainFrame.cpp:545-551` | `enabled=true` → `new StandaloneFloatingWidget(...); setActive(true)`；否则不创建悬浮窗 |
| 手动入口守卫 | `src/ui/MainFrame.cpp:1104-1115` | `enabled=false` 时 Ctrl+M/工具栏/菜单点击弹出 `wxMessageBox("监控代理进程未在配置中启用")` 并拒绝 |

### 2.4 悬浮窗刷新节奏

`src/ui/StandaloneFloatingWidget.cpp:345-403`：

- `Show(true)` 首次显示 → `timer_.Start(FloatingWidgetPolicy::clampIntervalMs(cfg_.proxy_process_monitor.checkIntervalMs))`。
- `applySettings(newCfg)` 更新配置后：`enabled=false` → `active_=false; Show(false);` 强制隐藏；`enabled=true` 且 timer 运行中 → 按新间隔重启 timer。
- `toggleActive()` / `setActive()` 均检查 `cfg_.proxy_process_monitor.enabled` 为 true 才允许显示。

### 2.5 配置热改链路

`src/ui/MainFrame.cpp:1183-1200`（`onMenuConfig` 保存回调）：

```cpp
bool proxyMonEnabledChanged = (cfg.proxy_process_monitor.enabled != oldProxyMonEnabled);
bool proxyMonIntervalChanged = (cfg.proxy_process_monitor.checkIntervalMs != oldProxyMonInterval);
if (proxyMonEnabledChanged || proxyMonIntervalChanged) {
    if (cfg.proxy_process_monitor.enabled) {
        startProxyMonitor(cfg.proxy_process_monitor.checkIntervalMs);
    } else {
        stopProxyMonitor();
    }
}
if (floatingWidget_) {
    floatingWidget_->applySettings(cfg);
}
syncFloatingWidgetControls();
```

保存后立即生效，无需重启进程。

### 2.6 语义归纳

> **`proxy_process_monitor.enabled` = 「监控代理进程悬浮窗」是否可用**
>
> - `true`：创建/保持 `StandaloneFloatingWidget`（悬浮球 + 池控制按钮 + 三勾选框），以 `check_interval_ms` 为周期刷新；状态栏圆点绿色；菜单/工具栏入口可用。
> - `false`：不创建悬浮窗；状态栏圆点灰色；菜单点击被拒；已有悬浮窗被强制隐藏。

不涉及池的启动/停止，也不涉及独立代理（standalone proxy，非池）本身的运行状态。

---

## 3. `standalone_pool.enabled` 作用

### 3.1 字段定义

`include/ConfigReader.h:10-35`（`StandalonePoolConfig` 完整结构）：

```cpp
struct StandalonePoolConfig {
    bool enabled = false;
    std::string mode = "pool";                    // "pool" | "select"
    int socksPort = 10809;
    int apiPort = 10810;
    std::string balancerStrategy = "leastPing";   // "random" | "leastPing" | "leastLoad"
    std::string probeUrl;
    struct {
        std::string type = "http";                // "http" | "ping"
        std::string destination = "https://www.google.com";
        int intervalSec = 5;
        int samplingCount = 10;
        int timeoutSec = 5;
    } observatory;
    struct {
        int intervalSec = 10;
        bool reportHealth = true;                 // a
        bool autoPruneDead = false;               // b
        int pruneFailStreak = 3;                  // b 阈值
        bool autoOptimize = false;                // c
        int probeWorkers = 2;                     // 常驻 Xray 探针 worker 数
    } evaluate;
};
```

### 3.2 消费点（运行时门禁）

`src/ui/AppController.cpp:1876-1881`：

```cpp
bool AppController::isProxyPoolEnabled() const {
    return config_.standalone_pool.enabled;
}

bool AppController::startProxyPool() {
    if (!config_.standalone_pool.enabled) return false;
    ...
}
```

`enabled=false` 是硬门禁：池不启动，不分配端口，不构造 `StandaloneProxyPool` 实例。

### 3.3 消费点（UI 联动）

`src/ui/StandaloneFloatingWidget.cpp:372-386`（`applySettings`）：

```cpp
const bool poolEnabled = cfg_.standalone_pool.enabled;
if (startStopBtn_) { startStopBtn_->Enable(poolEnabled); }
if (addBtn_)        { addBtn_->Enable(poolEnabled); }
if (refreshBtn_)    { refreshBtn_->Enable(poolEnabled); }
if (reportChk_)     { reportChk_->Enable(poolEnabled); }
if (pruneChk_)      { pruneChk_->Enable(poolEnabled); }
if (optimizeChk_)   { optimizeChk_->Enable(poolEnabled); }
```

`enabled=false` 时悬浮窗上 6 个池控件全部灰化（`启动池`/`添加代理`/`刷新`/`上报健康`/`自动剔除死亡`/`自动优化`），但监控表本身仍显示（悬浮窗由 `proxy_process_monitor.enabled` 独立控制）。

### 3.4 语义归纳

> **`standalone_pool.enabled` = 「代理池本体」是否可启动**
>
> - `true`：`AppController::startProxyPool()` 才会分配 SOCKS/API 端口（经 PortManager）、构造 `StandaloneProxyPool`、启动 balancer+observatory、开启 `evaluatorLoop`（`ProxyHealthEvaluator`）。
> - `false`：池不启动，悬浮窗上 6 个池控件灰化，注入/剔除成员不可用。

与 `proxy_process_monitor.enabled` 正交：即使池启用，悬浮窗仍需在 `proxy_process_monitor.enabled=true` 时才显示。

---

## 4. 独立代理健康度评估配置暴露清单

「独立代理健康度评估」在代码中对应两个概念，本报告均覆盖：

- **A. 代理池成员健康度评估** — `config.standalone_pool.evaluate.*`（6 字段），消费方 `StandaloneProxyPool::evaluatorLoop` + `ProxyHealthEvaluator`。
- **B. 独立代理评分权重** — `config.proxy.scoring_*_weight`（3 字段），消费方 `AppController.cpp:412-413` 的 `ProxyScorer::compute`。

### 4.1 A. `evaluate.*` 六字段暴露状态

| 字段 | 默认值 | ConfigDialog (propGrid) | StandaloneFloatingWidget | 备注 |
| :--- | :--- | :--- | :--- | :--- |
| `evaluate.intervalSec` | 10 | ✅ `pool_eval_interval` (`ConfigDialog.cpp:214`) | — | 评估循环周期 |
| `evaluate.reportHealth` | `true` | ✅ `pool_eval_report_health` (`:215`) | ✅ `reportChk_` (`StandaloneFloatingWidget.cpp:278/:384`) | 开关「上报健康」；写回历史表 |
| `evaluate.autoPruneDead` | `false` | ✅ `pool_eval_auto_prune` (`:216`) | ✅ `pruneChk_` (`:279/:385`) | 开关「自动剔除死亡」 |
| `evaluate.pruneFailStreak` | 3 | ✅ `pool_eval_prune_streak` (`:217`) | — | 剔除阈值（连续失败次数） |
| `evaluate.autoOptimize` | `false` | ✅ `pool_eval_auto_optimize` (`:218`) | ✅ `optimizeChk_` (`:280/:386`) | 开关「自动优化(预留记录式)」 |
| `evaluate.probeWorkers` | 2 | ✅ `pool_eval_probe_workers` (`:221`) | — | 0=禁用常驻探针池，1-64 worker |

**结论 A**：6 字段全部在 ConfigDialog 暴露；悬浮窗上暴露 3 个「开关类」字段（`reportHealth`/`autoPruneDead`/`autoOptimize`）供热切换，其余 3 项（周期/阈值/探针 worker 数）仅在配置对话框调整。

### 4.2 A. 同段未暴露字段（方案甲预留）

按 `docs/specs/2026-09-16-Spec-PoolConfigDialogAdjust-v1.0.md` 方案甲，以下字段**后端存在、UI 不暴露**，仅通过 `bin/config.json` 手工编辑：

| 字段 | 默认值 | 隐藏原因 | 代码证据 |
| :--- | :--- | :--- | :--- |
| `standalone_pool.mode` | `"pool"` | 仅 `pool` 值被消费，`select` 无运行时消费者 | `StandalonePoolConfigParser.h:30` 仅接受 `pool/select`；未检索到 `select` 分支消费 |
| `standalone_pool.observatory.type` | `"http"` | `ping` 未被 Xray 版本验证 | `StandalonePoolConfigParser.h:48` |
| `standalone_pool.observatory.samplingCount` | 10 | 后端透传，无 UI 编辑 | `StandalonePoolConfigParser.h:58` |
| `standalone_pool.observatory.destination` | `"https://www.google.com"` | 运行时被 `AppController::startProxyPool` 用 `config_.test_url` 覆盖，编辑无效 | `AppController.cpp:1921-1923`；`ConfigDialog.cpp:210-211` 注释「恒被 test.url 覆盖…不暴露 UI」 |
| `standalone_pool.probeUrl` | 空 | 仅在内存由 `startProxyPool` 从 `test_url` 注入，未序列化 | `ConfigReader.h:17-19` |

### 4.3 B. `proxy.scoring_*_weight` 三字段暴露状态

`include/ConfigReader.h:112-115`：

```cpp
// Scoring weights (0.0-1.0, sum not required to equal 1.0)
double scoring_delay_weight = 0.2;
double scoring_stability_weight = 0.3;
double scoring_history_weight = 0.5;
```

消费点仅一处：`src/ui/AppController.cpp:412-413`（`ProxyScorer` 速度评分归一化映射）：

```cpp
config_.proxy.scoring_delay_weight * 5000.0,  // speed min ~0ms
config_.proxy.scoring_delay_weight * 5000.0 + 3000.0); // speed max ~5000ms
```

**结论 B**：3 个评分权重字段在 `ConfigDialog` **未暴露任何 UI 控件**（grep `scoring_` 在 `src/ui/` 仅 AppController.cpp 两处，均非 UI 定义），仅可通过手工编辑 `bin/config.json` 生效。

> 备注：`scoring_stability_weight` / `scoring_history_weight` 未在本会话检索到 `src/` 消费点，属历史遗留（可能与评分计算逻辑分散相关，非本报告范围）。

### 4.4 完整暴露矩阵

| 分组 | 字段 | ConfigDialog | StandaloneFloatingWidget | config.json 手工 |
| :--- | :--- | :---: | :---: | :---: |
| `proxy_process_monitor` | `enabled` | ✅ | — | ✅ |
| `proxy_process_monitor` | `checkIntervalMs` | ✅ | — | ✅ |
| `standalone_pool` | `enabled` | ✅ | — | ✅ |
| `standalone_pool` | `socksPort` | ✅ | — | ✅ |
| `standalone_pool` | `apiPort` | ✅ | — | ✅ |
| `standalone_pool` | `balancerStrategy` | ✅ | — | ✅ |
| `standalone_pool` | `observatory.intervalSec` | ✅ | — | ✅ |
| `standalone_pool` | `observatory.timeoutSec` | ✅ | — | ✅ |
| `standalone_pool` | `observatory.type` | ❌ | — | ✅ |
| `standalone_pool` | `observatory.destination` | ❌ | — | ✅（运行时被 test.url 覆盖） |
| `standalone_pool` | `observatory.samplingCount` | ❌ | — | ✅ |
| `standalone_pool` | `mode` | ❌ | — | ✅ |
| `standalone_pool.evaluate` | `intervalSec` | ✅ | — | ✅ |
| `standalone_pool.evaluate` | `reportHealth` | ✅ | ✅ | ✅ |
| `standalone_pool.evaluate` | `autoPruneDead` | ✅ | ✅ | ✅ |
| `standalone_pool.evaluate` | `pruneFailStreak` | ✅ | — | ✅ |
| `standalone_pool.evaluate` | `autoOptimize` | ✅ | ✅ | ✅ |
| `standalone_pool.evaluate` | `probeWorkers` | ✅ | — | ✅ |
| `proxy` | `scoring_delay_weight` | ❌ | — | ✅ |
| `proxy` | `scoring_stability_weight` | ❌ | — | ✅ |
| `proxy` | `scoring_history_weight` | ❌ | — | ✅ |

**合计**：21 字段中 **15 项在 ConfigDialog 暴露**，**3 项在悬浮窗热切换**（与 ConfigDialog 重叠计入 15），**6 项未暴露（4 项 `standalone_pool` 预留 + 3 项 `proxy.scoring_*`，其中 `observatory.destination` 因被覆盖实际无效）**。

---

## 5. 当前生产 `bin/config.json` 快照（本次核对）

```json
"proxy_process_monitor": {
  "enabled": true,
  "check_interval_ms": 5000
},
"standalone_pool": {
  "enabled": true,
  "mode": "pool",
  "socksPort": 10809,
  "apiPort": 10810,
  "balancerStrategy": "leastPing",
  "observatory": {
    "type": "http",
    "destination": "https://www.google.com",
    "intervalSec": 5,
    "samplingCount": 10,
    "timeoutSec": 5
  },
  "evaluate": {
    "intervalSec": 10,
    "reportHealth": true,
    "autoPruneDead": true,
    "pruneFailStreak": 3,
    "autoOptimize": true,
    "probeWorkers": 2
  }
}
```

`check_interval_ms=5000` 处于夹逼下限（5000 ms），刷新节奏较激进，UI 端每 5 秒一次悬浮球重绘。

---

## 6. 结论与差距

1. **两个开关正交**：`proxy_process_monitor.enabled` 管「悬浮监控面板」，`standalone_pool.enabled` 管「池本体」。当前生产配置两者同时为 `true`，UI 与运行时均完整可用。
2. **`evaluate.*` 6 字段全暴露**：ConfigDialog 全部，悬浮窗暴露 3 个开关类字段。方案甲 4 个预留字段（`mode`/`observatory.type`/`observatory.samplingCount`/`observatory.destination`）保留透传，无 UI 编辑入口。
3. **`proxy.scoring_*_weight` 未暴露**：如用户「独立代理健康度评估」指评分权重（ProxyScorer 三因子），此部分**未在 ConfigDialog 提供控件**，需扩 UI 或继续手工编辑 config.json。若需暴露，建议**独立到新增「独立代理评估」子分类**（归属新增「健康度评估」父分类，与「代理池评估」子分类并列，避免 pool 本体配置与代理评分语义混淆），新增 `proxy_scoring_delay_weight` / `proxy_scoring_stability_weight` / `proxy_scoring_history_weight` 三 `wxDoubleProperty`，并在 `validateConfig` 增加 [0.0, 1.0] 区间校验；详见 §7.3 P2 与 §7.4 方案 B。
4. **文档关联**：
   - `docs/specs/2026-08-26-Spec-StandaloneProxyPool-v1.0.md` — 代理池总体设计与 a/b/c 三开关语义
   - `docs/specs/2026-08-24-Spec-StandaloneFloatingWidget-v1.1.md` — 悬浮窗统一接管版设计（复用 `proxy_process_monitor.enabled`+`checkIntervalMs`）
   - `docs/specs/2026-09-09-Note-PoolMemberHealthEvaluation-v1.0.md` — 池成员健康度评估现状与后续完善清单（P1/P2/P3 缺口）
   - `docs/specs/2026-09-16-Spec-PoolConfigDialogAdjust-v1.0.md` — 本轮 ConfigDialog 方案甲调整（移除 `observatory.destination`、新增 `probeWorkers`）
   - `docs/specs/2026-08-13-Spec-ProxyScoring-History-v1.0.md` — ProxyScorer 三因子评分设计与权重来源
   - `docs/review/2026-09-17-Review-results-of-configuration-item-reengineering.md` — 分类重构评审意见（v1.2 §7.3 层级树来源）
   - `docs/plans/2026-09-17-Plan-HealthMonitorUnify-v1.0.md` — 方案 B 实施计划（评审修订后 v1.1，执行状态见 tracker）

---

## 7. 配置项架构评估与合并方案（v1.1 附录，v1.2 评审修订）

> **触发**：用户在 v1.0 结论基础上追问三问：①`standalone_pool.enabled` 是否有必要；②健康度评估开关能否与 `proxy_process_monitor.enabled` 合并；③配置窗口中代理池、独立代理健康度评估混杂需区分。§7 结构化回答三问，并给出 A/B/C 三档合并方案对比。

### 7.1 `standalone_pool.enabled` 必要性评估

**结论：必要，不建议移除。**

| 维度 | 证据 | 位置 |
| :--- | :--- | :--- |
| 硬门禁 | `if (!config_.standalone_pool.enabled) return false;` — 阻止池构造/端口分配/evaluatorLoop 启动 | `src/ui/AppController.cpp:1881` |
| UI 联动 | 6 池控件（`startStopBtn_/addBtn_/refreshBtn_/reportChk_/pruneChk_/optimizeChk_`）按此字段 `Enable`/`Disable` 灰化 | `src/ui/StandaloneFloatingWidget.cpp:376-382` |
| CLI 独立性 | 无悬浮窗的 CLI-only 场景仍可控池启动（当前 CLI 未消费池，但保留门禁对未来扩展友好） | — |
| 与 `proxy_process_monitor.enabled` 正交性 | 前者=池本体启停（数据面），后者=悬浮窗显隐（展示面），合并将导致"关闭悬浮窗即禁用池"耦合副作用 | `§2.3, §3.2` |

**可选优化**（不推荐移除，仅语义清晰化）：
- 保留 `enabled` 硬门禁不变；
- 悬浮窗内 `启动池` 按钮已承担"手动启停"，配置层 `enabled=false` + 6 控件灰化**功能上略冗余**，但作为"配置声明式启停"入口语义明确，建议保留 UI 灰化传达"配置层禁用"信号。

### 7.2 健康度评估与 `proxy_process_monitor.enabled` 能否合并

**结论：不能合并，语义、数据面、消费者均不同。**

三条独立数据流（含独立代理 silent 探活）：

| 开关 / 数据流 | 语义 | 周期 | 数据写回 | 消费者 |
| :--- | :--- | :--- | :--- | :--- |
| `proxy_process_monitor.enabled` | **展示面**：悬浮窗显隐 | `check_interval_ms`（UI 刷新） | 无（仅读取独立代理/池状态） | `MainFrame.cpp:537-551`、`StandaloneFloatingWidget` |
| `standalone_pool.evaluate.*` 三开关 | **池内数据面**：池成员评估策略 | `evaluate.intervalSec` | ProfileExItem（delay/-1 剔除） | `StandaloneProxyPool::evaluatorLoop` + `ProxyHealthEvaluator` |
| `proxy.scoring_*_weight` 三权重 | **代理评分**：ProxyScorer 三因子 | 同步计算（无周期） | 内存 → UI 显示（Health 列） | `AppController.cpp:412-413` |
| 独立代理 silent 周期探活 | **独立代理数据面**：连通性 + 阈值 | 复用 `proxyMonTimer_` 周期 | ProfileExItem（updateTestResult） | `AppController::onProxyMonTimer`（2026-09-15 落地） |

**合并后果分析**：
- **若合并 `evaluate.*` 与 `proxy_process_monitor.enabled`**：关闭悬浮窗 = 关闭池评估 → 池成员死亡不剔除、健康度不写回 DB、PoolMember 表冻结。违反"数据面/展示面解耦"原则。
- **若合并 silent 探活与 `proxy_process_monitor.enabled`**：现状已经是耦合（silent 探活复用 `proxyMonTimer_`），但这是**刻意复用**——悬浮窗刷新节奏本身就是"独立代理健康度"展示需求；如需解耦，需新增 `independent_probe.enabled` 独立开关（见 §7.4 方案 B）。

**建议**：
- 保持四开关/数据流独立（`proxy_process_monitor.enabled` + `standalone_pool.evaluate.*` + `proxy.scoring_*_weight` + silent 探活）；
- 悬浮窗内 `reportChk_/pruneChk_/optimizeChk_` 作为池评估热切换入口已实现"数据面/展示面解耦"（`StandaloneFloatingWidget.cpp:384-386` 独立回填 `evaluate.*`，不受 `proxy_process_monitor.enabled` 门控）；
- 若需 silent 探活独立控制，走 §7.4 方案 B 新增 `independent_probe.enabled`。

### 7.3 配置窗口分类标识问题（P1-P6）

**当前 ConfigDialog 分类树**（`ConfigDialog.cpp:171-221`）：

```
├── 数据库 (SQL 查询 / 按 SubId 查询)
├── Xray (workers / start_port / api_port)
├── 测试 (URL / timeout_ms)
├── 日志 (enabled / console_level / file_level)
├── 订阅 (accelerator_url / update_methods / priority_subids)
├── 去重 (enabled / blacklist / dedup_subids)
├── 通知
├── 自动任务
├── 代理 (proxy: xray_executable / use_singbox / 3 singbox paths)
├── 监控代理进程 (proxy_process_monitor: enabled / check_interval_ms)
└── 独立代理池 (standalone_pool: 12 项 — 含 pool 本体 6 + evaluate 6)
```

**混杂问题清单**：

| # | 问题 | 影响 | 建议 |
| :---: | :--- | :--- | :--- |
| P1 | 「独立代理池」分类同时混入 **pool 本体配置**（`enabled/mode/socksPort/apiPort/balancerStrategy`）与 **evaluate 评估配置**（`intervalSec/reportHealth/autoPruneDead/pruneFailStreak/autoOptimize/probeWorkers`） | 用户不易区分「启停配置」和「评估策略」 | 拆分为「代理池配置」+「代理池评估」（见下方 v1.2 评审层级树） |
| P2 | 「独立代理」（非池）相关的 **`proxy.scoring_*_weight` 三权重未暴露**（`AppController.cpp:412-413` 消费但 ConfigDialog 无控件） | 用户改不了评分权重，只能手工编辑 JSON | 新增子分类「独立代理评估」（归属新增「健康度评估」父分类）或在「代理」分类下加三项 |
| P3 | 「代理」分类混合了 **xray 执行路径**（`xray_executable/template_config_path/asset_dir`）与 **singbox 执行路径**（`singbox_executable/singbox_asset_dir/singbox_template_config_path`） | 视觉拥挤，`use_singbox` 切换后另一套字段仍可见 | 用条件化可见性（`wxPropertyGrid::SetHidden`）隐藏未启用后端的路径字段 |
| P4 | 「独立代理池」分类**未标注**"pool 本体 vs 评估策略"两组语义 | 用户改评估参数时不知影响范围 | 拆分为「代理池配置」+「代理池评估」（见下方 v1.2 评审层级树） |
| P5 | 「监控代理进程」分类的 `enabled` 与「独立代理池」分类的 `enabled` **同名** | 用户混淆两者语义 | 前者重命名为「启用悬浮窗」，后者「启用代理池」 |
| P6 | 「独立代理池」分类字段数 12 超过单栏舒适展示（ConfigDialog 高度 600 px） | 滚动不便 | 拆分或折叠 |

**建议分类重构方案**（已按 `docs/review/2026-09-17-Review-results-of-configuration-item-reengineering.md` 评审意见修订，由「平铺 13 分类」改为「层级式」）：

```
├── 数据库
├── 监控悬浮窗             ← 重命名（原「监控代理进程」，解决 P5）：proxy_process_monitor.*
├── 健康度评估             ← 新增父分类
│   ├── 独立代理评估       ← 新增子分类（解决 P2）：proxy.scoring_*_weight 三项
│   └── 代理池评估         ← 拆分子分类（解决 P1/P4/P6）：standalone_pool.evaluate.* 六项
├── 代理池配置             ← 拆分（解决 P1/P4/P6）：standalone_pool.enabled/mode/socksPort/apiPort/balancerStrategy + observatory.intervalSec/timeoutSec（归入本分类，见下方注记）
├── 日志
├── Xray 全局 (workers / start_port / api_port)
├── 测试 (URL / timeout_ms)
├── 订阅
├── 去重
├── 通知
├── 自动任务
└── 代理后端选择           ← 新增：仅 use_singbox 切换器 + 后端路径字段（条件化可见，解决 P3）
```

> 归属注记：`observatory.intervalSec/timeoutSec` 评审树未列明，按 `docs/plans/2026-09-17-Plan-HealthMonitorUnify-v1.0.md` 归入「代理池配置」（属 pool 本体运行参数，非评估策略）。

**与上一稿（13 分类平铺）的差异**：

| 维度 | 上一稿（v1.1） | 评审修订（v1.2） |
| :--- | :--- | :--- |
| 结构 | 「独立代理 · 评分」/「独立代理池 · 本体」/「独立代理池 · 评估」三个平铺分类 | 「健康度评估」父分类 + 「独立代理评估」/「代理池评估」两个子分类；「代理池配置」承载 pool 本体 |
| `proxy.scoring_*_weight` 归属 | 「独立代理 · 评分」分类 | 「独立代理评估」子分类（与方案 B 新增 `independent_probe.enabled` 同组，语义=「独立代理健康度」） |
| pool 本体归属 | 「独立代理池 · 本体」分类 | 「代理池配置」分类（`enabled/mode/socksPort/apiPort/balancerStrategy`） |
| 池相关分类位置 | 分散在树末 | 集中放置（监控悬浮窗 → 健康度评估 → 代理池配置），符合「监控 → 评估 → 配置」心智流 |
| 不变项 | — | 「代理后端选择」独立分类（P3）与「监控悬浮窗」重命名（P5）保留 |

**实施前置**：需按 AGENTS.md §6.2 走 `writing-plans` skill 创建 Spec + Plan，再落地代码改动。

### 7.4 架构级合并方案评估（A/B/C 三档）

> **背景**：用户在 §7.2 结论「不能合并」基础上进一步追问「监控悬浮窗与健康度评估（池、独立代理）能否合并」，本节评估三档合并方案。

#### 7.4.1 三层现状扫描

| 层 | 已合并？ | 证据 |
| :--- | :---: | :--- |
| **UI 展示层** | ✅ 已合并 | `docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md`：悬浮窗 `UnifiedMonitorRow` + `MonitorType`（独立/池），Orb 计数=在线代理总数，`StandalonePoolDialog` 废弃，`ID_MENU_OPEN_POOL`/`poolDialog_` 清理 |
| **数据控制层** | ❌ 三独立数据流 | 池评估 `evaluatorLoop`（池线程）+ 独立 silent 探活（`proxyMonTimer_` UI 线程回调）+ ProxyScorer 权重（评分纯函数） |
| **配置层** | ❌ 三分类分散 | 「监控代理进程」+「独立代理池」（含 pool 本体 + evaluate）+ 独立代理评估缺失 |

#### 7.4.2 数据流可合并性分析

| 数据流 | 周期 | 数据写回 | 失败阈值 | 合并可行性 |
| :--- | :--- | :--- | :--- | :--- |
| 池评估 `evaluatorLoop` | `evaluate.intervalSec` (10s) | ProfileExItem（delay=-1） | `pruneFailStreak` (3) | 独立周期独立阈值 |
| 独立 silent 探活 | `proxyMonTimer_` 触发 | ProfileExItem（updateTestResult） | `pruneFailStreak`（复用池） | **与池评估阈值共享**，语义可统一 |
| ProxyScorer 权重 | 无周期（同步评分） | 内存计算 → UI 显示 | 无阈值 | 独立数据流，不可周期合并 |

**核心发现**：silent 探活与池评估的**失败阈值已共享** `pruneFailStreak`（见 `docs/specs/2026-09-15-Spec-StandalonePeriodicProbe-v1.0.md`），说明设计上已视作同类"评估策略"。若做架构级合并，可引入统一评估引擎。

#### 7.4.3 三档合并方案对比

| 方案 | 范围 | 改动量 | 风险 | 建议 |
| :---: | :--- | :--- | :--- | :--- |
| **A. 轻量：配置层聚合** | ConfigDialog 按 v1.2 评审层级树重组分类（「监控悬浮窗」重命名 + 「健康度评估」父分类含「独立代理评估」/「代理池评估」子分类 + 「代理池配置」承载 pool 本体 + 新增 `proxy.scoring_*_weight`） | 单文件 `ConfigDialog.cpp` ~50 行 | 低 | 立即可做 |
| **B. 中等：配置层聚合 + 控制层解耦** | A + 新增 `independent_probe.enabled` 独立开关（从 `proxy_process_monitor.enabled` 解耦 silent 探活）+ 悬浮窗内新增 1 个独立代理评估控件（`probeChk_`） | `ConfigDialog` + `StandaloneFloatingWidget` + `AppController` + `ConfigReader` + `ConfigJsonSerializer`，约 200 行 | 中 | **✅ 推荐** |
| **C. 深度：数据面统一评估引擎** | 引入 `UnifiedHealthEvaluator`，池评估 + silent 探活 + ProxyScorer 全走统一引擎 | 约 800-1200 行新代码 + 重构 6 文件 | 高 | ❌ 收益不匹配 |

#### 7.4.4 方案 B 详细设计

**目标**：将「监控悬浮窗 + 池健康度评估 + 独立代理健康度评估」在**配置层与控制层**统一，**数据面保持独立引擎**。

**改动清单**（5 项）：

1. **`ConfigReader` 新增字段**：
   ```cpp
   struct {
       bool enabled = true;               // 独立代理周期探活开关
       // intervalSec 复用 proxy_process_monitor.checkIntervalMs（不改）
       // threshold 复用 standalone_pool.evaluate.pruneFailStreak（不改）
   } independent_probe;
   ```

2. **`ConfigJsonSerializer` + Parser**：新增 `independent_probe` 段序列化 + 解析分支

3. **`ConfigDialog` 分类重构**（按 `2026-09-17` 评审层级式树重建）：
   ```
   健康度评估 (新增父分类)
     ├─ 独立代理评估  → independent_probe.enabled（新增）+ proxy.scoring_delay_weight / stability_weight / history_weight
     └─ 代理池评估    → standalone_pool.evaluate.* 6 项
   监控悬浮窗 (重命名，原「监控代理进程」)
     └─ proxy_process_monitor.enabled / check_interval_ms
   代理池配置 (拆分，承载 pool 本体)
     └─ standalone_pool.enabled / mode / socksPort / apiPort / balancerStrategy
   ```

4. **`StandaloneFloatingWidget` 新增控件**：
   - 独立代理评估勾选框 `probeChk_`（新增），绑定 `independent_probe.enabled`
   - `applySettings` 增加新字段回填
   - 控件灰化：`probeChk_` **不随池/探活状态灰化**（悬浮窗由 `proxy_process_monitor.enabled` 门控，存在期间可勾选），与池三开关（按 `standalone_pool.enabled` 灰化）正交。*(注：原稿「灰化条件 `poolEnabled || independentProbeEnabled`」会导致开关在关闭时自锁无法再开启，已按 Plan v1.1 评审修订为不灰化。)*

5. **`AppController`**：silent 探活触发条件从 `proxy_process_monitor.enabled` 改为 `independent_probe.enabled`

#### 7.4.5 收益与代价

**收益**：
- 用户可从配置层单独控制 silent 探活，不再受悬浮窗启停影响
- 配置分类语义清晰（「监控悬浮窗」+「健康度评估」父分类含「独立代理评估」/「代理池评估」子分类 +「代理池配置」，见 v1.2 评审层级树）
- 悬浮窗内控件完整（池 3 开关 + 独立代理 1 开关）

**代价**：
- 新增 1 个配置字段（`independent_probe.enabled`）+ 1 个 UI 控件
- 与 §7.2 结论「四开关/数据流独立」保持一致，是**渐进式扩展**而非重构

#### 7.4.6 关键决策点

4 项待用户决策（本轮 v1.1 评估未落地代码，仅列决策清单）：

| # | 决策 | 选项 |
| :---: | :--- | :--- |
| D1 | 方案档位 | A（轻量）/ B（推荐）/ C（深度） |
| D2 | silent 探活独立开关 | 是否引入 `independent_probe.enabled` 字段 |
| D3 | ProxyScorer 权重暴露 | 是否同步新增「独立代理评估」子分类（3 项 `wxDoubleProperty` + [0.0,1.0] 校验） |
| D4 | 实施路径 | 仅评估 → v1.1 报告（本次执行）；落地 → 独立 Spec + Plan |

#### 7.4.7 实施路径

- **仅评估**：v1.1 报告追加 §7.4（本次已执行）—— 提供决策依据，不落地代码。
- **落地方案 B**：需独立生成 Spec `docs/specs/2026-09-16-Spec-HealthMonitorUnify-v1.0.md` + 对应 Plan，按 AGENTS.md §6.2 走 `writing-plans` skill 生成，经用户审批后进入实施阶段（含 TDD：先 `tests/` 目录 Google Test 用例，再实现代码）。
- **不落地**：v1.1 报告作为长期决策依据，未来若需重构可参考 §7.4.4 改动清单。

---

## 附录：证据索引

- `proxy_process_monitor` 消费点：`src/ui/MainFrame.cpp:537-551, 1104-1115, 1183-1200`；`src/ui/StandaloneFloatingWidget.cpp:278-286, 345-403, 372-386`
- `standalone_pool.enabled` 硬门禁：`src/ui/AppController.cpp:1876-1881`
- ConfigDialog 属性定义：`src/ui.ConfigDialog.cpp:188-221`（监控代理进程 + 独立代理池分类）
- Parser：`include/config/sections/ProxyProcessMonitorConfigParser.h:15-42`；`include/config/sections/StandalonePoolConfigParser.h:16-92`
- 结构定义：`include/ConfigReader.h:10-35`（`StandalonePoolConfig`）、`include/ConfigReader.h:96-116`（`proxy_process_monitor` + `proxy`）
- ProxyScorer 权重消费：`src/ui/AppController.cpp:412-413`

### v1.1 评估章节依据

- `src/ui/ConfigDialog.cpp:171-221`（当前 ConfigDialog 分类树，§7.3 P1-P6 依据）
- `src/ui/StandaloneFloatingWidget.cpp:372-386`（`poolEnabled` 6 控件灰化 + `evaluate.*` 独立回填，§7.1 / §7.2 / §7.4.4 依据）
- `src/ui/AppController.cpp:1876-1881`（池硬门禁，§7.1 依据）
- `src/ui/AppController.cpp:412-413`（ProxyScorer 权重消费，§7.2 / §7.3 P2 依据）
- 架构评估参考：
  - `docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md`（悬浮窗已成统一展示入口，§7.4.1 UI 展示层已合并依据）
  - `docs/specs/2026-09-15-Spec-StandalonePeriodicProbe-v1.0.md`（silent 探活复用 `pruneFailStreak` 阈值，§7.4.2 语义可统一依据）
  - `docs/specs/2026-09-16-Spec-PoolConfigDialogAdjust-v1.0.md`（本轮 ConfigDialog 方案甲调整，§7.3 P1-P6 现状依据）

### v1.2 评审修订依据

- `docs/review/2026-09-17-Review-results-of-configuration-item-reengineering.md`（评审意见：§7.3 分类重构方案由「平铺 13 分类」修订为「层级式」——「健康度评估」父分类 + 「独立代理评估」/「代理池评估」子分类 + 「代理池配置」+ 「监控悬浮窗」重命名 + 「代理后端选择」独立分类）
