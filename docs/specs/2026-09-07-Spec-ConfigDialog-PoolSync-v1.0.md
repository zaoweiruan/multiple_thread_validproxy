---
title: "Spec: ConfigDialog 配置项与 config.json 同步 — 独立代理池 12 项 + singbox_asset_dir + 沙盒键名修正 + 移除网络错误日志项（v1.0）"
module: ConfigDialog
status: 待评审
date: 2026-09-07
supersedes: （无 — 新增规格）
---

# 规格说明：配置编辑窗口配置项与 config.json 同步（v1.0）

> 基于 2026-09-07 完成的《配置编辑窗口与 config.json 同步调研报告》产出本规格，供评审。
> 核心差距：`standalone_pool` 整节（15 字段，12 个需 UI 暴露）在 ConfigDialog 中完全缺失，用户只能手工编辑 JSON；
> `proxy.singbox_asset_dir` 有结构体字段、有序列化、有实际值，但无 UI 入口；
> `scripts/prepare-ui-sandbox.bat` 生成的沙盒配置中 `standalone_pool` 仍使用解析器不识别的旧键名。
> 附录 A（用户追加）：移除「网络错误日志」配置项，改由日志级别控制输出。

## 1. 目标

| 目标 | 说明 | 验收标准 |
| --- | --- | --- |
| ① 独立代理池配置可视化 | ConfigDialog 新增「独立代理池」分类，覆盖 `standalone_pool` 全部 12 个 UI 暴露字段（3 个预留字段按方案甲隐藏，往返由后端透传） | 打开设置对话框可见 12 项属性，值与 bin/config.json 当前内容一致 |
| ② 双向读写闭环 | loadConfig 载入 → 用户编辑 → saveConfig 回写 → 序列化落盘 → 重新打开对话框值保持 | 修改任一属性并确定后，重开对话框数值不回退，config.json 中对应键正确更新 |
| ③ 校验防错 | 新增属性纳入既有 validateConfig() 体系 | 端口越界（如 80）、间隔为 0/负数、剔除阈值 <1、非法 URL 均被拦截并提示 |
| ④ singbox 资源目录补齐 | 代理分类新增 `proxy_singbox_asset_dir` 文件属性 | 对话框可编辑该值，与 config.json `proxy.singbox_asset_dir` 双向一致 |
| ⑤ 沙盒配置键名修正 | prepare-ui-sandbox.bat 改用解析器真实键名 | 重新执行脚本后 test/ui-sandbox/config.json 的 standalone_pool 全部键可被 parser 读到 |

## 2. 现状与差距（调研结论摘要）

### 2.1 同步差距矩阵

| # | 差距 | 现状 | 定级 |
| --- | --- | --- | --- |
| ① | `standalone_pool` 整节缺失 | ConfigDialog 12 个分类约 45 项属性中无任何池配置项 | **主要**（本次核心） |
| ② | `proxy.singbox_asset_dir` 缺失 | ConfigDialog.cpp:63 已有 xray 资源目录（wxFileProperty），singbox 侧无对应项 | 次要 |
| ③ | `subscription.priority_subids` 缺失 | 序列化于 ConfigJsonSerializer.cpp:52-58，订阅置顶逻辑消费于 MainFrame.cpp:344-346 | 可选（Phase 2） |
| ④ | `auto_task.state_file` 缺失 | 序列化为可选键，无 UI | 可选（Phase 2） |

### 2.2 已排除的疑点（无需改动）

- **ERROR/ERR 枚举一致性**：`LoggerInstance::stringToLevel`（src/LoggerInstance.cpp:234-252）大小写不敏感且接受 "error" → `LogLevel::ERR`；`levelToString(ERR)` 返回 "ERROR"（LoggerInstance.cpp:225-233）。ConfigDialog 与 LogPanel 使用 "ERROR" 与输出侧自洽，非 bug。
- **生产 bin/config.json 键名**：应用 2026-09-07 10:25 重写后已自愈为解析器真实键名（enabled/mode/socksPort/apiPort/balancerStrategy/observatory{...}/evaluate{...}），无需再修。
- **解析器编排**：ConfigReader.cpp:17-29 已挂接全部 13 个 section parser，无遗漏。

