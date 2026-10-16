# Spec — 工具栏按钮文本调整（批量测试 / 终止当前测试 / 清洗 / 添加订阅 / 悬浮窗）

- **日期**: 2026-10-15
- **模块**: `src/ui/MainFrame`（`initToolBar`）、`tests/ui/UIIds.h`
- **版本**: v1.0

## 1. 背景

工具栏沿用早期 wxWidgets 的两字简短标签（「测试」「取消」「去重」「导入」「监控代理」）。经勘察，菜单栏已于 v1.0（`2026-10-15-Spec-UiMenuChineseI18n-v1.0.md`）全量中文化，工具栏成为最后的语义模糊区：

- 「测试」无法区分是单代理还是批量（菜单里同时存在「查找首个可用代理」与「查找最佳代理」两个细粒度入口）。
- 「取消」语义偏弱，实际动作是终止正在跑的批量测试。
- 「去重」是实现细节术语。
- 「导入」与「导入订阅链接…」菜单项、配置文件导入概念混淆。
- 「监控代理」过长（4 字）且与菜单项「独立代理监控…」表述不一致。

## 2. 需求

用户逐字诉求：**「调整功能：修改按钮文本：测试->批量测试、取消->终止当前测试、去重->清洗、导入->添加订阅、监控代理->悬浮窗 自行测试交付」**

| 工具 ID | 旧标签 | 新标签 |
|---|---|---|
| `ID_TOOL_TEST` | 测试 | **批量测试** |
| `ID_TOOL_CANCEL` | 取消 | **终止当前测试** |
| `ID_TOOL_DEDUP` | 去重 | **清洗** |
| `ID_TOOL_IMPORT` | 导入 | **添加订阅** |
| `ID_TOOL_STANDALONE_MON` | 监控代理 | **悬浮窗** |

## 3. 设计方案

### 3.1 改动点（`src/ui/MainFrame.cpp::initToolBar()`，仅第 2 参 label）

```cpp
m_toolbar->AddTool(ID_TOOL_TEST,         "批量测试",     ToolbarIcons::load("tool_test"),    "测试全部代理");
m_toolbar->AddTool(ID_TOOL_CANCEL,       "终止当前测试", ToolbarIcons::load("tool_cancel"),  "取消测试");
m_toolbar->AddTool(ID_TOOL_DEDUP,        "清洗",         ToolbarIcons::load("tool_dedup"),   "去重");
m_toolbar->AddTool(ID_TOOL_IMPORT,       "添加订阅",     ToolbarIcons::load("tool_import"),  "增加新订阅");
m_toolbar->AddTool(ID_TOOL_STANDALONE_MON,"悬浮窗",      ToolbarIcons::load("tool_monitoring_proxy_process"), "监控代理", wxITEM_CHECK);
```

**只改 label，不改 tooltip。** `AddTool(id, label, bitmap, shortHelp[, kind])` 中第 2 参是可见标签（同时是 UIA 意义上的名称来源），第 4 参是悬停短帮助。遵循本项目既定惯例 —— 参见 `ID_TOOL_FIND`（label「最佳代理」但 tooltip 保持「查找最佳代理」）—— **label 从简、tooltip 描述性**。逐一核对后，5 个新标签下原 tooltip 均不矛盾，故全部保留：

- `测试全部代理` 描述了「批量测试」的实际作用范围；
- `取消测试` 与「终止当前测试」同义；
- `去重` 解释了「清洗」具体做什么；
- `增加新订阅` 与「添加订阅」同义；
- `监控代理` 解释了「悬浮窗」是什么。

`EnableTool(ID_TOOL_CANCEL, false)`、`ToggleTool(ID_TOOL_STANDALONE_MON, ...)`、`wxITEM_CHECK` kind、工具顺序、图标、`AddSpacer(70)` 与搜索框布局**全部未动**。

### 3.2 测试侧：`UIIds.h` 登记（**不是 UIA locator**）

按铁律 1「选择器唯一合法位置是 `UIIds.h`」，5 个新文案登记进 `ids` 命名空间。但必须如实说明其性质：

> `wxAuiToolBar` 是**自绘**控件 —— 它是普通 `wxWindowNR`（不是原生 `wToolbarWindow32`），不暴露任何子 HWND，也不暴露子 UIA 元素。

`[dumptree]` 实测主窗口控件树（`dumpTree(12)`）中，工具栏只呈现为单个节点：

```
- type=50033 name="control" class="wxWindowNR" pid=15384      ← wxAuiToolBar 本体，子列表为空
```

