# Spec — 悬浮监控窗行定位手势改为双击（单击仅选中）

- **日期**: 2026-11-12
- **模块**: `src/ui/StandaloneFloatingWidget.h/.cpp`、`include/FloatingWidgetPolicy.h`、`tests/test_floating_widget_state.cpp`
- **版本**: v1.0

## 1. 背景

用户原话诉求：

> 修改功能：将悬浮弹窗中单击独立代理定位到主界面相关订阅、代理列表窗口功能改为双击执行，单击只是选中当前记录

现状（勘察结论，`src/ui/StandaloneFloatingWidget.cpp`）：

| 手势 | 当前行为 | 代码位置 |
|---|---|---|
| 列表行单击（`wxEVT_LIST_ITEM_SELECTED`） | 直接发 `LocateProxyEvent` → `MainFrame` 定位订阅 + 代理列表，并收起悬浮窗 | `onItemSelected`（:916-929）+ `list_->Bind`（:341） |
| 列表行双击（`wxEVT_LIST_ITEM_ACTIVATED`） | 切换主窗最大化 ⇄ 最小化 | `onItemActivated`（:909-914） |
| 列表任意区域双击（`wxEVT_LEFT_DCLICK`） | 切换主窗最大化 ⇄ 最小化（行双击时与上一条共用 `lastToggleTime_` 防抖） | `onListLeftDClick`（:609-617）+ `list_->Bind`（:344） |
| 悬浮窗背景/Orb 双击 | 切换主窗最大化 ⇄ 最小化 | `onLeftDClick`（:599-607） |
| 右键「定位到代理列表」 | 发 `LocateProxyEvent` | `onMenuLocateProxy`（:815-829） |

问题：单击行即触发定位（同时主窗被最大化、悬浮窗收起），用户无法在列表内「只是点一下看看」，误触成本高。

## 2. 需求

| # | 需求 | 验收锚点 |
|---|---|---|
| R1 | 列表行**单击**只选中当前记录，不定位、不收起悬浮窗 | `list_` 不再绑定 `wxEVT_LIST_ITEM_SELECTED`；行选择为 wxListCtrl 原生行为 |
| R2 | 列表行**双击**执行定位（订阅 + 代理列表） | `onItemActivated` 发 `LocateProxyEvent`；`MainFrame` 既有处理器完成双定位、收起悬浮窗、最大化主窗 |
| R3 | 列表**空白区**双击仍切换主窗最大化 ⇄ 最小化 | `onListLeftDClick` 按 `HitTest` 结果分流：命中行 → 不切换；未命中行 → 切换 |
| R4 | 悬浮窗背景 / Orb 双击语义不变 | `onLeftDClick` 不改 |
| R5 | 右键「定位到代理列表」入口不变 | `onMenuLocateProxy` 不改 |

## 3. 设计方案

### 3.1 纯策略函数（可单测）

`include/FloatingWidgetPolicy.h` 新增：

```cpp
// 双击悬浮窗监控列表：命中数据行 → 定位代理；空白区 → 切换主窗最大化。
enum class ListDoubleClickAction { LocateProxy, ToggleMainFrame };

inline ListDoubleClickAction listDoubleClickAction(long hitRow) {
    return hitRow >= 0 ? ListDoubleClickAction::LocateProxy
                       : ListDoubleClickAction::ToggleMainFrame;
}
```

`hitRow` 取 `wxListCtrl::HitTest` 返回值（未命中为 `wxNOT_FOUND` = -1）。

### 3.2 `StandaloneFloatingWidget` 改动点

1. 删除 `list_->Bind(wxEVT_LIST_ITEM_SELECTED, &StandaloneFloatingWidget::onItemSelected, this)` 及其注释；删除 `onItemSelected` 声明/定义。
2. `onItemActivated(wxListEvent& event)`：取 `event.GetIndex()` 行的 `COL_INDEX_ID` 文本，非空则 `wxQueueEvent(locateTarget_, new LocateProxyEvent(indexId))`（原单击实现整体迁移至此，去掉不再需要的 `controller_` 判空）。
3. `onListLeftDClick(wxMouseEvent& event)`：`const long hitRow = list_->HitTest(event.GetPosition());`，仅当 `FloatingWidgetPolicy::listDoubleClickAction(hitRow) == ToggleMainFrame` 时调用 `toggleMainFrameMaximize()`；仍 `event.Skip()` 让原生控件处理选择/激活。
4. 注释同步（绑定处、`onItemActivated`、`onListLeftDClick`），说明「单击=选中 / 行双击=定位 / 空白双击=最大化」。

事件顺序说明：wxMSW 下双击先由 `wxListCtrl` 处理 `WM_LBUTTONDBLCLK`（产生 `wxEVT_LEFT_DCLICK`），随后 `NM_DBLCLK` 产生 `wxEVT_LIST_ITEM_ACTIVATED`。因此行双击时 `onListLeftDClick` 先命中行并跳过切换，再由 `onItemActivated` 定位；不存在同一次双击既定位又切换最大化的路径。

