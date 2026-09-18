---
title: "feat: 可配置悬浮窗监控独立代理进程 (StandaloneFloatingWidget)"
type: feat
status: draft
date: 2026-08-24
origin: "用户需求：添加可配置悬浮窗，监控独立代理进程，生成方案用于评审"
---

# Spec: StandaloneFloatingWidget — 可配置悬浮窗监控独立代理进程

> ⚠️ **本文档（v1.0）已废弃** — 被 `docs/specs/2026-08-24-Spec-StandaloneFloatingWidget-v1.1.md` 取代（评审裁定：配置并入 `proxy_process_monitor` 复用、悬浮窗替代 `StandaloneMonitorDialog`、鼠标悬停展开）。请勿按本稿实施。
>
> **评审状态（v1.0，历史）**: 📝 draft — 已被 v1.1 取代。

---

## 1. 背景与问题

当前对独立代理进程（standalone proxy process）的监控存在两个入口，但均不满足"常驻轻量可视"的场景：

| 现有入口 | 形态 | 局限 |
|---|---|---|
| `StandaloneMonitorDialog`（`src/ui/StandaloneMonitorDialog.*`） | 860×360 非模态弹窗，2s wxTimer 刷新 | 占屏面积大，需手动 Ctrl+M 打开；不适合"挂机观察" |
| 状态栏 `proxyMonPanel_`（MainFrame.cpp L446-502） | 内嵌指示条，仅显示存活计数数字 | 信息量过少：无 host / 运行时长 / 端口 |

用户诉求：**添加一个可配置的桌面悬浮窗**，常驻显示正在 watch 的独立代理进程实时状态，且显示行为（是否置顶、透明度、刷新周期、空态隐藏、位置等）可通过配置文件定制。

## 2. 目标与非目标

### 2.1 目标 (v1.0)

- G1 新增无边框置顶悬浮窗 `StandaloneFloatingWidget`，实时展示每个受管独立代理进程的 Host、运行时长、SOCKS 端口与总数。
- G2 悬浮窗行为全部可配置：新增 `config.json` 配置段 `floating_widget`（总开关 / 开机自显 / 刷新间隔 / 透明度 / 置顶 / 空态自动隐藏 / 最大行数 / 位置记忆）。
- G3 复用既有数据链 `AppController::getWatchedStandaloneMonitors()`，**零新增业务查询、零 DB schema 变更**。
- G4 行单击 → 定位到 ProxyListPanel 对应行（复用 `LocateProxyEvent` 链路）；「详情」按钮 → 打开既有 `StandaloneMonitorDialog`。
- G5 菜单开关（Proxy 菜单 checkable 项）+ GUI 启动按配置自显；退出时记忆位置回写 config.json。

### 2.2 非目标 (明确排除)

- N1 不修改 `StandaloneMonitorDialog` 与状态栏指示条的现有行为。
- N2 不提供行内"停止进程"按钮（`stopStandaloneProxy` 集成延后至 v1.1，见 D5）。
- N3 不做 ConfigDialog 图形化配置集成（v1.0 以 config.json 为唯一配置源，见 D3；后续版本可补属性页）。
- N4 不支持运行期切换置顶（wxSTAY_ON_TOP 创建时定死，修改后重启生效，见风险 R5）。
- N5 不引入多显示器智能定位（默认锚点仅计算主显示器工作区）。

## 3. 现状盘点（既有资产）

```text
数据链（已存在，直接复用）
AppController::getWatchedStandaloneMonitors()          ← UI 线程安全快照
   ├─ Pass1: standaloneMutex_ 锁内拷贝 standaloneProxies_ (running && managed)
   ├─ Pass2: ProxyRuntimeHistoryDAO::getInProgressSessions() 联结 pid/startedAt
   ├─ ProfileitemDAO::getByIndexId() 逐行填 host（watched ≤ 个位数）
   └─ 返回 std::vector<StandaloneMonitorRow>{indexId, host, startedAt, durationMs, socksPort, pid}

事件链（已存在，直接复用）
LocateProxyEvent (src/ui/Events.h:325)                  ← 弹窗双击定位同款
   └─ MainFrame 已 Bind → proxyPanel_->selectProxyByIndexId

UI 容器（已存在）
MainFrame::monitorDialog_                               ← 详情弹窗，懒创建复用

配置体系（已存在，扩展点）
include/config/sections/XxxConfigParser.h               ← header-only 分段解析器模式
src/config/ConfigJsonSerializer.cpp                     ← 全量写回序列化器
src/ConfigReader.cpp:79                                 ← 解析器注册点
```

