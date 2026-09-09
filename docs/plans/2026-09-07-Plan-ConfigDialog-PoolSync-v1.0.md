# ConfigDialog 配置项与 config.json 同步 + 移除网络错误日志项 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `standalone_pool` 12 个生效字段 + `proxy.singbox_asset_dir` 补入配置编辑对话框，修正沙盒脚本键名，并按附录 A 移除 `log.network_failures` 配置项（改由日志级别控制输出）。

**Architecture:** 全部为 UI 层与配置层的增量同步——ConfigDialog（wxPropertyGrid）新增 1 个分类 12 个属性 + 1 个 File 属性；后端 struct/parser/serializer **零改动**（方案甲：预留字段 mode/type/samplingCount 仅 UI 隐藏，后端保留往返能力）；附录 A 涉及 7 个文件的删除式修改（含 2 个测试文件）；沙盒脚本键名对齐解析器真实键。验证 = 构建 + ctest 全量 + UI 套件 + 手工清单。

**Tech Stack:** C++17（项目代码禁用 `auto` 类型推导）、wxWidgets 3.3.3（wxPropertyGrid）、boost.json、CMake + Ninja、Google Test / Catch2、PowerShell。

**关联规格:** `docs/specs/2026-09-07-Spec-ConfigDialog-PoolSync-v1.0.md`（待评审，主文 + 附录 A；本计划已采用方案甲，Task 0 将规格同步为 12 属性版）。

---

## 硬性约束（每个任务都必须遵守）

1. **C++17 + 禁止 `auto`**：所有新增/修改的项目代码（src/、include/）不得使用 `auto` 类型推导；测试代码可沿用现有风格（允许 `auto`）。
2. **PowerShell 语法**：所有命令以 PowerShell 运行。
3. **ctest 必须在 `build/` 目录运行**（仓库根目录的 DartConfiguration.tcl 会显示 0 个测试）。
4. **不得改动**: `src/ui/TrayIcon.cpp`、`src/ui/MainFrame.cpp` 析构（fix#5：仅 UnInit+delete，禁止任何 PopEventHandler）、序列化器与解析器（Task 4 例外：仅删除 log_network_failures 相关行）。
5. **文档先行**：Task 0 先更新规格，再动代码。
6. 悬挂进程 xray PID 6720/8616（08:40 产生）**不得杀除**（测试会自动收养，无害）。

---

## 文件结构总览

| 文件 | 操作 | 任务 |
|---|---|---|
| docs/specs/2026-09-07-Spec-ConfigDialog-PoolSync-v1.0.md | 修改（方案甲同步：§4.1 15→12、§2.3 预留→UI隐藏、R2 已决、§7 验证更新） | 0 |
| src/ui/ConfigDialog.cpp | 修改（新增独立代理池分类 12 属性 + singbox_asset_dir + load/save/validate + 删 log_network_failures 两处） | 1、2、4 |
| scripts/prepare-ui-sandbox.bat | 修改（standalone_pool 键名对齐解析器） | 3 |
| include/ConfigReader.h | 修改（删 log_network_failures 字段） | 4 |
| src/config/ConfigJsonSerializer.cpp | 修改（删 :31 写入行） | 4 |
| include/config/sections/LogConfigParser.h | 修改（删解析分支） | 4 |
| src/ProxyBatchTester.cpp | 修改（删 3 个 if 门 + INFO→DEBUG） | 4 |
| AGENTS.md | 修改（§4.2 删一行文档） | 4 |
| tests/test_config_reader.cpp | 修改（删 3 处引用） | 4 |
| tests/test_config_reader_load.cpp | 修改（删 4 处引用，**保留** :109 作为未知键回归夹具） | 4 |

---

## Task 0: 规格文档同步（方案甲落地）

**Files:**
- Modify: `docs/specs/2026-09-07-Spec-ConfigDialog-PoolSync-v1.0.md`

**背景:** 用户质疑 R2 后采用方案甲——UI 隐藏 3 个无消费方的预留字段（`standalone_pool.mode`、`observatory.type`、`observatory.samplingCount`），对话框从 15 属性减至 12 属性；后端零改动保留往返能力。

- [ ] **Step 1: 更新 §4.1 属性表（15→12）**