### 2.3 字段生效状态（本次调研新确认，评审重点）

15 个池字段按运行时消费情况分三档（其中 12 个设 UI 属性，3 个预留字段按方案甲 UI 隐藏）：

| 档位 | 字段 | 消费位置 |
| --- | --- | --- |
| **生效** | enabled | AppController::startProxyPool 前置开关（AppController.cpp:1763） |
| **生效** | socksPort / apiPort | 仅作期望端口，实际由 PortManager 分配（AppController.cpp:1794-1807） |
| **生效** | balancerStrategy | 写入 xray `routing.balancers[].strategy.type`（ConfigGenerator.cpp:199） |
| **生效** | observatory.destination | test.url 为空时的探测 URL 兜底（ConfigGenerator.cpp:182、StandaloneProxyPool.cpp:236） |
| **生效** | observatory.intervalSec | xray probeInterval（ConfigGenerator.cpp:183/210） |
| **生效** | observatory.timeoutSec | evaluator 探测总超时，≤0 时回退 10s（StandaloneProxyPool.cpp:237-238） |
| **生效** | evaluate.intervalSec | evaluator 轮询周期（StandaloneProxyPool.cpp:264） |
| **生效** | evaluate.reportHealth / autoPruneDead / pruneFailStreak / autoOptimize | evaluatorLoop 策略开关（StandaloneProxyPool.cpp:287-295；autoOptimize 当前为记录式占位） |
| **预留** | mode | 解析+序列化，全代码库无消费方（仅 AppController.cpp:1806 处 probeUrl 覆盖涉及 test_url） |
| **预留** | observatory.type | 解析+序列化，无消费方（buildPoolConfig 不区分 http/ping） |
| **预留** | observatory.samplingCount | 解析+序列化，无消费方 |

> 预留三字段按**方案甲**处理：UI 隐藏（不设属性），后端往返原值透传，见 §4.1「预留字段处理」。

> **不在 UI 暴露的字段**：`probeUrl` 为运行时派生值——`startProxyPool` 每次以 `config_.test_url` 覆盖（AppController.cpp:1806），不持久化，不属于 config.json schema，**不设 UI 属性**。

## 3. 变更范围

| 文件 | 变更 |
| --- | --- |
| `src/ui/ConfigDialog.cpp` | ① 新增「独立代理池」分类（12 项属性，见 4.1）；② 代理分类新增 `proxy_singbox_asset_dir`（wxFileProperty，样式对齐 proxy_xray_asset_dir，ConfigDialog.cpp:63-67）；③ `loadConfig()` 增加池字段回填；④ `saveConfig()` 增加池字段读回 + singbox_asset_dir 读回；⑤ `validateConfig()` 增加校验规则（见 4.4） |
| `scripts/prepare-ui-sandbox.bat` | standalone_pool 键名由 `selection/min_members/max_members/eval_interval_ms/health_check` 修正为解析器真实键（见 4.6） |
| `tests/` | 无既有 ConfigDialog 单测（grep 确认）；验证依赖 UI 套件回归 + 手工验收（见 §7） |
| （Phase 2，可选）`src/ui/MainFrame.cpp` | onMenuConfig 增加池参数热应用分支（见 4.7） |
| （Phase 2，可选）`src/ui/ConfigDialog.cpp` | 新增 priority_subids / state_file 属性或只读说明 |

## 4. 设计要点

### 4.1 独立代理池分类（12 项属性）

分类置于「监控代理进程」分类之后（对话框底部追加，不扰动既有分类顺序与肌肉记忆）。属性清单：

