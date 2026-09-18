# [2026-09-11] Bugfix — TrayIcon 托盘辅助窗口 m_win 双删除崩溃（含 UI 测试主窗口枚举加固）

- **文档类型**: Bugfix
- **模块**: `src/ui/TrayIcon` / `tests/ui/framework/Application`
- **版本**: v1.0
- **状态**: completed
- **关联**: `2026-09-11-Bugfix-FloatingWidget-CloseDoubleDelete-v1.0.md`（同族双删除问题，前一修复后残留的最后一个崩溃宿主）

## 1. 问题概述

悬浮窗 WM_CLOSE 双删除修复（前一份 bugfix）落地后，UI 自动化套件仍有用例在
夹具 teardown 阶段产生崩溃 dump（10:04–10:08 批次），且 UI_EXITASSERT 稳定失败。
经 dump 符号化定位：**wxTaskBarIcon 内部懒创建的隐藏辅助窗口 m_win
（wxTaskBarIconWindow : public wxFrame）收到 WM_CLOSE 广播后默认 Destroy()
进入 wxPendingDelete，而 ~wxTaskBarIcon 随后仍会 raw delete m_win —— 与悬浮窗
完全同族的双删除 UAF，宿主换成了托盘**。另查明 UI_EXITASSERT 失败为独立缺陷：
测试框架主窗口枚举竞态（把悬浮窗当成主窗口）。

## 2. 崩溃事实（dump 证据链）

- 崩溃 dump：`bin/temp/crash_20260911_100454~100755.dmp`（7 枚，2026-09-11
  10:04–10:08，UI 自动化运行期间；悬浮窗修复已包含在这些运行所用构建中）。
- 异常：c0000005，Parameter[0]=8（执行违例），RIP=堆地址 —— 与前两批完全同型。
- cdb 栈（`temp/cdb_100755.log`）：
  `[堆垃圾] → validproxy+0xf09e4 → validproxy+0xf0a04 → validproxy+0xb51cc →
  validproxy+0xb525c → wxAppConsoleBase::DeletePendingObjects+0x97 →
  ProcessIdle → MainLoop → OnRun → main`。
- addr2line（ImageBase 0x140000000）：
  - `0x1400f09e4` / `0x1400f0a04` → **`TrayIcon::~TrayIcon() TrayIcon.cpp:39`**
    （即 `RemoveIcon()` 调用点，~TrayIcon → ~wxTaskBarIcon 链上）
  - `0x1400b51cc` → `~MainFrame MainFrame.cpp:598`（`delete trayIcon_`）
  - `0x1400b525c` → `~MainFrame MainFrame.cpp:605`（栈帧残留）
  → 真实早链 = DeletePendingObjects 删除 MainFrame → :598 `delete trayIcon_`
  → ~wxTaskBarIcon raw delete m_win；随后 DeletePendingObjects 后续迭代对
  已释放的 pending 对象（m_win 本体或同批对象）虚调用 → 跳转堆垃圾。

## 3. 根因分析

### 3.1 托盘 m_win 双删除（崩溃根因）

1. `wxTaskBarIcon::SetIcon`（wxWidgets src/msw/taskbar.cpp DoSetIcon）懒创建
   **wxTaskBarIconWindow**（`class wxTaskBarIconWindow : public wxFrame`，
   parentless、无标题的顶层隐藏窗口）持有 HWND 供 Shell_NotifyIcon 使用；
   `m_win` 为 wxTaskBarIcon **private** 成员，应用侧无公开 API 可引用。
2. 该窗口只转发托盘消息（gs_msgTaskbar 等），其余消息（含 WM_CLOSE）走
   wxFrame 默认处理：`Destroy()` → C++ 对象进入 wxPendingDelete 延迟链。
3. 任务栏图标关闭路径正常（仅 MainFrame 收 WM_CLOSE）不会触发；但
   **WM_CLOSE 广播**场景（UI 测试 terminate()、任务管理器"结束任务"、
   系统关机/注销）会命中 m_win：
   `~MainFrame:598 delete trayIcon_` → `~wxTaskBarIcon` 中
   `delete m_win`（wx 注释明确"必须 delete 而非 Destroy()"）→ **m_win 已被
   pending 链释放，二次 delete** → vtable 堆垃圾 → 执行违例。
4. 修复悬浮窗后崩溃点"迁移"到本处的观察与此完全吻合：广播按 Z 序命中
   widget（已被拦截）→ m_win → MainFrame，pending 链为 [m_win, MainFrame]，
   任何一轮 idle 处理都会踩中 m_win 双删。

### 3.2 UI 测试主窗口枚举竞态（UI_EXITASSERT 稳定失败根因）

- `AppProcess::waitForMainWindow` 的 `enumProc`（tests/ui/framework/Application.cpp）
  取 **Z 序第一个**可见、有标题（或 ownerless）的顶层窗口。悬浮窗 topmost
  且带标题 `StandaloneFloatingWidget`，常被枚举在 MainFrame 之前 →
  `mainWindow()` 返回悬浮窗 HWND。
- TestExitAssert 向 `mainWindow()` PostMessage WM_CLOSE → 悬浮窗修复后该
  消息被拦截为"仅隐藏" → **主窗口永远收不到关闭** → 应用存活 90s →
  `REQUIRE(exited)` 失败（修复前它"碰巧通过"：悬浮窗被销毁引发双删崩溃，
  进程快速死亡被误判为正常退出）。
- TestStandaloneProxyPool.cpp 内早已自带正确逻辑（findMainWindow：排除
  FloatingWidgetName 与无标题窗口），本修复将该逻辑固化到框架层。

