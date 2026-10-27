# 诊断日志桩清理方案 独立评审

**评审对象：** [`docs/plans/2026-10-27-Plan-DiagnosticLogCleanup-v1.0.md`](../plans/2026-10-27-Plan-DiagnosticLogCleanup-v1.0.md)
**评审基线：** `bbca222`（工作区 clean）
**评审方式：** 三视角独立核查 —— A：技术断言攻击（清单/行号/计数是否与代码一致）；B：可实施性（步骤能否在 Windows/MinGW + wxMSW 环境一次跑通）；C：文档一致性与验收效力（判据是否真能判红/判绿）。
**结论：** ✅ **APPROVE WITH CHANGES（有条件通过）** —— 清理方向与分组完全成立，无伪需求；但有 **4 条阻断项** 必须先修订，其中 B1 是必须重算的计数错误，B3 是会导致「临时调试配置被提交」的实际事故风险。

---

## 1. 复核结果（对方案 §0 证据的独立验证）

| 方案断言 | 复核命令 | 结果 |
|---|---|---|
| `[DIAG]` 共 5 条，位于 3 个文件 | `rg "\[DIAG\]" src` | ✅ 精确命中 5 条：`ProxyListModel.cpp:332/336/347/582`、`SubscriptionListModel.cpp:145` |
| 无测试依赖这些文本 | `rg "\[DIAG\]\|initMenuBar step\|initPanels done" tests` | ✅ 零命中 |
| `MainFrame` 存在阶段级粗粒度标记 | 读 `MainFrame.cpp:278-291` | ✅ `initMenuBar...` / `initToolBar...` / `initStatusBar...` / `initAuiManager...` / `initPanels...` 五条均在 |
| `MainFrame` DEBUG 叙事日志共 27 条 | `rg "Logger::write\(.*LogLevel::DEBUG" src/ui/MainFrame.cpp` | ✅ 27 条（方案子集 18 条，见 B1） |
| `log.file_level = WARN` | 读 `bin/config.json` | ✅ 且 **该文件已被纳入版本控制**（见 B3） |

**通过项 P1–P7：**

- **P1** 分组正确。A（`[DIAG]`）/ B（启动叙事）/ C（计时探针）三类性质不同，处置不同，未混为一谈。
- **P2** A1–A3 与 A4–A5 的**分级差异判断成立**：前者是热路径上的防御分支（可恢复异常），后者是 `GetItem()` 返回 `INVALID` 的不变量破坏（缺陷复发信号）。这个区分是本方案最有价值的判断。
- **P3** 保留 `MainFrame.cpp:914/916`（`auiManager_->Update()` 进入/返回）的判断正确。该处是本项目 GUI 启动 hang 的唯一定位锚点，删除将造成**永久性诊断能力损失**，降级为 `TRACE` 而非删除是恰当取舍。
- **P4** 非目标划定干净：88 处业务 DEBUG、CLI `std::cout`、3 个 TODO 均明确排除，避免「清理」演变为大范围日志改造。
- **P5** 回滚方案正确：纯日志改动 + `git revert` 无副作用。
- **P6** 命名与落位合规：`2026-10-27-Plan-DiagnosticLogCleanup-v1.0.md` 符合 `[YYYY-MM-DD]-[DocType]-[Module]-[Version].md`，且落在既有 `docs/plans/` 下。
- **P7** 未引入 `auto`，未改动任何判断/返回值/控制流（方案 §约束 明确写了，且处置动作只有「删/降级/改前缀」三种，不触碰逻辑）。

---

## 2. 阻断项（必须先修订）

### B1（阻断，计数错误）§0.1 与 §1.2 的条数自相矛盾