用编辑工具将 §4.1 表格中的以下 3 行**删除**：
- `pool_mode` 行
- `pool_obs_type` 行
- `pool_obs_sampling` 行

表格标题从「15 项属性」改为「12 项属性」，并在表后追加一段说明：

```markdown
> 按方案甲（用户已采纳）：`mode`、`observatory.type`、`observatory.samplingCount`
> 三个预留字段无运行时消费方（已 grep 验证：src/ 中仅 ConfigReader.h 默认值、
> StandalonePoolConfigParser.h 解析、ConfigJsonSerializer.cpp 序列化，零消费），
> **UI 隐藏**以避免制造新的「界面项 ↔ 实际生效」错位（与附录 A 同一原则）。
> 后端 struct/parser/serializer/tests 零改动：手工编辑过的旧 config.json 中的
> 这些键在保存时仍被原样保留（往返能力不受影响）。待 select 模式实现时再暴露 `mode`。
```

- [ ] **Step 2: 更新 §2.3 预留字段判定**

将 §2.3 中预留 3 项的结论从「暴露 + 「预留」标注」改为「UI 隐藏」，理由引用 Step 1 的说明。

- [ ] **Step 3: 更新 §6 决策点 R2**

将 R2 从开放决策点改为已决议：

```markdown
R2 **已决议（方案甲）**：3 个预留字段（mode / observatory.type /
observatory.samplingCount）在 UI 中隐藏，后端零改动。对话框为 12 属性。
```

- [ ] **Step 4: 更新 §7 验证步骤**

将 §7 中「15 项属性与 bin/config.json 对照」改为「12 项属性与 bin/config.json 对照」，并追加一条验证项：

```markdown
- 隐藏的 3 个预留字段在 config.json 中仍被后端往返保留
  （手工在 JSON 中设置 `"samplingCount": 7`，打开对话框→确定保存→重读 JSON，
  值仍为 7，证明 UI 隐藏不影响后端透传）。
```

- [ ] **Step 5: 核对无遗漏**

Run: `Select-String -Path "docs/specs/2026-09-07-Spec-ConfigDialog-PoolSync-v1.0.md" -Pattern "pool_mode|pool_obs_type|pool_obs_sampling"`
Expected: 除「方案甲隐藏说明」段落外，属性表与验证步骤中不再出现这 3 个键。

---

## Task 1: ConfigDialog 新增「独立代理池」分类（12 属性）

**Files:**
- Modify: `src/ui/ConfigDialog.cpp`（ctor ~:179-183 插入点、loadConfig ~:266-267 尾部、saveConfig ~:365-371 proxy 块后、validateConfig ~:510-518 return true 前）

**背景:** bin/config.json 中 standalone_pool 有 14 个键（3 个预留隐藏后 12 个生效字段需 UI 暴露），当前对话框完全没有此分类，用户只能手工编辑 JSON。属性键名、默认值、枚举白名单全部来自 `ConfigReader.h:11-46` 的 `StandalonePoolConfig` 与 `StandalonePoolConfigParser.h` 白名单。

- [ ] **Step 1: ctor 插入分类与 12 个属性**

在 `// --- 监控代理进程 配置 ---` 分类块（`proxy_process_monitor_check_interval_ms` 属性 Append 之后）与 `propGrid_->SetPropertyAttributeAll(wxPG_BOOL_USE_CHECKBOX, true);` 之间插入：

