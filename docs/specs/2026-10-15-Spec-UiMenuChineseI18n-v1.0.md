# Spec — UI 菜单栏 / 工具栏中文化（英文菜单项全改中文 + 「查找」按钮改「最佳代理」）

- **日期**: 2026-10-15
- **模块**: `src/ui/MainFrame`（菜单栏 + 工具栏）、`tests/ui/UIIds.h`、`tests/ui/framework/Menu.h`、`tests/ui/TestMenuAction.cpp`
- **版本**: v1.0

## 1. 背景

`MainFrame::initMenuBar()` 早期沿用 wxWidgets 英文惯例（`&File` / `&Proxy` / `&Settings` / `&Help`、`Import Subscription URL…`、`Find Best Proxy` 等），而项目其余 UI（面板右键菜单、托盘菜单、ConfigDialog 属性网格、工具栏按钮）早已完成中文化，导致菜单栏成为唯一的中英混杂区域。同时工具栏 `ID_TOOL_FIND` 按钮标签为「查找」，与菜单中的「查找最佳代理」语义不一致，用户难以判断二者差别。

## 2. 需求

1. **菜单栏所有英文文案改为中文**：顶层菜单标题与全部叶子菜单项。
2. **工具栏 `ID_TOOL_FIND` 按钮标签由「查找」改为「最佳代理」**。
3. 加速键（Accelerator）与菜单命令 ID **必须保持不变**。
4. 助记符（`&`）必须保留，Alt+助记键导航功能不受影响。

## 3. 改动清单

### 3.1 `src/ui/MainFrame.cpp::initMenuBar()`

仅替换 `Append()` 的显示文本，**结构 / ID / 加速键 / 顺序全部不变**：

| 顶层 | 原英文 | 新中文 | ID | 加速键 |
|---|---|---|---|---|
| `&文件` | `&File` | `&文件` | — | — |
| | Import Subscription URL… | 导入订阅链接… | 6100 | Ctrl+I |
| | Update All Subscriptions | 更新全部订阅 | 6103 | Ctrl+U |
| | Sync Database… | 同步数据库… | 6101 | — |
| | Exit | 退出 | 6102 | Alt+X |
| `&代理` | `&Proxy` | `&代理` | — | — |
| | Find First Working Proxy | 查找首个可用代理 | 6105 | Ctrl+F |
| | Find Best Proxy | 查找最佳代理 | 6106 | Ctrl+Shift+F |
| | Remove Duplicates | 移除重复代理 | 6107 | — |
| | Export Share Links | 导出分享链接 | 6108 | — |
| | Generate Config… | 生成配置… | 6109 | — |
| | 独立代理监控… | *（原本已中文，未改）* | 6114 | Ctrl+M |
| `&任务` | `&任务` | *（原本已中文，未改）* | — | — |
| | 执行自动任务 / 恢复自动任务 / 取消任务 | *（原本已中文，未改）* | 6112/6113/6208 | Ctrl+T |
| `&设置` | `&Settings` | `&设置` | — | — |
| | Configuration… | 配置… | 6110 | Ctrl+, |
| `&帮助` | `&Help` | `&帮助` | — | — |
| | About | 关于 | 6111 | — |

因源码为 UTF-8 且同函数内已有中文宽字面量，全部新文案使用 `L"..."` 宽字面量，与既有风格一致。

### 3.2 `src/ui/MainFrame.cpp::initToolBar()`

- `ID_TOOL_FIND` 按钮标签 `"查找"` → `"最佳代理"`；短帮助（tooltip）保持 `"查找最佳代理"` 不变。
- 详情面板按钮短帮助 `"Toggle Detail Panel"` → `"切换详情面板"`（顺带中文化，非菜单项但同属英文残留）。

### 3.3 明确不改的范围

以下英文**不属于菜单项**，本次刻意保留：

- 对话框标题（`MainFrame.cpp` 的 `"Import Subscription"` / `"Sync Database"` / `"Generate Config"`、`UIApp.cpp` 的 `"Configuration Error"`）。
- 状态栏 `"Ready"`、AUI pane `.Caption("Proxy Details")`、`"Loading data..."`、`"Search: "`。
- 主窗口标题 `"validproxy - Proxy Manager"` —— **多套 UI 测试依赖它**（`TestChainPreview.cpp`、`TestStandaloneFloatingWidget.cpp` 断言 `L"Proxy Manager"`），改动会造成大面积测试回归，收益为零。
- `src/config/ComponentRegistry.cpp` 的 xray 配置分类名、`ConfigFileStore.cpp` / `ConfigJsonParser.cpp` 的异常消息。

## 4. 测试侧改动（`ui-automation-test` 技能铁律 1：选择器唯一合法位置是 `UIIds.h`）

菜单文案进入 UIA 可观测层，等同于「一个可被自动化测试定位的控件标识」，因此必须按规范登记常量，禁止在 `TEST_CASE` 内写字符串字面量。

### 4.1 `tests/ui/UIIds.h`

- 行 7 注释 `(File / Proxy / 任务 / Settings / Help)` → `(文件 / 代理 / 任务 / 设置 / 帮助)`。
- **新增菜单文案常量块**（位于 `ProxyListAddToPoolMenuName` 之后）：
  - 顶层：`MenuFileCaption` / `MenuProxyCaption` / `MenuTaskCaption` / `MenuSettingsCaption` / `MenuHelpCaption`，值为**裸标题**（`L"文件"` 等）。
  - 叶子：`MenuImportSubItem` / `MenuUpdateAllItem` / `MenuSyncDbItem` / `MenuExitItem` / `MenuFindProxyItem` / `MenuFindBestItem` / `MenuDedupItem` / `MenuExportItem` / `MenuGenConfigItem` / `MenuStandaloneMonItem` / `MenuAutoTaskRunItem` / `MenuAutoTaskResumeItem` / `MenuCancelTaskItem` / `MenuConfigItem` / `MenuAboutItem`。
  - 工具栏：`ToolBarFindBestLabel = L"最佳代理"`。