## 4. 修复方案

### 4.1 TrayIcon：差分捕获 m_win 并拦截其 close

**原则：m_win 生命周期完全归属 ~wxTaskBarIcon 的 raw delete，外部关闭请求一律吞掉。**

`TrayIcon` 构造函数中，对全局 `wxTopLevelWindows` 列表做 SetIcon 前后快照差分，
定位本次懒创建的 m_win（唯一可行手段——m_win 无公开访问途径），对其
`Bind(wxEVT_CLOSE_WINDOW, ...)` 一个**无捕获空 lambda**（不 Skip、不 Destroy、
不 Veto）：

```cpp
// SetIcon 前快照
std::vector<wxWindow*> before = /* 遍历 wxTopLevelWindows 收集 */;
SetIcon(icon, "validproxy");              // 懒创建 m_win
// 差分：新增的顶层窗口即 m_win
for (遍历 wxTopLevelWindows) {
    if (std::find(before.begin(), before.end(), win) == before.end()) {
        win->Bind(wxEVT_CLOSE_WINDOW, [](wxCloseEvent& event) {
            // 有意吞掉：m_win 只允许经 ~wxTaskBarIcon raw delete 释放。
        });
    }
}
```

- 无捕获 lambda → 零 UAF 风险；绑定随 m_win 自身的 handler 表销毁。
- 不调用 `event.Skip()` 即不进入默认 Destroy 路径（与悬浮窗修复同一已验证
  语义，UI_CLEAR 已在该模式下通过）。
- 差分可能命中 0 或 1 个新窗口（SetIcon 失败未创建时命中 0，防御性跳过）。

### 4.2 测试框架 enumProc：主窗口标题优先

`enumProc` 改为单遍两阶段判定：

1. 排除无标题窗口（托盘 m_win 等 hidden helper 均无标题）；
2. 排除标题 == `ids::FloatingWidgetName` 的悬浮窗（其 WM_CLOSE 已被应用
   拦截为仅隐藏，向它发关闭等于丢弃）；
3. 记录首个合格候选（保留原"第一个可见有标题顶层"语义）；
4. 若遇到标题精确等于 `ids::MainWindowName`（`validproxy - Proxy Manager`）
   的窗口则立即采用并终止枚举（镜像 TestStandaloneProxyPool::findMainWindow）。

## 5. 影响面与风险

- TrayIcon：构造函数新增快照差分 + 一次 Bind，无 API/成员变化；C++17、
  无 `auto`。用户日常关闭路径（托盘菜单退出 / 主窗口关闭）不受影响——
  m_win 正常场景从不收 WM_CLOSE。
- 测试框架：仅改 enumProc 判定逻辑 + 新增 `UIIds.h`、`<cwchar>` include
  （UITests include 根为 tests/ui，UIIds.h 可直接引用）。
- UIIds.h 既有常量（MainWindowName/FloatingWidgetName）原样复用，无新增。
- 行为变化：任务管理器"结束任务"/系统关机广播下，m_win 不再自毁，由
  ~wxTaskBarIcon 统一释放 —— 消除双删。

## 6. 验证记录（2026-09-11 实测）

| 项目 | 结果 |
|------|------|
| 全量构建 `cmake --build build --parallel 8` | **[9/9] OK，0 error**（TrayIcon.cpp / Application.cpp 编译，validproxy.exe + validproxy-cli.exe + UITests.exe 链接；仅既有 BLENDFUNCTION 告警） |
| 非 UI ctest 全套 | **38/38 PASS**（95.37s）无回归 |
| **UI_EXITASSERT（enumProc 竞态直接受害者）** | **92.82s 超时失败 → 0.96s PASS** —— mainWindow() 现解析到真实 MainFrame，WM_CLOSE 正常走 onClose → 退出 = 修复生效直接证据 |
| UI_CLEAR / UI_MAINWINDOW / UI_SEARCH（waitForMainWindow 依赖方） | 全部维持 PASS（1.07s / 1.05s / 1.45s） |
| UI 自动化全套 | **7/7 ALL TESTS PASSED（72.34s）**，TestStandaloneProxyPool（启动池后关闭）持续绿 |
| bin/temp/ 新 dump | **10:08 后 0 枚**（修复前每轮 UI 套件必产 ~7 枚；10:07:55 的 crash_20260911_100755 为最后一枚） |
| 遗留追踪 | 生产配置点击「启动池」AppHang（缺陷④：evaluatorLoop 递归锁，沙箱三开关 false + 不 wire onMembersChanged 故 UI 套件不触发）→ 已由 `2026-09-11-Bugfix-PoolEvaluator-RecursiveLock-Hang-v1.0.md` 修复，其修复后 UI_POOL 53.80s→10.09s（stop() 真完成、不再被 terminate() 强杀掩盖）佐证本缺陷③期间 UI_POOL 的 53.80s 挂起被误判 PASS |

## 7. 结论

关闭期崩溃的最后一个宿主为托盘 wxTaskBarIcon 的内部辅助窗口 m_win：
WM_CLOSE 广播下的默认延迟销毁与 ~wxTaskBarIcon raw delete 构成双删除，
通过差分捕获 + close 拦截消除；同时修复 UI 测试框架把悬浮窗误判为主窗口
的枚举竞态（UI_EXITASSERT 稳定失败根因）。至此三批次修复（关闭序列加固、
悬浮窗拦截、托盘拦截）完整覆盖统一池监控变更引入的全部关闭期崩溃路径。