```cpp
    // --- 独立代理池 配置 ---
    // 与 config.json standalone_pool 节同步（StandalonePoolConfigParser 真实键）。
    // 方案甲：mode / observatory.type / observatory.samplingCount 三个预留字段
    // 无运行时消费方，UI 隐藏（后端 struct/parser/serializer 保留往返能力）。
    // 端口为期望值：实际由 PortManager 动态分配防碰撞（AppController::startProxyPool）。
    propGrid_->Append(new wxPropertyCategory(L"独立代理池"));
    propGrid_->Append(new wxBoolProperty(L"启用独立代理池", "pool_enabled",
                                         cfg.standalone_pool.enabled));
    propGrid_->Append(new wxIntProperty(L"SOCKS 端口（期望值）", "pool_socks_port",
                                        cfg.standalone_pool.socksPort));
    propGrid_->Append(new wxIntProperty(L"API 端口（期望值）", "pool_api_port",
                                        cfg.standalone_pool.apiPort));
    wxArrayString strategyChoices;
    strategyChoices.Add("random");
    strategyChoices.Add("leastPing");
    strategyChoices.Add("leastLoad");
    propGrid_->Append(new wxEnumProperty(L"均衡策略", "pool_balancer_strategy",
                                         strategyChoices, strategyChoices));
    propGrid_->SetPropertyValue("pool_balancer_strategy",
                                 wxString(cfg.standalone_pool.balancerStrategy));
    propGrid_->Append(new wxStringProperty(L"观测探测地址（兜底）", "pool_obs_destination",
                                           cfg.standalone_pool.observatory.destination));
    propGrid_->Append(new wxIntProperty(L"观测间隔（秒）", "pool_obs_interval",
                                        cfg.standalone_pool.observatory.intervalSec));
    propGrid_->Append(new wxIntProperty(L"观测超时（秒）", "pool_obs_timeout",
                                        cfg.standalone_pool.observatory.timeoutSec));
    propGrid_->Append(new wxIntProperty(L"健康评估间隔（秒）", "pool_eval_interval",
                                        cfg.standalone_pool.evaluate.intervalSec));
    propGrid_->Append(new wxBoolProperty(L"健康结果日志", "pool_eval_report_health",
                                         cfg.standalone_pool.evaluate.reportHealth));
    propGrid_->Append(new wxBoolProperty(L"自动剔除失效成员", "pool_eval_auto_prune",
                                         cfg.standalone_pool.evaluate.autoPruneDead));
    propGrid_->Append(new wxIntProperty(L"剔除阈值（连续失败次数）", "pool_eval_prune_streak",
                                        cfg.standalone_pool.evaluate.pruneFailStreak));
    propGrid_->Append(new wxBoolProperty(L"自动优化（预留记录式）", "pool_eval_auto_optimize",
                                         cfg.standalone_pool.evaluate.autoOptimize));
```

注意：
- 探测地址标签注明「兜底」——运行时 `AppController::startProxyPool` 会用全局 `test.url` 覆盖 `probeUrl`，仅当 `test.url` 为空时才落到此值（依据 `AppController.cpp:1806`、`StandaloneProxyPool.cpp:236` 回退链）。
- `autoOptimize` 当前为记录式占位（`StandaloneProxyPool.cpp:287-295`），标签注明「预留记录式」。

- [ ] **Step 2: loadConfig 回填（追加到 :266-267 之后）**

在 `propGrid_->SetPropertyValue("proxy_process_monitor_check_interval_ms", ...)` 之后追加：

```cpp
    // 独立代理池回填（12 生效字段；预留 3 字段 UI 隐藏）
    propGrid_->SetPropertyValue("pool_enabled", cfg.standalone_pool.enabled);
    propGrid_->SetPropertyValue("pool_socks_port", cfg.standalone_pool.socksPort);
    propGrid_->SetPropertyValue("pool_api_port", cfg.standalone_pool.apiPort);
    propGrid_->SetPropertyValue("pool_balancer_strategy",
                                wxString(cfg.standalone_pool.balancerStrategy));
    propGrid_->SetPropertyValue("pool_obs_destination",
                                wxString(cfg.standalone_pool.observatory.destination));
    propGrid_->SetPropertyValue("pool_obs_interval",
                                cfg.standalone_pool.observatory.intervalSec);
    propGrid_->SetPropertyValue("pool_obs_timeout",
                                cfg.standalone_pool.observatory.timeoutSec);
    propGrid_->SetPropertyValue("pool_eval_interval",
                                cfg.standalone_pool.evaluate.intervalSec);
    propGrid_->SetPropertyValue("pool_eval_report_health",
                                cfg.standalone_pool.evaluate.reportHealth);
    propGrid_->SetPropertyValue("pool_eval_auto_prune",
                                cfg.standalone_pool.evaluate.autoPruneDead);
    propGrid_->SetPropertyValue("pool_eval_prune_streak",
                                cfg.standalone_pool.evaluate.pruneFailStreak);
    propGrid_->SetPropertyValue("pool_eval_auto_optimize",
                                cfg.standalone_pool.evaluate.autoOptimize);
```

