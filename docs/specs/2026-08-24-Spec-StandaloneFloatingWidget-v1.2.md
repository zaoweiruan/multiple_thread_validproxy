updated: 2026-08-24
title: "Spec: StandaloneFloatingWidget 圆形悬浮窗 v1.2"
type: spec
status: completed
module: StandaloneFloatingWidget
version: 1.2
---

# 规格说明：圆形悬浮窗监控独立代理进程（StandaloneFloatingWidget）v1.2

> 继 v1.1（悬浮窗统一接管 StandaloneMonitorDialog）之后，将悬浮窗的**视觉形态与交互**从「矩形折叠条 + 悬停展开列表」升级为**圆形悬浮球（orb）**方案。
> 本次变更**不新增任何配置键**，全部沿用 `proxy_process_monitor.enabled` + `checkIntervalMs`。

## 1. 目标与非目标

| 项目 | 说明 |
|------|------|
| 目标 | ① 悬浮窗为**圆形**；② 背景图为**编译进 exe 的资源**（非运行时文件读取）；③ 支持**拖动**；④ hover 展开 / 离开**延时收起**；⑤ 拖动后**自由停靠**（停在原地，钳制屏幕内）；⑥ 底部**小滑条**控制收起延时 |
| 非目标 | 不改动监控数据源（`getWatchedStandaloneMonitors`）、不新增 config 键、不改变 Ctrl+M/工具栏/菜单入口语义、不改变 LocateProxyEvent 定位链路 |

## 2. 资源加载（背景图编译进资源）

沿用项目既有 `icons.rc` + `ToolbarIcons::loadPngFromResource` 的 RCDATA 模式（与工具栏图标完全一致），**不新建 .rc / 不改动 CMake**：

```
float_monitor_process_png  RCDATA "docs/design/ui/icon/png/float_monitor_process.png"
```

- 加载：`ToolbarIcons::loadPngFromResource(L"float_monitor_process", img)`（内部按 `name + "_png"` 拼资源名，类型 `RT_RCDATA`，`FindResource → wxMemoryInputStream → wxImage`）。
- 运行时无任何磁盘文件依赖；资源缺失时回退为纯色圆 + 文字，不崩溃。

## 3. 形态与状态机

| 状态 | 形态 | 行为 |
|------|------|------|
| **Orb（折叠）** | 直径 `2*radius`（默认 72 DIP）的**圆形**；窗口经**分层窗口 + 品红(255,0,255)色键（`LWA_COLORKEY`）**实现**真正透明**（未绘制/透明处透出桌面，彻底消除灰色客户区背景）；绘制背景 PNG（PNG 透明区域同样透出桌面），居中显示 `独立\n<count>`（白色） | 常驻屏幕边缘；可拖动；鼠标**悬停 `hoverExpandDelayMs_`（默认 500ms）后**才展开 Panel（短暂划过不弹，便于先抓取并拖动） |
| **Panel（展开）** | 深色调面板（填充 `32,36,44`），**无边框（透明笔，视觉最细）**，`wxListCtrl`（IndexId/Host/起始时间/运行时长(分)/监听端口/PID）+ 底部 `wxSlider` | 鼠标离开后启动 `hideDelayMs` 一次性定时器，到期收起为 Orb |

- 切换时保持视觉中心不变（`keepCenter`），避免跳变。
- `SetShape(wxRegion)` 实现圆形 / 圆角裁剪（region 由 magenta 透明键位的 alpha 位图生成，沿用项目既有可靠方案）。
- **实施要点（Gotcha）**：`SetShape()` 在 wxMSW 下要求窗口创建时带 `wxFRAME_SHAPED` 样式，否则运行时触发断言 `assert "HasFlag(0x0010)" failed in SetShape: Shaped windows must be created with the wxFRAME_SHAPED style`。构造函数样式必须为 `wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxBORDER_NONE | wxFRAME_SHAPED`，**不可移除 `wxFRAME_SHAPED`**。
- **透明背景 Gotcha**：wxMSW 下 `SetShape()` 只裁剪窗口**外部**，窗口**客户区内部默认被擦成系统灰色**（`WM_ERASEBKGND`）。要让悬浮窗真正透明（透出桌面），必须：(1) 绑定 `wxEVT_ERASE_BACKGROUND` 空处理（不擦灰）并设 `wxBG_STYLE_PAINT`；(2) 构造函数启用 `WS_EX_LAYERED` 并调用 `SetLayeredWindowAttributes(hwnd, RGB(255,0,255), 0, LWA_COLORKEY)`，用**品红做透明色键**——`onPaint` 中先 `DrawRectangle` 铺满品红，绘制内容（PNG/文字）覆盖其上，品红处即透出桌面。注意 `LWA_COLORKEY`（非 `LWA_ALPHA`）模式下子控件（列表/滑条）仍可正常绘制。(3) 色键透明像素默认 `click-through`，需重写 `MSWWindowProc` 对 `WM_NCHITTEST` 返回 `HTCLIENT`，使整颗悬浮球（含透明区）可拖动/悬停。

