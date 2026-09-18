# 悬浮窗双击切换主界面最大化/还原设计

- **文档编号**: `2026-09-03-Design-StandaloneFloatingWidget-DoubleClickToggleMaximize-v1.0`
- **类型**: Design（功能设计）
- **模块**: `StandaloneFloatingWidget`（进程监控悬浮窗）
- **版本**: v1.0
- **日期**: 2026-09-03
- **状态**: 待实施 → 已实施（v1.0 已实现；v1.1 依实测修订「手动双击检测」方案；v1.2 依再次实测「捕获保持+延迟展开」方案；v1.3 依用户需求修订「仅面板模式双击切换，悬浮球不捕获双击」，回退 v1.1/v1.2 Orb 双击机制；v1.4 依用户需求「列表双击也应切换」将 onItemActivated 由定位改为切换主界面；v1.5 依用户需求「双击列表内任何区域均触发」在 list_ 子控件上直接绑定 wxEVT_LEFT_DCLICK 捕获任意区域双击；v1.6 依用户需求「将双击效果改为最大化、最小化」将切换目标改为「窗口化/最小化→最大化；最大化→最小化」。本文档以 v1.6 为准）
- **关联**:
  - `docs/specs/2026-09-01-Spec-StandaloneFloatingWidget-v1.3.md`
  - `docs/specs/2026-08-24-Spec-StandaloneFloatingWidget-v1.2.md`
  - `docs/bugfix/` 悬浮窗右键菜单 ID 修复（Bind 无 id + wxID_ANY 导致「关闭代理」同时触发「测试在线代理」）

---

## 1. 需求背景

进程监控悬浮窗（`StandaloneFloatingWidget`）以 `wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxBORDER_NONE | wxFRAME_SHAPED` 样式创建，始终置顶显示在主界面之上，用于展示运行中的独立代理进程（host/socks 端口/indexId/pid）。

主界面（`MainFrame`，作为悬浮窗的 `parent`）通常以最大化方式启动（`UIApp.cpp` 中 `frame_->Maximize(true)`）。用户在使用悬浮窗时，希望直接**双击悬浮窗**即可在**主界面最大化 ↔ 还原（窗口化）**之间切换，无需离开悬浮窗去操作主窗口标题栏或任务栏。

## 2. 目标

- 在进程监控悬浮窗上**双击**左键，切换主界面 `MainFrame` 的「最大化 ⇄ 还原」状态。
- 行为约束：
  - **Panel（面板）模式**下双击生效，切换主界面最大化 ⇄ 还原；**Orb（圆点）模式不捕获双击**（v1.3 用户需求：悬浮球状态单击展开、双击不切换）。
  - 双击**悬浮窗背景**（非代理列表行）时触发切换。
  - **不得**破坏既有交互：
    - 单击 Orb 展开为 Panel（`onLeftUp` 中的 `isClick` 逻辑）。
    - 拖拽移动悬浮窗（`onLeftDown`/`onMouseMove` 的 `dragging_` 机制）。
    - 双击代理列表行触发的 `onItemActivated`（列表自身事件，不改主窗口）。
    - 右键菜单（关闭代理 / 测试在线代理 / 退出）。

## 3. 现状分析（关键代码路径）

`src/ui/StandaloneFloatingWidget.cpp`：

- **构造器**（约 204-283 行）：`locateTarget_(parent)`、`parentWindow_(parent)`；`parent` 即 `MainFrame`（见 `MainFrame.cpp` 540/1088 行 `new StandaloneFloatingWidget(config_, controller_, this)`）。
- **鼠标交互**：
  - `Bind(wxEVT_LEFT_DOWN, &StandaloneFloatingWidget::onLeftDown, this)` → 设置 `dragging_=true`、记录 `dragStartPos_`、`CaptureMouse()`。
  - `Bind(wxEVT_LEFT_UP, &StandaloneFloatingWidget::onLeftUp, this)` → 释放捕获；`isClick = (|dx|,|dy| < 5)`；Orb 模式单击展开 Panel。
  - `Bind(wxEVT_MOTION, &StandaloneFloatingWidget::onMouseMove, this)` → 拖拽移动。
