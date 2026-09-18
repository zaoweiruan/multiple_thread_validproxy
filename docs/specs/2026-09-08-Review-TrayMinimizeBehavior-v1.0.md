# 需求评审：监控弹窗双击 / 最小化到托盘 / 托盘图标

- **文档编号**：2026-09-08-Review-TrayMinimizeBehavior-v1.0
- **发布日期**：2026-09-08
- **状态**：评审通过，待实施
- **适用范围**：`src/ui/StandaloneFloatingWidget.cpp`、`src/ui/MainFrame.cpp`、`src/ui/TrayIcon.cpp`

## 1. 需求来源

用户原话（2026-09-08）：

> 修复：1.监控弹窗双击行为，主窗口最大化->最小化间切换 2.主窗口最小化—>托盘 3.托盘图标改为主图标 生成需求评审文档

对应三项行为需求：

| 编号 | 需求 | 模块 |
| :--- | :--- | :--- |
| R1 | 监控弹窗（悬浮窗）双击时，主窗口在 **最大化 ⇄ 最小化** 之间切换 | `StandaloneFloatingWidget` |
| R2 | 主窗口 **最小化 → 托盘**（不进任务栏） | `MainFrame::onIconize` |
| R3 | **托盘图标改为主图标**（当前是 wx 内置询问图标） | `TrayIcon` |

## 2. 现状与差距分析

### 2.1 R1 监控弹窗双击 → 最大化 ⇄ 最小化

**现状**（`StandaloneFloatingWidget.cpp:477-500` `toggleMainFrameMaximize()`）：

```cpp
if (frame->IsMaximized()) {
    frame->Iconize(true);          // 最大化 → 最小化
} else {
    if (frame->IsIconized()) {
        frame->Restore();          // 先取消最小化（wxMSW 对最小化窗口 Maximize 是空操作）
    }
    frame->Maximize(true);         // 窗口化/最小化 → 最大化
}
```

已有 `lastToggleTime_` 200ms 防抖（悬浮窗存在手动 WM_LBUTTONDOWN 检测与系统 DBLCLK 派发两条路径，防抖保证只切一次）。入口三处：背景 `wxEVT_LEFT_DCLICK`、列表 `onListLeftDClick`（`event.Skip()`）、`wxEVT_LIST_ITEM_ACTIVATED`。

**差距（唯一缺口）**：当主窗口处于 **最小化 → 托盘隐藏**（R2 生效，窗口 `Hide()`）时，悬浮窗双击进入 else 分支：
- `frame->IsIconized()` 对已隐藏窗口可能为 `false`（隐藏后最小化状态不被保留/查询返回假）；
- 直接对隐藏窗口 `Maximize(true)` 是 wxMSW 空操作；

→ **结论：主窗口被藏进托盘后，悬浮窗双击无法将其唤回。** 托盘侧 `TrayIcon::onLeftDClick`（`TrayIcon.cpp:45-62`）已正确处理此场景（隐藏 → `Show(true)+Raise()` 再 `Maximize(true)`），悬浮窗侧缺失同样逻辑。

**修复**：else 分支在 `Restore`/`Maximize` 前补充隐藏态恢复：

```cpp
} else {
    if (!frame->IsShown()) {
        frame->Show(true);
        frame->Raise();
    }
    if (frame->IsIconized()) {
        frame->Restore();
    }
    frame->Maximize(true);
}
```

无需求变更：两处恢复路径（托盘双击 / 悬浮窗双击）语义保持一致，均为「隐藏/窗口化/最小化 → 最大化」。

### 2.2 R2 主窗口最小化 → 托盘

**现状**（`MainFrame.cpp:1009-1018` `onIconize`，事件绑定 `MainFrame.cpp:82`）：

```cpp
void MainFrame::onIconize(wxIconizeEvent& event) {
    if (event.IsIconized() && trayIcon_) {
        Hide();                    // 最小化时隐藏到托盘，不进任务栏
        return;                    // 不吃 Skip：任务栏不出现最小化窗口
    }
    event.Skip();
}
```

`initTrayIcon()`（`MainFrame.cpp:900-901`）在 ctor（`:384`）无条件创建 `trayIcon_`；`onClose` 中 `RemoveIcon()` 但不删除对象（对象在 `~MainFrame` 释放，避免弹窗嵌套消息循环挂死）。