| # | 属性 Key | 控件 | 中文标签 | config.json 键路径 | 默认值 | 生效状态标注 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `pool_enabled` | wxBoolProperty | 启用独立代理池 | standalone_pool.enabled | false | 生效 |
| 2 | `pool_socks_port` | wxIntProperty | Socks 端口（期望值） | standalone_pool.socksPort | 10809 | 生效* |
| 3 | `pool_api_port` | wxIntProperty | API 端口（期望值） | standalone_pool.apiPort | 10810 | 生效* |
| 4 | `pool_balancer_strategy` | wxEnumProperty | 均衡策略 | standalone_pool.balancerStrategy | leastPing | 生效 |
| 5 | `pool_obs_destination` | wxStringProperty | 探测 URL（test.url 为空时生效） | standalone_pool.observatory.destination | https://www.google.com | 生效 |
| 6 | `pool_obs_interval` | wxIntProperty | 探测间隔（秒） | standalone_pool.observatory.intervalSec | 5 | 生效 |
| 7 | `pool_obs_timeout` | wxIntProperty | 探测超时（秒） | standalone_pool.observatory.timeoutSec | 5 | 生效 |
| 8 | `pool_eval_interval` | wxIntProperty | 评估轮询间隔（秒） | standalone_pool.evaluate.intervalSec | 10 | 生效 |
| 9 | `pool_eval_report_health` | wxBoolProperty | 报告健康状态（日志） | standalone_pool.evaluate.reportHealth | true | 生效 |
| 10 | `pool_eval_auto_prune` | wxBoolProperty | 连败自动剔除 | standalone_pool.evaluate.autoPruneDead | false | 生效 |
| 11 | `pool_eval_prune_streak` | wxIntProperty | 剔除阈值（连续失败次数） | standalone_pool.evaluate.pruneFailStreak | 3 | 生效 |
| 12 | `pool_eval_auto_optimize` | wxBoolProperty | 自动优选（当前仅记录） | standalone_pool.evaluate.autoOptimize | false | 生效（占位） |

\* 端口仅为期望值：`resolvePoolPorts` 经 PortManager 实际分配（AppController.cpp:1794 注释「desired hints」）。已知既有行为：启动重试时基准回退为硬编码 10809（AppController.cpp:1807 `poolCfg.socksPort = 10809 + attempt`），本规格不改动该行为，仅在评审中记录为后续独立改进项。

**预留字段处理**（方案甲，2026-09-07 用户采纳）：三预留字段（mode / obs.type / samplingCount）经 grep 实证零运行时消费方，**UI 隐藏**——不设属性即无「预留」误导，与附录 A 同一原则（UI 项必须与实际效果一致）。后端 struct / parser / serializer 零改动：`loadConfig()` 以 `editedConfig_ = cfg` 起底，保存时不读回这三项，手工改 JSON 的 mode / type / samplingCount 值在「打开→保存」后**原样落盘**，不会被 UI 改回默认。mode UI 待 select 模式实现时再暴露。

### 4.2 属性创建代码要点

```cpp
// ── 独立代理池 ──────────────────────────────────────────────
propGrid_->Append(new wxPropertyCategory(L"独立代理池"));

propGrid_->Append(new wxBoolProperty(L"启用独立代理池", "pool_enabled",
                                     cfg.standalone_pool.enabled));

wxPGChoices modeChoices;                       // 预留字段：值仍随保存往返
propGrid_->Append(new wxIntProperty(L"Socks 端口（期望值）", "pool_socks_port",
                                    cfg.standalone_pool.socksPort));
propGrid_->Append(new wxIntProperty(L"API 端口（期望值）", "pool_api_port",
                                    cfg.standalone_pool.apiPort));

wxPGChoices strategyChoices;                   // 与 StandalonePoolConfigParser.h:42 白名单一致
strategyChoices.Add(L"random");
strategyChoices.Add(L"leastPing");
strategyChoices.Add(L"leastLoad");
propGrid_->Append(new wxEnumProperty(L"均衡策略", "pool_balancer_strategy",
                                     strategyChoices,
                                     /*按当前值定位索引*/ ...));

// observatory.* / evaluate.* 依表逐项 Append，样式同上（Int 用 wxIntProperty，
// Bool 用 wxBoolProperty，destination 用 wxStringProperty）
```