- `onContextMenu`：使用 `ID_MENU_CLOSE_PROXY` / `ID_MENU_TEST_ONLINE` 唯一 ID（本次改动的平行历史）。
- `onActivate`：Panel 模式失活 → 折叠回 Orb。

## 4. 设计方案

### 4.1 新增菜单/事件

1. **新增成员函数**（`.h` 声明 + `.cpp` 实现）：
   ```cpp
   void onLeftDClick(wxMouseEvent& event);
   ```

2. **构造器中绑定**（与 `onLeftDown`/`onLeftUp` 并列）：
   ```cpp
   Bind(wxEVT_LEFT_DCLICK, &StandaloneFloatingWidget::onLeftDClick, this);
   ```

### 4.2 处理逻辑

```cpp
void StandaloneFloatingWidget::onLeftDClick(wxMouseEvent& WXUNUSED(event)) {
    // 仅当双击命中悬浮窗背景/Orb 本体（而非代理列表子窗口）时切换主窗口。
    wxFrame* frame = wxDynamicCast(parentWindow_, wxFrame);
    if (frame == nullptr) {
        return;
    }
    if (frame->IsMaximized()) {
        frame->Maximize(false);   // 还原（窗口化）
    } else {
        frame->Maximize(true);    // 最大化
    }
}
```

要点说明：

- `parentWindow_` 指向 `MainFrame`，`wxDynamicCast` 安全下转为 `wxFrame`。`wxDynamicCast` 需要 RTTI，wxWidgets 在 debug 下默认开启；若该界面关闭 RTTI，则退化为直接 `static_cast<wxFrame*>(parentWindow_)`（本项目窗口类均继承自 `wxFrame`，来源确定，安全）。
- 双击事件 `wxEVT_LEFT_DCLICK` 只挂载在悬浮窗 **Frame 本体**上（`Bind` 到 `this`），`list_`（子 `wxListCtrl`）有独立事件处理，不受影响；双击列表行走 `onItemActivated`（定位代理），不切换主窗口。

### 4.3 与既有交互的协调（关键）

### 4.3 与既有交互的协调（关键，v1.1 实测修正）

Windows 双击时序：`WM_LBUTTONDOWN`（第 1 击）→ ... → `WM_LBUTTONDOWN`（第 2 击被系统转换为 `WM_LBUTTONDBLCLK`）。

**实测发现（v1.1 关键修正）**：仅依赖 `onLeftDClick`/`wxEVT_LEFT_DCLICK` 在 **Orb 模式**下**双击无效**。根因：第 1 击在 `onLeftUp` 走 Orb 展开分支 `setMode(Mode::Panel)` → `applyShape` → `SetSize(380×300)` + `Move` + 切换 `WS_EX_LAYERED`，窗口几何尺寸/位置在两次击键之间改变，破坏了 Windows 双击引擎 → 第 2 击仍派发为 `WM_LBUTTONDOWN`（进入 `onLeftDown`/`onLeftUp`）而非 `WM_LBUTTONDBLCLK` → `onLeftDClick` 永不触发。

**修复方案（v1.1）—— 手动双击检测**：不再完全依赖 OS 的 `WM_LBUTTONDBLCLK`。

1. **`onLeftDown`** 开头（mode 分支之前）记录两次按下时刻与屏幕坐标：
   ```cpp
   prevDownTime_ = curDownTime_;  prevDownScreen_ = curDownScreen_;
   curDownTime_ = wxGetLocalTimeMillis();  curDownScreen_ = wxGetMousePosition();
   ```
2. **`onLeftUp`**（计算 `isClick` 后、Orb 展开分支**之前**）插入手动双击判定：
   ```cpp
   if (isClick && isDoubleClick()) {
       toggleMainFrameMaximize();  event.Skip();  return;
   }
   ```
3. **`isDoubleClick()`**：`curDownTime_−prevDownTime_ ≤ ::GetDoubleClickTime()` 且位移在 `GetSystemMetrics(SM_CX/SM_CYDOUBLECLK)/2` 矩形内。
4. **`toggleMainFrameMaximize()`**：`wxDynamicCast(parentWindow_, wxFrame)` → `Maximize(!IsMaximized())`；内置 `lastToggleTime_ < 200ms` 防抖。
5. **`onLeftDClick`** 保留并委托给 `toggleMainFrameMaximize()`（Panel 模式无几何变化，OS 仍派发 `WM_LBUTTONDBLCLK`）。

