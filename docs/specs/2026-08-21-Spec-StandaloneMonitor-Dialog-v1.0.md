# Spec: 独立代理监控弹窗（StandaloneMonitorDialog）v1.0

- 日期: 2026-08-21
- 模块: AppController / ProxyRuntimeHistoryDAO / MainFrame / 新增 StandaloneMonitorDialog
- 状态: 已完成（构建 0 error + ctest 31/31，含 DAO GetInProgressSessions 3 新用例）
- 关联: `2026-08-20-Spec-RuntimeHistory-PidMatching-v1.0.md`（pid 三元组匹配）、`docs/design/process-exit-listener-design.md`

## 1. 需求

新增功能：独立代理监控弹窗，实时展示**正在被 watch 的代理进程**在 `proxy_runtime_history` 表中的会话数据：

| 列 | 来源 |
| --- | --- |
| 索引ID | history.index_id |
| 起始时间 | history.started_at（进程系统创建时间） |
| 运行时长(分) | 实时计算 now − started_at（保留 1 位小数） |
| 监听端口（新增） | StandaloneProxyInfo.socksPort |
| PID | history.pid |

同时吸收前次请求：`[StandaloneProxy]` 监控日志补充监听端口（纳管、停止日志）。

## 2. 输入 / 输出 / 边界

- 输入：`standaloneProxies_`（内存态，standaloneMutex_ 保护）∪ `proxy_runtime_history` 中 `ended_at IS NULL` 行。
- 输出：弹窗列表，2 秒定时刷新；无网络、无外部进程操作。
- 边界：
  - 未纳管（managed=false 或无 watchKey）的进程**不展示**（与"正在 watch"语义一致）。
  - history 行缺失（insertStart 失败）：pid 显示 `-1`，起始时间回退 `info.startedAt`（可能为空则显示 `-`）。
  - 弹窗关闭即停止定时器；对话框为非模态（wxSHOW_EFFECT 无阻塞），允许与主窗口并行操作。
  - 线程安全：快照方法先短锁复制 watched 集合，解锁后再查 DB（PK 级小查询，行数 <10，与既有 `getRunningDurations()` 同模式）。

## 3. 变更清单

### 3.1 DAO 层（include/ProxyRuntimeHistory.h, src/ProxyRuntimeHistory.cpp）
1. `ProxyRuntimeHistoryItem` 增加 `int64_t pid = -1;`。
2. 新增 `std::vector<ProxyRuntimeHistoryItem> getInProgressSessions(sqlite3* db = nullptr);`
   SQL: `SELECT id, index_id, started_at, pid FROM proxy_runtime_history WHERE ended_at IS NULL ORDER BY started_at;`

### 3.2 AppController（src/ui/AppController.h/.cpp）
1. 新增 `struct StandaloneMonitorRow { std::string indexId; std::string startedAt; int64_t durationMs; int socksPort; int64_t pid; };`
2. 新增 `std::vector<StandaloneMonitorRow> getWatchedStandaloneMonitors();`
   - 锁内收集 `running && managed` 的 `{indexId, socksPort, runtimeHistoryId}`；
   - 解锁后 `getInProgressSessions()` 按 row.id == runtimeHistoryId 匹配补 pid/startedAt；
   - `durationMs = ProcessInspector::durationMsBetween(startedAt, now)`。
3. **缺陷修复（正常启动路径回填）**：`startStandaloneProxy` 中 `insertStart` 成功且 `watch()` 建立后，将 `runtimeHistoryId / watchKey / startedAt` 写回 `standaloneProxies_[indexId]`。否则 `stopStandaloneProxy` 因 `watchKey==0` 跳过 `unwatch` 造成 watcher 泄漏。
4. **纳管端口补齐**：`adoptDanglingStandaloneProxies` 解析 config 文件 JSON（xray `inbounds[0].port` / sing-box `inbounds[0].listen_port`）写入 `info.socksPort`；解析失败保持 0 并 WARN。
5. 日志增强：adoption 与 stop 日志追加 `" on SOCKS5 :<port>"`。

### 3.3 UI 层
1. 新文件 `src/ui/StandaloneMonitorDialog.h/.cpp`：
   - `wxDialog`，标题「独立代理监控」，尺寸 720×360，`CentreOnScreen`；
   - `wxListCtrl`（report）五列：索引ID / 起始时间 / 运行时长(分) / 监听端口 / PID；
   - `wxTimer` 2000ms 触发刷新（调 `getWatchedStandaloneMonitors()` 全量重建行）；
   - 底部 Close 按钮（`wxID_CLOSE`）；析构停表。
2. `MainFrame`：新 ID `ID_MENU_STANDALONE_MON = wxID_HIGHEST + 114`，Proxy 菜单追加「独立代理监控…\tCtrl+M」，handler `onMenuStandaloneMonitor` 创建/置顶非模态弹窗（单例指针，存在则 `Raise()`）。
3. `CMakeLists.txt`：UI_SOURCES 追加两个新文件。

## 4. 测试计划

- `tests/test_proxy_runtime_history.cpp` 新增用例：
  - `GetInProgressSessionsReturnsPidAndStartedAt`：插入两条 start（不同 pid），断言返回行数、pid、startedAt、排序；
  - finalize 一条后再次查询，断言只剩未结束行。
- 回归：`cmake --build build --parallel 8` + `ctest --test-dir build --parallel 8`（31 + 新增 ≥ 32 通过）。
- 手工验证项（GUI）：启动独立代理 → 打开弹窗见行数据、时长递增；停止代理 → 行消失；纳管悬垂进程 → 端口列非 0。

## 5. 约束遵循

- 全程禁用 `auto`；PowerShell 命令语法；文档登记至 `docs/INDEX.md` 与 `docs/context.md`。