- [ ] **Step 3: saveConfig 读回（追加到 proxy 块 singbox_template 之后）**

在 `editedConfig_.proxy.singbox_template_config_path = ...` 之后追加：

```cpp
    // 独立代理池读回（12 生效字段）
    editedConfig_.standalone_pool.enabled =
        propGrid_->GetPropertyValueAsBool("pool_enabled");
    editedConfig_.standalone_pool.socksPort =
        propGrid_->GetPropertyValueAsInt("pool_socks_port");
    editedConfig_.standalone_pool.apiPort =
        propGrid_->GetPropertyValueAsInt("pool_api_port");
    editedConfig_.standalone_pool.balancerStrategy =
        propGrid_->GetPropertyValueAsString("pool_balancer_strategy").ToStdString();
    editedConfig_.standalone_pool.observatory.destination =
        propGrid_->GetPropertyValueAsString("pool_obs_destination").ToStdString();
    editedConfig_.standalone_pool.observatory.intervalSec =
        propGrid_->GetPropertyValueAsInt("pool_obs_interval");
    editedConfig_.standalone_pool.observatory.timeoutSec =
        propGrid_->GetPropertyValueAsInt("pool_obs_timeout");
    editedConfig_.standalone_pool.evaluate.intervalSec =
        propGrid_->GetPropertyValueAsInt("pool_eval_interval");
    editedConfig_.standalone_pool.evaluate.reportHealth =
        propGrid_->GetPropertyValueAsBool("pool_eval_report_health");
    editedConfig_.standalone_pool.evaluate.autoPruneDead =
        propGrid_->GetPropertyValueAsBool("pool_eval_auto_prune");
    editedConfig_.standalone_pool.evaluate.pruneFailStreak =
        propGrid_->GetPropertyValueAsInt("pool_eval_prune_streak");
    editedConfig_.standalone_pool.evaluate.autoOptimize =
        propGrid_->GetPropertyValueAsBool("pool_eval_auto_optimize");
```

注意：`probeUrl` 是运行时派生字段（`startProxyPool` 每次用 `config_.test_url` 覆盖），不属于持久化 schema，**不读回**。

- [ ] **Step 4: validateConfig 追加池校验（插到 `return true;` 之前）**

在 ppm 间隔校验块之后、`return true;` 之前插入（沿用现有中文 wxMessageBox 风格；URL 校验沿用 network_monitor :505-507 的 `utils::isValidUrlFormat` 先例）：

```cpp
    // 独立代理池校验（启用时才强制）
    if (editedConfig_.standalone_pool.enabled) {
        if (editedConfig_.standalone_pool.socksPort < 1024 ||
            editedConfig_.standalone_pool.socksPort > 65535) {
            wxMessageBox("独立代理池 SOCKS 端口必须在 1024 到 65535 之间", "验证错误",
                         wxOK | wxICON_ERROR);
            return false;
        }
        if (editedConfig_.standalone_pool.apiPort < 1024 ||
            editedConfig_.standalone_pool.apiPort > 65535) {
            wxMessageBox("独立代理池 API 端口必须在 1024 到 65535 之间", "验证错误",
                         wxOK | wxICON_ERROR);
            return false;
        }
        if (editedConfig_.standalone_pool.observatory.intervalSec < 1 ||
            editedConfig_.standalone_pool.observatory.intervalSec > 300) {
            wxMessageBox("独立代理池观测间隔必须在 1 到 300 秒之间", "验证错误",
                         wxOK | wxICON_ERROR);
            return false;
        }
        if (editedConfig_.standalone_pool.observatory.timeoutSec < 1 ||
            editedConfig_.standalone_pool.observatory.timeoutSec > 60) {
            wxMessageBox("独立代理池观测超时必须在 1 到 60 秒之间", "验证错误",
                         wxOK | wxICON_ERROR);
            return false;
        }
        if (!editedConfig_.standalone_pool.observatory.destination.empty() &&
            !utils::isValidUrlFormat(editedConfig_.standalone_pool.observatory.destination)) {
            wxMessageBox("独立代理池观测探测地址格式无效: " +
                             editedConfig_.standalone_pool.observatory.destination,
                         "URL格式错误", wxOK | wxICON_WARNING);
            return false;
        }
        if (editedConfig_.test_url.empty() &&
            editedConfig_.standalone_pool.observatory.destination.empty()) {
            // 双空仅警告：观测探测将无可用地址，但不阻断保存
            wxMessageBox("全局测试 URL 与观测探测地址均为空，代理池观测探测将无法工作",
                         "配置警告", wxOK | wxICON_WARNING);
        }
        if (editedConfig_.standalone_pool.evaluate.intervalSec < 1 ||
            editedConfig_.standalone_pool.evaluate.intervalSec > 600) {
            wxMessageBox("独立代理池健康评估间隔必须在 1 到 600 秒之间", "验证错误",
                         wxOK | wxICON_ERROR);
            return false;
        }
        if (editedConfig_.standalone_pool.evaluate.pruneFailStreak < 1) {
            wxMessageBox("独立代理池剔除阈值必须至少为 1 次", "验证错误",
                         wxOK | wxICON_ERROR);
            return false;
        }
    }
```