**无双重触发**：Orb 情景 OS 对第 2 击派发 `WM_LBUTTONDOWN`（仅手动路径触发）；Panel 情景派发 `WM_LBUTTONDBLCLK`（仅 `onLeftDClick` 触发）；残余伪双触发由 200ms 防抖吸收。

**行为约束不变**：单击 Orb 展开为 Panel；拖拽（位移 >5px 不计 click）正常；双击代理列表行仍走 `onItemActivated` 定位，不切换主窗口。

### 4.4 与既有交互的协调（v1.2 二次实测修正）

**再次实测发现（v1.2 关键修正）**：v1.1 手动双击检测在 **Orb 模式**下**仍然无效**。根因不再停留在「OS 是否派发 `WM_LBUTTONDBLCLK`」，而是：

- 第 1 击在 `onLeftUp` 走 Orb 展开分支 `setMode(Mode::Panel)` 把 ~50px 圆点缩放为 **380×300 Panel**，并将子控件 `list_`（`wxListCtrl`，sizer `Add(list_,1,wxEXPAND|wxALL,6)` 铺满面板）显示出来。
- `onLeftUp` 在第 1 击结束时已 `ReleaseMouse()`（`if(GetCapture()==this)ReleaseMouse()`）。
- 于是第 2 击坐标落在**子控件 `list_` 上**（光标仍在 Orb 中心，展开后被 list_ 覆盖）。OS 将鼠标事件路由给光标下最顶层的**子窗口 `list_`**，而**不是 frame** → frame 的 `onLeftDown`/`onLeftUp` 不触发 → `prev/curDownTime_` 永不更新 → `isDoubleClick()` 恒 false → 无切换。

即：**手动双击检测要求第 2 击到达 frame 的 `onLeftDown`，但 list_ 子控件覆盖光标 + 第 1 击已释放捕获使这个前提不成立。**

**修复方案（v1.2）—— 捕获保持 + 延迟展开**：在第 1 击（Orb 单击）后**保持鼠标捕获**并**延迟展开**，在约 `GetDoubleClickTime()` 时间内等待第 2 击：

1. **Header 新增成员**：`wxTimer dClickTimer_;`（在 timer 组声明，消除 -Wreorder）+ `bool awaitingDClick_{false};`。
2. **构造器**：成员初始化列表加 `dClickTimer_(this)`；`onTimer` 按 `&event.GetTimer()` 同源分发新增 `dClickTimer_` 分支。
3. **`onLeftUp` 重构**：
   - `isClick && isDoubleClick()`：第 2 击 → 停 `dClickTimer_`、`awaitingDClick_=false`、`ReleaseMouse()`、`toggleMainFrameMaximize()`，return（**不展开**）。
   - 否则 `mode_==Orb && isClick`：第 1 击 → `awaitingDClick_=true`、保持捕获、启动 `dClickTimer_.Start(::GetDoubleClickTime(), wxTIMER_ONE_SHOT)`，return（**延迟展开**）。
   - 其余（拖拽结束 / Panel 非双击单击）：停计时器、`awaitingDClick_=false`、`ReleaseMouse()`，再走既有停靠/钳制逻辑。
4. **`onTimer` 新增 `dClickTimer_` 分支**：超时未出现第 2 击 → 判定为「单击」：`awaitingDClick_=false`、`ReleaseMouse()`、若 `mode_==Orb && active_ && enabled` 执行 `setMode(Mode::Panel)`（**延迟的单击展开**）。
5. **`Show(false)` else 分支**：一并 `dClickTimer_.Stop()` + `awaitingDClick_=false` + `ReleaseMouse()`，避免隐藏时捕获残留。

**为何有效**：第 1 击保持捕获直至双击超时。期间的任何鼠标输入（含落在 list_ 上的第 2 击）都会被 OS 统一路由给**捕获者 frame**，从而 `onLeftDown` 必定触发、手动双击判中。若确为单击，超时（`GetDoubleClickTime`，默认 ~500ms）后释放捕获并展开 Panel，单击展开行为保持不变。

