# Plan: 代理评分引入历史服务记录——实施计划（P1-P4）

- 日期: 2026-08-14
- 类型: Plan（实施计划，承接 `docs/specs/2026-08-13-Spec-ProxyScoring-History-v1.0.md` v1.3）
- 状态: 🔄 in-progress
- 开发环境: **数据库 `E:/eclipse_workspace/multiple_thread_validproxy/test/guiNDB.db`；配置 `bin/config.test.json`（已重写为完整开发配置）**

---

## 0. 承接关系

设计文档 v1.3（评审通过，commit 已含 R1-R6）定义了完整方案。本计划只做**任务拆解与实施顺序**，不再重复设计细节；每阶段实施以设计文档对应章节为准。

| 阶段 | 设计文档章节 | 目标 |
|------|-------------|------|
| P1 数据层 | §3.1/§3.2（DDL/DAO） | 明细表 + 聚合列 + ProxyRuntimeHistoryDAO |
| P2 采集层 | §3.3（R1/R2/R3） | ProcessInspector + startStandaloneProxy 改造 + ProcessExitListener |
| P3 评分引擎 | §3.4（ProxyScorer） | 三因子评分纯函数 |
| P4 集成展示 | §3.5 | score 写回 + UI 列 + scoring.weights 配置 |

## 1. 开发环境基线

- **数据库**：`test/guiNDB.db`（154MB，2026-08-14 已更新）。注意：**`test/` 目录仅放数据，禁止放编译产物**（AGENTS.md 红线）；P1 迁移会改动该库 schema，实施前应做备份副本（如 `test/guiNDB.db.bak`）。
- **配置**：`bin/config.test.json` 已重写，补全 `proxy`/`network_monitor`/`auto_task` 段，`database.path` 指向 test 库；CLI 用 `-c bin/config.test.json` 启动。
- **验证命令**：`cmake --build build --parallel 8 && ctest -V`（每阶段结束必跑）。

---

## P1 数据层（1-2 天）

### P1.1 明细表 + 聚合列迁移（设计 §3.1）
- 新建 `include/ProxyRuntimeHistory.h` + `src/ProxyRuntimeHistory.cpp`（新 DAO，构造 `ProxyRuntimeHistoryDAO(sqlite3* db)`，构造内 `migrateTable(db)`）。
- 迁移（同 `ProfileExItemDAO::migrateTable` 幂等模式）：
  - `CREATE TABLE IF NOT EXISTS proxy_runtime_history (id INTEGER PRIMARY KEY AUTOINCREMENT, index_id TEXT NOT NULL, started_at TEXT NOT NULL, ended_at TEXT NULL, exit_code INTEGER, duration_ms INTEGER, source TEXT DEFAULT 'standalone')` + 索引 `CREATE INDEX IF NOT EXISTS idx_runtime_history_index ON proxy_runtime_history(index_id, started_at)`。
  - `ALTER TABLE ProfileExItem ADD COLUMN start_count INTEGER NOT NULL DEFAULT 0`（忽略已存在错误=幂等）。
  - `ALTER TABLE ProfileExItem ADD COLUMN total_runtime_ms INTEGER NOT NULL DEFAULT 0`。
  - `ALTER TABLE ProfileExItem ADD COLUMN crash_count INTEGER NOT NULL DEFAULT 0`。

### P1.2 DAO 方法（设计 §3.2）
- `struct ProxyRuntimeHistoryItem { int64_t id; std::string indexId; std::string startedAt; std::string endedAt; int exitCode; int64_t durationMs; std::string source; }`。
- `int64_t insertStart(const std::string& indexId, const std::string& startedAt, sqlite3* db)` → 明细行 INSERT + `UPDATE ProfileExItem SET start_count = start_count + 1` **同一事务**；返回 `last_insert_rowid()`。
- `void finalizeStop(int64_t historyId, const std::string& endedAt, int exitCode, int64_t durationMs, const std::string& indexId, sqlite3* db)` → `UPDATE proxy_runtime_history SET ended_at/exit_code/duration_ms WHERE id=?`（仅 ended_at IS NULL 时，幂等）+ `UPDATE ProfileExItem SET total_runtime_ms = total_runtime_ms + ?, crash_count = crash_count + (exit_code == 259 ? 1 : 0) WHERE index_id=?` 同一事务。
- **嵌套事务遵循 `sqlite3_get_autocommit(execDb)` 模式**（与 `updateTestResultBatch` 一致，见 CONTEXT.md 2026-08-13 记录）：仅外层无事务时 BEGIN/COMMIT/ROLLBACK。
- `getRuntimeStats(const std::string& indexId, sqlite3* db)` → 读取 3 聚合列（供评分/UI）。
- 时间戳格式 `yyyy-MM-dd HH:mm:ss`（复用 `ProfileExItemDAO::currentTimeString` 或独立实现，19 字符）。