注意：需要确认 `utils::isValidUrlFormat` 已在本文件可见（network_monitor 校验已使用它，无需新增 include；若编译报错则在文件头部与其它 utils 引用一同补 `#include "Utils.h"`——按现有 include 现状，它已被引入）。

- [ ] **Step 5: 编译验证**

Run: `cmake --build build --parallel 8`
Expected: 编译成功，0 error（ConfigDialog.cpp 重新编译，validproxy.exe 重链接；clangd LSP 噪声忽略）。

---

## Task 2: 补齐 proxy.singbox_asset_dir 属性

**Files:**
- Modify: `src/ui/ConfigDialog.cpp`（ctor singbox 块 :77-88、loadConfig :259 区域、saveConfig :369 区域）

**背景:** `AppConfig.proxy.singbox_asset_dir` 字段、序列化写入、bin/config.json 中都有值（`E:/soft/v2rayN-windows-64/bin/srss`），但对话框无此属性——是规格 §2.1 差距②。

- [ ] **Step 1: ctor 插入 File 属性（sbExecProp 块与 sbTmplProp 块之间）**

在 `proxy_singbox_executable` 的 `wxPG_DIALOG_TITLE` 设置行之后、`proxy_singbox_template_config_path` 块之前插入：

```cpp
    wxFileProperty* sbAssetProp = new wxFileProperty(
        L"Sing-box资源目录", "proxy_singbox_asset_dir",
        cfg.proxy.singbox_asset_dir);
    propGrid_->Append(sbAssetProp);
    propGrid_->SetPropertyAttribute("proxy_singbox_asset_dir",
                                    wxPG_FILE_SHOW_FULL_PATH, (long)1);
    sbAssetProp->ChangeFlag(wxPGFlags::ShowFullFileName, true);
    propGrid_->SetPropertyAttribute("proxy_singbox_asset_dir",
                                    wxPG_DIALOG_TITLE, L"选择 Sing-box 资源目录");
```

- [ ] **Step 2: loadConfig 回填**

在 `propGrid_->SetPropertyValue("proxy_singbox_executable", ...)` 之后追加：

```cpp
    propGrid_->SetPropertyValue("proxy_singbox_asset_dir",
                                wxString(cfg.proxy.singbox_asset_dir));
```

- [ ] **Step 3: saveConfig 读回**

在 `editedConfig_.proxy.singbox_executable = ...` 之后追加：

```cpp
    editedConfig_.proxy.singbox_asset_dir =
        propGrid_->GetPropertyValueAsString("proxy_singbox_asset_dir").ToStdString();
```

- [ ] **Step 4: 编译验证**

Run: `cmake --build build --parallel 8`
Expected: 编译成功，0 error。

---

## Task 3: 沙盒脚本键名修正

**Files:**
- Modify: `scripts/prepare-ui-sandbox.bat`

**背景:** 沙盒 config.json 的 standalone_pool 用了错误键名（selection/min_members/max_members/eval_interval_ms/health_check），解析器只认 mode/socksPort/apiPort/balancerStrategy/observatory/evaluate——除 enabled 与 evaluate 三布尔外全部静默丢失（规格 §2.1 差距⑤）。端口建议改为 11009/11010（与沙盒 xray 11080 段对齐，决策点 R5 推荐）。