## 4. 总体设计

### 4.1 架构与数据流

```text
┌─────────────────────────────────────────────────────────────────┐
│ MainFrame                                                       │
│  ├─ Proxy 菜单 [✓]悬浮监控窗 Ctrl+Shift+M  (ID_MENU_FLOATING_WIDGET)
│  ├─ onMenuFloatingWidget()  → 懒创建/Show/Hide + 菜单勾选同步     │
│  ├─ startMonitoring()       → show_on_startup 自显钩子           │
│  ├─ onClose()               → 位置回写 saveConfig                │
│  └─ ~MainFrame()            → delete floatingWidget_（无父窗口） │
└──────────────┬──────────────────────────────────────────────────┘
               │ 构造注入: AppController* / locateTarget(MainFrame) /
               │           openDetails 回调(lambda→onMenuStandaloneMonitor)
               ▼
┌─────────────────────────────────────────────────────────────────┐
│ StandaloneFloatingWidget : wxFrame (顶层, 无父窗口)              │
│  样式: wxFRAME_NO_TASKBAR | wxBORDER_SIMPLE [| wxSTAY_ON_TOP]    │
│  wxTimer(refresh_interval_ms)                                    │
│    └─ onRefreshTimer → controller_->getWatchedStandaloneMonitors │
│                        → rows_ 缓存 → Refresh()                  │
│  全客户区自绘 (EVT_PAINT):                                        │
│    ● 独立代理 N        [详情] [×]      ← 头行(绿点=N>0/灰点=0)   │
│    host-a.com         12:34   10808   ← 数据行(单击定位+隐藏)    │
│    host-b.xyz      1:05:07   10809   ← 超出 max_rows 显示 "+M"   │
│    （空态）无运行中的独立代理                                     │
│  交互: EVT_LEFT_DOWN 头行空白区 → WM_NCLBUTTONDOWN 拖拽           │
│        EVT_RIGHT_UP → 上下文菜单[打开详情/重置位置/关闭]          │
│  状态机: visibleByUser_ × autoHidden_ × userClosed_ (见 5.3)     │
└─────────────────────────────────────────────────────────────────┘
```

### 4.2 文件结构（职责划分）

| 文件 | 操作 | 职责 |
|---|---|---|
| `include/FloatingWidgetPolicy.h` | 新建 | **纯 std 无 wx 依赖**的钳制函数与可见性决策函数（可单测） |
| `tests/test_floating_widget_state.cpp` | 新建 | Policy 单元测试（GTest） |
| `include/ConfigReader.h` | 修改 | AppConfig 增加 `floating_widget` 嵌套结构体 |
| `include/config/sections/FloatingWidgetConfigParser.h` | 新建 | header-only 分段解析器（含类型 WARN + 数值钳制） |
| `src/ConfigReader.cpp` | 修改 | 注册解析器（L79 附近追加一行） |
| `src/config/ConfigJsonSerializer.cpp` | 修改 | 序列化写回 `floating_widget` 段 |
| `src/ui/StandaloneFloatingWidget.h/.cpp` | 新建 | 悬浮窗本体（wxFrame 子类） |
| `src/ui/MainFrame.h/.cpp` | 修改 | 菜单项 / 成员 / 生命周期 / 启动自显 / 退出位置回写 |
| `tests/ui/UIIds.h` | 修改 | 固化悬浮窗 UIA 定位常量 |
| `CMakeLists.txt` | 修改 | 注册 test_floating_widget_state 目标 |

## 5. 详细设计

### 5.1 配置模型

**config.json 新增段**（缺省整段 → 全部取默认值，向后兼容旧配置文件）：

```json
"floating_widget": {
    "enabled": true,
    "show_on_startup": false,
    "refresh_interval_ms": 2000,
    "opacity_percent": 85,
    "always_on_top": true,
    "hide_when_empty": true,
    "max_rows": 5,
    "pos_x": -1,
    "pos_y": -1
}
```

**字段语义与钳制规则**：

