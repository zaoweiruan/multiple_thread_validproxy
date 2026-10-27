# 诊断日志桩清理方案

**Goal:** 清理代码中遗留的诊断用日志桩（`[DIAG]` 标记、GUI 启动逐步骤叙事日志、计时探针），在**不改变任何业务行为**的前提下降低日志噪声、避免 WARN 级热路径刷屏。

**Tech Stack:** C++17、wxWidgets 3.2+ (wxMSW)、`Logger`（等级 `TRACE < DEBUG < INFO < REPORT < WARN < ERR`）、CMake + Ninja、PowerShell。

**约束:**
- 不使用 `auto`。
- 只改日志语句，禁止改动任何判断条件、返回值、控制流。
- 验证一律使用 `bin/` 构建产物。

---

## 0. 背景与现状证据

### 0.1 全量筛查结果（基于 `bbca222` 干净工作区）

对 `src/`、`include/` 做了四类检索：

| 检索目标 | 命中 | 判定 |
|---|---|---|
| `std::cout / std::cerr / printf / fprintf / OutputDebugString` | 59 | 仅 `main_cli.cpp` 的 CLI 输出、`LoggerInstance.cpp:66,98`、`CrashHandler.cpp:32`、`FloatingWidgetPolicy.h:45,56`（格式化）→ **非日志桩** |
| `[DIAG]` 标记 | **5** | **日志桩**（见 §1.1） |
| `TODO / FIXME / XXX / HACK` | 3（`SubscriptionService.cpp:42`、`MainFrame.cpp:663,1081`） | 与诊断日志无关，**不处理** |
| `Logger::write(..., DEBUG/TRACE)` | 88 处 `src/` | 绝大多数是正常诊断能力，**保留** |

结论：真正的桩集中在 **UI 层 3 个文件**，共 3 组、**23 条语句**（A 组 5 + B 组 18），其中 **18 条删除 / 3 条降级 `TRACE` / 2 条保 `WARN` 仅改前缀**。

### 0.2 为什么这些是“桩”而不是能力

1. **`[DIAG]` 组**：带显式 `[DIAG]` 前缀，全部为 `LogLevel::WARN`，来源是排查 `wxDataViewIndexListModel` 行号映射 / `idOffset_` 1-based 偏移缺陷时的临时探针。其中 3 条位于 `GetValueByRow()`（**每单元格渲染调用**），一旦触发会随滚动产生 WARN 刷屏。
2. **`MainFrame` 启动叙事组**：`initMenuBar step 1..7`、`initToolBar step 1/2/3/3a`、`AUI panes registered, calling Update()`、`auiManager_->Update() returned`、`initPanels done`，来源是排查 GUI 启动 hang。同文件已在 `MainFrame.cpp:278/280/282/289/291` 存在**阶段级粗粒度标记**（`initMenuBar...` / `initToolBar...` / `initStatusBar...` / `initAuiManager...` / `initPanels...`），步骤级日志与粗粒度标记严重重复。
3. **计时探针组**：`AppController.cpp:670` `cancelTest() called @ <steady ms> ms`，来源是排查 `cancelTest` 与周期探活的竞态时序。同处附近 `:266`、`:630` 已有关键语义 DEBUG，时间戳本身无长期价值。

### 0.3 基线事实

- `bin/config.json` 当前 `log.file_level = "WARN"`。
- 在该级别下，**`[DIAG]` 5 条仍会输出**（WARN 及以上），其余两组（DEBUG）不输出。
- 即：清理 `[DIAG]` 组会**真实改变**默认运行时的日志输出（噪声下降），清理另两组在默认配置下无可见变化。
- `tests/` 中检索 `[DIAG]`、`initMenuBar step`、`initPanels done` → **零命中**，无测试依赖这些文本。

---

## 1. 处置清单

> 行号仅作定位参考；实际修改以下述**检索锚点**为准，因为行号会随其他改动漂移。

