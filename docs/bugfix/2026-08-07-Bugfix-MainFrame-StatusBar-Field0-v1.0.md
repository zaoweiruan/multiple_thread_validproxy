# Bugfix: MainFrame 状态栏 Field0 被菜单帮助文本机制清空且点击关闭不恢复

- **日期**: 2026-08-07
- **模块**: `src/ui/MainFrame.cpp` / `src/ui/MainFrame.h` / `src/ui/AppController.cpp`
- **版本**: v1.0
- **状态**: 已修复并验证

## 1. 问题现象

用户报告：状态栏 Field0（状态消息区）空白，丢失状态信息（如 `Ready`、
`Testing all proxies...` 等）。运行中的 GUI 实例（PID 9656）经原生
`SB_GETTEXTLENGTH` 实测：`parts=4`、`part0=0`（空）、`part1=22`（日志文件名）、
`part3=68`（数据库路径）——只有 Field0 被清空。

## 2. 根因分析

### 2.1 清空机制：菜单/工具栏帮助文本（DoGiveHelp）

wxWidgets 的菜单帮助文本机制在用户打开菜单时把 Field0 写成菜单项的 help 字符串
（无 help 则为空串，即清空）：

1. 点击菜单栏 → `WM_MENUSELECT` → `wxEVT_MENU_HIGHLIGHT`（window.cpp L2462）
   → `wxFrameBase::OnMenuHighlight`（framecmn.cpp L434-446）→
   `DoGiveHelp(menuItem->GetHelp(), true)`；本应用菜单项均未设置 help → **help 为空
   → Field0 被清空**（实测打开菜单瞬间 part0 5→0）。
2. 鼠标悬停工具栏按钮 → `wxToolBarBase::OnMouseEnter`（tbarbase.cpp L736-759）→
   `DoGiveHelp(tool->GetLongHelp(), true)`；本应用工具栏按钮未设 long help →
   同样清空 Field0。

### 2.2 恢复失效：点击关闭菜单路径

`DoGiveHelp` 恢复分支（framecmn.cpp L614-634）本应在菜单关闭时把 Field0 恢复为
菜单打开前的文本（`m_oldStatusText`）：

- **ESC 关闭菜单**：实测恢复成功（part0 回到 5）。
- **点击空白/点击菜单项关闭**：实测**不恢复**（part0 保持 0，直到应用再次显式写入）。

恢复链路为 `WM_UNINITMENUPOPUP`（window.cpp L3772-3774）→
`HandleMenuPopup(wxEVT_MENU_CLOSE, hMenu)` → `MSWFindMenuFromHMENU(hMenu)`。
`MSWFindMenuFromHMENU`（window.cpp L2503-2508）**仅匹配 `wxCurrentPopupMenu`
（右键弹出菜单）**，菜单栏下拉菜单返回 `nullptr`。`wxFrame::DoSendMenuOpenCloseEvent`
（frame.cpp L427-449）虽在 `menu==nullptr` 时仍发送 `wxEVT_MENU_CLOSE`，但实测
点击关闭路径 Field0 未被恢复（与 wxWidgets 3.2.5/MinGW 下的菜单跟踪状态有关）。

### 2.3 应用层排除

- 全部 Field0 写入点均为非空文本（46 处 `StatusUpdateEvent` 发送点 + MainFrame
  `STATUS_UPDATE` lambda + 6 处 `SetStatusText` 直接调用），应用层无空串写入。
- `dumpStatusBarGeometry` 诊断输出显示 wxWidgets `m_panes` 层几何/文本完全正常
  （`field0 rect=0,2 259x22 'Ready'`）——问题不在布局，而在原生控件文本被清空。

## 3. 修复方案

### 3.1 禁用菜单/工具栏帮助文本机制（根治）

`wxFrameBase::DoGiveHelp` 自带官方开关：`m_statusBarPane < 0` 时直接返回
（framecmn.cpp L577）。在 `initStatusBar()` 之后调用 `SetStatusBarPane(-1)`，
菜单/工具栏不再触碰 Field0（本应用菜单项无 help，无功能损失）：

```cpp
    Logger::write("[MainFrame] initStatusBar...", LogLevel::DEBUG);
    initStatusBar();
    // Disable the menu/toolbar help-text mechanism (wxFrameBase::DoGiveHelp).
    // Opening a menu with empty help strings would otherwise clear status bar
    // field 0 ("Ready"); on wxMSW, clicking to close the menu does not reliably
    // restore it, leaving the field permanently blank until the app writes again.
    SetStatusBarPane(-1);
    Logger::write("[MainFrame] initAuiManager...", LogLevel::DEBUG);
```

### 3.2 补齐测试启动状态文本

满足用户对 "testing all proxy" 状态信息的期望——测试开始时立即写入 Field0
（此前只有结束时才写 "Test completed"，启动后长耗时批量测试期间 Field0 无反馈）：

```cpp
// AppController::doTestAllProxies 入口（tester.run() 之前）
if (wxHandler) {
    wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "Testing all proxies..."));
}

// AppController::doTestSingleProxy 入口（tester.runWithIndexId(indexId) 之前）
if (wxHandler) {
    wxQueueEvent(wxHandler, new StatusUpdateEvent(0, std::string("Testing proxy ") + indexId + "..."));
}
```

### 3.3 移除诊断代码

删除 `dumpStatusBarGeometry`（MainFrame.h 声明、MainFrame.cpp 实现及三处调用：
`initStatusBar-end` / `after-SendSizeEvent` / `after-loadSettings`），恢复日志清洁。

## 4. 验证

- 重新构建：`cmake --build build --parallel 8` 成功（8/8）。
- 运行新版本（PID 6700）原生 `SB_GETTEXTLENGTH` 实测，菜单全链路 Field0 恒定：

| 操作 | 修复前 | 修复后 |
| :--- | :--- | :--- |
| 初始 | `lens=[5,22,0,61]` | `lens=[5,22,0,61]` |
| 打开任务菜单 | `lens=[0,22,0,61]`（被清空） | `lens=[5,22,0,61]` ✓ |
| 点击空白关闭 | `lens=[0,22,0,61]`（不恢复） | `lens=[5,22,0,61]` ✓ |
| 再次打开菜单 | `lens=[0,22,0,61]` | `lens=[5,22,0,61]` ✓ |
| 点击菜单项执行命令关闭 | `lens=[0,22,0,61]`（不恢复） | `lens=[5,22,0,61]` ✓ |

- 新实例日志无 `dumpStatusBarGeometry` 输出（诊断代码已彻底移除）。

## 5. 相关文件

- `src/ui/MainFrame.cpp`：构造 `SetStatusBarPane(-1)`；移除 3 处
  `dumpStatusBarGeometry` 调用及其实现
- `src/ui/MainFrame.h`：移除 `dumpStatusBarGeometry` 声明
- `src/ui/AppController.cpp`：`doTestAllProxies` / `doTestSingleProxy` 启动时发送
  Field0 状态文本（`StatusUpdateEvent`，经 `wxQueueEvent`）
