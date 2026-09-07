# Bugfix: 独立代理闪崩时 startStandaloneProxy 空等长超时（UI 卡顿 / 延迟失败弹窗）

- 日期: 2026-08-25
- 模块: `AppController::startStandaloneProxy` + `proxy::ConnectivityVerifier::waitForPort`
- 版本: v1.0
- 状态: 已修复 + 单元测试验证（GUI 端到端桌面复现待用户执行）

---

## 1. 问题现象

用户在 GUI 中右键某代理 → 菜单「开启代理」(`ID_CONTEXT_START_PROXY`) 后，若底层 xray / sing-box 进程**启动后立即闪崩**（例如配置非法、端口被占用导致启动失败、资产文件缺失等），`startStandaloneProxy` 不会立即报错，而是：

1. 进入 `waitForPort(socksPort, timeout)` 循环，**持续 TCP connect 轮询**整段超时预算（`config_.test_timeout_ms * 3`）；
2. 生产默认 `timeout_ms = 5000` → 等待 **15000ms** 后才以失败返回；
3. 之后再弹出 `wxMessageBox` 失败提示并结束。

期间 UI 表现为「点了开启代理没反应 / 卡住约 15 秒」，用户体验差且易误判为程序假死。

## 2. 根因

`proxy::ConnectivityVerifier::waitForPort(port, timeoutMs)`（`src/proxy/ProxyConnectivityVerifier.cpp`）只做 TCP 端口连通性轮询，**完全不知道后端进程的生命周期**。后端进程在启动瞬间崩溃退出后，SOCKS 端口永远不会打开，但 `waitForPort` 没有任何机制感知「进程已经死了」，于是傻等满 `timeout`。

旧调用点（`src/ui/AppController.cpp` 的 `startStandaloneProxy`）持有 `PROCESS_INFORMATION pi`，其 `pi.hProcess` 正好能反映后端存活，但**没有把它传给 waitForPort**。

## 3. 修复

### 3.1 `ProxyConnectivityVerifier`（新增进程感知重载）

`include/proxy/ProxyConnectivityVerifier.h` / `src/proxy/ProxyConnectivityVerifier.cpp`：

- 保留旧 2 参重载，内部改为转调新 3 参重载并传 `nullptr`：
  ```cpp
  bool ConnectivityVerifier::waitForPort(int port, int timeoutMs) {
      return waitForPort(port, timeoutMs, nullptr);
  }
  ```
- 新增 3 参崩溃感知重载（`void* processHandle`，Windows 下 `HANDLE` 即 `void*`，避免在纯头文件中引入 `windows.h`）：
  ```cpp
  static bool waitForPort(int port, int timeoutMs, void* processHandle);
  ```
- 每次轮询迭代中，若 `processHandle != nullptr && != INVALID_HANDLE_VALUE`，先 `WaitForSingleObject(h, 0)`：
  - 返回 `WAIT_OBJECT_0`（进程已退出）→ **立即 `return false`**，不等待剩余超时；
  - 同时输出 `LogLevel::ERR` 日志：
    ```
    [ConnectivityVerifier] backing process exited before SOCKS port <port> became ready (flash-crash); bailing out early instead of waiting the full timeout
    ```
- `processHandle == nullptr` 时退化为旧行为（永不提前退出），确保 `ProxyBatchTester` 等其它调用方零回归。

### 3.2 `AppController::startStandaloneProxy`（透传句柄）

`src/ui/AppController.cpp:834-836`：

```cpp
const bool portReady = proxy::ConnectivityVerifier::waitForPort(
    socksPort, config_.test_timeout_ms * 3, pi.hProcess);
```

将已创建的 `pi.hProcess` 传入新重载，使闪崩能被即时感知。

### 3.3 单元测试

`tests/test_connectivity_verify.cpp`（回归用例，随同文件既有套件提交）：