**无双重触发**：Orb 情景第 2 击以 `WM_LBUTTONDOWN` 到达 frame（仅手动路径触发）；Panel 情景无几何变化、OS 仍派发 `WM_LBUTTONDBLCLK`（仅 `onLeftDClick` 触发）；残余伪双触发由 `lastToggleTime_<200ms` 防抖吸收。

**行为约束不变**：单击 Orb 仍展开 Panel（仅延迟至双击超时）；拖拽（位移 >5px 不计 click）正常；双击代理列表行仍走 `onItemActivated` 定位；右键菜单功能正常。

### 4.5 用户需求修订（v1.3 简化）

**需求变更（用户明确）**：『需求理解错误，简化：只在 Panel 模式中双击切换主界面显示状态，悬浮球状态不捕获双击』。

即**回归 v1.0 原始语义**，彻底移除 v1.1/v1.2 为「Orb 模式支持双击」引入的整套检测机制：

1. **Header 删除成员**：`wxTimer dClickTimer_;`、`bool awaitingDClick_{false};`、`bool isDoubleClick() const;` 声明、手动双击检测成员 `prevDownTime_/curDownTime_/prevDownScreen_/curDownScreen_`。
2. **构造器**：删除成员初始化列表 `dClickTimer_(this)`。
3. **`onLeftUp` 恢复 v1.0 语义**：
   - `mode_ == Orb && isClick` → `ReleaseMouse()` + `setMode(Mode::Panel)`（立即单击展开，不延迟、不等待双击）。
   - 其余（拖拽结束 / Panel 单击）→ `ReleaseMouse()` + 既有停靠/钳制逻辑。
   - 删除 v1.1 的 `if (isClick && isDoubleClick()) toggleMainFrameMaximize()` 分支（第 2 击手动判定）与 v1.2 的延迟展开分支。
4. **`onTimer` 删除 `dClickTimer_` 分支**（保留 `timer_/hideTimer_/hoverTimer_`）。
5. **`onLeftDown` 删除**手动双击时刻/坐标记录块（`prev/curDown*` 滚动）。
6. **`Show(false)` else 分支**：仅保留 `if (GetCapture()==this) ReleaseMouse()`（拖动中途隐藏时释放捕获防残留），删除 `dClickTimer_.Stop()` 与 `awaitingDClick_ = false`。
7. **`isDoubleClick()` 函数定义**整个删除。

**最终语义**：
- **Orb 模式**：单击展开为 Panel；**不捕获双击**（Orb 双击至多表现为一次单击展开，不切换主窗口）。
- **Panel 模式**：双击（OS 派发 `WM_LBUTTONDBLCLK` → `onLeftDClick`）→ `toggleMainFrameMaximize()`（`lastToggleTime_<200ms` 防抖）切换主界面「最大化 ↻ 还原」。
- 拖拽（>5px）正常移动；Panel 列表双击切换主界面见 v1.4 §4.6（v1.3 版本此处为「双击列表行走 `onItemActivated` 定位」，v1.4 起改为切换）。

**为何 Panel 双击有效而无需手动检测**：Panel 模式双击期间窗口无几何变化，OS 正常派发 `WM_LBUTTONDBLCLK`（这正是 v1.0/v1.1/v1.2 都依赖的 `onLeftDClick` 路径），故 Panel 双击切换始终可行。

### 4.6 列表双击亦切换主界面（v1.4 用户需求）

**需求变更（用户明确）**：『只能在 panel 边框双击才有有效，边框太窄了，在 wxListCtrl 双击应该也有效，单击是定位』。

即**仅靠 Panel 背景双击切换太窄**——列表区域占 Panel 主体，边框很窄。用户要求**列表行上也支持双击切换**，同时**单击仍保持定位**：

1. **`onItemSelected`（`wxEVT_LIST_ITEM_SELECTED`，单击行）**：不变，仍为**定位**（`wxQueueEvent(locateTarget_, new LocateProxyEvent(indexId))`）。
2. **`onItemActivated`（`wxEVT_LIST_ITEM_ACTIVATED`，双击行）**：由「定位」**改为 `toggleMainFrameMaximize()`**（切换主界面 最大化 ⇄ 还原）。其形参 `event` 以 `WXUNUSED` 标记，不再解析行 indexId。
3. **`onLeftDClick`（`wxEVT_LEFT_DCLICK`，Panel 背景双击）**：不变，仍为 `toggleMainFrameMaximize()`；注释同步更新，说明「背景双击」与「列表行双击（onItemActivated）」两条路径均切换、共用 `lastToggleTime_` 防抖。