| 字段 | 类型 | 默认 | 钳制 | 语义 |
|---|---|---|---|---|
| `enabled` | bool | `true` | — | 功能总开关；false 时菜单项禁用、启动不显示 |
| `show_on_startup` | bool | `false` | — | GUI 启动后自动显示 |
| `refresh_interval_ms` | int | `2000` | `[500, 60000]` | 数据刷新周期（钳制下限防 UI 线程 IO 过载，见 R1） |
| `opacity_percent` | int | `85` | `[10, 100]` | 窗口透明度；100 时跳过 SetTransparent 调用 |
| `always_on_top` | bool | `true` | — | 置顶；创建时生效，改后重启生效（N4） |
| `hide_when_empty` | bool | `true` | — | 无运行进程时自动隐藏，出现进程时恢复（见 D4） |
| `max_rows` | int | `5` | `[1, 20]` | 数据行上限，超出部分折叠为 "+M more" |
| `pos_x` / `pos_y` | int | `-1` | `≥ -1` | 上次窗口位置；`-1,-1` = 主显示器工作区右下角锚点 |

**AppConfig 结构体**（`include/ConfigReader.h`，置于 `proxy_process_monitor` 之后，遵循既有 in-class initializer 风格，禁用 auto）：

```cpp
// Floating monitor widget configuration (standalone proxy floating window)
struct {
    bool enabled = true;
    bool show_on_startup = false;
    int refresh_interval_ms = 2000;   // clamp [500, 60000]
    int opacity_percent = 85;         // clamp [10, 100]
    bool always_on_top = true;
    bool hide_when_empty = true;
    int max_rows = 5;                 // clamp [1, 20]
    int pos_x = -1;                   // -1 = anchor at primary work area bottom-right
    int pos_y = -1;
} floating_widget;
```

**分段解析器**（新建 `include/config/sections/FloatingWidgetConfigParser.h`，逐字镜像 `ProxyProcessMonitorConfigParser.h` 的判定/WARN/钳制三段式模式）：

```cpp
#ifndef CONFIG_SECTIONS_FLOATING_WIDGET_H
#define CONFIG_SECTIONS_FLOATING_WIDGET_H

#include <boost/json.hpp>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class FloatingWidgetConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config);
};

inline int clampInt(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

inline void FloatingWidgetConfigParser::parse(const boost::json::value& root, AppConfig& config) {
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();

    if (obj.contains("floating_widget") && obj.at("floating_widget").is_object()) {
        const boost::json::object& fw = obj.at("floating_widget").as_object();

        if (fw.contains("enabled") && fw.at("enabled").is_bool()) {
            config.floating_widget.enabled = fw.at("enabled").as_bool();
        } else if (fw.contains("enabled")) {
            Logger::write("WARNING: config.floating_widget.enabled has wrong type (expected bool), using default", LogLevel::WARN);
        }

        if (fw.contains("show_on_startup") && fw.at("show_on_startup").is_bool()) {
            config.floating_widget.show_on_startup = fw.at("show_on_startup").as_bool();
        } else if (fw.contains("show_on_startup")) {
            Logger::write("WARNING: config.floating_widget.show_on_startup has wrong type (expected bool), using default", LogLevel::WARN);
        }

        if (fw.contains("refresh_interval_ms") && fw.at("refresh_interval_ms").is_int64()) {
            int v = static_cast<int>(fw.at("refresh_interval_ms").as_int64());
            config.floating_widget.refresh_interval_ms =
                clampInt(v, FloatingWidgetDefaults::kMinIntervalMs, FloatingWidgetDefaults::kMaxIntervalMs);
        } else if (fw.contains("refresh_interval_ms")) {
            Logger::write("WARNING: config.floating_widget.refresh_interval_ms has wrong type (expected int64), using default", LogLevel::WARN);
        }

        // opacity_percent / max_rows / always_on_top / hide_when_empty /
        // pos_x / pos_y 同构，略——实现时逐字段展开（同一模式，共 9 字段）
    } else if (obj.contains("floating_widget")) {
        Logger::write("WARNING: config.floating_widget has wrong type (expected object), using default", LogLevel::WARN);
    }
}

} // namespace config
#endif
```

> 注：钳制上下限常量统一定义在 `include/FloatingWidgetPolicy.h` 的 `FloatingWidgetDefaults` 中（解析器 include 该头），避免魔数散落。

**注册**（`src/ConfigReader.cpp` L79 后追加一行）：

```cpp
#include "config/sections/FloatingWidgetConfigParser.h"
// ... load() 内：
ProxyProcessMonitorConfigParser().parse(jv, config);
FloatingWidgetConfigParser().parse(jv, config);   // ← 新增
```

**序列化写回**（`src/config/ConfigJsonSerializer.cpp`，`proxy_process_monitor` 块后新增，保证 save()/load() 往返完整）：

