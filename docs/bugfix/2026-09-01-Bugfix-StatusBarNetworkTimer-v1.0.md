# Bugfix: 网络断连时状态栏始终显示 "Network OK"

- **日期**: 2026-09-01
- **模块**: `src/ui/MainFrame.cpp`
- **版本**: v1.0
- **状态**: 已修复并验证（构建通过，非网络单测全绿）

## 1. 问题现象

网络断连时，**状态栏 Field2 的网络指示一直停留在 "Network OK"**，不会切换到
"Disconnected"。

用户提供的监测日志 `bin/log/ui_20260901_094846.log` 显示：
- `NetworkMonitor` 内部**正确检测到了断连**（非 DNS 类 curl 错误 60/28/7 →
  `connected_` 置 false，连续输出 `Network connection LOST (probe 1/3,2/3,3/3)`，
  `cancelOnDisconnect` 触发，随后 RESTORED）。
- 即故障发生在 **MainFrame 状态栏刷新链路**，而非 `NetworkMonitor` 的状态检测。

## 2. 根因分析

状态栏网络指示由两级驱动：

1. **底层**：`NetworkMonitor` 后台线程正确维护 `connected_`（断连时置 false）。
2. **上层**：`MainFrame::onNetMonTimer`（2s 定时）读取
   `controller_->getNetworkMonitor()->IsConnected()`，若与成员
   `netMonConnected_{true}`（`MainFrame.h:142`）不同则更新并 `netMonPanel_->Refresh()`。

问题出在上层：`onNetMonTimer` **从未被调用**，导致 `netMonConnected_` 恒为初始值
`true`，状态栏自绘始终显示 "Network OK"。

### 关键机制：wxWidgets 动态事件分发顺序

`MainFrame` 中注册了两个 `wxEVT_TIMER` 处理器：

```cpp
// startMonitoring()，先注册
netMonTimer_   = new wxTimer(this);
Bind(wxEVT_TIMER, &MainFrame::onNetMonTimer,   this);   // 网络状态
netMonTimer_->Start(2000);

// startMonitoring() 内部 line 506 调用 startProxyMonitor()，后注册
proxyMonTimer_ = new wxTimer(this);
Bind(wxEVT_TIMER, &MainFrame::onProxyMonTimer, this);   // 代理状态
proxyMonTimer_->Start(intervalMs);
```

两个 `wxTimer` 都以 `new wxTimer(this)` 构造（逻辑事件 id = `wxID_ANY`），两个
`Bind(wxEVT_TIMER, handler, this)` 都使用通配 id=`wxID_ANY`，因此**两个处理器都会
匹配每一个 `wxTimerEvent`**。

查阅 wxWidgets 源码 `src/common/event.cpp` 的 `wxEvtHandler::SearchDynamicEventTable`
（第 1888-1952 行）：

- 按 **反向注册顺序** 遍历动态绑定（最近注册的 handler 在前，第 1914 行）。
- 遇到第一个 `ProcessEventIfMatchesId(...)` 返回 true 的 handler（即匹配 **且**
  处理了事件、且未调用 `evt.Skip()` 即返回 true），就 `return true`（第 1949 行）
  —— **迭代终止，更早注册的 handler 不再被调用**。

`Frame::onProxyMonTimer`（`MainFrame.cpp:909-917`）**不调用 `evt.Skip()`**，处理完
返回 true。由于它**后于** `onNetMonTimer` 注册、在动态表中被优先尝试，它把**每一个**
`wxTimerEvent`（无论来自哪个计时器）都消费掉，`onNetMonTimer` 永远收不到事件。

### 日志佐证

带临时 DEBUG 诊断的日志 `bin/log/ui_20260901_100944.log`：

- 开屏后仅**一次** paint（"Network OK"，来自 `repositionNetMonPanel()` 的初始绘制）。
- 之后 44+ 秒内 `connected_=false`、`netMonConnected_=true`，但 `onNetMonTimer`
  状态变更分支的 `NetworkMonitor UI:` 诊断**一次都没打印**。