**最终语义（v1.4）**：
- **Orb 模式**：单击展开为 Panel；**不捕获双击**（Orb 双击至多表现为一次单击展开，不切换主窗口）。
- **Panel 背景双击** → `onLeftDClick` → `toggleMainFrameMaximize()`。
- **Panel 列表行双击** → `onItemActivated` → `toggleMainFrameMaximize()`。
- **Panel 列表行单击** → `onItemSelected` → **定位**到主界面代理列表对应行。
- 所有切换路径共用 `lastToggleTime_<200ms` 防抖，避免「列表行双击」与「背景双击」同时触发造成双重切换。

> 说明：单击定位不受影响——`wxListCtrl` 单击选中行即触发 `wxEVT_LIST_ITEM_SELECTED`（`onItemSelected`）定位；双击时单击的选中事件已先行定位。故「单击定位、双击切换」二者语义清晰无冲突。

### 4.7 双击列表内任意区域亦切换（v1.5 用户需求）

**需求变更（用户明确）**：『不应该是双击 Panel 内代理列表行，应该双击列表内任何区域均触发』。

v1.4 的 `onItemActivated`（`wxEVT_LIST_ITEM_ACTIVATED`）仅在双击**数据行**时触发；双击列表的**空白区**（行下方、列间留白）不会产生激活事件 → 无切换。同时 frame 的 `onLeftDClick` 只接收落在 **frame 自身背景**上的双击——`list_` 子控件吞掉了自己边界内所有鼠标双击（行与空白区），不向上传递给 frame。

**修复方案（v1.5）**：直接在 `list_`（`wxListCtrl` 子控件）上 `Bind(wxEVT_LEFT_DCLICK, ...)`，捕获**列表边界内任意区域**（数据行 + 行间/行下方空白）的双击：

```cpp
// 列表内任意区域（含行间/行下方空白）双击 → 切换主界面。wxListCtrl 为
// 子窗口，其边界内（行与空白区）的双击都先到达 list_，故在此直接绑定。
list_->Bind(wxEVT_LEFT_DCLICK, &StandaloneFloatingWidget::onListLeftDClick, this);
```

新增回调：

```cpp
void StandaloneFloatingWidget::onListLeftDClick(wxMouseEvent& event) {
    toggleMainFrameMaximize();
    event.Skip();  // 交由原生 wxListCtrl 继续处理（行选择/激活）
}
```

**最终语义（v1.5）**：
- **Orb 模式**：单击展开为 Panel；**不捕获双击**（Orb 双击至多表现为一次单击展开）。
- **Panel 背景双击** → `onLeftDClick` → `toggleMainFrameMaximize()`。
- **Panel 列表内任意区域双击**（行 + 空白区）→ `onListLeftDClick` → `toggleMainFrameMaximize()`（v1.5 新增）。
- **Panel 列表行双击** → `onItemActivated` → `toggleMainFrameMaximize()`（保留；行双击时与 `onListLeftDClick` 双触发，由防抖吸收只切换一次）。
- **Panel 列表行单击** → `onItemSelected` → 定位到主界面代理列表对应行。
- 所有切换路径共用 `lastToggleTime_<200ms` 防抖。
- **切换目标（v1.6 用户需求 `toggleMainFrameMaximize` 统一实现）**：当前窗口化/最小化 → `Maximize(true)`（最大化）；当前最大化 → `Iconize(true)`（最小化）。`toggleMainFrameMaximize` 名为历史遗留，实际切换「最大化 ⇄ 最小化」。

**为何有效**：`list_` 是悬浮窗的子窗口，列表边界内（含空白区）的任何双击都先派发到 `list_` 控件，故在其上绑定 `wxEVT_LEFT_DCLICK` 可捕获**任意区域**双击；`event.Skip()` 保证原生 `wxListCtrl` 处理（行选择/激活定位）不被破坏。

## 5. 变更清单