```cpp
// floating_widget
boost::json::object fwObj;
fwObj["enabled"] = config.floating_widget.enabled;
fwObj["show_on_startup"] = config.floating_widget.show_on_startup;
fwObj["refresh_interval_ms"] = config.floating_widget.refresh_interval_ms;
fwObj["opacity_percent"] = config.floating_widget.opacity_percent;
fwObj["always_on_top"] = config.floating_widget.always_on_top;
fwObj["hide_when_empty"] = config.floating_widget.hide_when_empty;
fwObj["max_rows"] = config.floating_widget.max_rows;
fwObj["pos_x"] = config.floating_widget.pos_x;
fwObj["pos_y"] = config.floating_widget.pos_y;
root["floating_widget"] = fwObj;
```

### 5.2 策略头（纯逻辑，可单测）

新建 `include/FloatingWidgetPolicy.h` —— **不包含任何 wx 头**，使 GTest 目标无需链接 wxWidgets / comctl32 manifest（规避 2026-08-18 已知 test exe manifest 陷阱）：

```cpp
#ifndef FLOATING_WIDGET_POLICY_H
#define FLOATING_WIDGET_POLICY_H

#include <cstddef>

namespace floatwidget {

struct Defaults {
    static const int kMinIntervalMs = 500;
    static const int kMaxIntervalMs = 60000;
    static const int kMinOpacityPercent = 10;
    static const int kMaxOpacityPercent = 100;
    static const int kMinRows = 1;
    static const int kMaxRows = 20;
};

inline int clampInt(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

inline int clampIntervalMs(int v) { return clampInt(v, Defaults::kMinIntervalMs, Defaults::kMaxIntervalMs); }
inline int clampOpacityPercent(int v) { return clampInt(v, Defaults::kMinOpacityPercent, Defaults::kMaxOpacityPercent); }
inline int clampMaxRows(int v) { return clampInt(v, Defaults::kMinRows, Defaults::kMaxRows); }

// Visibility decision: the widget is shown when the user asked for it AND
// NOT (auto-hide requested AND there is nothing to show). A manual close
// (userClosed_) always wins and is handled by the caller clearing userVisible.
inline bool shouldShow(bool userVisible, bool hideWhenEmpty, std::size_t rowCount) {
    if (!userVisible) return false;
    if (hideWhenEmpty && rowCount == 0) return false;
    return true;
}

} // namespace floatwidget
#endif
```

### 5.3 StandaloneFloatingWidget 类设计

**头文件**（`src/ui/StandaloneFloatingWidget.h`）：

```cpp
#ifndef UI_STANDALONE_FLOATING_WIDGET_H
#define UI_STANDALONE_FLOATING_WIDGET_H

#include <wx/frame.h>
#include <wx/timer.h>
#include <vector>
#include <functional>

#include "AppController.h"      // StandaloneMonitorRow
#include "FloatingWidgetPolicy.h"

// ---------------------------------------------------------------
// StandaloneFloatingWidget — frameless always-on-top desktop widget
// showing a compact live view of watched standalone proxy processes.
// Spec: docs/specs/2026-08-24-Spec-StandaloneFloatingWidget-v1.0.md
//
// Top-level window (no parent): MUST be deleted explicitly by MainFrame.
// Fully custom-painted; row click posts LocateProxyEvent to locateTarget.
// ---------------------------------------------------------------
class StandaloneFloatingWidget : public wxFrame {
public:
    StandaloneFloatingWidget(const config::AppConfig& cfg,
                             AppController* controller,
                             wxEvtHandler* locateTarget,
                             std::function<void()> openDetails);

    // Show/hide together with the refresh timer. Returns wxFrame::Show.
    bool Show(bool show = true) override;

    // Re-read clamped settings (interval/opacity/max_rows) without recreation.
    void applySettings(const config::AppConfig& cfg);

    // Context-menu action: reset position to the default anchor.
    void resetPosition();

    bool isUserVisible() const { return visibleByUser_; }

private:
    void onRefreshTimer(wxTimerEvent& event);
    void onPaint(wxPaintEvent& event);
    void onLeftDown(wxMouseEvent& event);
    void onRightUp(wxContextMenuEvent& event);
    void onCloseWindow(wxCloseEvent& event);

    void refreshRows();                       // fetch snapshot + Refresh()
    void applyVisibility();                   // shouldShow() → Show/Hide
    void anchorDefaultPosition();             // primary work area bottom-right

    // Layout geometry (device px, computed once from FromDIP).
    struct Layout {
        int width;
        int headerHeight;
        int rowHeight;
        int padding;
    };
    Layout layout_;
    wxRect headerDetailsRect_;
    wxRect headerCloseRect_;

    config::AppConfig cfg_;                   // clamped copy of widget fields
    AppController* controller_;
    wxEvtHandler* locateTarget_;
    std::function<void()> openDetails_;
    wxTimer timer_;
    std::vector<StandaloneMonitorRow> rows_;

    // Visibility state machine (see spec §5.3.2):
    bool visibleByUser_{false};   // user intent (menu toggle / startup / auto-restore)
    bool userClosed_{false};      // sticky: closed via [×] until re-enabled by menu

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_STANDALONE_FLOATING_WIDGET_H
```