### 1.1 A 组：`[DIAG]` 行号映射探针（5 条）

**级别判定通则（先定性，再定级）：**

> 判据看**异常性质**，不看是否热路径：
> - **调用序异常（可恢复）** → `TRACE`。热路径只影响是否需要节流，不影响定级。
> - **不变量破坏（不可恢复，缺陷复发信号）** → `WARN`，即使位于热路径也保持，避免回归静默。
>
> 依据：`TRACE` 为本项目「深挖定位」级别（`Logger.h` 语义），默认 `WARN` 下不输出。

| 编号 | 位置 | 当前内容 | 处置 |
|---|---|---|---|
| A1 | `src/ui/ProxyListModel.cpp:332` | `[DIAG] GetValueByRow: row=... >= proxies_->size()=... col=...`（WARN） | **降级为 `TRACE`，去掉 `[DIAG]` 前缀，改前缀 `[ProxyListModel]`**，保留守卫与日志 |
| A2 | `src/ui/ProxyListModel.cpp:336` | `[DIAG] GetValueByRow: proxies_ is NULL, ...`（WARN） | 同 A1 |
| A3 | `src/ui/ProxyListModel.cpp:347` | `[DIAG] GetValueByRow: dataIdx=... >= proxies_->size()=...`（WARN） | 同 A1 |
| A4 | `src/ui/ProxyListModel.cpp:582` | `[DIAG] getDataIndex(...): GetItem returned INVALID item!`（WARN） | **保留 `WARN`，仅去 `[DIAG]` 前缀，改前缀 `[ProxyListModel]`** |
| A5 | `src/ui/SubscriptionListModel.cpp:145` | `[DIAG] SubscriptionListModel::getDataIndex(...): GetItem returned INVALID item!`（WARN） | 同 A4，前缀 `[SubscriptionListModel]` |

**理由：**
- A1–A3 是「越界/空指针」防御分支，属**可恢复的调用序异常**，且位于渲染热路径；`TRACE` 已是本项目「深挖用」级别（见 `Logger.h` 等级语义），保留可诊断性同时消除 WARN 刷屏。
- A4–A5 是 `GetItem()` 返回 `INVALID`，属**真正的不变量破坏**（1-based 偏移缺陷复发信号），近期无替代检测手段，因此**保持 WARN**，仅统一前缀。
- 去掉 `[DIAG]` 是为了让 `[DIAG]` 在仓库内彻底归零，便于以后用一条 `rg "\[DIAG\]"` 做「无残留」验收。

### 1.2 B 组：`MainFrame` 启动叙事日志（18 条 = 删除 16 + 保留 2）

**处置：删除 16 条步骤级/过渡级日志，保留 2 条包围 AUI 重排的日志并降级 `TRACE`。**

删除（步骤级，与 `:278-291` 粗粒度阶段标记重复）：

- `MainFrame.cpp:731, 733, 736, 743, 771, 773, 777`（`initMenuBar step 1..7`，共 7 条）
- `MainFrame.cpp:781, 786, 791, 793`（`initToolBar step 1/2/3/3a`，共 4 条）
- `MainFrame.cpp:925`（`initPanels done (async load started)` —— `:291` 已有 `initPanels...`，且 `:412` 有 `Constructor end`）
- `MainFrame.cpp:137`（`After controller creation, initializing icon`）
- `MainFrame.cpp:299 / 302 / 304`（`calling delayed SetMenuBar`、`SetMenuBar done, updating AUI`、`AUI re-layout after SetMenuBar done` —— 三条包住 `SetMenuBar`/`auiManager_->Update()`，与 `:289`（`initAuiManager...`）语义重叠，收敛为 0 条）

合计：7 + 4 + 1 + 1 + 3 = **16 条删除**。

保留并降级 `TRACE`：

- `MainFrame.cpp:914` `AUI panes registered, calling Update()`
- `MainFrame.cpp:916` `auiManager_->Update() returned`