| 文件 | 变更 |
|------|------|
| `src/ui/StandaloneFloatingWidget.h` | 新增成员函数声明 `bool isDoubleClick() const;`、`void toggleMainFrameMaximize();`、`void onLeftDClick(wxMouseEvent& event);`；新增手动双击检测成员 `prevDownTime_/curDownTime_/prevDownScreen_/curDownScreen_/lastToggleTime_`；**v1.2 新增** `wxTimer dClickTimer_;`（timer 组声明，消除 -Wreorder）+ `bool awaitingDClick_{false};`。**v1.3 全部移除**这些 Orb 双击机制（`isDoubleClick`/`dClickTimer_`/`awaitingDClick_`/`prevDown*`/`curDown*`），仅保留 `toggleMainFrameMaximize`/`onLeftDClick`/`lastToggleTime_` |
| `src/ui/StandaloneFloatingWidget.cpp` | 构造器 `Bind(wxEVT_LEFT_DCLICK, ...)` + 成员初始化列表加 `dClickTimer_(this)`；`onLeftDown` 记录两次按下时刻/坐标；`onLeftUp` 在 Orb 展开前插入 `if(isClick&&isDoubleClick()) toggleMainFrameMaximize()`；新增 `isDoubleClick`/`toggleMainFrameMaximize`/`onLeftDClick`（统一委托）实现；`#include <wx/utils.h>`（`wxGetLocalTimeMillis`）；**v1.2** `onLeftUp` 重构为捕获保持+延迟展开（第1击保持捕获+启动 dClickTimer_，第2击判中切换，超时释放捕获+展开）；`onTimer` 新增 `dClickTimer_` 分支（超时→释放捕获+Orb→Panel 展开）；`Show(false)` else 分支补 `dClickTimer_.Stop()`+`awaitingDClick_=false`+`ReleaseMouse()`。**v1.3 回退**：删除成员初始化 `dClickTimer_(this)`；`onLeftUp` 恢复 v1.0（Orb 单击→立即 `setMode(Panel)`，Panel 单击→仅 `ReleaseMouse`）；删除 `onLeftUp` 双击判定分支、`onTimer` `dClickTimer_` 分支、`onLeftDown` 时刻/坐标记录、`isDoubleClick()` 定义；`Show(false)` 仅保留 `GetCapture()==this` 时 `ReleaseMouse()`。**v1.4**：`onItemActivated` 由「定位」改为 `toggleMainFrameMaximize()`（`wxListEvent& WXUNUSED(event)`，列表行双击也切换主界面）；`onItemSelected`（单击）保持定位；`onLeftDClick` 注释同步更新（背景与列表行双击均切换、共用 `lastToggleTime_` 防抖）。**v1.5**：`list_` 子控件上新增 `Bind(wxEVT_LEFT_DCLICK, onListLeftDClick)` 捕获列表内任意区域（行 + 空白）双击；Header 声明 `void onListLeftDClick(wxMouseEvent& event);`；实现 `onListLeftDClick { toggleMainFrameMaximize(); event.Skip(); }`；`onLeftDClick` 注释更新（背景双击由 frame、列表内双击由 onListLeftDClick 接管）。**v1.6**：`toggleMainFrameMaximize` 切换目标改为「最大化 ⇄ 最小化」——`frame->IsMaximized() ? Iconize(true)（最小化） : Maximize(true)（窗口化/最小化→最大化）`；`onLeftDClick`/`onListLeftDClick` 注释头「最大化 ⇄ 还原」统一改「最大化 ⇄ 最小化」 |
| `tests/ui/TestStandaloneFloatingWidget.cpp` | 新增 `dblClickWindow(HWND)` 辅助（PostMessageW `WM_LBUTTONDOWN`+`WM_LBUTTONDBLCLK`+`WM_LBUTTONUP`）+ `TEST_CASE("Double-click floating widget toggles main frame maximize/restore","[floatingwidget]")`（记录 `::IsZoomed(hMain)` 双击后翻转断言） |
| `docs/design/2026-09-03-Design-StandaloneFloatingWidget-DoubleClickToggleMaximize-v1.0.md` | 本文档（v1.1 实测修订 / v1.2 二次实测 / v1.3 用户需求简化：仅 Panel 双击，Orb 不捕获双击 / v1.4 用户需求：列表行双击亦切换，单击仍定位 / v1.5 用户需求：双击列表内任意区域亦切换 / v1.6 用户需求：双击改为最大化⇄最小化） |
| `docs/INDEX.md` | 登记本文档至 §5 设计规范 |

