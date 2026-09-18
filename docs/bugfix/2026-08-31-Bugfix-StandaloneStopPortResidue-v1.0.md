# Bugfix: 独立代理停止后端口残留 LISTEN 致立即重启报「端口占用 / 顺延端口」

- **日期**: 2026-08-31
- **模块**: `src/ui/AppController.cpp` — `stopStandaloneProxy` / `shutdownStandaloneProxies`
- **状态**: ✅ completed（构建 0 error；ctest UI_PORTCLOSE / UI_POOL / StandaloneProxyPoolTest / ProcessInspectorTest 全绿）
- **关联文档**:
  - `docs/bugfix/2026-08-31-Bugfix-IsPortAvailable-Wildcard-v1.0.md`（端口占用判定本身的逻辑）
  - `docs/specs/2026-08-31-Spec-StandaloneProxyPreStartCleanup-v1.0.md`（启动前清理重复进程）

---

## 一、症状（用户报告）

启动独立代理 → 测试连通性失败 → 关闭该进程 → **立即**启动另一个代理（或重启同一端口代理）时，`ProxyListPanel::onStartProxy` 弹出「端口 `<socks_base_port>` 已被占用，是否使用端口 `<+1>` 开启代理？」对话框，需顺延端口才能继续。

现象特征：**「立刻」重启必现，「稍等几秒」再启动则不报占用**——与时间相关，疑似某种端口释放延迟。

---

## 二、根因

`stopStandaloneProxy`（≈1571-1572）与 `shutdownStandaloneProxies`（≈1591-1592）在终止独立代理进程时：

```cpp
TerminateProcess(hProcess, 1);
CloseHandle(hProcess);   // ← 立即关闭句柄，未等待进程真正退出
```

**`TerminateProcess` 是异步的**：它只是向进程发出终止请求，控制权随即返回；此时进程对象与它的监听套接字尚未被内核回收。在进程真正退出之前，Windows `GetExtendedTcpTable`（`TCP_TABLE_OWNER_PID_ALL`）仍保有该进程的 `dwState=LISTEN` 条目。

而 `Utils::isPortAvailable(port)`（`src/Utils.cpp:239`）= `!isPortOccupiedInTable(port)`，其中 `isPortOccupiedInTable` 把 **`dwState ∈ {LISTEN, TIME_WAIT}`** 判为占用。于是下一次启动在端口检查这一刻枚举到旧 `LISTEN` 行 → 返回「占用」→ 触发顺延提示。

> **澄清**：这不是 TIME_WAIT。监听套接字自身关闭不会产生 TIME_WAIT（已在上轮调查确认）。真正的成因是 **「进程终止残留窗口」**——`TerminateProcess` 到内核真正回收套接字之间，旧进程仍被 OS 视为存活并持有 LISTEN 端口。该窗口极短（实测 <200ms），因此「稍等」即可避开，而「立即」启动会撞上。

---

## 三、修复

在两处 `TerminateProcess` 之后、`CloseHandle` 之前，加入**有界** `WaitForSingleObject`，等待进程真正退出、内核回收监听套接字，再放行关闭：

```cpp
TerminateProcess(hProcess, 1);
// 有界等待：TerminateProcess 异步，需等 OS 回收监听套接字，避免下一代理 isPortAvailable()
// 仍把旧 LISTEN 行判为占用而误报「端口占用 / 顺延端口」。3s 仅最坏上限，正常 <200ms 即返回。
WaitForSingleObject(hProcess, 3000);
CloseHandle(hProcess);
```

- 等待对象为已持有的 `hProcess`，`WaitForSingleObject` 在进程已退出时立即返回，无额外开销。
- 3s 为最坏情况上限（进程异常卡死时避免无限阻塞调用方，多为 UI 线程）；实测健捷终止在 <200ms 内 `WAIT_OBJECT_0` 返回。
- `shutdownStandaloneProxies` 在 `standaloneMutex_` 持锁时调用，等待期间持锁；因正常终止极快，对持锁窗口影响可忽略（若后续需进一步降低 UI 阻塞，可改为启动路径的短轮询退避，但当前阻塞式已足够且最直接）。

---

## 四、验证

新增纯 Win32 UI 测试 `tests/ui/TestStandalonePortClose.cpp`（tag `[portclose]`，注册进 CMake `UI_TEST_SOURCES` + `iphlpapi` 链接 + `add_test(UI_PORTCLOSE [portclose])`）：

- 真实启动 xray（`E:\v2rayN-windows-64\bin\xray\xray.exe`）监听固定端口（10810），建立连接，用与程序**完全一致**的 `TerminateProcess + CloseHandle` 强制杀进程，立即枚举 `GetExtendedTcpTable` 复刻 `Utils::isPortOccupiedInTable` 的判定（LISTEN / TIME_WAIT = 占用）。
- **场景 D（修复验证）**：`TerminateProcess` 后补 `WaitForSingleObject(hProcess, 5000)` → `rows=0 occupied=no; waitRes=exited`——端口立刻可复用，证明本修复直接消除残留。
- **场景 A（未等待）**：`TerminateProcess` 返回瞬间 `rows=2 state=LISTEN`（残留存在）；与修复前行为一致，对照证明根因。

测试结果：`ctest -R UI_PORTCLOSE -V` → **PASS（4.05s，7 断言）**。

回归：`ctest -R "UI_PORTCLOSE|UI_POOL|StandaloneProxyPool|ProcessInspector"` → **4/4 全绿**（含 `UI_POOL` 16.85s 真实 GUI 池启动）。

附带修正：测试中发现的 `bin/config/probe2.json` 与测试配置里 `"listen":"0.0.0.0:10810"` 写法被 xray 26.3.27 拒绝（exit 23 `unable to listen on domain address`），改为 `"listen":"0.0.0.0","port":10810` 拆分（同时解除 `TestStandaloneProxyPool` 的 `xrayCanStart` 跳过）。

---

## 五、影响面 / 风险

- 改动仅限独立代理停止路径的终止收尾，不影响代理实际运行、连通性、日志转发。
- 停止操作最坏多等待 3s（UI 线程）；正常终止 <200ms。
- 与 `exitListener_` 进程退出监听不冲突（后者用于状态回填，本等待仅确保句柄/套接字回收）。

---

## 六、涉及文件

| 文件 | 变更 |
|------|------|
| `src/ui/AppController.cpp` | `stopStandaloneProxy` / `shutdownStandaloneProxies` 的 `TerminateProcess` 后加 `WaitForSingleObject(hProcess, 3000)` |
| `tests/ui/TestStandalonePortClose.cpp` | 新增 `[portclose]` UI 测试（A/B/C/D 场景） |
| `CMakeLists.txt` | `UI_TEST_SOURCES` 注册、`UITests` 链接 `iphlpapi`、`add_test(UI_PORTCLOSE [portclose])` |
| `bin/config/probe2.json` | `listen`/`port` 拆分写法修正 |