#### 5.3.1 绘制规格

```text
┌──────────────────────────────────────┐ ← 宽度 FromDIP(280)，wxBORDER_SIMPLE
│ ● 独立代理 2          [详情]  [×]    │ ← 头行高 FromDIP(26)；● 绿(N>0)/灰(N=0)
├──────────────────────────────────────┤
│ host-a.com                12:34 10808│ ← 行高 FromDIP(22)；host 截断省略
│ host-b.xyz             1:05:07 10809 │    时长 h:mm:ss / mm:ss 自适应
│ …（≤ max_rows 行）                    │
│ +3 more…                             │ ← 折叠提示行（超出时）
└──────────────────────────────────────┘
（空态）居中灰字：无运行中的独立代理
```

- 全部尺寸经 `FromDIP()` 换算（R7）；字体 `wxSystemSettings` 默认 UI 字号。
- `durationMs` 格式化辅助：`<60min → "mm:ss"；否则 "h:mm:ss"`（静态方法 `formatDuration`，供绘制调用，逻辑放 Policy 头以便单测）。
- 透明度：构造与 `applySettings` 中 `if (opacity < 100) SetTransparent(clampOpacityPercent(...))`。

#### 5.3.2 可见性状态机

| 输入 | 动作 | 结果状态 |
|---|---|---|
| 菜单勾选 ON | `visibleByUser_=true; userClosed_=false` → applyVisibility | 显示（含定时器启动） |
| 菜单取消 / [×] / 关闭菜单项 | `visibleByUser_=false`（[×] 另置 `userClosed_=true`） | 隐藏（定时器停止） |
| 定时 tick 且 `hide_when_empty && rows.empty()` | 保持 visibleByUser_，applyVisibility 自动 Hide | 隐藏但意图保留 |
| 定时 tick 且行数 >0 且 !userClosed_ | applyVisibility 自动 Show（若此前被自动隐藏） | 恢复显示 |
| `userClosed_==true` 且行数 >0 | **不**自动复活；须菜单重新勾选 | 保持隐藏 |

> 设计要点：区分"用户主动关"与"空态自动藏"，避免进程重启后浮窗违背用户意愿强行弹出；同时保证勾选状态下进程出现即恢复可见。

#### 5.3.3 拖拽与点击命中（MSW）

```cpp
void StandaloneFloatingWidget::onLeftDown(wxMouseEvent& event) {
    const wxPoint pos = event.GetPosition();
    if (headerCloseRect_.Contains(pos) || headerDetailsRect_.Contains(pos)) {
        event.Skip();
        return;
    }
    long hitRow = hitTestRow(pos);                 // -1 = 头行/边框区域
    if (hitRow >= 0) {
        // D2-A（推荐）：单击行 = 定位到列表并隐藏浮窗
        if (locateTarget_ && static_cast<std::size_t>(hitRow) < rows_.size()) {
            wxQueueEvent(locateTarget_,
                         new LocateProxyEvent(rows_[hitRow].indexId));
        }
        Show(false);
        return;
    }
    // 头部空白区拖拽窗口（MSW 原生移动循环）
    ReleaseCapture();
    SendMessageA(reinterpret_cast<HWND>(GetHandle()),
                 WM_NCLBUTTONDOWN, HTCAPTION, 0);
}
```

右键上下文菜单：`打开详情`（触发 `openDetails_`）、`重置位置`（`resetPosition()`：pos=-1/-1 + anchorDefaultPosition + Move）、`关闭`。事件表：

```cpp
wxBEGIN_EVENT_TABLE(StandaloneFloatingWidget, wxFrame)
    EVT_TIMER(wxID_ANY, StandaloneFloatingWidget::onRefreshTimer)
    EVT_PAINT(StandaloneFloatingWidget::onPaint)
    EVT_LEFT_DOWN(StandaloneFloatingWidget::onLeftDown)
    EVT_CONTEXT_MENU(StandaloneFloatingWidget::onRightUp)
    EVT_CLOSE(StandaloneFloatingWidget::onCloseWindow)
wxEND_EVENT_TABLE()
```