- [ ] **Step 1: 替换 standalone_pool 块**

将 bat 中写 standalone_pool 的 echo 行整体替换为（保持 bat 的行拼接格式，以下为 JSON 内容）：

```text
"standalone_pool": { "enabled": true, "mode": "pool", "socksPort": 11009, "apiPort": 11010, "balancerStrategy": "leastPing", "observatory": { "type": "http", "destination": "https://www.google.com/generate_204", "intervalSec": 5, "samplingCount": 10, "timeoutSec": 5 }, "evaluate": { "intervalSec": 10, "reportHealth": false, "autoPruneDead": false, "pruneFailStreak": 3, "autoOptimize": false } }
```

- [ ] **Step 2: 运行沙盒脚本验证产物**

Run: `.\scripts\prepare-ui-sandbox.bat`
Expected: 脚本执行无报错（或仅既有守护提示）。

Run: `Select-String -Path "test\ui-sandbox\config.json" -Pattern '"socksPort": 11009'`
Expected: 命中 1 行，证明新键名写入。

Run: `Select-String -Path "test\ui-sandbox\config.json" -Pattern 'selection|health_check|min_members'`
Expected: 无匹配（旧键已消失）。

---

## Task 4: 附录 A — 移除 log_network_failures（7 个文件）

**Files:**
- Modify: `src/ui/ConfigDialog.cpp:101` + `:287`
- Modify: `include/ConfigReader.h:46`
- Modify: `src/config/ConfigJsonSerializer.cpp:31`
- Modify: `include/config/sections/LogConfigParser.h:29-33` + `:50`
- Modify: `src/ProxyBatchTester.cpp` 3 处（~:523、~:556、~:566）
- Modify: `AGENTS.md` §4.2 一行
- Modify: `tests/test_config_reader.cpp:40` + `:90` + `:139`
- Modify: `tests/test_config_reader_load.cpp:391` + `:399` + `:572` + `:621`（**保留** `:109`）

**背景:** 该配置项存在三方语义错位——UI 标签「网络错误日志」、实际仅门控 3 条批量测试启动进度 INFO 日志、AGENTS.md 又写成「降级为 TRACE」。真正按代理网络结果日志（ProxyBatchTester.cpp:301/:306）从来不受它控制。按附录 A 方案 A：移除开关，3 条日志降为 DEBUG，由日志级别统一控制（默认 console INFO / file ERROR 下自动静默；file_level=DEBUG 时重现）。

- [ ] **Step 1: 删除 ConfigDialog 属性与读回**

删除 `src/ui/ConfigDialog.cpp:101`：

```cpp
    propGrid_->Append(new wxBoolProperty(L"网络错误日志", "log_network_failures",
                                         cfg.log_network_failures));
```

删除 `src/ui/ConfigDialog.cpp:287`：

```cpp
    editedConfig_.log_network_failures =
        propGrid_->GetPropertyValueAsBool("log_network_failures");
```

日志分类此后仅剩 console_level + file_level 两个枚举属性。

- [ ] **Step 2: 删除 AppConfig 字段**

删除 `include/ConfigReader.h:46`：

```cpp
    bool log_network_failures = false;
```

（第 45 行 `bool log_enabled = true;` 保留。）

- [ ] **Step 3: 删除序列化写入**

删除 `src/config/ConfigJsonSerializer.cpp:31`：

```cpp
    logObj["network_failures"] = config.log_network_failures;
```

- [ ] **Step 4: 删除解析分支**

删除 `include/config/sections/LogConfigParser.h` 第 29-33 行的 3 分支链：

```cpp
    if (log.contains("network_failures") && log.at("network_failures").is_bool()) {
        config.log_network_failures = log.at("network_failures").as_bool();
    } else if (log.contains("network_failures")) {
        Logger::write("[ConfigReader] log.network_failures 无效类型，使用默认 false", LogLevel::WARN);
        config.log_network_failures = false;
    } else {
        config.log_network_failures = false;
    }
```

同时删除第 50 行（无 log 节默认分支内）：