## 4. 策略函数（纯函数，TDD 先行，见 `FloatingWidgetPolicy.h` / `test_floating_widget_state.cpp`）

| 函数 | 语义 |
|------|------|
| `clampRadius(r)` | 半径钳制到 [24, 80] |
| `clampToScreen(x,y,w,h,anchor)` | 将窗口左上角钳制在屏幕内（含 margin） |
| `nearestEdge(cx,cy,sw,sh)` | 由窗口中心选最近屏幕边 |
| `dockPosition(edge,w,h,anchor,x,y)` | 计算某边缘停靠后的左上角（窗口在该边居中） |
| `clampHideDelayMs(v)` / `sliderToHideDelay(pos)` / `hideDelayToSlider(ms)` | 收起延时 [300,4000] ms 与滑条 [0,1000] 的线性映射与互逆 |
| `clampHoverExpandDelayMs(v)` | 悬停展开延时钳制到 [200,2000] ms（默认 500） |
| `dragThresholdPx()` | 拖动判定阈值（4px） |

## 5. 交互细节

- **拖动**：仅 Orb 模式捕获左键（`CaptureMouse`），`wxGetMousePosition` 实时 `Move`；松开时 `ReleaseMouse`，**停在原地（钳制在屏幕内，自由停靠）**，不再吸附最近边缘；`dockEdge_` 仅作首次 `Show` 的兜底参考。**首次 `Show` 后才定位（`placed_` 标记），之后重新 `Show`（关闭弹窗 / 开关监控）仍保留用户自由落点，不再回到边缘起始位置。**
- **悬停展开延时**：鼠标进入悬浮球不再立即展开，而是启动一次性 `hoverTimer_`（延时 `hoverExpandDelayMs_`，默认 500ms，钳制 [200,2000]）；延时内离开则取消展开，保持悬浮球稳定，便于先抓取并拖动；延时结束且仍在悬浮球上才展开 Panel。开始拖动时同样取消待展开，避免展开打断拖动。
- **延时收起**：离开 Panel 时 `hideTimer_.Start(hideDelayMs_, ONE_SHOT)`；回调中若指针仍在窗内（`pointerInside()`）则取消收起，避免误收。
- **底部滑条（作用）**：位于展开面板底部，实时调节 `hideDelayMs_`（映射范围 **300–4000 ms**，默认 1500）。它控制「鼠标移出弹窗后，悬浮窗自动收起回圆形球所需等待的延时」——**向左拖 = 鼠标一离开就快速收起；向右拖 = 离开后停留更久再收起**。该值与「鼠标移出自动关闭」行为直接联动。

## 6. 接口兼容性

`StandaloneFloatingWidget` 公共 API 保持不变，MainFrame 无需改动：

```cpp
StandaloneFloatingWidget(cfg, controller, locateTarget);
bool Show(bool show = true) override;   // 内部 reshapes 到 Orb
void applySettings(const config::AppConfig&);
void toggleActive();
void setActive(bool);
bool isActive() const;
```

## 7. 测试策略

- **纯策略单测**：`test_floating_widget_state`（FloatingWidgetStateTest）扩展覆盖 §4 全部函数（已新增 ClampRadius / ClampToScreen / NearestEdge / DockPosition / HideDelay 用例）。
- **UI 自动化**：`tests/ui/TestStandaloneFloatingWidget.cpp`（[floatingwidget]）仍按 `UIIds::FloatingWidgetName` 查找窗口、Ctrl+M 显隐；形态变更不改变窗口 `SetName`，用例无需改动（需 GUI 会话 + xray 路径方可实跑，遵循 `docs/DEV-PROCESS.md` v1.1 规范）。

## 8. 验收

- `ctest -R FloatingWidgetStateTest` 全绿（纯函数，无需 GUI）。
- `cmake --build build` 编译 `validproxy` 0 error（资源嵌入 + 圆形窗口形态）。
- （可选，需 GUI 会话）`.\scripts\build-and-test.bat` 的 UI_FLOATINGWIDGET 仍通过。
- `grep -rn "collapsedLabel_\|wxListCtrl.*collapsedLabel" src/ui/StandaloneFloatingWidget.*` 无残留（旧折叠条标签已移除）。