因此**任何单个工具按钮都不存在可测量的 UIA Name**。铁律 1 要求「绝不猜测 Name/ClassName」，凭空编造 locator 本身即违规。故这些常量被定位为**文案唯一真源**（供评审与将来的截图/像素 diff 工具读取），并补齐了未改动的 5 个标签（更新 / 同步 / 自动任务 / 配置 / 详情）使整条工具栏文案集中可读；另加 `ToolBarHostName = L"control"`（实测值）供将来的工具栏级几何断言使用。

### 3.3 为什么没有新增 `[toolbar]` 用例

1. **不可观测**：工具标签在 UIA 与 Win32 两条通道都读不到（见 3.2），写了断言也只能断言「标签等于自己」，是自证式假测试。
2. **真实回归风险已被既有套件覆盖**：本次标签变长（测试 2→4 字、取消 2→6 字、导入 2→4 字、悬浮窗 4→3 字）会撑宽 `wxAuiToolBar`，把 `AddSpacer(70)` 之后的搜索框右推，可能挤出可视区。该风险**已由 `UI_LAYOUTSENSING` 的两条既有断言承担**：

   ```cpp
   validator.expectVisible(ids::SearchCtrlName);
   validator.expectInside(ids::SearchCtrlName);
   ```

   故不重复造用例，只在验证阶段重点复跑该套件。

## 4. 边界与约束

- **禁用 `auto`**：项目 C++17 硬约束，本次新增代码未引入 `auto`。
- **绝不能改的两处**（本次均未触碰）：
  - `m_searchBox->SetName("searchCtrl")` —— `TestSearch.cpp:15-20` 依赖该 UIA Name。
  - `MainWindowName = L"validproxy - Proxy Manager"` —— `TestChainPreview` / `TestStandaloneFloatingWidget` 断言依赖。
- **菜单文案不动**：用户只要求工具栏按钮。菜单仍为「移除重复代理」「导入订阅链接…」等上一轮定稿文案，与工具栏的「清洗」「添加订阅」构成长短搭配，不冲突。
- **勘察确认的易误伤点（本次未触碰）**：`tests/ui/TestStandaloneProxyPool.cpp:640` 的 `findNamed(picker, L"取消", ...)` 命中的是 `AddPoolMemberDialog` 内的 `wxButton`，**不是工具栏按钮**；`TestChainPreview.cpp:204-206` 用 `WM_COMMAND` + id `6205` 触发配置按钮，走命令 id 而非 label。两者均不受影响。

## 5. 验证

| 项 | 结果 |
|---|---|
| `cmake --build build --parallel 8` | **EXIT 0，0 error** |
| `[dumptree]` 实测 | EXIT 0；工具栏呈现为单个 `wxWindowNR name="control"`、子列表为空 → 印证 §3.2 |
| `UITests.exe "[layout]"`（标签变长的直接风险面） | **EXIT 0 — All tests passed (267 assertions in 6 test cases)** |
| `scripts\build-and-test.bat`（首跑） | EXIT 8 —— 仅 `UI_FLOATINGWIDGET` 失败（`orbCx1<=80` 实测 113、`hitCount==probeCount` 20/25） |
| `UITests.exe "[floatingwidget]"` 单跑复验 | **EXIT 0 / 12.93s / All tests passed (54 assertions in 6 test cases)** |
| `scripts\build-and-test.bat`（复跑） | **EXIT 0 — 100% tests passed, 0 tests failed out of 12，ALL TESTS PASSED** |

### 5.1 首跑失败归因

`UI_FLOATINGWIDGET` 的两处失败是 `ui-automation-test` 技能「Known baseline failures」表已记录的**并行负载下既有 flaky**（`flaky under parallel load; passes standalone`）：同一断言上一轮门禁首跑也曾以 124/80 失败、单跑 16.5s 通过。本次单跑 12.93s / 54 断言全绿，且该用例与工具栏文案无任何数据耦合（只测悬浮窗 Orb 命中区与展开/收回位置），判定为非回归。复跑门禁取绿即最终交付证据。

## 6. 风险

| 风险 | 评估 |
|---|---|
| 标签变长撑宽工具栏、搜索框被挤出可视区 | **已消除** —— `[layout]` 267 断言全绿，`expectVisible`/`expectInside(SearchCtrlName)` 覆盖 |
| 外部脚本/文档按旧标签定位 | 全仓 grep 确认 `tests/` 与 `src/` 对这 5 个标签**零处依赖**（除 `initToolBar()` 自身） |
| 文案再次漂移 | 5 个新文案连同未改动的 5 个集中在 `UIIds.h`，评审与将来的截图 diff 工具从单处读取 |