- §0.1 结论写「共 3 组、可归并为 **22 条**语句」→ 按 5 + 18 = **23**；按 5 + 17 = **22**。
- §1.2 标题写「`MainFrame` 启动叙事日志（**17 条**）」，正文写「删除 **15 条** …… 保留 **2 条**」→ 15 + 2 = 17，与标题自洽。
- 但**枚举出的删除清单实际是 16 条**：`initMenuBar` 7 条（731/733/736/743/771/773/777）+ `initToolBar` 4 条（781/786/791/793）+ 925（1 条）+ 137（1 条）+ 299/302/304（3 条）= **16**。
- 因此真实总数是 **18 条（16 删 + 2 留）**，方案 §0.1 的 22 与 §1.2 的 15/17 **全部需要重算**。

> 本项目历史评审中，「计数器记账不闭合」是反复出现的失败模式（参见 `2026-10-16-Review-SubscriptionBase64UrlFix-v1.2` 的 T15、`2026-10-19-Review-ProxyBatchTester-WorkerThreadStallFix-v1.0` 的 B1）。本次是同一类问题：**文字结论与枚举清单不一致**。
> **修订要求：** 以枚举清单为唯一事实源，重算为「18 条：删除 16 / 保留 2」，并同步修正 §0.1 的总数为 23（A 组 5 + B 组 18）。

### B2（阻断，安全一致性）A4/A5 同为热路径却保留 WARN，理由未论证

方案给 A1–A3 降级的理由之一是「位于 `GetValueByRow()`（每单元格渲染调用）」，但：

- `ProxyListModel.cpp:582` 的 `getDataIndex()` 同样在**行/列映射热路径**上被调用（`GetValueByRow` 内部即调用 `getDataIndex`），方案未说明为什么它不适用同一理由。
- `SubscriptionListModel.cpp:145` 位于订阅列表的同一映射路径。

**处置：** 这不是「必须降级」，而是**必须给出判定标准**。建议在 §1.1 增加一条通则：

> 判据 = 该日志是否表征「调用序异常（可恢复）」还是「不变量破坏（不可恢复）」。前者 `TRACE`，后者 `WARN`。热路径只影响**是否需要节流**，不影响级别。

并显式说明 A4/A5 属后者，故即使热路径也保持 `WARN`；若担心刷屏，应改为**首次触发才记录**（`static bool`/`std::once_flag`）而非降级。否则评审无法区分「有意保留」与「漏改」。

### B3（阻断，事故风险）S8 临时改 `log.file_level=TRACE` 会污染已受版本控制的 `bin/config.json`

- `bin` 在 `.gitignore` 中被忽略，但 `bin/config.json` 已在提交 `bbca222` 中**强制加入索引**，当前处于**被跟踪状态**。
- 方案 S8 要求「临时把 `log.file_level` 设为 `TRACE` 启动 GUI 一次，随后恢复 `WARN`」，一旦遗漏恢复，`git status` 会出现 ` M bin/config.json`，极易被顺手提交，把生产默认日志级别改成 TRACE（日志量、磁盘占用、性能均受影响）。
- **修订要求：** S8 改为 **不改动已跟踪文件**：复制 `bin/config.json` 为 `bin/config.trace.json`（临时文件，不提交），用 `validproxy.exe -c bin/config.trace.json` 启动；或仅在 `temp/` 下生成临时配置。同时增加验收步骤：S8 结束后 `git status --short` 必须为空。

### B4（阻断，判据效力）V2 的检索式缺乏鉴别力且有误报风险

- `rg "step [0-9]" src/ui` 作为 V2 判据：
  - **漏检**：方案保留的 `Constructor begin/end`、`initMenuBar...` 等叙事日志不含 `step N`，该判据无法证明「叙事日志已收敛」。
  - **误报**：其它 UI 文件若出现含 `step 1` 的正常文案/注释，会被判红，评审者需人工逐条排除。
- **修订要求：** V2 改为**逐条锚定已删除文本**的清单式校验，例如：

```powershell
rg -n "initMenuBar step|initToolBar step|AUI panes registered|Update\(\) returned|initPanels done|delayed SetMenuBar|initializing icon" src
```

期望：`initMenuBar step` / `initToolBar step` / `initPanels done` / `delayed SetMenuBar` / `initializing icon` 零命中；`AUI panes registered` 与 `Update() returned` **各命中 1 次**（证明保留项仍在）。

---