### 3.3 定位链路不变

`MainFrame` 的 `wxEVT_LOCATE_PROXY` 处理器（`src/ui/MainFrame.cpp:262-295`）已实现：解析所属订阅 → `applySubscriptionFilter` → `selectProxyByIndexId` → `floatingWidget_->collapseToOrb()` → 主窗 `Show/Raise/Restore/Maximize`。本变更只改触发手势，不改链路。

## 4. 输入 / 输出 / 前置条件 / 边界条件

- **输入**：用户在悬浮窗 Panel 列表上的鼠标单击 / 双击（行或空白区）。
- **输出**：行单击 → 仅 `wxListCtrl` 选中态变化；行双击 → `LocateProxyEvent` 投递至 `MainFrame`；空白双击 → 主窗最大化状态切换。
- **前置条件**：`proxy_process_monitor.enabled = true`，悬浮窗已展开为 Panel 模式；`locateTarget_`（主窗）非空。
- **边界条件**：
  - 行 `COL_INDEX_ID` 为空（占位 `-`）→ 双击不投递事件。
  - `locateTarget_ == nullptr` → 双击不投递事件。
  - 双击落在行与行之间的空白、行下方空白 → 归入「空白区」→ 最大化切换。
  - 池成员行（类型列「代理池」）双击与独立行一致，携带真实 indexId 定位。
  - Orb 模式与悬浮窗背景双击语义保持不变。

## 5. 验收标准

1. `test_floating_widget_state` 新增 `ListDoubleClickAction*` 用例 GREEN（行 → `LocateProxy`，空白 → `ToggleMainFrame`）。
2. 构建 0 error（`cmake --build build --parallel 8`）。
3. `ctest --test-dir build -V` 无新增失败（既有环境噪声除外，需给出同签名证据）。
4. UI 套件 `UI_FLOATINGWIDGET` 通过（`scripts/build-and-test.bat` 或 `ctest -R ^UI_FLOATINGWIDGET$`），确认双击最大化、悬浮窗保位等既有手势未被破坏。
5. 源码复核：`wxEVT_LIST_ITEM_SELECTED` 不再绑定到定位处理器；`onItemSelected` 全仓无残留引用。

## 6. 非目标

- 不改 `MainFrame` 定位处理器、`LocateProxyEvent`、`ProxyListPanel::selectProxyByIndexId`。
- 不改「在线代理测试结果」弹窗（`TestOnlineResultDialog`，其单击定位是独立交互，本次未纳入范围）。
- 不改右键菜单项与「测试在线代理」链路。
- 不改悬浮窗尺寸/位置/悬停展开/自动收起策略。

## 7. 回归测试

- **Google Test**（`tests/test_floating_widget_state.cpp`）：`ListDoubleClickActionOnRowLocatesProxy`、`ListDoubleClickActionOnBlankTogglesMainFrame`（含 `wxNOT_FOUND` 等价 -1 与越界负值）。
- **UI 回归**：`tests/ui/TestStandaloneFloatingWidget.cpp` 既有用例覆盖「双击背景切换最大化」与悬浮窗保位/收起；本变更未新增 UI 断言（列表行内容依赖运行时独立代理进程，沙箱下不可稳定构造）。

## 8. 实施与验证记录（2026-11-12）

| 项 | 命令 | 结果 |
|---|---|---|
| 构建 | `cmake --build build --parallel 8` | EXIT 0；新增代码 0 warning（既有 `-Wmissing-field-initializers` 噪声不变） |
| 纯逻辑单测 | `ctest --test-dir build -R FloatingWidgetStateTest -V` | 1/1 PASSED，15/15 用例（含新增 2 例） |
| UI 套件（单跑） | `ctest --test-dir build -R "^UI_FLOATINGWIDGET$"` | 1/1 PASSED（17.94s / 24.71s 两次） |
| UI 门禁 | `scripts\build-and-test.bat` | 15/16，唯一失败 `#59 UI_FLOATINGWIDGET` 的 `CHECK(hitCount == probeCount)`（`TestStandaloneFloatingWidget.cpp:751`）：25 个采样点仅 5 个命中悬浮球，`childMisses=0` 且 miss 目标为外部 `Windows.UI.Core.CoreWindow` 覆盖 → 既有环境 z-order flake，非本变更路径 |
| 全量回归 | `ctest --test-dir build` | 70/71（245.01s），唯一失败 `#46 XrayManagerStartupTest`（既有环境基线：需真实 xray API 端口就绪） |

源码复核：`wxEVT_LIST_ITEM_SELECTED` 不再出现在 `StandaloneFloatingWidget`；`onItemSelected` 在 `src/ui` 仅剩 `TaskMonitorPanel` / `TestOnlineResultDialog` 等无关模块的同名成员；新增代码 `auto` 0 处。