singbox 资源目录（对齐既有 xray 资源目录写法，ConfigDialog.cpp:63-67）：

```cpp
wxFileProperty* singboxAssetProp =
    new wxFileProperty(L"singbox 资源目录", "proxy_singbox_asset_dir",
                       cfg.proxy.singbox_asset_dir);
propGrid_->Append(singboxAssetProp);
propGrid_->SetPropertyAttribute("proxy_singbox_asset_dir", wxPG_FILE_SHOW_FULL_PATH, (long)1);
propGrid_->SetPropertyAttribute("proxy_singbox_asset_dir", wxPG_DIALOG_TITLE, L"选择 singbox 资源目录");
```

### 4.3 loadConfig / saveConfig 映射

`loadConfig()`（12 项回填，Int/Bool/Enum 按控件类型取值）：

```cpp
propGrid_->SetPropertyValue("pool_enabled", cfg.standalone_pool.enabled);
propGrid_->SetPropertyValue("pool_socks_port", cfg.standalone_pool.socksPort);
propGrid_->SetPropertyValue("pool_obs_destination",
                            wxString(cfg.standalone_pool.observatory.destination));
// ...其余 9 项同型
```

`saveConfig()`（读回 editedConfig_，Enum 取字符串后按解析器白名单回写）：

```cpp
editedConfig_.standalone_pool.enabled =
    propGrid_->GetPropertyValueAsBool("pool_enabled");
editedConfig_.standalone_pool.socksPort =
    static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_socks_port"));
editedConfig_.standalone_pool.apiPort =
    static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_api_port"));
editedConfig_.standalone_pool.balancerStrategy =
    propGrid_->GetPropertyValueAsString("pool_balancer_strategy").ToStdString();
editedConfig_.standalone_pool.observatory.destination =
    propGrid_->GetPropertyValueAsString("pool_obs_destination").ToStdString();
editedConfig_.standalone_pool.observatory.intervalSec =
    static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_obs_interval"));
editedConfig_.standalone_pool.observatory.timeoutSec =
    static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_obs_timeout"));
editedConfig_.standalone_pool.evaluate.intervalSec =
    static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_eval_interval"));
editedConfig_.standalone_pool.evaluate.reportHealth =
    propGrid_->GetPropertyValueAsBool("pool_eval_report_health");
editedConfig_.standalone_pool.evaluate.autoPruneDead =
    propGrid_->GetPropertyValueAsBool("pool_eval_auto_prune");
editedConfig_.standalone_pool.evaluate.pruneFailStreak =
    static_cast<int>(propGrid_->GetPropertyValueAsInt("pool_eval_prune_streak"));
editedConfig_.standalone_pool.evaluate.autoOptimize =
    propGrid_->GetPropertyValueAsBool("pool_eval_auto_optimize");
editedConfig_.proxy.singbox_asset_dir =
    propGrid_->GetPropertyValueAsString("proxy_singbox_asset_dir").ToStdString();
```

> 保存路径闭环已具备：`onMenuConfig` → `controller_->saveConfig(cfg)` → `ConfigJsonSerializer::serialize`（standalone_pool 全部 15 键已实现，ConfigJsonSerializer.cpp:163-170 区域；mode/type/samplingCount 由 `loadConfig()` 起底的 `editedConfig_ = cfg` 原值透传，不经 UI 读回）→ 落盘。**序列化器无需改动**。

### 4.4 validateConfig() 新增校验规则

沿用既有 validateConfig 错误提示风格（ConfigDialog.cpp:430-520）：

