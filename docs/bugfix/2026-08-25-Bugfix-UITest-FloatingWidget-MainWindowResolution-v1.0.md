# Bugfix: UI 回归测试悬浮窗切换失败（mainWindow() 误解析到悬浮窗而非主窗口）

## 日期
2026-08-25

## 现象
新增 GUI 回归用例 `TestStandaloneFloatingWidget`（验证「拖到自由位置 → Ctrl+M 隐藏 → 重新显示后位置保持不变」）出现反常的**隔离失败、整跑多半通过**：
- 隔离运行 `[floatingwidget]`：**稳定 0/2 失败**。
- 完整 `UITests` 套件（9 用例）：**7–8/9 通过，偶发 6/9**。
- 失败均发生在第一次 `toggle off`（`waitForFloating(false)`）——窗体启动可见（通过），但 `WM_COMMAND 6114` 切换隐藏**未生效**。

临时 `[DIAG]` 抓到决定性证据：
```
[DIAG] mainWindow() HWND=... title=StandaloneFloatingWidget      <- 错把悬浮窗当成主窗口
[DIAG]   topwin hwnd=... title='validproxy - Proxy Manager'      <- 真正的 MainFrame
[DIAG] toggleFloating pid=... wasVisible=1 sendResult=1 dtMs=0   <- 0ms 即返回，消息被默认窗口过程当作 no-op
[DIAG] toggleFloating DID NOT FLIP within poll; now visible=1
```

## 根因
测试通过框架 `AppProcess::mainWindow()` 取得「主窗口」HWND，再向其 `PostMessage`/`SendMessage` 发送 `WM_COMMAND 6114`（`ID_MENU_STANDALONE_MON` 切换命令）。但 `mainWindow()` 的实现（tests/ui/framework/Application.cpp:118-133）是：

> 枚举 PID 下**第一个**「可见顶层窗口且（标题非空 或 无 GW_OWNER）」的窗口即返回。

而本次新增的悬浮窗 `StandaloneFloatingWidget` 正是这样一个窗口：
- 它是 `wxFrame(nullptr, ..., wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | ...)` —— **可见顶层窗口**；
- 其 ctor 用 `SetTitle(L"StandaloneFloatingWidget")` 设置了**稳定非空标题**。

因此 `mainWindow()` 在枚举时（顺序不确定）可能返回**悬浮窗自身的 HWND**，而不是承载 `EVT_MENU(ID_MENU_STANDALONE_MON)` 处理器的 `MainFrame`。向悬浮窗发送 `WM_COMMAND 6114`：悬浮窗没有该菜单 ID 的处理器 → 由默认窗口过程处理 → 瞬时返回（`dtMs=0`、`sendResult=1` 仅为默认返回值），**切换命令被静默丢弃**，窗体永远停留在可见态。

这同时解释了「隔离必挂、整跑多半过」：隔离时本进程第一次启动 app，悬浮窗随启动立即出现并常被枚举在前；整跑时受其它用例遗留窗口 / 枚举顺序影响，偶尔能命中真正的 MainFrame，于是通过。

> 附带根因（放大了所有 GUI 测试的抖动）：框架 `AppProcess::terminate()` 原来**只杀主进程**，app 派生的 `xray.exe`/`validproxy-v1.4.9`（受管独立代理进程）子进程残留并跨用例累积，逐渐占满 CPU，使后续 GUI 用例在高负载下更易出现消息处理延迟/竞态。

## 修复
1. **测试侧（根因修复，主）** — `tests/ui/TestStandaloneFloatingWidget.cpp`：
   - 新增 `findMainWindow(DWORD pid)`：枚举 PID 下可见顶层窗口时**排除悬浮窗已知稳定标题** `ids::FloatingWidgetName`（`L"StandaloneFloatingWidget"`）及无标题窗口，稳定返回真正的 `MainFrame`（标题形如 `validproxy - Proxy Manager`）。
   - 两个用例均以 `HWND hMain = findMainWindow(fx.pid());` 取代 `fx.app().mainWindow()`，确保切换命令始终发往 MainFrame。
2. **框架侧（附带加固）** — `tests/ui/framework/Application.cpp`：
   - `AppProcess::terminate()` 在拆除时调用新增的 `killProcessTree(pi_.dwProcessId)`，经 Toolhelp 父进程映射解析出 app 的**全部后代进程**（含 xray/worker 子进程）并 `TerminateProcess`，避免子进程跨用例累积。

> 生产侧修复（本系列前序工作，非本次测试失败根因，仅作背景）：`StandaloneFloatingWidget` ctor 用 `SetTitle(L"StandaloneFloatingWidget")` 提供稳定识别名；`Show()` 仅首次出现时定位（`placed_` 守卫）；`onLeftUp` 改为自由停靠（仅钳制在屏幕内）。`MainFrame::onMenuStandaloneMonitor` 经 `toggleActive()` 切换，无自动重显路径。

## 验证
- 构建：`.\scripts\build-and-test.bat` 路径下 `cmake --build build --target UITests` → **0 error**，`tests/UITests.exe` 链接成功。
- 隔离稳定性：`./tests/UITests.exe [floatingwidget]` **连跑 3 次全绿**（`17 assertions in 2 test cases`）。
- 完整套件稳定性：`./tests/UITests.exe` **连跑 4 次全绿**（`42 assertions in 9 test cases`）。
- 残留校验：每轮用例拆后 `Get-Process` 确认无 `validproxy`/`xray`/`UITests` 残留（`killProcessTree` 正确回收子进程）。
- 临时 `[DIAG]` 输出已从测试源码清除。

## 影响范围
- 仅 `tests/ui/TestStandaloneFloatingWidget.cpp`（新增 `findMainWindow` 并切换用例目标窗口）与 `tests/ui/framework/Application.cpp`（`killProcessTree` 加固）。
- 不触碰任何生产业务代码；`mainWindow()` 框架接口语义未变（仅测试内部改用更精确的定位）。
- 全仓其它对 `AppProcess::mainWindow()` 的调用不受影响（本次仅本测试文件改用 `findMainWindow`）。