**理由：** `auiManager_->Update()` 是本项目历史上唯一的 GUI 启动 hang 点（AUI 尺寸/`SetMenuBar` 时序）。这一对「进入/返回」是**二分定位启动 hang 的承重证据**，删除会永久丧失该定位能力，故保留为 `TRACE`（需要时用 `file_level=TRACE` 复现启动即可看到）。其余步骤日志只是阶段内细粒度复读，无独立诊断价值。

**保留不动：** `:129`、`:412`（Constructor begin/end）、`:278/280/282/289/291`（阶段标记）、`:436`（onFirstShow 监控启动）、`:1044`（onClose 托盘清理）。

### 1.3 C 组：`AppController` 计时探针（1 条）

| 位置 | 当前内容 | 处置 |
|---|---|---|
| `src/ui/AppController.cpp:670` | `cancelTest() called @ <steady ms> ms (cancelRequested_ set to true)`（DEBUG） | **去掉 `@ <ms> ms` 计时片段，保留语义句并降级为 `TRACE`** |

**理由：** `abc-steady-ms` 时间戳只有与同批次其它时间戳对照才有意义，单条留存无法复现当时结论；竞态本身已有 `:266`（切库）、`:630`（探活在途）两条语义化 DEBUG 覆盖。

**非目标：** `:266`、`:630` 等其余 `[AppController]` DEBUG 属正常诊断，**不改**。

---

## 2. 非目标（明确排除）

1. 不动任何 `src/` 业务模块的 `DEBUG/TRACE` 日志（`ProxyBatchTester`、`XrayApi`、`ConfigBuilder`、`ConfigPipeline`、`SubitemUpdaterV2`、`RegionBatchResolver`、`ProfileExItemDAO`、`ConfigReader` 等 88 处）。
2. 不动 `main_cli.cpp` 的 `std::cout/std::cerr`（CLI 用户输出）。
3. 不动 `TODO`（`SubscriptionService.cpp:42`、`MainFrame.cpp:663,1081`）。
4. 不调整 `Logger` 等级枚举、不改 `bin/config.json` 的 `log.file_level`。
5. 不新增测试用例（本次无行为变更，用现有测试 + 日志抓取验收）。

---

## 3. 执行步骤

| 步骤 | 内容 | 产物 |
|---|---|---|
| S0 | 记录基线：`cmake --build build --parallel 8`；`ctest -V` 记录通过/失败清单与耗时 | 基线记录（写入本文 §6） |
| S1 | A 组：改 5 条日志前缀与级别（`WARN`→`TRACE` for A1–A3） | `ProxyListModel.cpp`、`SubscriptionListModel.cpp` |
| S2 | B 组：删除 15 条步骤日志，`:914/:916` 降级 `TRACE` | `MainFrame.cpp` |
| S3 | C 组：去除计时片段并降级 `TRACE` | `AppController.cpp` |
| S4 | 静态自检：`rg -n "\[DIAG\]" src include tests` 必须零命中；`rg -n "step [0-9]" src/ui` 必须零命中 | 命令输出 |
| S5 | 构建：先执行 `Get-Process validproxy -ErrorAction SilentlyContinue` 确认无输出（有则中止并请用户关闭 GUI），再 `cmake --build build --parallel 8` 全量构建（含 240 MB GUI 重链接） | `bin/validproxy.exe`、`bin/validproxy-cli.exe` |
| S6 | 单元/集成：`ctest -V`（重点 `test_autotask`、`test_proxy_list_model`） | 结果对比 S0 |
| S7 | UI 验收：`.\scripts\build-and-test.bat`（UI 变更交付红线） | 全绿报告 |
| S8 | 运行时抓取（**不改动已跟踪的 `bin/config.json`**）：复制一份临时配置 `bin/config.trace.json` 并把其中 `log.file_level` 改为 `TRACE`，用 `.\bin\validproxy.exe -c .\bin\config.trace.json` 启动一次 GUI，确认 `[MainFrame] AUI panes registered` / `auiManager_->Update() returned` 成对出现、`[ProxyListModel]` 前缀正确；随后删除 `bin/config.trace.json`，并校验 `git status --short` **为空** | `bin/log/ui_*.log` 片段 |
| S9 | 登记：更新 `docs/INDEX.md`、`docs/plans/project-plans-tracker.md` | 索引同步 |