- `WaitForPort_CrashAwareBailsEarlyOnFlashCrash`：绑定一个临时本地端口后立刻释放（使 `connect` 永远失败），再用 `CreateProcessA` 启动一个会立即退出的进程（`cmd.exe /c exit 0`）作为闪崩替身，把其 `pi.hProcess` 传入 3 参重载；设 `timeoutMs = 8000`（若不崩溃感知会傻等满窗），断言 `!ready` 且 `elapsedMs < 2000`（即早期退出）。
- `WaitForPort_NullHandleNoEarlyBail`：传 `nullptr` 验证旧行为精确保留——端口未开时阻塞至整个超时窗才返回，确保其它调用方零回归。

## 4. 验证结果

| 项 | 结果 |
|----|------|
| 构建（validproxy + 单测） | 0 error |
| `WaitForPort_CrashAwareBailsEarlyOnFlashCrash` | PASS（8s 预算下 <2s 早期退出，日志出现 `bailing out early`） |
| `WaitForPort_NullHandleNoEarlyBail` | PASS（旧行为保留，阻塞至超时窗） |
| `AppController` 集成装配（代码审查） | `pi.hProcess` 已正确透传至 3 参重载 |

## 5. 未覆盖 / 残留风险

- **GUI 端到端（真实点击「开启代理」）无法在本无头环境执行**：UIA 无法识别窗口（无显示会话）；子进程 GUI 无法从测试内反射调用；且失败分支会弹模态 `wxMessageBox`，headless 下无人点击会挂起；成功验证路径还需外网（`verify()` 经 SOCKS 访问 `test_url`），本沙箱外网超时。
- 已准备隔离桌面复现场景：`test/ui-e2e/config.json`（隔离端口 `socks 31080 / xray 29501 / 29601` + `timeout_ms = 2000` + 已放开默认列表过滤）+ `test/ui-e2e/guiNDB_e2e.db`（目标代理 `4214682372947796598` 的 `Delay` 种子为 120），供用户在桌面执行后回传日志做日志级分析。

## 6. 建议

- `startStandaloneProxy` 失败弹窗建议增加 `topWindow_ == nullptr` 时**自动按「保持进程不管理」处理**的分支，使 CLI / headless 调用不会卡在模态 `wxMessageBox`。
- 可补一条离线可跑的集成测试：让 `verify()` 指向本地起的 HTTP server（绕过外网），在 `tests/` 增加不弹窗的 `startStandaloneProxy` 集成用例。

---

## 7. 后续补强（同会话用户追问 Q1 / Q2）

原 v1.0 仅修复「不傻等」，但复盘日志 `ui_20260825_145843.log` 发现两个衍生缺陷，同日补强：

### 7.1 Q1 — 日志缺闪崩根因（xray 崩溃原因不可见）

**根因**：`startStandaloneProxy` 用 `CreateProcessA(..., CREATE_NEW_CONSOLE, ...)` 启动 xray/sing-box，**不重定向 stdout/stderr**，子进程报错文本（配置解析失败 / `address already in use` / 资产缺失 / panic 栈）被丢进无人读取的控制台窗口；闪崩分支只写「process exited」，既不调 `GetExitCodeProcess` 也不读 stderr。受管路径 `XrayInstance` 早已重定向 `xray_stderr_<port>.log` 并在 `logDeathDetails()` 输出退出码+stderr 尾，独立路径一直缺这步。

