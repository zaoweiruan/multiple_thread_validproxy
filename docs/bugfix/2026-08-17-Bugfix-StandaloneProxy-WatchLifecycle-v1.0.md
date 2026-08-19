# Standalone 代理 Watch 生命周期完善（查重前置 / 心跳 / 悬垂进程纳管）

- 文档类型：Bugfix / 功能完善
- 版本：v1.0
- 日期：2026-08-17
- 涉及模块：`src/ui/AppController.cpp`、`src/ui/AppController.h`、`src/ui/ProxyListPanel.cpp`、`src/ui/MainFrame.cpp`、`include/ProcessInspector.h`、`src/ProcessInspector.cpp`、`include/ProcessExitListener.h`、`src/ProcessExitListener.cpp`、`include/ProxyRuntimeHistory.h`、`src/ProxyRuntimeHistory.cpp`

## 背景

standalone 代理（GUI 单代理启动，xray / sing-box + `standalone_<indexId>-xray.json` 配置）的启动与监控存在三处缺口：

1. **重复启动查重时机过晚**：查重位于 `startStandaloneProxy` 内部（outbound 生成之后），UI 层在调用前先做端口检查/分配——相同代理重复启动时仍先出现端口绑定操作。
2. **watch 期间无历史表刷新**：代理运行中 `proxy_runtime_history.duration_ms` 只有启动/停止时才写入；异常中断或外部终止（taskkill）后无法评价运行时长。
3. **悬垂进程无法纳管**：GUI 重启后仍在运行的 standalone 代理进程（旧版残留、外部启动）没有历史记录、不被 watch，退出时不留评价数据。

## 修改内容

### 一、查重前置（R1 + UI 层）

- `AppController` 新增公共方法 `bool isStandaloneProxyRunning(const std::string& indexId) const`：以 `standalone_<indexId>-xray.json` / `-singbox.json` 为 config 名，调用 `proc::ProcessInspector::isProcessRunningWithConfig` 探测同名配置进程是否已运行。
- `ProxyListPanel::onStartProxy`：在**端口检查之前**调用 `isStandaloneProxyRunning`，已运行则弹窗「该代理已作为独立进程运行，请先停止后再启动。」+ WARN 日志 + 直接返回，不再进入端口检查/分配。
- `startStandaloneProxy` 内部原查重块保留（防御性，直接调用仍有效），并提前至 profile 获取后、outbound 生成前。

### 二、watch 心跳（定期更新历史表）

- `ProcessExitListener` 新增 `HeartbeatFn` 回调类型（`(indexId, historyId, elapsedMs)`），`watch()` 签名追加 `heartbeatFn = nullptr, int heartbeatIntervalMs = 30000`，Watcher 结构新增对应成员。
- watch 线程由 `WaitForSingleObject(handle, INFINITE)` 改为循环等待：`WAIT_OBJECT_0`（退出）走原 handleProcessExit 逻辑（R6 takeover / ShouldFinalize / finalizeStop 原样保留）；`WAIT_TIMEOUT`（每 30s）调用 `heartbeatFn`；`WAIT_FAILED` / `!running` 则 break。
- `ProxyRuntimeHistoryDAO` 新增 `bool touchHeartbeat(int64_t historyId, int64_t durationMs, sqlite3* db = nullptr)`：`UPDATE proxy_runtime_history SET duration_ms = ? WHERE id = ? AND ended_at IS NULL`（单语句原子，只更新进行中会话的时长，不改 ended_at/exit_code，聚合只在 finalizeStop 时累加）。
- `AppController` 两处 watch 调用（初始 watch + R6 auto-takeover 内 watch）均追加 heartbeatFn（调用 `historyDao_.touchHeartbeat(hid, elapsedMs, db_)`）。注意：R6 内 watch 需显式传 `takeoverFn=nullptr` + `configFileName=""` 才能到达第 11 参位置（曾因漏传导致编译错误）。

### 三、悬垂进程纳管（watch 启动时评价）

- `ProcessInspector` 新增 `static std::string extractConfigFileName(const std::wstring& commandLine)`：从进程命令行提取 `standalone_*.json` 纯文件名（找 `standalone_` marker → 找 `.json` → token 内含 `\`、`/`、`"` 则视为非纯文件名返回空）。
- `ProxyRuntimeHistoryDAO` 新增 `int64_t findInProgressHistory(const std::string& indexId, sqlite3* db = nullptr)`：`SELECT id FROM proxy_runtime_history WHERE index_id = ? AND ended_at IS NULL ORDER BY id DESC LIMIT 1`，复用最近一条未结束会话（无则 -1）。
- `AppController` 新增公共方法 `void adoptDanglingStandaloneProxies()`：
  - 枚举 `xray.exe` + `sing-box.exe` → 每条进程 `extractConfigFileName` → 空跳过 → 从 `standalone_<indexId>-xray.json` / `-singbox.json` 提取 indexId；
  - `standaloneProxies_` / `proxyWatchKeys_` 已含 indexId 则跳过；`OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE)` 失败跳过；
  - `rhId = findInProgressHistory`，无则 `insertStart`；watch（finalizeFn→finalizeStop、notifyFn→wxQueueEvent、takeoverFn=nullptr、configFileName=提取值、heartbeatFn→touchHeartbeat）；
  - 更新 `standaloneProxies_[indexId]`（running=true, managed=true, runtimeHistoryId, watchKey）+ `proxyWatchKeys_[indexId]`，REPORT 日志。
- `MainFrame` 构造 `controller_` 后调用 `controller_->adoptDanglingStandaloneProxies();`（GUI 启动、watch 机制就绪时 = 「watch 启动时」）。

## 验证

- 构建：`cmake --build build --target validproxy --parallel 2` → **98/98 链接成功**。
- 实机验证（2026-08-17 16:06-16:08）：
  1. 手动启动悬垂进程（PID 4084，`standalone_4025339273190890146-xray.json`），环境存在旧版残留（PID 5544，`standalone_5251642538024887981-xray.json`）；
  2. 启动 GUI → 日志出现两条 `Adopted dangling process`（PID 5544 + 4084 均被纳管）；
  3. 历史表新增 id=9/10 两条进行中会话（ended_at 空）；
  4. `taskkill /F` 两个进程 → 3 秒后历史表自动收尾：ended_at=2026-08-17 16:07:39、exit_code=1、duration_ms≈31s（watch 退出回调 → finalizeStop 自动回填）。

## 注意事项

- taskkill /F 强制终止返回 exit_code=1（非 259 STILL_ACTIVE），不累加 crash_count——crash 判定仅在 exitCode==259 时触发（既有 finalizeStop 语义）。
- 非 standalone 配置的 xray 进程（如 `xray_config_*.json` 全局批量测试）不会被纳管（extractConfigFileName 不匹配），符合预期。
- `topWindow_` 在现有代码中无调用点（既有缺口），notifyFn 的 wxQueueEvent 不触发，不影响历史收尾。