## 3. 重要问题（建议修订，不阻断）

| 编号 | 问题 | 建议 |
|---|---|---|
| S1 | §7 引用了 `docs/DEV-PROCESS.md` 的「日志规范」，但方案**未实际核对该规范是否规定了诊断日志的级别约定**（`TRACE` 是否被定义为此类用途） | 执行前读一次 `DEV-PROCESS.md` 相关章节；若其中已有级别约定，方案 §1.1 应引用条款号，避免将来被反向「修正」 |
| S2 | 未说明清理后**如何防止复发**（后续开发者再次加 `step N` 叙事日志） | 在 `docs/specs/2026-09-16-Spec-LogLevelRefinement-v1.0.md` 追加一条约定：临时诊断日志必须带 `[TEMP]` 前缀并在同一 PR 内删除；或在 `DEV-PROCESS.md` 补一行 |
| S3 | 工时 2.0h 未包含 **240 MB GUI 二进制的重新链接**与全量 `ctest` 实际耗时 | 明确 S5/S6/S7 的墙钟预估（链接 + ctest + UI 套件 90–250s），总工时改为 2.5h 更稳妥 |
| S4 | 未规定提交粒度 | 要求**两个原子提交**：① 仅日志改动（3 文件）；② 文档与索引登记。便于 `git revert` 精确回滚代码而不牵连文档 |
| S5 | V7 只断言「日志中不再出现 `[DIAG]`」，未断言新前缀正确 | 增加：`TRACE` 日志中出现 `[ProxyListModel]` / `[SubscriptionListModel]` 且**不出现** `[DIAG]` |
| S6 | S5 前置条件「GUI 需先关闭」只是文字提示，没有可执行检查 | 增加命令：`Get-Process validproxy -ErrorAction SilentlyContinue`，有输出则中止并提示用户关闭 |

---

## 4. 遗漏项（方案未覆盖，但建议同批处理或明确排除）

| 项 | 说明 | 建议 |
|---|---|---|
| `AppController.cpp` 其它叙事 DEBUG | 方案只处理 `:670`，但同文件 `:266`、`:630` 亦为排查期加入的单点叙事（非承重语义） | 明确「保留」并写出理由（方案 §1.3 已含糊提到，建议提升为独立小表） |
| `MainFrame.cpp:1044`（onClose 托盘清理） | 方案在「保留不动」中列了，但未说明理由 | 补一句：该处为关闭路径唯一痕迹，保留 |
| `LogPanel` 的级别过滤器枚举 | 若后续有人把日志级别重命名，过滤器会错位；本次不动 | 明确列入非目标即可（当前 §2 未提） |

---

## 5. 修订清单（执行顺序）

1. **B1** 重算条数：§0.1 → 23 条；§1.2 → 「18 条（删除 16 / 保留 2）」。
2. **B2** 在 §1.1 前增加「级别判定通则」，并说明 A4/A5 保 WARN 的依据（或改首触发记录）。
3. **B3** 改写 S8 为临时配置文件方案（`bin/config.trace.json`，不入库），并新增「S8 后 `git status --short` 为空」验收。
4. **B4** 重写 V2 为逐条锚定式校验，期望值写死命中/零命中。
5. S1–S6 按建议补入；遗漏项补表。
6. 修订完成后更新本文 §结论 为「APPROVE」。

---

## 6. 总评

方案的**判断质量高于其记数质量**：分级处置（热路径防御 vs 不变量破坏）、保留 AUI 进入/返回锚点、非目标划定，这三点都经得起代码核对，属于高质量取舍。方案失效点集中在**工程记账与判据效力**——条数自相矛盾（B1）、同级不同判（B2）、临时改已跟踪配置（B3）、验收检索式缺鉴别力（B4）。这四项均可在不改变大方向的前提下就地修补。

**评分：** 技术判断 8.5/10；可实施性 6.5/10（受 B3 拖累）；文档一致性 6/10（受 B1 拖累）；验收效力 6/10（受 B4 拖累）。**综合 7.0/10，修订 B1–B4 后可执行。**