### 5.4 MainFrame 集成

| 触点 | 位置 | 变更 |
|---|---|---|
| ID | MainFrame.cpp 枚举 | `ID_MENU_FLOATING_WIDGET = wxID_HIGHEST + 115`（114 已被占用） |
| 菜单 | initMenuBar() Proxy 段 L660 之后 | `proxyMenu->AppendCheckItem(ID_MENU_FLOATING_WIDGET, L"悬浮监控窗\tCtrl+Shift+M");` |
| 成员 | MainFrame.h | `StandaloneFloatingWidget* floatingWidget_{nullptr};` + handler 声明 |
| Handler | 新增 `onMenuFloatingWidget` | 未创建且 cfg.enabled → 懒创建（注入 locateTarget=this、openDetails=lambda 调 onMenuStandaloneMonitor）→ toggle Show + 同步菜单 Check |
| 启动自显 | startMonitoring() 尾部（GUI 就绪路径，与悬垂纳管同源） | `cfg.floating_widget.enabled && show_on_startup` → 模拟菜单勾选 ON |
| 退出回写 | onClose()（RemoveTrayIcon 之后、event.Skip() 之前） | 若浮窗曾显示：`config_.floating_widget.pos_x/y = GetPosition()` → `controller_->saveConfig(config_)`（失败仅 WARN 日志，不打断退出） |
| 析构防护 | ~MainFrame() | `delete floatingWidget_`（顶层无父窗口必须显式释放；onClose 与析构双点清理，指针置空防双重释放） |

懒创建代码骨架：

```cpp
void MainFrame::onMenuFloatingWidget(wxCommandEvent&) {
    const config::AppConfig& cfg = controller_->getConfig();
    if (!cfg.floating_widget.enabled) {
        wxMessageBox(L"悬浮监控窗已在配置中禁用 (floating_widget.enabled=false)",
                     L"悬浮监控窗", wxOK | wxICON_INFORMATION);
        return;
    }
    if (!floatingWidget_) {
        floatingWidget_ = new StandaloneFloatingWidget(
            cfg, controller_, this,
            std::bind(&MainFrame::onMenuStandaloneMonitor, this, std::placeholders::_1));
    }
    bool target = !floatingWidget_->isUserVisible();
    floatingWidget_->Show(target);
    menubar check 同步…
}
```

> v1.0 不做 ConfigDialog OK 后热应用（N3/D3）：配置经手工编辑 config.json + 重启生效；菜单开关即时可用。热应用钩子（`applySettings`）已预留于类接口，Phase 2 接入成本极低。

## 6. 决策点（评审项）

| # | 问题 | 方案 A（推荐） | 方案 B | 影响 |
|---|---|---|---|---|
| D1 | 窗口形态 | **无边框自绘 + WM_NCLBUTTONDOWN 原生拖拽**：外观干净贴合"悬浮窗"预期 | wxTINY_CAPTION_HORIZ 微型标题栏：原生拖拽零 hack 但观感笨重 | R2 |
| D2 | 行点击语义 | **单击即定位并隐藏浮窗**（对齐弹窗双击语义，交互最短路径） | 双击定位（防误触，但多一层操作） | 5.3.3 |
| D3 | 配置入口范围 | **v1.0 仅 config.json**，改动后重启生效；ConfigDialog 属性页列 Phase 2 | v1.0 同步加 ConfigDialog 9 个属性 + 保存热应用 | 工作量 ±40% |
| D4 | `hide_when_empty` 默认值 | **true**（空态自动隐没，进程出现即浮现，符合"环境感知"定位） | false（常驻显示空态占位） | 5.3.2 |
| D5 | 行内停止按钮 | **延后 v1.1**（误触杀进程风险 + 需二次确认 UI） | v1.0 即含 stop 按钮 | N2 |

## 7. 实施计划（TDD 任务分解）

> 执行约束：严格遵循 DEV-PROCESS 7 步流；每任务先测后码；全栈禁用 `auto`；构建命令见 AGENTS.md §4.1。

### U1: 策略头 + 单元测试（RED→GREEN）

**Files**: Create `include/FloatingWidgetPolicy.h`, `tests/test_floating_widget_state.cpp`; Modify `CMakeLists.txt`