### P1.3 ProfileExItem 结构体扩展
- `include/ProfileExitem.h` 增加 `int startCount = 0; int64_t totalRuntimeMs = 0; int crashCount = 0;`。
- `ProfileExItemDAO` 的 `fromStmt`/`createTable`/`toJson`（或等价读写路径）同步 3 字段。

### P1.4 测试（`tests/`）
- `RuntimeHistoryDAOTest.InsertFinalize`：完整生命周期 insert→finalize，明细行字段 + 聚合列正确。
- `RuntimeHistoryDAOTest.InsertThenFinalizeTwice`：二次 finalize 幂等（ended_at 已回填不再覆盖、聚合不重复累加）。
- `RuntimeHistoryDAOTest.Aggregation`：多次启动/退出累加正确。
- `RuntimeHistoryDAOTest.Transaction`：明细+聚合单事务，中途失败回滚一致。
- 迁移幂等测试：重复构造 DAO 不报错。

## P2 采集层（2-3 天）

### P2.1 ProcessInspector（设计 §3.2.3，P0 ⑦）
- 新建 `include/ProcessInspector.h` + `src/ProcessInspector.cpp`。
- `struct ProcessInfo { DWORD pid; std::wstring commandLine; }`。
- `static bool isProcessRunningWithConfig(const std::string& configFileName)`：Toolhelp32 枚举（复用 Utils.cpp:254 模式）→ 匹配 `xray.exe`/`sing-box.exe` 进程名 → `OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ)` → PEB `ReadProcessMemory` 读 CommandLine（x64: PEB+0x20 ProcessParameters → +0x70 CommandLine UNICODE_STRING）→ 匹配 `configFileName` 子串；PEB 读取失败**降级为进程名匹配**（保守拦截，WARN 日志不崩溃）。
- `static std::vector<ProcessInfo> enumerateByName(const std::string& exeName)`：返回 {pid, commandLine} 列表（供 R1 查重 + R6 自动接管 OpenProcess）。
- 单元测试辅助：接受"mock 进程名 + 期望 commandLine"注入或基于真实工具进程（如 cmd /c 带参数）验证。

### P2.2 startStandaloneProxy 改造（设计 §3.3.1，R1/R2）
- `StandaloneProxyInfo` 扩展：`int64_t runtimeHistoryId = -1; bool managed = false; std::string configFileName; std::string startedAt;`（AppController.h:28）。
- **R1 启动前查重**：fetch profile 后、CreateProcessA 前，`isProcessRunningWithConfig(configFileName)` → `wxMessageBox("代理已在运行...")` + `return false`。
- **R2 启动后连通性验证**（P0 ⑤ 定案：实施时选「同步 1 次短超时 + 失败弹窗」或「后台线程回弹窗」，需在实施中确认；默认先做同步 1 次 `fetchViaProxy(socksPort, test.url, test.timeout_ms)`，成功则继续，失败 `wxMessageBox(YES/NO/CANCEL)`：YES=TerminateProcess+return false；NO/CANCEL=弃管 managed=false/running=false）。
- 验证通过：`exDao.updateStartupTime(indexId)`（既有 :729）→ `historyDao.insertStart` → `info.{runtimeHistoryId, managed=true, running=true, startedAt}` → 启动 ProcessExitListener 等待线程（P2.3）。

### P2.3 ProcessExitListener（设计 §3.3.2，R3/R5/R6）
- AppController 新增：`std::atomic<bool> shutdownRequested_{false}`、`std::vector<std::thread> exitListeners_`、`std::mutex exitListenersMutex_`、顶层窗口 `wxEvtHandler* topWindow_`（P0 ③：`setTopWindow`/构造注入）。
- 每受管进程 emplace 专用等待线程：`WaitForSingleObject(info->processHandle, INFINITE)` → `handleProcessExit(indexId)`。
- `handleProcessExit`：
  - `GetExitCodeProcess` → `currentTimeString` → `durationMs`。
  - 一次性 `isProcessRunningWithConfig(configFileName)`：
    - 无同配置进程 → `finalizeStop` 正常回填（exit_code 判正常 0/崩溃 259）→ `running=false` → `wxQueueEvent(topWindow, StandaloneProxyEvent(indexId,"",socksPort,false,exitCodeMsg))`。
    - 有同配置进程（**R6 自动接管**）→ `finalizeStop(exit_code=0)` → `wxQueueEvent(restart-takeover)` → `enumerateByName` 找新 pid → `OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_SYNCHRONIZE)` → `insertStart` 新会话 → `CloseHandle` 旧句柄 → 更新 `info{processHandle, runtimeHistoryId, startedAt, managed=true, running=true}` → 重新 emplace 等待线程 → return；接管失败 WARN + `runtimeHistoryId=-1/managed=false/running=false`。