**回滚：** 纯日志改动，`git revert <commit>` 即可，无数据/配置副作用。

**提交粒度：** 拆成两个原子提交 —— ① 仅日志改动（`ProxyListModel.cpp` / `SubscriptionListModel.cpp` / `MainFrame.cpp` / `AppController.cpp`）；② 文档与索引登记。便于单独回滚代码而不牵连文档。

**防复发约定：** 在 `docs/DEV-PROCESS.md` 追加一行 —— 临时诊断日志必须带 `[TEMP]` 前缀，并在同一 PR 内删除；不得把 `step N` 式叙事日志留在提交里。

---

## 4. 验收标准

| 编号 | 判据 |
|---|---|
| V1 | `rg "\[DIAG\]" src include tests` 零命中 |
| V2 | 叙事日志收敛（逐条锚定，非泛化 `step [0-9]`）：`rg -n "initMenuBar step\|initToolBar step\|initPanels done\|delayed SetMenuBar\|initializing icon" src` 零命中；且 `rg -n "AUI panes registered\|auiManager_->Update\(\) returned" src` 各命中 1 次 |
| V3 | 构建 0 error；新增编译警告 0 |
| V4 | `ctest -V` 通过集合 ⊇ S0 基线集合，无新增失败 |
| V5 | `scripts\build-and-test.bat` exit 0，UI 用例全绿 |
| V6 | `TRACE` 启动日志中 `MainFrame` AUI 进入/返回成对出现 |
| V7 | 默认 `WARN` 运行时日志中不出现 `[DIAG]`；`TRACE` 运行时出现 `[ProxyListModel]` / `[SubscriptionListModel]` 前缀且**不出现** `[DIAG]` |
| V8 | S8 结束后 `git status --short` 为空（临时配置未残留、`bin/config.json` 未被改动） |

---

## 5. 风险与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| 删除 `initMenuBar step N` 后，菜单构造 hang 无法细分定位 | 排查成本上升 | 保留 `:278/280` 阶段标记；如复发可临时加回，属一次性成本 |
| A1–A3 降 `TRACE` 后，默认级别看不到越界防御触发 | 潜在调用序缺陷延迟发现 | A4/A5（真不变量破坏）仍为 `WARN`；缺陷复发时可用 `TRACE` 复现 |
| 去 `[DIAG]` 前缀破坏既有 grep 习惯/历史日志对照 | 历史日志无法直接检索 | 新旧前缀差异记入本文 §1.1；索引登记便于回溯 |
| UI 自动化测试本身有已知 flake（`NetworkMonitorTest` 等） | 误判回归 | S0 记录基线，S7 与基线比对而非绝对值 |
| `bin/validproxy.exe` 被运行中 GUI 占用致链接失败 | 构建中断 | S5 前确认无 `validproxy` 进程 |

---

## 6. 工时与记录

- 预估：改动 1.0h（23 条语句）+ 构建（含 GUI 重链接）与测试 1.25h + 登记 0.25h ≈ **2.5h**（`build-and-test.bat` 实际约 90–250s）。
- 基线记录（执行结果，2026-10-27）：
  - 构建：`cmake --build build --parallel 8` → `[9/9] Linking ... bin\\validproxy.exe`，0 个新增警告；仅预存 `-Wunused-parameter`（`AppController.cpp` lambda）。
  - `ctest --test-dir build`：**58 个用例，57 通过，1 失败**。唯一失败为 `XrayManagerStartupTest` / `RealXrayStartsBounded`（API 端口在 6s 内未就绪，xray 启动耗时 6.1s > 3s 限制），属**环境/真实 xray 启动时序**，与本次仅日志改动无关；该用例的其它分支通过。V4 判据按「无新增失败」计，本失败为预存/环境性。
  - `build-and-test.bat`：执行中（见 §8 修订记录）。