- [ ] Step 1 写失败测试（GTest，覆盖：interval/opacity/max_rows 三组钳制边界、shouldShow 四象限真值表、formatDuration 分钟/小时分界）
- [ ] Step 2 CMake 注册（镜像 test_proxy_scorer 模式）：
  ```cmake
  add_executable(test_floating_widget_state tests/test_floating_widget_state.cpp)
  target_include_directories(test_floating_widget_state PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(test_floating_widget_state PRIVATE gtest_main gtest)
  set_target_properties(test_floating_widget_state PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/tests")
  add_test(NAME FloatingWidgetStateTest COMMAND test_floating_widget_state)
  ```
- [ ] Step 3 运行确认 FAIL（头文件不存在）→ Step 4 实现 Policy 头 → Step 5 PASS
- [ ] Step 6 commit: `feat(ui): add floating widget policy helpers with unit tests`

### U2: 配置链路（parser + serializer + 用例）

**Files**: Modify `include/ConfigReader.h`, `src/ConfigReader.cpp`, `src/config/ConfigJsonSerializer.cpp`; Create `include/config/sections/FloatingWidgetConfigParser.h`; Modify `tests/test_config_reader_load.cpp`, `tests/test_config_reader.cpp`

- [ ] Step 1 失败测试先行：
  - `ConfigReaderLoadTest.FloatingWidget_DefaultsOnMissingSection`
  - `ConfigReaderLoadTest.FloatingWidget_FullValuesParsed`
  - `ConfigReaderLoadTest.FloatingWidget_ClampsOutOfRangeValues`（interval=100→500；opacity=0→10；max_rows=99→20）
  - `ConfigReaderTest.SaveRoundTrip_FieldCompleteness` 追加 9 字段断言
- [ ] Step 2 实现 AppConfig 结构体 + 解析器 + 注册 + 序列化（§5.1 全文）
- [ ] Step 3 `ctest -R ConfigReaderTest -V` PASS → Step 4 commit: `feat(config): add floating_widget section with parser and serializer`

### U3: 悬浮窗组件实现

**Files**: Create `src/ui/StandaloneFloatingWidget.h/.cpp`; Modify `CMakeLists.txt`（validproxy 目标源列表追加 .cpp）

- [ ] Step 1 按 §5.2/5.3 实现类（布局计算、自绘、拖拽、状态机、定时器、上下文菜单）
- [ ] Step 2 编译验证 `cmake --build build --parallel 8` 0 error（UI 类以编译期验证为主，行为由 U5 UIA 冒烟覆盖）
- [ ] Step 3 commit: `feat(ui): implement StandaloneFloatingWidget frameless monitor`

### U4: MainFrame 集成

**Files**: Modify `src/ui/MainFrame.h/.cpp`

- [ ] Step 1 按 §5.4 六触点接入（ID/菜单/成员/handler/启动自显/退出回写/析构防护）
- [ ] Step 2 手工冒烟：启动 GUI → Ctrl+Shift+M 开合 → 启动一个独立代理 → 观察 2s 内行出现、时长跳动 → 单击行定位 → [×] 关闭后启动新进程不复活 → 重启后位置保持
- [ ] Step 3 commit: `feat(ui): wire floating widget menu, startup hook and position persistence`

### U5: UI 自动化冒烟（DEV-PROCESS v1.1 强制项）

**Files**: Modify `tests/ui/UIIds.h`; Create `tests/ui/TestStandaloneFloatingWidget.cpp`; Modify UITests 注册

- [ ] Step 1 先跑 `--dumptree` 实测悬浮窗 Name/Class 再固化常量（禁猜选择器）：
  ```cpp
  inline const wchar_t* const FloatingWidgetName  = L"独立代理";   // ← 以实测为准
  inline const wchar_t* const FloatingWidgetClass = L"wxWindowNR";
  ```
- [ ] Step 2 用例：菜单激活 → UIA 按名查找窗口存在 → 再次激活 → 窗口消失（沙箱库 test/ui-sandbox，红线：严禁触碰 bin/worker 与 test/guindb.db）
- [ ] Step 3 `.\scripts\build-and-test.bat` 一键验收 ALL TESTS PASSED exit 0
- [ ] Step 4 commit: `test(ui): add floating widget smoke test with pinned UIIds`

### U6: 文档收尾

- [ ] 本文档 status → completed 并记录验证结果；INDEX.md 条目状态更新
- [ ] `docs/plans/project-plans-tracker.md` 登记交付条目
- [ ] commit: `docs(spec): finalize floating widget spec v1.0`