| 属性 | 规则 | 违规提示示例 |
| --- | --- | --- |
| pool_socks_port / pool_api_port | 1024–65535（对齐 xray_start_port 既有规则） | 「独立代理池端口必须在 1024-65535 之间」 |
| pool_obs_interval | 1–300 秒 | 「探测间隔必须在 1-300 秒之间」 |
| pool_obs_timeout | 1–60 秒 | 「探测超时必须在 1-60 秒之间」 |
| pool_obs_destination | 非空时必须通过 `utils::isValidUrlFormat`；**不静默清空**（与 accelerator_url 的清空策略不同，因 destination 是 test.url 为空时的唯一兜底） | 「探测 URL 格式无效」 |
| pool_eval_interval | 1–600 秒 | 「评估间隔必须在 1-600 秒之间」 |
| pool_eval_prune_streak | ≥1 | 「剔除阈值必须 ≥1」 |
| pool_obs_destination 与 test_url 关系 | 二者均为空时警告（探测将无 URL 可用，buildPoolConfig 会生成空 probeURL） | 警告不阻断：「探测 URL 与测试 URL 均为空，代理池健康探测将不可用」 |

### 4.5 属性顺序与枚举白名单一致性

- UI 仅暴露 balancerStrategy 枚举，可取值必须与 `StandalonePoolConfigParser.h` 白名单完全一致：balancerStrategy ∈ {random, leastPing, leastLoad}。mode / observatory.type 不再设 UI（方案甲），其取值白名单由后端 parser 维护。序列化侧（ConfigJsonSerializer.cpp:163-170）原样回写，无二次过滤，UI 是 balancerStrategy 的唯一防线。

### 4.6 沙盒脚本键名修正（prepare-ui-sandbox.bat）

当前错误键名（仅 enabled 与 evaluate 三布尔可被解析器读到）：

```bat
echo   "standalone_pool": {
echo     "enabled": true, "selection": "high_priority", "auto_start": false,
echo     "min_members": 1, "max_members": 8, "eval_interval_ms": 5000,
echo     "health_check": { "enabled": true, "timeout_ms": 5000, "url": "..." },
echo     "evaluate": { "reportHealth": false, "autoPruneDead": false, "autoOptimize": false }
```

修正为解析器真实键名（保持沙盒语义：池可用、评估关、探测走 google generate_204）：

```bat
echo   "standalone_pool": {
echo     "enabled": true, "mode": "pool", "socksPort": 11009, "apiPort": 11010,
echo     "balancerStrategy": "leastPing",
echo     "observatory": { "type": "http", "destination": "https://www.google.com/generate_204",
echo       "intervalSec": 5, "samplingCount": 10, "timeoutSec": 5 },
echo     "evaluate": { "intervalSec": 10, "reportHealth": false, "autoPruneDead": false,
echo       "pruneFailStreak": 3, "autoOptimize": false }
echo   },
```

> 注：沙盒 socksPort 从 10809 挪至 11009，与既有「xray start_port 11080」错开段一致，降低与生产池/独立代理的期望端口重叠（实际仍由 PortManager 兜底）。此为建议值，评审可裁。

### 4.7 热应用策略（Phase 2，可选，本次不实现）

现状：onMenuConfig（MainFrame.cpp:1122-1249）对 network_monitor / ppm / 日志级别 / 数据库路径 / 悬浮窗均有热应用，**standalone_pool 无任何热应用**——全部参数在下次池启动时才生效（隐式契约，用户无感知）。

Phase 2 建议（按风险递增排序）：

1. **evaluate 三开关热应用（零风险）**：AppController 已有 `setPoolReportHealth / setPoolAutoPruneDead / setPoolAutoOptimize`（AppController.cpp:1869-1904），onMenuConfig 中检测到三开关变化且池在运行时直接调用，无需重启。
2. **探测/间隔类参数（中风险）**：observatory.* 与 evaluate.intervalSec 涉及 xray 运行配置与 evaluator 循环，需 stop→start 重启池。**已确认风险**：`StandaloneProxyPool::reinjectAll()` 依赖 `members_` 内存，stop 销毁池对象后成员列表丢失——重启需先快照成员再重注入，或明确提示「重启代理池将清空当前成员」。评审决策点 B：是否接受清空提示方案。
3. **端口/策略/开关（enabled）**：同 2 的重启语义，仅当 1/2 落地后再评估。