**结论：R2 已满足**，无需代码修改。验收仅需验证「最小化 → 窗口消失且托盘图标存活」。恢复路径由 R1（悬浮窗）与托盘双击共同提供。

### 2.3 R3 托盘图标改为主图标

**现状**（`TrayIcon.cpp:21-23` ctor）：

```cpp
wxIcon icon = wxArtProvider::GetIcon(wxART_INFORMATION, wxART_OTHER, wxSize(16, 16));
SetIcon(icon, "validproxy");
```

托盘图标使用 wx 内置 `wxART_INFORMATION`（蓝色问号信息图标），**不是**主图标。

**主图标参照**（`MainFrame.cpp:140-156` ctor 顶部）：

```cpp
#ifdef __WXMSW__
wxIcon appIcon("icon_ico", wxBITMAP_TYPE_ICO_RESOURCE);   // icon.ico → 资源 "icon_ico"
#else
wxIcon appIcon;
#endif
if (appIcon.IsOk()) { SetIcon(appIcon); }
else { /* 回退 wxART_FRAME_ICON */ }
```

**差距**：托盘未加载 `icon_ico` 资源。

**修复**：`TrayIcon::TrayIcon` ctor 与主窗口一致地加载 `"icon_ico"` 资源；失败时回退原 `wxART_INFORMATION`（保持托盘必须可用的底线），避免 `SetIcon` 空图标导致 shell 通知出错。

```cpp
#ifdef __WXMSW__
wxIcon icon("icon_ico", wxBITMAP_TYPE_ICO_RESOURCE);
#else
wxIcon icon;
#endif
if (!icon.IsOk()) {
    icon = wxArtProvider::GetIcon(wxART_INFORMATION, wxART_OTHER, wxSize(16, 16));
}
SetIcon(icon, "validproxy");
```

## 3. 实施计划（3 项改动）

| 文件 | 改动 | 性质 |
| :--- | :--- | :--- |
| `src/ui/StandaloneFloatingWidget.cpp` | `toggleMainFrameMaximize()` else 分支补隐藏态 `Show(true)+Raise()` | 修改 |
| `src/ui/MainFrame.cpp` | 无（R2 已满足，仅文档确认） | 不改 |
| `src/ui/TrayIcon.cpp` | ctor 图标 `wxART_INFORMATION` → 资源 `"icon_ico"`（带回退） | 修改 |

## 4. 验收标准

1. **R1**：主窗口最大化时双击悬浮窗 → 主窗口最小化并隐藏到托盘；窗口隐藏/最小化/窗口化时双击悬浮窗 → 主窗口恢复并最大化。连续快速双击（<200ms）不重复切换。
2. **R2**：主窗口最小化 → 窗口从任务栏消失，托盘图标仍在；托盘双击 / 悬浮窗双击可唤回。
3. **R3**：托盘图标显示为主窗口同款图标（`icon_ico`），不再是 wx 内置问号图标。
4. 全量回归：`ctest --test-dir build -R "UI_"` 保持 7/7 全绿（现有 `FLOATINGWIDGET` 用例覆盖双击切换 + 主窗口状态）。

## 5. 风险与注意

- `ID_TRAY_EXIT = wxID_HIGHEST + 1002` 为 UI 测试稳定常量，**不得改动**。
- 悬浮窗与托盘双击恢复路径语义对齐后，`FLOATINGWIDGET` 用例（`TestStandaloneFloatingWidget`）内双击断言需保持兼容；若用例显式断言「最小化后 Maximize」需复核。
- 托盘图标尺寸：shell 对托盘图标自动缩放，`icon_ico` 资源含多尺寸即可，无需额外 16x16 变体。

## 6. 关联文档

- `docs/design/2026-09-04-Design-TrayIcon-DoubleClickToggle-v1.0.md`（托盘双击切换设计）
- `docs/design/2026-09-03-Design-StandaloneFloatingWidget-DoubleClickToggleMaximize-v1.0.md`（悬浮窗双击设计）
- `docs/specs/2026-08-24-Spec-StandaloneFloatingWidget-v1.3.md`（悬浮窗演进规格）