## 6. 测试计划

### 6.1 单元/UI 测试（Google Test，`tests/`）

UI 自动化依赖交互式桌面会话（`win.valid()==true`）。在 `build-and-test.bat` 交互式环境下运行：

- **T1 双击 Orb → 主界面最大化状态翻转**：
  - 构造带 `MainFrame` 的悬浮窗；将主窗体初始化为非最大化。
  - 模拟双击（用 `wxMouseEvent` 派发 `EVT_LEFT_DCLICK` 到悬浮窗，或直接调用 `onLeftDClick`，后者更稳健）。
  - 断言 `frame->IsMaximized()==true`。
- **T2 双击还原**：初始最大化 → 双击 → `IsMaximized()==false`。
- **T3 空父窗口/非 Frame 父窗口稳健性**：传入非 `wxFrame` 父窗口 → `parentWindow_` 非 Frame 时函数返回、无异常、不崩溃。

> 说明：`onLeftDClick` 核心逻辑仅有「下转型 + `IsMaximized`/`Maximize` 翻转」，可直接对处理器做确定性断言；真实双击派发依赖有桌面句柄的 UI 测试环境，作为 TM 补充。

### 6.2 手动验证

1. 启动程序，主界面最大化，打开进程监控悬浮窗（Orb）。
2. 单击 Orb → 展开为 Panel；在 Panel 背景**双击** → 主界面还原为窗口化；再次双击 → 最大化。（v1.3：Orb 状态不捕获双击。）
3. 展开为 Panel 后双击面板背景 → 同样切换。
4. 双击面板背景或 Panel 内代理**列表任意区域**（数据行、行间/行下方空白均可）→ 主界面最大化 / **最小化**切换（v1.6：窗口化/最小化 → 最大化；最大化 → 最小化）；**单击**列表行 → 定位到主界面代理列表对应行。
5. 拖拽悬浮窗 → 正常移动，不触发切换。
6. 右键菜单各项 → 功能正常（关闭代理 / 测试在线代理 / 退出）。

## 7. 风险与权衡

- **RTTI 依赖**：`wxDynamicCast` 需要 RTTI。本项目编译为 Debug 且 wxWidgets 调试构建默认开启，风险低。叠加了非 Frame 父窗口的 null 保护，即使下转型失败也只是不响应双击，不会崩溃。
- **Orb 双击的行为**（v1.3）：Orb 不捕获双击，双击至多表现为一次单击展开（展开为 Panel），不切换主窗口。用户可在 Panel 模式双击切换主界面。——若用户后续希望 Orb 双击也切换，需重新引入手动检测（见 §4.3/4.4 失效根因）。
- **列表双击路径未加入门禁**（v1.4/v1.5）：v1.4「列表行双击 → `onItemActivated` → 切换」+ v1.5「列表任意区域双击 → `onListLeftDClick` → 切换」与既有「背景双击 → `onLeftDClick` → 切换」语义一致且共用 `lastToggleTime_` 防抖，无逻辑风险；行双击时 `onListLeftDClick` 与 `onItemActivated` 双触发也只会切换一次（防抖吸收）。因真实 `wxListCtrl` 双击在无交互桌面难稳定驱动，未新增 UI 自动化用例，依赖既有 `onLeftDClick` 端到端测试 + §6.2 手动验证。
- **拖动与双击并存**：位移阈值 5px 天然区分「单击/双击」与「拖拽」，无冲突。
- **最大化 ⇄ 最小化**（v1.6）：wxMSW 上 `Iconize(true)` 最小化主界面；`Maximize(true)` 从最小化状态会取消最小化并最大化。注意：主界面**最小化**时 `wxFrame::IsMaximized()` 返回 false，此时在仍可见置顶的悬浮窗内双击会再次 `Maximize(true)` → 主界面从最小化恢复为最大化——符合本次所选语义「窗口化/最小化→最大化；最大化→最小化」。若用户期望最小化时悬浮窗双击不应再切换，需在此补充最小化状态检测（如 `frame->IsIconized()`），暂未纳入。