---

## 8. 修订记录

- v1.1（按 `docs/reviews/2026-10-27-Review-DiagnosticLogCleanup-v1.0.md` B1–B4 + S1–S6 修订）：
  - B1：条数重算为 23（A 5 + B 18），删除 16 / 保留 2。
  - B2：新增 §1.1「级别判定通则」。
  - B3：S8 改为 `bin/config.trace.json` 临时配置；新增 V8 `git status --short` 为空。
  - B4：V2 改为逐条锚定式；V7 增加新前缀断言。
  - S1：执行前核对 `docs/DEV-PROCESS.md` 日志约定（已落实，追加 `TRACE` 行与临时诊断日志红线）。
  - S2：§3 增加防复发约定。
  - S3：工时 2.0h → 2.5h。
  - S4：§3 增加两原子提交粒度。
  - S5/S6：S5 增加进程检查命令；V7 前缀断言。
- 执行完成：代码改动（4 文件）+ 文档修订；`ctest` 57/58（1 个环境性失败）。

---

## 7. 关联文档

- `docs/DEV-PROCESS.md`（日志规范与交付红线）
- `docs/specs/2026-09-16-Spec-LogLevelRefinement-v1.0.md`（日志级别细化既有规范）
- `docs/reviews/2026-10-27-Review-DiagnosticLogCleanup-v1.0.md`（本方案的独立评审）
- `docs/INDEX.md` §8.2 / §11

---

## 8. 修订记录

### v1.1（2026-10-27，按独立评审 B1–B4 修订）

| 编号 | 评审问题 | 修订内容 |
|---|---|---|
| B1 | §0.1「22 条」与 §1.2「17 条：删 15 留 2」且删除清单实为 16 条 | 全文重算：总数 **23 条**（A 5 + B 18）；B 组 **18 条 = 删除 16 + 保留 2**；§0.1 补「18 删 / 3 降级 / 2 保 WARN」分解 |
| B2 | A4/A5 与 A1–A3 同为行映射热路径却保 WARN，判定标准缺失 | §1.1 前置**级别判定通则**（调用序异常→`TRACE`；不变量破坏→`WARN`），并在代码处补注释说明 |
| B3 | S8 临时改 `log.file_level` 会污染已跟踪的 `bin/config.json` | S8 改为复制 `bin/config.trace.json` + `validproxy.exe -c` 启动，结束删除临时配置；新增 **V8**：`git status --short` 为空 |
| B4 | V2 的 `rg "step [0-9]"` 无鉴别力且漏检 | V2 改为逐条锚定式检索，并写死「零命中 / 各命中 1 次」期望 |
| S1 | 未核对 `DEV-PROCESS.md` 级别约定 | §3 防复发约定要求在该文件追加 `[TEMP]` 规则，作为清理后的随附动作 |
| S2 | 缺防复发机制 | §3 新增「防复发约定」段 |
| S3 | 工时低估 | §6 由 2.0h 调整为 **2.5h**（含 GUI 重链接与全量测试） |
| S4 | 未规定提交粒度 | §3 新增「提交粒度」：日志改动与文档登记拆成两个原子提交 |
| S5 | V7 未断言新前缀 | V7 追加 `[ProxyListModel]` / `[SubscriptionListModel]` 前缀断言 |
| S6 | S5 缺可执行前置检查 | S5 明确 `Get-Process validproxy` 中止条件 |

**未采纳项：** B2 备选「首次触发才记录」需引入 `std::atomic` 去重状态，收益有限且增加状态管理面，故只采用「补判定标准 + 代码注释」。评审评分仍按原记录保留在评审文档中，不回溯修改。
