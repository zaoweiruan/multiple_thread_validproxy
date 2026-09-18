# Bugfix: 悬浮窗菜单项 Check() 触发 wxWidgets IsCheckable 断言 (StandaloneFloatingWidget)

## 日期
2026-08-24

## 现象
本地运行 `.\scripts\build-and-test.bat`，应用**启动即弹出** wxWidgets 调试断言对话框：
```
A debugging check in this application has failed.
assert "IsCheckable()" failed in Check(): only checkable items may be checked
D:/vcpkg/buildtrees/wxwidgets/src/v3.3.3-.../src/msw/menuitem.cpp
```
（Stop / Continue / Don't show this dialog again）
该弹框导致 UI 自动化 `AppFixture` 取不到有效主窗口（`mainWindow().valid() == false`），`UI_MAINWINDOW` / `UI_SEARCH` / `UI_CLEAR` / `UI_FLOATINGWIDGET` 全部在 `REQUIRE(win.valid())` 处失败。

## 根因
`MainFrame::initMenu()` 用默认 kind 创建菜单项：
```cpp
proxyMenu_->Append(ID_MENU_STANDALONE_MON, L"独立代理监控…\tCtrl+M");   // 默认 wxITEM_NORMAL
```
但 `onMenuConfig` 应用配置（initMenu 内紧随的 `proxyMenu_->Check(ID_MENU_STANDALONE_MON, config_.proxy_process_monitor.enabled)`）与 `MainFrame::syncFloatingWidgetControls()`（`proxyMenu_->Check(ID_MENU_STANDALONE_MON, on)`）均对该项调用 `wxMenu::Check()`。wxWidgets 对**不可勾选（wxITEM_NORMAL）** 的菜单项执行 `Check()` 时，在 Debug 构建触发 `assert IsCheckable()`（src/msw/menuitem.cpp）。该断言在 `MainFrame` 构造 → `syncFloatingWidgetControls()` → `Check()` 的启动路径上即触发，故一启动就弹框。

工具栏按钮 `m_toolbar->AddTool(ID_TOOL_STANDALONE_MON, ..., wxITEM_CHECK)` 本就正确，`ToggleTool` 同步无碍，**非**断言来源。

## 修复
创建菜单项时声明为可勾选（仅 `src/ui/MainFrame.cpp` 一行）：
```cpp
proxyMenu_->Append(ID_MENU_STANDALONE_MON, L"独立代理监控…\tCtrl+M", "显示/隐藏独立代理悬浮窗", wxITEM_CHECK);
```

## 验证
- `cmake --build build --target validproxy validproxy-cli UITests` → **0 error**，`bin/validproxy.exe` 链接成功（不再触及 menuitem.cpp 断言路径）。
- 逻辑：菜单项为 `wxITEM_CHECK`，`Check()` 不再断言。
- UI 自动化需在 **有显示器的 Windows 会话** 经 `.\scripts\build-and-test.bat` 验收（`UI_FLOATINGWIDGET` 等用例本 headless 环境无法跑）。

## 影响范围
仅 `MainFrame.cpp` 菜单项创建一处；全仓无其他对 non-checkable 项的 `Check()` 调用。
