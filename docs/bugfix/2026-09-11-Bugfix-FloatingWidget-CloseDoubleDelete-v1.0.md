# [2026-09-11] Bugfix — StandaloneFloatingWidget WM_CLOSE 双删除崩溃

- **文档类型**: Bugfix
- **模块**: `src/ui/StandaloneFloatingWidget` / `src/ui/MainFrame`
- **版本**: v1.0
- **状态**: completed
- **关联**: `2026-09-10-Bugfix-AppShutdown-PoolWatcher-UAF-v1.0.md`（同批崩溃问题，前一份修复后残留的真根因）

## 1. 问题概述

代理池与独立代理监控弹窗（悬浮窗）统一变更后，应用在**启动 ~5 秒后关闭**时稳定崩溃
（c0000005，执行不可执行堆地址）。经 `2026-09-10` 批次的关闭序列加固
（~AppController 先 stopProxyPool、ProcessExitListener 唤醒事件 join）后，
崩溃仍复现，且新 dump 与当次构建 exe 校验和完全匹配，得以精确符号化，
定位到**真根因：悬浮窗收到外部 WM_CLOSE 广播后默认 Destroy() 延迟删除，
随后 ~MainFrame 再次 `delete floatingWidget_` 形成双删除（UAF）**。

## 2. 崩溃事实（dump 证据链）

- 崩溃 dump：`bin/temp/crash_20260911_0924xx~0928xx.dmp`（9 枚，2026-09-11 09:24–09:28，
  UI 自动化测试运行期间产生）。
- dump 内 exe CheckSum `0x0b1c2a85` == 当日 `bin/validproxy.exe`（修复池/看门狗后的构建）→ 符号可信。
- 异常：c0000005，Parameter[0]=8（**执行违例**，DEP），RIP=`0x0000023eeac1e1f0`（堆地址）。
- cdb 栈（`temp/cdb_new.log`）：
  `[堆垃圾] → validproxy+0xb50e2 → validproxy+0xb525c →
  wxAppConsoleBase::DeletePendingObjects+0x97 → ProcessIdle → MainLoop → OnRun → main`。
- addr2line（ImageBase 0x140000000）：
  - `0x1400b50e2` → **`MainFrame::~MainFrame() MainFrame.cpp:572`**
  - `0x1400b525c` → **`MainFrame::~MainFrame() MainFrame.cpp:605`**
  → 崩溃点即 `~MainFrame` 中 `delete floatingWidget_`（:570-573）。
- stderr 工件：10× `DestroyWindow failed with error 0x00000578（无效的窗口句柄）`
  —— 悬浮窗约 10 个子控件的 HWND 在其 C++ 析构时已失效，证明悬浮窗
  在 ~MainFrame 之前已被 MSW 层 DestroyWindow。

## 3. 根因分析

1. UI 测试框架 `AppProcess::terminate()`（`tests/ui/framework/Application.cpp:162-183`）
   向应用 PID 的**所有**顶层窗口 PostMessage `WM_CLOSE`；现实中任务管理器
   "结束任务"、系统关机/注销广播同样向全部顶层窗口发送 WM_CLOSE。
2. 悬浮窗 `StandaloneFloatingWidget` 是顶层 wxFrame（wxFRAME_NO_TASKBAR），
   **未绑定 wxEVT_CLOSE** → 走 wxFrame 默认处理：`Destroy()`（C++ 对象进入
   wxPendingDelete 延迟删除链）+ MSW 立即 `DestroyWindow`。
3. 下一次 idle 时 `DeletePendingObjects` 先删除悬浮窗对象；同一轮
   `~MainFrame`（同样经 Destroy/pending 链删除）随后执行
   `delete floatingWidget_`（MainFrame.cpp:571）→ **二次 delete 已释放对象** →
   vtable 为堆垃圾 → 虚调用跳转到堆地址 → 执行违例。
4. 早期 "池启动后才崩" 的相关性纯属时序：池启动测试用例使存活时间延长到
   ~4-5s，越过 onFirstShow 的 CallAfter（悬浮窗在 startMonitoring :543-547 创建），
   保证悬浮窗已存在；早关的用例在悬浮窗创建前就退出，故未复现。

## 4. 修复方案

**原则：悬浮窗生命周期完全归属 MainFrame（raw delete），自身永不自毁。**

`StandaloneFloatingWidget` 构造函数新增：

```cpp
Bind(wxEVT_CLOSE, &StandaloneFloatingWidget::onClose, this);
```

新增处理函数（不 Skip、不 Destroy、不 Veto）：

```cpp
void StandaloneFloatingWidget::onClose(wxCloseEvent&) {
    // 仅隐藏，保留对象存活；整体退出由主窗口 / ~MainFrame 掌控。
    Show(false);
}
```

- 收到外部 WM_CLOSE 时窗口仅隐藏（定时器随 Show(false) 停止），对象仍由
  MainFrame 持有，杜绝 pending-delete 与 raw delete 双路径。
- 应用自身退出（主窗口 WM_CLOSE → Destroy → ~MainFrame）不变，
  `delete floatingWidget_` 成为唯一删除路径。

## 5. 影响面与风险

- 改动仅限悬浮窗类，新增一个事件绑定与一个私有成员函数；C++17，无 `auto`。
- 行为变化：任务栏/系统广播关闭悬浮窗时，悬浮窗隐藏而非销毁 —— 与其
  "辅助窗口"定位一致（菜单切换/重启应用可再次显示）。
- 无 API/数据结构变化。

## 6. 验证记录（2026-09-11 实测）

| 项目 | 结果 |
|------|------|
| 全量构建 `cmake --build build --parallel 8` | **[7/7] OK，0 error**（仅既有 BLENDFUNCTION 告警；首试 `wxEVT_CLOSE` 编译报错——wx3.3 无该常量——改 `wxEVT_CLOSE_WINDOW` 后通过） |
| 非 UI ctest 全套 | **38/38 PASS**（212.71s）无回归 |
| **UI_CLEAR（本崩溃直接受害者）** | **稳定失败 → PASS（1.07s）** —— 修复前应用在测试找到「清空日志窗口」按钮前即被该崩溃杀死（8s 超时找不到按钮），修复后存活关闭正常 = 修复生效直接证据 |
| UI 自动化当轮 | 6/7：UI_EXITASSERT 仍 92.82s 失败 + 新产生 dump（crash_20260911_10xx，校验和与本修复构建匹配）→ 符号化定位崩溃点迁移至 `TrayIcon.cpp:39`/`~MainFrame:598` = **残留缺陷③托盘 m_win 同族双删除**；UI_EXITASSERT 失败 = **enumProc 把悬浮窗（topmost）误当主窗口**，其 WM_CLOSE 被本修复拦截仅隐藏 → 主窗口收不到关闭（修复前该测试"碰巧通过"：悬浮窗双删崩溃令进程快速死亡被误判为正常退出） |
| 缺陷③修复后终态 | **UI 7/7 ALL TESTS PASSED（72.34s）+ bin/temp/ 10:08 后 0 新 dump**（见 `2026-09-11-Bugfix-TrayIcon-HelperWindow-DoubleDelete-v1.0.md`） |

## 7. 结论

关闭期崩溃的真根因为悬浮窗 WM_CLOSE 默认销毁与 ~MainFrame raw delete 的
双删除；通过拦截 wxEVT_CLOSE（仅隐藏）消除。前批次的
~AppController stopProxyPool 先行、ProcessExitListener 唤醒-join 修复保留，
作为关闭序列的正确加固。