```cpp
        config.log_network_failures = false;
```

（第 49 行 `config.log_enabled = true;` 保留。）

- [ ] **Step 5: ProxyBatchTester 3 门改 DEBUG**

`src/ProxyBatchTester.cpp` run() ~:523 与 runWithSubId() ~:566，两处相同内容——将：

```cpp
    if (config_.log_network_failures) {
        Logger::write("Started " + std::to_string(instanceCount) + " xray instances",
                      LogLevel::INFO);
    }
```

替换为：

```cpp
    Logger::write("Started " + std::to_string(instanceCount) + " xray instances",
                  LogLevel::DEBUG);
```

runWithSubId() ~:556——将：

```cpp
    if (config_.log_network_failures) {
        Logger::write("Testing " + std::to_string(totalProxies_) +
                          " proxies from subscription: " + subId, LogLevel::INFO);
    }
```

替换为：

```cpp
    Logger::write("Testing " + std::to_string(totalProxies_) +
                      " proxies from subscription: " + subId, LogLevel::DEBUG);
```

- [ ] **Step 6: AGENTS.md 文档行删除**

删除 `AGENTS.md` §4.2 代码块中的这一行：

```text
# log.network_failures: true  （网络失败日志从 INFO 降级为 TRACE）
```

- [ ] **Step 7: 测试引用删除（test_config_reader.cpp 3 处）**

删除 `:40`（SaveRoundTrip 内）：

```cpp
    original.log_network_failures = true;
```

删除 `:90`（SaveRoundTrip_FieldCompleteness 内）：

```cpp
    original.log_network_failures = true;
```

删除 `:139`：

```cpp
    EXPECT_EQ(loaded->log_network_failures, original.log_network_failures);
```

- [ ] **Step 8: 测试引用删除（test_config_reader_load.cpp 4 处删 + 1 处保留）**

删除 `:391`（SectionDefaults_Log 夹具内）：

```cpp
        "network_failures": true,
```

删除 `:399`：

```cpp
    EXPECT_TRUE(result->log_network_failures);
```

删除 `:572`（往返夹具 log 块内）：

```cpp
        "network_failures": true,
```

删除 `:621`：

```cpp
    EXPECT_EQ(reloaded->log_network_failures, original->log_network_failures);
```

**保留** `:109` full.json 夹具中的 `"network_failures": false`——作为「旧配置残留未知键 → 解析器静默忽略不报错」的回归夹具（附录 A.4 向后兼容验证点）。

- [ ] **Step 9: 全局残留扫描**

Run: `Get-ChildItem -Recurse -Include *.cpp,*.h,*.bat,*.md -Path src,include,tests,scripts,AGENTS.md | Select-String -Pattern "log_network_failures|network_failures"`
Expected: 仅剩 `tests/test_config_reader_load.cpp:109`（故意保留的夹具）与 docs/ 下的历史文档引用（bugfix/spec 归档，不改）。

---

## Task 5: 构建 + 全量测试 + 手工验证清单

**Files:**
- Test: 全仓构建与测试基线（无新增测试文件；ConfigReader 相关测试经 Task 4 修剪后须保持全绿）

- [ ] **Step 1: 全量构建**

Run: `cmake --build build --parallel 8`
Expected: 编译成功，0 error（含全部测试目标；仅既有警告）。

- [ ] **Step 2: 配置读写单测**

Run: `ctest --test-dir build -R "ConfigReader" -V`
Expected: ConfigReaderTest + ConfigReaderLoadTest 全部 PASS（残留 :109 夹具被静默忽略，不报错）。

- [ ] **Step 3: 全量测试基线**

Run: `ctest --test-dir build -V`
Expected: 44/44 PASS（基线与此前一致；若 ConfigReader 计数因删除断言而变化，以全绿为准）。

- [ ] **Step 4: UI 套件**

Run: `ctest --test-dir build -R "^UI_" -V`
Expected: 7/7 PASS（UI_MAINWINDOW/SEARCH/CLEAR/FLOATINGWIDGET/POOL/PORTCLOSE/EXITASSERT；SEARCH/CLEAR/POOL 为已知 UIA 间歇性抖动，单测失败先隔离重跑再判定）。