### 4.8 不变式

- 退出路径、托盘行为、`~MainFrame` fix#5（UnInit+delete only）零接触。
- ConfigJsonSerializer / 全部 section parser 零改动（它们已是正确基准，本次是 UI 向 schema 对齐）。
- 既有 45 项属性的行为与顺序零改动；新分类纯追加。
- C++17 且**禁用 auto 类型推导**（AGENTS.md 核心约束）。

## 5. 分期建议

| 阶段 | 内容 | 规模 |
| --- | --- | --- |
| **Phase 1（本规格验收范围）** | 4.1–4.6：12 项池属性 + singbox_asset_dir + 沙盒键名修正 | ConfigDialog.cpp 约 +120 行，prepare-ui-sandbox.bat 约 ±8 行 |
| **Phase 2（独立评审后实施）** | 4.7 热应用（先做 evaluate 三开关零风险项）+ priority_subids / state_file 属性 | 另立规格 |

## 6. 风险与备选方案（评审决策点）

| # | 风险/争议点 | 推荐方案 | 备选 |
| --- | --- | --- | --- |
| R1 | 新分类 12 项使对话框变长 | wxPropertyCategory 自带折叠能力，默认展开；追加在底部不扰动既有布局 | 拆独立「代理池设置」对话框（否决：背离"同步进 config 编辑器"的任务目标） |
| R2 | 预留字段（mode/type/samplingCount）暴露后用户误以为立即生效 | **已决议（方案甲，2026-09-07 用户采纳）**：隐藏三项，UI 仅 12 属性；往返由后端保留原值（见 4.1） | 已否决：暴露+「预留」标注（制造 UI↔效果错位，与附录 A 同一原则冲突）；已否决：物理删除全链（~20 处测试改动，丢失 parser 往返覆盖） |
| R3 | observatory.destination 在 test.url 非空时不生效（被 AppController.cpp:1806 覆盖）易困惑 | 标签注明「test.url 为空时生效」+ 4.4 的双空警告 | 去掉覆盖逻辑改用 destination 优先（否决：会改变既有探测行为，超出本任务范围） |
| R4 | 端口重试回退硬编码 10809（AppController.cpp:1807）与用户配置不一致 | 本规格不动，记录为独立后续改进项 | 顺手修复（否决：行为变更需单独评审） |
| R5 | 沙盒 socksPort 改 11009 是否必要 | 建议采纳（段位与沙盒 xray start_port 11080 一致） | 保持 10809（与生产默认相同，靠 PortManager 兜底也安全） |
| R6 | 无 ConfigDialog 单测，验证依赖手工 | UI 套件回归 + §7 手工清单兜底 | 为 ConfigDialog 新增 GTest（Mock wx 较重，成本高，建议不做；若做仅测 validateConfig 纯逻辑段） |

## 7. 验证