- 析构：`shutdownRequested_=true` → 已结束线程 join + 仍阻塞线程 detach；等待线程唤醒后检查 `shutdownRequested_` 跳过写库。
- **事件投递**：`wxQueueEvent(topWindow_, new StandaloneProxyEvent(...))`；MainFrame 需 Bind 或转发给 ProxyListPanel（ProxyListPanel:82 已绑定 onStandaloneProxyEvent，仅日志）。

### P2.4 测试（`tests/`）
- `ProcessExitListenerTest.MockProcessExit` / `MockProcessKill`（exit_code=259→crash_count+1）/ `MockProcessRestart`（旧会话 finalize(0) + 自动接管新进程）/ `ShutdownNoDbWrite`。
- `ProcessInspectorTest.FindByConfig` / `CommandLineReadFailure`。
- `ConnectivityVerifyTest.SocksProxyOk` / `SocksProxyFail`。

## P3 评分引擎（1-2 天，设计 §3.4）

- 新建 `include/ProxyScorer.h` + `src/ProxyScorer.cpp`（**纯函数不碰 DB**）：
  - `static int score(int speedScore, int stabilityScore, const RuntimeStats& stats, const Weights& w)`。
  - 公式：`historyScore = 100*(0.30*kicker + 0.40*health + 0.30*duration)`；`kicker = min(log2(start_count+1)/log2(6),1)`；`health = (stable+1)/(start_count+2)`；`duration = clamp(avgRuntimeMs/1_800_000,0,1)`。
  - `总分 = 0.20×速度 + 0.30×稳定性 + 0.50×历史`；冷启动 `start_count==0` → historyScore=50、历史权重收缩为 0、速度/稳定性按 **40:60** 归一化（零回归）。
- 测试：`ProxyScorerTest.NoHistory`（40:60 归一化）/ `MixedHistory` / `WeightNormalization`。

## P4 集成展示（1 天，设计 §3.5）

- `score` 写回 `ProfileExItem.score`（评分时机：P2 finalize 或 P3 后置，实施时定；沿用 0-100→5 星映射）。
- ProxyListPanel 增加列：启动次数 / 累计运行时长 / 健康度（正常退出率）。
- `config.json` 增加 `scoring.weights`（默认 20/30/50）+ `ConfigReader` 解析兜底。

---

## 2. 实施顺序与验证

```
P1 →（ctest 通过）→ P2 →（ctest + ASAN 验证 ProcessExitListener 竞态）→ P3 →（ctest）→ P4 →（GUI 手动验证）
```

- 每阶段：`cmake --build build --parallel 8 && ctest -V`。
- P2 额外：ASAN 构建验证等待线程/析构竞态（`-DENABLE_SANITIZERS=ON`）。
- 开发验证场景：CLI `-c bin/config.test.json` 对 test 库执行批量测试/单代理启动；GUI 用 test 配置启动独立代理验证 R1/R2/R3 弹窗与历史记录。

## 3. 风险与回滚

- **test/guiNDB.db 被 P1 迁移修改**：实施前备份副本；迁移幂等可重入。
- ProcessExitListener 线程与 UI 并发写 sqlite：复用 FULLMUTEX serialized 模型；失败降级日志。
- 自动接管竞态（枚举窗口内进程又退出）：接管失败放弃，下次启动重走全流程（设计已定义）。
- 弃管进程不产生记录、不纳入评分（设计 §7 明确不做）。

## 4. 完成定义（DoD）

1. 全部 ctest 通过（新增 ≥15 用例）。
2. CLI/GUI 使用 `bin/config.test.json` + `test/guiNDB.db` 完成端到端验证：启动独立代理 → R1 重复启动弹窗 → R2 连通验证 → 正常退出/强杀/外部重启三种场景历史记录正确 → 评分写回可见。
3. docs/plans/project-plans-tracker.md 更新状态。