- [ ] **Step 5: 手工清单（用户执行）**

1. 运行 `bin\validproxy.exe` → 菜单打开配置编辑窗口。
2. 「独立代理池」分类显示 12 项，初值与 bin/config.json 当前值一致：
   启用=true、10809/10810、leastPing、https://www.google.com、5/5、10、true、false、3、false。
3. 「代理」分类新增「Sing-box资源目录」，初值 `E:/soft/v2rayN-windows-64/bin/srss`。
4. 「日志」分类仅剩 2 项（控制台/文件级别），「网络错误日志」已消失。
5. 修改 balancerStrategy=leastLoad、剔除阈值=5 → 确定 → 重开对话框值不回退；bin/config.json 中对应键更新。
6. 校验拦截：端口改 80 → 确定被拦截；探测地址改裸域名 `www.baidu.com` → 确定被拦截。
7. 预留字段往返：手工在 bin/config.json 设 `"samplingCount": 7` → 打开对话框→确定→重读 JSON，值仍为 7。
8. 旧键自愈：确认保存后 config.json 中不再出现 `network_failures` 键。
9. 批量测试默认级别跑一轮：控制台/日志文件中无 "Started N xray instances" INFO 行；临时把 file_level 改 DEBUG 再跑：该行以 DEBUG 级重现。

- [ ] **Step 6: 提交（用户确认后）**

按 git-master 技能原子提交（本计划变更 + 规格 + 计划文档一并，遵守仓库既有提交风格）。

---

## 自检记录（Self-Review）

**1. 规格覆盖：**
- 主文 §1 五项目标：①独立代理池可视化 12 字段 → Task 1；②双向读写闭环 → Task 1 load/save + Task 5 手工清单 5；③校验拦截 → Task 1 Step 4 + Task 5 手工清单 6；④singbox_asset_dir 补齐 → Task 2；⑤沙盒键名修正 → Task 3。✓
- 主文 §4.1 属性表（方案甲 12 项）→ Task 1 全 12 键逐一对应 ✓；§4.3 load/save 代码 → Task 1 Step 2/3 ✓；§4.4 校验规则 → Task 1 Step 4 全覆盖（端口/间隔/超时/URL/双空警告/阈值）✓；§4.5 枚举白名单 → Task 1 Enum choices = parser 白名单 ✓；§4.6 沙盒修正 → Task 3 ✓；§4.8 不变式 → 硬性约束 §4 条 ✓。
- 附录 A A.3 变更清单 9 行：ConfigDialog 两处 → Task 4 Step 1；ConfigReader.h → Step 2；Serializer → Step 3；LogConfigParser → Step 4；ProxyBatchTester → Step 5；AGENTS.md → Step 6；test_config_reader.cpp → Step 7；test_config_reader_load.cpp → Step 8（:109 保留）✓；scripts 零引用 → 无任务（正确）。
- 方案甲（用户 m2289 采纳）→ Task 0 全部 4 步 ✓。

**2. 占位符扫描：** 全文无 TBD/TODO/「类似 Task N」/空实现步骤；每个代码步骤均为完整可粘贴代码块。Task 1 Step 4 的 include 说明为条件性指引（引用既有事实：network_monitor 校验已在用 `utils::isValidUrlFormat`），非占位符。✓

**3. 类型一致性：** 12 个属性键名在 ctor/loadConfig/saveConfig 三处完全一致；`GetPropertyValueAsInt` 对应 wxIntProperty、`AsBool` 对应 wxBoolProperty、`AsString().ToStdString()` 对应 wxStringProperty/wxEnumProperty——与规格 §4.1 键名及 ConfigReader.h 字段路径（standalone_pool.socksPort 等）逐一对齐；LogConfigParser 删除行号与 b26 锚点一致。✓

**遗留决策点（不阻塞本计划，实施时按推荐执行）：** R5 沙盒端口 11009/11010（Task 3 已按推荐值写入，用户评审时可改回 10809）；R4 端口重试硬编码 10809 回退（AppController.cpp:1807，独立改进项不在本计划）；R1 对话框变长（12 属性分类追加在底部 + 分类可折叠，无需额外处理）；R6 无单测（Task 5 手工清单 + 回归兜底已覆盖）。
