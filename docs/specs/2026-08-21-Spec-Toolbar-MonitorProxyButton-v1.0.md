# Spec — 工具栏新增「监控代理」按钮（打开独立代理监控对话框）

- **日期**: 2026-08-21
- **模块**: `MainFrame` 工具栏 / `StandaloneMonitorDialog`
- **版本**: v1.0

## 1. 背景
独立代理监控对话框（`StandaloneMonitorDialog`）当前仅能通过菜单「独立代理监控…Ctrl+M」打开。为提升可达性，需要在工具栏增加直接入口按钮。

## 2. 需求
- 工具栏新增按钮，**点击打开 `StandaloneMonitorDialog`**。
- 图标：`docs/design/ui/icon/png/tool_monitoring_proxy_process.png`
  - 经 `src/ui/icons.rc` 嵌入为资源 `tool_monitoring_proxy_process_png`；
  - 由 `ToolbarIcons::load("tool_monitoring_proxy_process")` 解析（与既有工具按钮同一加载链路）。
- 显示文本：**`监控代理`**。
- 位置：**置于「配置」按钮左侧**（即 `ID_TOOL_CONFIG` 之前添加）。

## 3. 设计方案
| 项 | 方案 |
|---|---|
| 工具 ID | `ID_TOOL_STANDALONE_MON = wxID_HIGHEST + 211`（匿名 enum，复用既有区间；211 未被占用） |
| 事件绑定 | 复用既有 `MainFrame::onMenuStandaloneMonitor`（懒创建 + `Show(true)` + `Raise()`），**不新增处理函数** |
| 工具栏添加 | `m_toolbar->AddTool(ID_TOOL_STANDALONE_MON, "监控代理", ToolbarIcons::load("tool_monitoring_proxy_process"), "监控代理");` 插入于 `ID_TOOL_CONFIG` 之前 |
| 资源嵌入 | `src/ui/icons.rc` 增加 `tool_monitoring_proxy_process_png RCDATA "docs/design/ui/icon/png/tool_monitoring_proxy_process.png"`；windres 已配置 `-I${CMAKE_SOURCE_DIR}`，相对路径可解析 |
| 开发期回退 | 复制 png 至 `bin/icons/tool_monitoring_proxy_process.png`（命中 `load()` 的第二回退路径），与既有 `bin/icons/*.png` 一致 |

## 4. 边界与约束
- **图标加载失败回退链**：资源缺失 → `bin/icons/*.png` → `wxArtProvider` 兜底（本名无 stock 映射，会显示 `wxART_MISSING_IMAGE`）。资源成功嵌入后不会出现该情况。
- **约束**：全栈禁用 `auto`（项目 C++17 硬约束）；本变更未引入任何 `auto`。
- **复用而非新增**：直接复用 `onMenuStandaloneMonitor`，避免重复懒创建/隐藏复用逻辑。
- **位置语义**：用户要求「左侧」，即在视觉顺序上位于「配置」之前；工具栏按 `AddTool` 调用顺序从左到右排列，故在 `ID_TOOL_CONFIG` 的 `AddTool` 前插入即可。

## 5. 验证
- 构建：0 error（仅既有无害警告）。
- 功能：GUI 运行后点击「监控代理」按钮弹出独立代理监控对话框；按钮位于「配置」左侧。
- 回归：`ctest` 全绿（UI 改动不新增自动化测试，依赖手工 GUI 验证；既有单测不受影响）。