1. **构建**：`cmake --build build --parallel 8` → 0 error。
2. **加载一致性**：启动 `bin\validproxy.exe` → 打开设置 → 「独立代理池」12 项与 bin/config.json 当前值逐一对照（enabled=true、leastPing、10809/10810、5/10/5、10/false/false/3/false）。
3. **往返闭环**：将均衡策略改为 `leastLoad`、评估间隔改为 15、勾选连败剔除 → 确定 → 重开对话框确认三项保持；检查 config.json 中 `balancerStrategy:"leastLoad"`、`evaluate.intervalSec:15`、`autoPruneDead:true`。
4. **校验拦截**：socksPort 输入 80 → 确定 → 报「端口必须在 1024-65535 之间」；探测 URL 输入 `www.baidu.com`（无 scheme）→ 报格式无效。
5. **singbox_asset_dir**：修改值 → 确定 → config.json `proxy.singbox_asset_dir` 更新；重开对话框回显。
5a. **隐藏字段往返（方案甲）**：手工在 config.json 将 `observatory.samplingCount` 设为 7（或 `mode` 改为 `"select"`）→ 打开配置窗口 → 确定 → 保存后值**原样保留**（不被 UI 改回默认），验证 UI 隐藏不破坏后端往返。
6. **沙盒修正**：执行 `scripts\prepare-ui-sandbox.bat` → 检查 test/ui-sandbox/config.json 含 mode/socksPort/apiPort/balancerStrategy/observatory 五键/evaluate 五键 → `ctest -R "^UI_"`（build/ 目录）全绿。
7. **回归**：UI_EXITASSERT 等退出路径不受影响（ConfigDialog 改动不触析构路径）；全量 `ctest` 44 项基线不回退。

## 8. 引用来源

- `src/ui/ConfigDialog.cpp`（12 分类 45 属性清单、saveConfig/validateConfig 既有模式，:63-67/:80-300/:430-520）
- `src/ui/ConfigDialog.h`（40 行，propGrid_ 结构）
- `include/ConfigReader.h:11-46`（StandalonePoolConfig 结构体与默认值）、`:48-116`（AppConfig）
- `include/config/sections/StandalonePoolConfigParser.h:26-89`（真实键名白名单）
- `src/config/ConfigJsonSerializer.cpp:5-184`（写侧 schema，standalone_pool :163-170）
- `src/ConfigReader.cpp:17-29, 78-81`（13 parser 编排）
- `src/ui/MainFrame.cpp:1122-1249`（onMenuConfig 热应用现状）、`:344-346`（priority_subids 消费）
- `src/ui/AppController.cpp:1763-1904`（startProxyPool 端口语义 :1794-1807、probeUrl 覆盖 :1806、setPool 三开关 :1869-1904）
- `src/StandaloneProxyPool.cpp:236-238`（destination 兜底、timeoutSec 消费）、`:264-328`（evaluatorLoop）
- `src/ConfigGenerator.cpp:182-210`（probeUrl/intervalSec 消费点、balancer 策略写入）
- `src/LoggerInstance.cpp:225-252`（ERROR/ERR 闭环验证）
- `scripts/prepare-ui-sandbox.bat:25-31`（待修正键名）
- `bin/config.json`（3636B，2026-09-07 10:25 应用重写后的正确基准）

---

## 附录 A：移除「网络错误日志」配置项，改由日志级别控制输出（并入评审）

> 用户于 2026-09-07 追加决策：「能否去除此配置，改为日志级别控制输出？」——调研确认可行，作为本规格附录一并评审实施。
> 与主文 §4.1「日志分类保留 console_level + file_level 两项」自然衔接：移除后日志分类恰剩 2 项。

### A.1 背景与根因（调研结论）

`log.network_failures`（UI 标签「网络错误日志」）存在**三方语义错位**：

| 视角 | 认知 | 实际 |
| --- | --- | --- |
| UI 标签 | 控制网络错误日志的输出 | 全代码库仅 3 处消费，且 gate 的是**批量测试启动进度行**（非网络错误） |
| 运行时代码 | — | ProxyBatchTester.cpp:523/556/566 三处 `if (config_.log_network_failures)` 仅包住 `Started N xray instances` / `Testing N proxies from subscription` 两条 INFO 级启动概况行 |
| AGENTS.md §4.2 | 「网络失败日志从 INFO 降级为 TRACE」 | 无任何降级逻辑；开关关闭时 3 行被整体抑制 |