**修复**：
- `src/ui/AppController.cpp` 启动改用**匿名管道**（`CreatePipe`）+ `STARTF_USESTDHANDLES` + `CREATE_NO_WINDOW`：子进程 stdout/stderr 写进管道，再用一个 **detached 读取线程**把每行实时转发到**应用本地日志**（错误行 `[xray:<id>] ...` 记 `LogLevel::ERR`，其余记 `LogLevel::TRACE`）；崩溃时 `logCrashReason()` 只补一句 `exitCode=...` 与「崩溃原因见上方本地日志」。xray/sing-box 的真实报错（配置解析失败 / `address already in use` / 资产缺失 / panic 栈）因此直接出现在用户原本就看的本地日志里，无需再翻独立文件。
- 转发逻辑已抽取为独立单元 `standalone_proxy::forwardChildLog`（`include/StandaloneProxyLogForwarder.h` + `src/StandaloneProxyLogForwarder.cpp`，不依赖 wxWidgets/UI），由 `AppController` 的 detached 线程调用；并有 `tests/test_standalone_proxy_log_forwarder.cpp` 做**无 GUI 集成测试**（模拟子进程经管道写入，断言 error 行路由到 `LogLevel::ERR` 且带 `[xray:<id>]` 前缀、CRLF 被剥离、末尾无换行行仍输出、`nullptr` 句柄安全）。
- `src/proxy/ProxyConnectivityVerifier.cpp` 的 `waitForPort` 3 参重载在检测到 `WAIT_OBJECT_0` 时额外 `GetExitCodeProcess`，把 `exitCode=N` 追加到现有 flash-crash 日志行（即时给出退出码）。
- 子进程退出后管道写端关闭，读取线程读至 EOF 自行结束（detached，无需 join）；失败/成功分支不再持有独立的 stderr 文件句柄。

> 权衡：不再为独立代理弹出独立控制台窗口，xray/sing-box 输出经管道实时进入本地日志（每代理 1 个 detached 读取线程，进程退出后随 EOF 自然结束）；若希望保留可见窗口，可改回 `CREATE_NEW_CONSOLE` 并配合后台管道读 stderr。

### 7.2 Q2 — 闪崩后不应呈现「做了连通性测试」的假象

**根因**：
1. 在 `ui_20260825_145843.log` 中闪崩发生在 `waitForPort` 轮询期（端口从未就绪），实际**未进入** `verify()`；但失败弹窗文案写「代理已启动但**连通性验证失败**」（标题「连通性验证失败」），误导用户以为跑过连通性测试 —— 是文案误导。
2. 潜在真 bug：若端口曾短暂打开（`waitForPort` 返回 true）后进程在 `verify()` 前死去，原代码在 `waitForPort` 与 `verify()` 之间**无存活复检**，会真的对死进程跑 `verify()` 并报告误导性「Connectivity test FAILED」。

**修复**：
- `portReady==false` 分支：用 `WaitForSingleObject(pi.hProcess,0)` 区分「进程已死（真闪崩）」vs「进程活着但端口超时未开」；弹窗标题改「代理启动失败」，文案分别说明「进程闪崩（SOCKS 端口从未就绪），原因见日志」或「进程已启动但端口超时未就绪」—— 不再谎称连通性验证失败。
- 在 `verify()` 前新增**存活复检 gate**：若此时进程已退出，直接走 `logCrashReason(...)` + 失败弹窗并 return，**跳过无意义的连通性测试**（避免对死代理空跑、并报出误导的连通性错误）。

### 7.3 验证（同会话）

| 项 | 结果 |
|----|------|
| 构建（validproxy.exe，含全部改动） | 0 error（仅既有 unused-parameter 警告，非本次引入） |
| 构建 + 运行 `ConnectivityVerifyTest` | 11/11 PASS；`WaitForPort_CrashAwareBailsEarlyOnFlashCrash` 日志现含 `exitCode=0`，证明退出码已追加 |
| `AppController` 改动 | 经编译器（链接入 validproxy.exe）+ 代码审查确认 |
| `StandaloneProxyLogForwarderTest`（无 GUI，模拟子进程经管道写日志） | 2/2 PASS；断言 error 行→ERR 且前缀 `[xray:42]`、CRLF 剥离、末尾无换行行仍输出、`nullptr` 句柄安全 |
| GUI 端到端（真实点击「开启代理」+ 本地日志出现 `[xray:<id>] ...` 报错行） | 待用户在桌面执行（headless 环境无法弹窗/外网） |