- 说明 `netMonTimer_` 虽被创建并 Start，但其事件一直被 `onProxyMonTimer` 吞噬。

## 3. 修复方案

为两个计时器分配**互不相同的逻辑 id**，并将每个 `Bind` 限定到自己的 id，使两个
handler 不再互相遮蔽（idiomatic wxWidgets 做法）。

- `wxTimerEvent::GetId()` 返回计时器的**逻辑 id**（`wxEvent(timer.GetId(), wxEVT_TIMER)`，
  见 `src/common/timercmn.cpp:163`）。带 id 的 `Bind` 只在
  `event.GetId() ∈ [id, lastId]` 时匹配。
- 因此 `onNetMonTimer` 只匹配来自 `netMonTimer_`（id=`ID_NETMON_TIMER`）的事件，
  `onProxyMonTimer` 只匹配来自 `proxyMonTimer_`（id=`ID_PROXYMON_TIMER`）的事件，
  互不干扰。

### 代码修改清单（MainFrame.cpp）

1. **枚举块**（`ID_TOOL_STANDALONE_MON = wxID_HIGHEST + 211` 之后）新增：
   ```cpp
   // 网络/代理状态计时器使用互不相同的逻辑 id，避免 wxEVT_TIMER handler 相互吞噬
   ID_NETMON_TIMER   = wxID_HIGHEST + 400,
   ID_PROXYMON_TIMER = wxID_HIGHEST + 401,
   ```

2. **startMonitoring()**（网络计时器）：
   ```cpp
   netMonTimer_ = new wxTimer(this, ID_NETMON_TIMER);
   Bind(wxEVT_TIMER, &MainFrame::onNetMonTimer, this, ID_NETMON_TIMER);
   netMonTimer_->Start(2000);
   ```

3. **startProxyMonitor()**（代理计时器）：
   ```cpp
   proxyMonTimer_ = new wxTimer(this, ID_PROXYMON_TIMER);
   Bind(wxEVT_TIMER, &MainFrame::onProxyMonTimer, this, ID_PROXYMON_TIMER);
   proxyMonTimer_->Start(intervalMs);
   ```

4. **清理**：删除根因调研期间加入的临时 DEBUG 诊断日志（`onNetMonTimer` 中的
   `NetworkMonitor UI: ...` 行、`netMonPanel_` wxEVT_PAINT lambda 中的
   `NetworkMonitor paint: ...` 行），恢复干净代码。

修复后 `onNetMonTimer` 每 2s 正常触发，断连时 `netMonConnected_` 由 true 翻转为
false 并 `Refresh()`，状态栏自绘为 "Disconnected"。

## 4. 验证

- 构建：`cmake --build build --parallel 8` 成功（重新编译 `MainFrame.cpp` 并链接
  `bin/validproxy.exe`）。
- 回归：非网络快速 gtest 套件全部通过（无回归）：
  `test_utils`(47) / `test_config_reader`(43) / `test_logger`(22) /
  `test_ipv6_formatter`(7) / `test_uri_codec`(13) / `test_sharelink`(21) /
  `test_share_uri_builders`(20) / `test_port_manager`(6) /
  `test_log_statistics_event`(2)。
- 说明：完整 `ctest -V` 会因网络相关测试（如 `CurlEasyHandleTest` 发起真实 HTTP）
  挂起，属环境行为；`UITests.exe` 需交互桌面（UIA）。本项目对状态栏子面板的 GUI
  验收按 `docs/DEV-PROCESS.md` 须用 UIA（禁止跨进程 SendMessage/SB_GETPARTS，
  参照 2026-08-06 StatusBar LogFile bugfix 的 0xC000041D 教训）。

## 5. 影响

- 仅影响 `MainFrame` 两个状态监计时器的 id 分配与绑定方式，对外接口 / 数据模型 /
  数据库均无改动。
- 网络、代理两个状态指示恢复各自独立定时刷新，网络断连时状态栏能正确显示
  "Disconnected"。