真正的每代理网络结果日志（ProxyBatchTester.cpp:301 `OK 123ms` / :306 `FAIL errorMsg`）**从不受此开关控制**，已经由 `file_level` 纯级别控制。被 gate 的 3 行高度冗余：`Started N xray instances` 与 XrayManager.cpp:107/:169 的无条件 REPORT 行重复；`Testing N from subscription` 仅 subId 细节独有（相邻 :555 有无条件 REPORT `Testing N proxies total`）。

### A.2 方案（Option A：降级保留，推荐）

移除配置项全链路，3 条启动概况行降为 `LogLevel::DEBUG`：

- 默认级别（console INFO / file ERROR）→ 3 行自动静默，等效开关关闭；
- file_level 调至 DEBUG → 3 行可见，等效开关开启；
- 这正是「改为日志级别控制输出」——且为逐行粒度，非全模块压制。

备选 Option B（物理删除 3 行）被否决：丢失 subId 细节，且 XrayManager REPORT 行风格不完全等价。

### A.3 变更清单（6 文件 + 1 文档行 + 3 测试文件）

| 文件 | 变更 |
| --- | --- |
| `src/ui/ConfigDialog.cpp` | 删 :101 属性创建 + :287 saveConfig 读回（日志分类剩 console_level/file_level 两项） |
| `include/ConfigReader.h` | 删 `AppConfig.log_network_failures` 字段声明 |
| `src/config/ConfigJsonSerializer.cpp` | 删 :31 `logObj["network_failures"]` 写出 |
| `include/config/sections/LogConfigParser.h` | 删 network_failures 解析块（该键自此成为解析器静默忽略的未知键） |
| `src/ProxyBatchTester.cpp` | 删 :523/:556/:566 三处 if-gate；行内容保留、级别 `INFO` → `DEBUG` |
| `AGENTS.md` §4.2 | 删除「log.network_failures」说明行 |
| `tests/test_config_reader.cpp` | :40 / :90 字段赋值删除（字段移除后编译失败）；:139 `EXPECT_EQ(loaded->log_network_failures, ...)` 删除 |
| `tests/test_config_reader_load.cpp` | :391 fixture + :399 `EXPECT_TRUE(result->log_network_failures)` 断言删除（该用例改为仅验证含未知键的 log 段可正常加载，即向后兼容用例）；:572/:621 round-trip 赋值与断言删除；:109 `full.json` 中 `"network_failures": false` **保留**（充当陈旧键静默忽略的回归验证） |
| `scripts/` | grep 确认零引用，无需改动 |

### A.4 向后兼容

- 旧 config.json 中的 `log.network_failures` 键：parser 静默忽略（LogConfigParser 未知键不报错）；应用下次经配置窗口保存后该键从文件消失（与 standalone_pool 错键自愈同模式），零迁移。
- 测试侧刻意保留一处含陈旧键的 fixture（test_config_reader_load.cpp:109），锁定「未知键不致加载失败」的行为。

### A.5 验证（并入 §7 主清单）

1. `cmake --build build --parallel 8` → 0 error（含测试目标重编译）。
2. `ctest`（build/ 目录）→ ConfigReaderTest / ConfigReaderLoadTest 全绿（删除断言后无失败项）。
3. 配置窗口「日志」分类仅剩 2 项（控制台级别/文件级别）。
4. 默认级别跑批量测试：3 条启动概况行不出现在日志文件；file_level 改 DEBUG 后重现。
5. 手工在 config.json 添加 `"network_failures": true`（log 段）→ 启动加载无警告无报错；经配置窗口保存后该键消失。

### A.6 引用来源

- ProxyBatchTester.cpp :301/:306（真实网络结果行，无 gate）、:523/:556/:566（gate 点）
- XrayManager.cpp :107/:169（REPORT 级等价信息）
- ConfigDialog.cpp :101/:287；ConfigJsonSerializer.cpp :31；LogConfigParser.h；ConfigReader.h
- tests/test_config_reader.cpp :40/:90/:139；tests/test_config_reader_load.cpp :109/:391/:399/:572/:621
- AGENTS.md §4.2（待删文档行）
