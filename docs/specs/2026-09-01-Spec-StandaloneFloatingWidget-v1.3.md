---
title: "Spec: StandaloneFloatingWidget 悬浮球 v1.3 — 缩小 30% + 仅显示进程数量（绿色加粗）"
module: StandaloneFloatingWidget
status: completed
date: 2026-09-01
supersedes: docs/specs/2026-08-24-Spec-StandaloneFloatingWidget-v1.2.md
---

# 规格说明：悬浮球尺寸缩小 30% 且仅显示监控进程数量（v1.3）

> 在 v1.2（圆形悬浮球 + 悬停展开面板）基础上调整悬浮球的**尺寸与折叠态文本**：折叠悬浮球直径缩小 30%，其中**仅**居中显示受管监控进程数量，且改用**绿色加粗**字体显示，不再叠加「独立」等其它文字。

## 1. 目标

| 目标 | 说明 |
| --- | --- |
| ① 尺寸缩小 30% | 折叠悬浮球默认直径由 72 DIP 缩小至约 50 DIP（半径 36 → 25） |
| ② 仅显示数量 | 折叠悬浮球内居中只渲染受管监控进程数量（`rows_.size()`），删除「独立」文字行 |
| ③ 绿色加粗 | 数量字体设为粗体，颜色为绿色（0,200,0） |

## 2. 变更范围

| 文件 | 变更 |
| --- | --- |
| `include/FloatingWidgetPolicy.h` | `CircleDefaults::kDefaultRadius` 由 `36` 改为 `25`（并改为 `constexpr`，避免 `EXPECT_EQ` 取址导致的 odr-use 链接错误） |
| `src/ui/StandaloneFloatingWidget.cpp` | `onPaint` 折叠态文本：`L"独立\n%d"` 白色 → 仅 `L"%d"` 绿色加粗 |
| `src/ui/StandaloneFloatingWidget.h` | 类注释同步更新折叠态描述 |
| `src/ui/icons.rc` | 悬浮球背景资源源文件由 `float_monitor_process.png` 改为 `float_monitor_process_1.png`（资源名 `float_monitor_process_png` 不变） |
| `src/ui/StandaloneFloatingWidget.cpp` | 放弃 `LWA_COLORKEY` 色键透明方案，改用 `UpdateLayeredWindow` + 逐像素 alpha 混合：构造函数仅保留 `WS_EX_LAYERED`，移除 `SetLayeredWindowAttributes`；`loadBackground()` 保留 PNG 原始 alpha；`onPaint` 渲染到 32-bit ARGB `wxBitmap` 后调用新增的 `updateLayeredWindow()` 更新窗口 |
| `src/ui/StandaloneFloatingWidget.h` | 新增 `updateLayeredWindow(const wxBitmap&)` 声明 |
| `src/ui/StandaloneFloatingWidget.cpp` | `buildCircleRegion()` 圆形区域半径由 `d/2 - 1` 改为 `d/2`，使窗口形状与位图绘制区域完全对齐，消除 1px inset 导致的断断续续边框 |
| `src/ui/StandaloneFloatingWidget.h` | 新增 `onContextMenu` / `onMenuExit` 声明 |
| `src/ui/StandaloneFloatingWidget.cpp` | 绑定 `wxEVT_CONTEXT_MENU`，右键弹出菜单含「关闭程序」项，调用 `wxExit()` 退出应用 |
| `tests/test_floating_widget_state.cpp` | 新增 `DefaultRadiusShrunkBy30Percent` 用例 |

## 3. 设计要点

### 3.1 尺寸

- 旧默认半径 36 → 直径 72；新默认半径 25 → 直径约 50（72 × 0.7 ≈ 50）。
- 25 仍在 `clampRadius` 有效区间 `[24, 80]` 内，无需放宽限制。

### 3.2 折叠态渲染（`onPaint`）

```cpp
const int n = static_cast<int>(rows_.size());
wxFont f = GetFont();
f.SetPointSize(wxMax(10, FromDIP(radius_) / 2));
f.SetWeight(wxFONTWEIGHT_BOLD);
dc.SetFont(f);
dc.SetTextForeground(wxColour(0, 200, 0));
dc.DrawLabel(wxString::Format(L"%d", n),
             wxRect(0, 0, sz.x, sz.y),
             wxALIGN_CENTER_HORIZONTAL | wxALIGN_CENTER_VERTICAL);
```

- 仅渲染数量 `n`，不再渲染「独立」及其换行。
- 粗体 `wxFONTWEIGHT_BOLD`；绿色 `wxColour(0,200,0)`。
- 字号在缩小后的球内保持可读（`max(10, radius/2)`，半径 25 时约 12pt）。

### 3.3 悬浮球背景图片与透明机制

- 背景资源源文件由 `docs/design/ui/icon/png/float_monitor_process.png` 更换为同目录下的 `float_monitor_process_1.png`（均 512×512）。
- 沿用既有 RCDATA 资源机制：`icons.rc` 中 `float_monitor_process_png RCDATA "docs/design/ui/icon/png/float_monitor_process_1.png"`，资源名与运行时加载代码（`ToolbarIcons::loadPngFromResource(L"float_monitor_process", img)`）**均不变**，仅替换打包进 exe 的源图片，保持运行时零磁盘依赖。
- **透明机制升级**：放弃 `LWA_COLORKEY` 色键方案，改用 `UpdateLayeredWindow` + 逐像素 alpha 混合：
  - 构造函数仅设置 `WS_EX_LAYERED`，移除 `SetLayeredWindowAttributes`。
  - `loadBackground()` 保留 PNG 原始 alpha 通道，不再做色键对齐。
  - `onPaint` 渲染到 32-bit ARGB `wxBitmap`，由新增的 `updateLayeredWindow()` 调用 Win32 `UpdateLayeredWindow` 更新窗口，彻底消除缩放后半透明抗锯齿边缘形成的紫色轮廓。

### 3.4 圆形区域对齐

`buildCircleRegion()` 圆形区域半径由 `d/2 - 1` 改为 `d/2`，使窗口形状与位图绘制区域完全对齐，消除 1px inset 导致的断断续续边框。

### 3.5 右键菜单（关闭程序）

悬浮球支持右键上下文菜单：

- 绑定 `wxEVT_CONTEXT_MENU`，在悬浮球任意位置右键弹出菜单。
- 菜单仅含一项：「关闭程序」（`wxID_EXIT`）。
- 点击后调用 `wxExit()` 退出整个应用程序，与主窗口菜单栏「退出」行为一致。

### 3.6 悬停展开面板

悬停展开的详情面板（IndexId/Host/时长/端口/PID）**保持不变**。面板模式下关闭 `WS_EX_LAYERED`，恢复 wxWidgets 正常 DC 绘制，使子控件（wxListCtrl / wxSlider）能正常显示；折叠态（Orb）则保持 `WS_EX_LAYERED` + `UpdateLayeredWindow` 逐像素 alpha 渲染。

## 4. 验证

- 构建：`cmake --build build --parallel 8` 0 error（validproxy / validproxy-cli 均链接成功）。
- 单测：`FloatingWidgetStateTest` → 14/14 PASS。