- **不变更**：`MainWindowName`、`SearchCtrlName`（`TestSearch.cpp` 依赖）、`MainWindowClass`、DataView / 日志 / 监控面板等既有常量。

> 关键机制：`readNativeMenu()` 读回的顶层标题**保留** `&` 前缀（即实际为 `"&文件"`），而常量存裸标题；`Menu.h::menuCaptionMatches()` 比较时逐字符 `towlower` 并**跳过 `&`**，故两种形式指向同一条目。中文字符经 `towlower` 原样返回，安全。

### 4.2 `tests/ui/framework/Menu.h`（纯注释中文化，无逻辑改动）

- `MenuEntry::caption` 字段注释、`knownAccelerator()` 的 8 条 case 注释、`menuCaptionMatches()` 的 `&` 语义说明、`clickMenu()` 的用法示例，全部改为中文。逻辑与常量（`kMenuSeparatorId = 65534`、各 case 返回值）**一字未动**。

### 4.3 `tests/ui/TestMenuAction.cpp`

- 菜单结构注释中文化。
- `findMenuEntry(entries, ids::MenuFindProxyItem, &first)` 替换硬编码 `L"Find First Working Proxy"`。
- **新增穷举断言**：`leaves[]`（15 项）+ `tops[]`（5 项）逐个 `CHECK(findMenuEntry(...))`，任一菜单文案漏改即测试失败 —— 这把「英文是否改干净」从人工检查升级为自动化门禁。
- unit 用例改用中文常量：`menuCaptionMatches(ids::MenuFileCaption, L"文件")` / `menuCaptionMatches(L"&文件", ids::MenuFileCaption)` / `menuCaptionMatches(L"&设置", ids::MenuSettingsCaption)` / `CHECK_FALSE(menuCaptionMatches(ids::MenuFileCaption, ids::MenuProxyCaption))`。
- action 用例目标改为 `clickMenu(hwnd, ids::MenuHelpCaption, ids::MenuAboutItem, &trace)`（「帮助 → 关于」仍是最安全的交互目标，弹出的模态框不得被判为错误对话框）。
- 负例 `clickMenu(hwnd, L"NoSuchMenuItem", L"", &trace)` 保持不变（与真实菜单无关的纯负例）。

## 5. 边界与约束

- **禁用 `auto`**：项目 C++17 硬约束，本次新增代码未引入 `auto`。
- **`--dumptree` 未重新执行**：菜单文案由 `readNativeMenu()` 经 `GetMenuStringW` 直接读取 Win32 HMENU，不经过 UIA 文本缓存；且本次是「已知常量 → 已知值」的单向替换（并非猜测控件标识），不违反铁律 1 的前提。
- **助记符有效性**：中文菜单的 `&文件` / `&代理` / `&设置` / `&帮助` 在 wxMSW 下 Alt+字母导航正常，`clickMenuViaKeyboard` 路径保持可用。

## 6. 验证

| 项 | 结果 |
|---|---|
| `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug` + `cmake --build build --parallel 8` | EXIT 0，0 error |
| `.\tests\UITests.exe "[menu]"`（`prepare-ui-sandbox.bat` 重建沙箱后） | EXIT 0 — All tests passed (64 assertions in 4 test cases) |
| `.\scripts\build-and-test.bat` | **EXIT 0 — 100% tests passed, 0 failed out of 12，ALL TESTS PASSED**（95.70s） |
| 全量 `ctest --test-dir build -j 4` | 52/53 通过；唯一失败 `XrayManagerStartupTest`，属技能「Known baseline failures」记录的既有基线失败（需真实就绪的 xray），非本次回归 |

### 6.1 过程中修复的两个**既有 HEAD 链接缺陷**（阻塞交付门禁，最小修复）

首次 Debug 构建在 303/316 处 FAILED，经 `git status --porcelain` + `git diff CMakeLists.txt` 确认本次改动未触及这两个 target，判定为 HEAD 既有缺陷：

1. **`test_config_reader`** 缺 `config::setEchEnabled()` 的实现文件 → 追加 `src/config/StreamSettingsBuilder.cpp`。
2. **`test_region_batch_resolver`** 缺 8 个符号 → 追加 `src/config/{StepToComponentMapper,ConfigGenerator,BundleChainResolver,ComponentRegistry,ComponentBundleDAO,ConflictDetector,ComponentResolver}.cpp`。注意 `config::PipelineConfigGenerator` 定义在 **`src/config/ConfigGenerator.cpp`**，而**不是** `src/ConfigGenerator.cpp`（项目存在两个同名文件）。

修复后首次 `[menu]` 单跑暴露一处**本次引入**的断言错误（`menuCaptionMatches(L"&Settings", ids::MenuSettingsCaption)` 英文残留比对，已改为 `L"&设置"`），修正后全绿。

## 7. 风险

| 风险 | 评估 |
|---|---|
| 改动菜单文案导致 UI 测试选择器失效 | **已消除** —— 全部选择器集中登记于 `UIIds.h`，`TestMenuAction.cpp` 穷举断言强制覆盖 |
| 外部脚本 / 文档按英文菜单名定位 | 全仓 grep 确认无任何代码依赖这些英文串；文档仅作描述用途 |
| 中文菜单在 Alt+助记键下的可达性 | wxMSW 正常支持，且 `&` 保留；`[menu]` 用例实测通过 |