## 8. 测试计划汇总

| 层级 | 载体 | 用例数 | 覆盖点 |
|---|---|---|---|
| 纯逻辑单测 | `test_floating_widget_state`（GTest，无 wx 依赖） | ~8 | 钳制边界、shouldShow 真值表、formatDuration 分界 |
| 配置单测 | `test_config_reader_load` / `test_config_reader` | +4 | 缺省段默认值、全量解析、越界钳制、save/load 往返 |
| UI 自动化 | UITests（Catch2 + UIA） | +1 | 菜单开合 → 窗口出现/消失（Name 定位 + PID 过滤） |
| 手工回归 | GUI 实机 | 清单见 U4-Step2 | 含位置记忆、userClosed_ 粘滞、透明度观感 |

## 9. 风险与缓解

| # | 风险 | 等级 | 缓解 |
|---|---|---|---|
| R1 | UI 线程周期性 DB 读（getInProgressSessions + getByIndexId×N） | 低 | 与既有弹窗完全同模式；watched ≤ 个位数；interval 下限 500ms 钳制；如后续观测卡顿可平移至 getRunningDurationsAsync 同款后台线程模式 |
| R2 | WM_NCLBUTTONDOWN 拖拽技巧在异常 DPI/主题下失效 | 低 | MSW-only 项目；备选降级 D1-B 微型标题栏；拖拽失败不影响其它功能 |
| R3 | 浮窗被拖出屏幕丢失 | 低 | 右键「重置位置」+ 手工改 pos_x/y=-1 兜底 |
| R4 | 顶层无父窗口泄漏/双重释放 | 中 | onClose + ~MainFrame 双点 delete + 指针置空；与 trayIcon_/configDialog_ 既有清理模式一致 |
| R5 | wxSTAY_ON_TOP 运行期切换不可靠 | 低 | v1.0 创建时定死（N4），修改配置重启生效 |
| R6 | saveConfig 全量写回覆盖用户手工编辑 | 低 | 仅退出时位置变化才写一次（与 ConfigDialog 保存同先例）；写入前仅改 pos_x/pos_y 两字段 |
| R7 | DPI 缩放错位 | 低 | 全部几何经 FromDIP；默认锚点用 GetClientArea 实时计算 |
| R8 | 透明分层窗口在远程桌面/精简系统下渲染异常 | 低 | opacity_percent=100 可关闭分层；默认 85 经 SetTransparent MSW 稳定实现 |

## 10. 文件变更列表

| 文件 | 操作 | 预估规模 |
|---|---|---|
| `include/FloatingWidgetPolicy.h` | 新建 | ~45 行 |
| `include/config/sections/FloatingWidgetConfigParser.h` | 新建 | ~110 行 |
| `src/ui/StandaloneFloatingWidget.h` | 新建 | ~90 行 |
| `src/ui/StandaloneFloatingWidget.cpp` | 新建 | ~330 行 |
| `tests/test_floating_widget_state.cpp` | 新建 | ~90 行 |
| `tests/ui/TestStandaloneFloatingWidget.cpp` | 新建 | ~60 行 |
| `include/ConfigReader.h` | 修改 | +12 行 |
| `src/ConfigReader.cpp` | 修改 | +2 行 |
| `src/config/ConfigJsonSerializer.cpp` | 修改 | +13 行 |
| `src/ui/MainFrame.h` | 修改 | +4 行 |
| `src/ui/MainFrame.cpp` | 修改 | +55 行 |
| `CMakeLists.txt` | 修改 | +8 行 |
| `tests/ui/UIIds.h` | 修改 | +4 行 |

## 11. 验证步骤（实施完成后）

```powershell
# 1. Debug 构建
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8

# 2. 全量回归（基线 ctest 32 项 + 新增 FloatingWidgetStateTest/UI 用例）
ctest -V

# 3. 新增单测定点复跑
ctest -R FloatingWidgetStateTest -V
ctest -R ConfigReaderTest -V

# 4. UI 自动化一键验收（UI 变更交付门，DEV-PROCESS v1.1）
.\scripts\build-and-test.bat

# 5. 功能验证（bin/config.json 加 floating_widget 段后实机走查 U4-Step2 清单）
.\build\validproxy.exe
```

**通过标准**: 构建 0 error；ctest 全绿（NetworkMonitorTest 已知环境抖动白名单除外，单独复跑）；build-and-test.bat ALL TESTS PASSED exit 0；手工清单全项通过。
