# 纳管独立代理进程的端口回退实施方案（历史表单一来源 + 失败丢弃）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 启动独立代理时把 SOCKS 端口写入 `proxy_runtime_history`；纳管游离进程时若配置文件不可用则从历史表取回端口；三层均失败则丢弃该进程（不纳管、不监控）。

**Architecture:** 历史表新增单一列 `socks_port`；端口解析顺序为「命令行配置路径 → 已知目录探测 → 历史表 → 丢弃」；优先级判定抽成纯头文件以便单测。

**Tech Stack:** C++17 / SQLite（idempotent ALTER）/ GoogleTest / CMake + Ninja（MinGW GCC）

**依据文档：** `docs/specs/2026-10-20-Spec-StandaloneAdoptionPortFallback-v1.2.md`（被本文取代）

**约束：** 全栈禁止使用 `auto` 进行类型推导（AGENTS.md §1）；测试源码只允许放在 `tests/`。

---

## 0. 变更记录

| 版本 | 内容 |
| :--- | :--- |
| v1.2 | owner 裁决：全失败丢弃、仅存 `socks_port`、OS 层延后 |
| **v1.3** | 依据两份独立评审修订：修正函数名与测试目标（B3）、补齐 R6 接管端口来源与悬垂引用陷阱（B1）、丢弃时收尾历史行（B2）、纳管新建行回写端口（R-D）、切库重跑迁移（R-B）、丢弃日志去重（R-A）、抽取纯策略头以便单测 |

---

## 1. 背景与目标

### 1.1 现状（已核实，行号为当前工作区）

`AppController::adoptDanglingStandaloneProxies()`（`src/ui/AppController.cpp:1545-1733`）端口解析：

1. **Step (a)** `:1675-1682`：`extractConfigFullPath(commandLine)` → `readStandaloneInboundPort()`
2. **Step (b)** `:1686-1699`：探测 `<exeDir>\config\`、`<exeDir>\worker\config\`
3. 失败 → WARN `:1700-1703` → 仍以 `info.socksPort = 0` 纳管（`:1711`）

`proxy_runtime_history` 现有列（`src/ProxyRuntimeHistory.cpp:15-37`）：`id, index_id, started_at, ended_at, exit_code, duration_ms, source, pid`，无运行参数列。

监控表格实为 `AppController::getWatchedStandaloneMonitors()`（`:1373-1486`），端口块 `:1429-1447`，取值 `:1446`。

### 1.2 目标

| 编号 | 目标 | 验收方式 |
| :--- | :--- | :--- |
| G1 | 启动时写入 `socks_port`，端口必为已验证监听的真实端口 | `SocksPort_RoundTrip` 单测；写入点位于 `waitForPort` 成功之后 |
| G2 | 纳管时从历史表取回端口 | `LookupSocksPort_*` 6 个单测 |
| G3 | 三层失败则丢弃且不残留状态 | 走查：无 watch key、无 `insertStart`（含 `start_count` 自增）、句柄已关闭、既有行已收尾 |
| G4 | 丢弃时日志可操作且不刷屏 | 同一 pid 仅在首次丢弃时 WARN，其后 DEBUG |
| G5 | 端口解析优先级可单测 | `StandaloneAdoptionPolicyTest.*`（纯函数） |
| G6 | 切库后新列仍可用 | `switchDatabase` 后重跑 `migrateTable`；走查 |

### 1.3 非目标

| 编号 | 非目标 | 说明 |
| :--- | :--- | :--- |
| N1 | OS TCP 表查询 | 延后（owner 裁决 Q3） |
| N2 | `config_path` / `api_port` / `backend` 列 | 无消费价值（Q2） |
| N3 | 反写/重建配置文件 | 后续需求 |
| N4 | `netstat` 子进程取证 | CMD 闪窗（AGENTS.md §3.1） |
| N5 | 监控表格磁盘值可信性校验（原 v1.2 的 D7） | 拆为独立缺陷修复；本方案不改 `getWatchedStandaloneMonitors` 的取值规则 |
| N6 | 修复 `standaloneProxies_` 中 `running=false` 残留条目阻塞再纳管（`AppController.cpp:1584-1586` 只查 key 存在） | 既有缺陷，单独登记 |

---

## 2. 设计决策

### D1：只存 `socks_port`

用途唯一：跨会话恢复端口。其余运行参数或可从持久源推导，或在该场景下无意义。

### D2：写入采用「插入后补写」，不改 `insertStart` 签名

`insertStart` 在 autocommit 时**自身拥有事务**（`ProxyRuntimeHistory.cpp:60-69`），并在同一事务内 `start_count + 1`（`:96-110`）。补写是**另一条独立事务**，中间崩溃会留下 `socks_port = NULL` 的行——该行将落入「必被丢弃」，属可接受代价，需在注释中写明。

```cpp
// Backfills the SOCKS inbound port of an in-progress session.
// Returns false when no in-progress row matched the id (0 rows affected).
// Does NOT open a transaction of its own; callers must not wrap it in BEGIN.
bool updateSocksPort(int64_t historyId, int socksPort, sqlite3* db = nullptr);
```

**返回语义（定死）**：`socksPort <= 0` → 不执行 UPDATE，返回 `true`；执行后以 `sqlite3_changes() > 0` 判定。

### D3：读取查询在 `startedAt` 为空时降级为双键

`ProcessInfo::creationTime` 在 PEB 读取失败时为空（`include/ProcessInspector.h`），旧行写入时用的是**当时**的秒级兜底时间，本次重新枚举会得到**现在**的时间 → 三元组不等 → 假阴性。假阴性在新方案中的代价是**丢弃**，因此必须收敛：

```cpp
// Returns the recorded SOCKS port of the matching in-progress session,
// or 0 when there is no match / NULL / invalid.
// When startedAt is empty, matches on (indexId, pid) only — creation time
// could not be read, so the triplet is not reconstructible.
int lookupSocksPort(const std::string& indexId, int64_t pid,
                    const std::string& startedAt, sqlite3* db = nullptr);
```

- `startedAt` 非空：`WHERE index_id=? AND pid=? AND started_at=? AND ended_at IS NULL AND socks_port IS NOT NULL`
- `startedAt` 为空：`WHERE index_id=? AND pid=? AND ended_at IS NULL AND socks_port IS NOT NULL`

调用方**必须传原始 `procs[i].creationTime`**（可为空），不得传「空则取现在」的兜底值。

### D4：解析必须在 `OpenProcess` / `watch` / `insertStart` **之前**

当前在解析（`:1673`）之前已产生：`OpenProcess` 句柄（`:1594`）、`findInProgressHistory`（`:1609`）、`insertStart` 新行 + `start_count + 1`（`:1613`）、`watch` 注册（`:1623`）。若在原地丢弃，代价是永久多记一次启动、并泄漏 watch key（进程退出时 watcher 仍会 `finalizeStop` 并向 UI 发事件）。

**唯一正确插入点**：`:1582-1588` 的「已在管则跳过」之后、`:1594` 的 `OpenProcess` 之前。

`procStartedAt`（`procs[i].creationTime`，空则 `getCurrentTimestampFormatted()`）**只求值一次**，供 `findInProgressHistory` 与 `insertStart` 共用；秒级时间两次求值可能相差 1 秒，会使三元组失配。

### D5：丢弃的语义（含历史行收尾）

丢弃 = 不纳管、不监控、不建行，**且收尾既有会话**：

1. 不 `OpenProcess`（已前置，通常未打开）；
2. 不注册 `watch`；
3. 不调用 `insertStart`（因此不增 `start_count`）；
4. **若既有 in-progress 行存在**：调用现有 `historyDao_.finalizeStop(rhId, now, /*exitCode*/0, durationMs, db_)`。
   理由：不收尾会让该行永久 `ended_at IS NULL`，被 `getRunningDurations()`（`AppController.cpp:458-476` / `:484-522`）当作「运行中」，进而使 `ProxyListModel::setRunningDurations()`（`ProxyListModel.cpp:109-161`）的 Runtime 列永久显示冻结时长、并给予健康分「运行中」加成；`COL_TOTAL_RUNTIME_MS` 亦被永久多算。
   `finalizeStop` 累加的是**真实已运行时长**（`ProxyRuntimeHistory.cpp:200-213`），`exitCode = 0` 不触发 `crash_count`（`:203`），属正确记账而非污染。
5. **日志去重**：新增 `std::unordered_set<DWORD> discardedStandalonePids_`（受 `standaloneMutex_` 保护）。首次丢弃该 pid → WARN；后续 → DEBUG。
   WARN 文案：`[StandaloneProxy] Discarding dangling process pid=<pid> indexId=<id>: config missing and no runtime-history port. It is no longer tracked; clicking Start will offer to close it.`
6. `continue` 处理下一个进程（**不得** `return` 中断循环）。

### D6：纳管成功时必须回写 `socks_port`

`insertStart` 的三处调用点中，纳管路径的 `:1613` 建新行时不写端口，会导致「外部启动 → 纳管建行 → 删配置 → 重启」在第 2 次重启时被丢弃，而这正是本方案的目标人群。

- 复用既有行（`findInProgressHistory >= 0` 分支）：解析成功后 `updateSocksPort(rhId, adoptedPort, db_)`
- 新建行（`insertStart` 返回 `rhId >= 0`）：同上

### D7：优先级判定抽成纯头文件

`AppController` 不在任何单测 target 内（`CMakeLists.txt:1526-1545` 仅 UI 自动化），纳管判定不可测。沿用仓库既有先例（`src/ui/UnifiedMonitorRows.h`、`include/FloatingWidgetPolicy.h`）抽取纯函数：

`include/StandaloneAdoptionPolicy.h`

```cpp
namespace standalone_adoption {

enum class PortSource { None, CmdlineConfig, DirProbe, RuntimeHistory };

struct PortResolution {
    int port = 0;                    // 0 = 未解析
    PortSource source = PortSource::None;
};

// First non-positive input is skipped; order: cmdline > dirProbe > history.
PortResolution resolvePort(int cmdlinePort, int dirProbePort, int historyPort);

// "cmdline-config" / "dir-probe" / "runtime-history" / "none"
const char* portSourceLabel(PortSource source);

} // namespace standalone_adoption
```

`resolvePort` 返回 `port == 0` 即代表**丢弃**，判定与日志标签因此 100% 可单测。

### D8：R6 接管路径的端口来源

接管路径（`insertStart` @`:1240`）**没有已解析端口**，且：

- `socksPort` 是 `startStandaloneProxy` 的局部量（`:854`），`takeoverFn` lambda 捕获列表为 `[this, &configFileNameCopy, indexId]`（`:1184`），**未捕获 `socksPort`**；
- `&configFileNameCopy`（定义于 `:1143`）是**悬垂引用**——lambda 在 watcher 线程、`startStandaloneProxy` 返回后才执行。目前未爆仅因 lambda 内实际使用的是形参 `cfName`（`:1186`）。

**决策**：

1. `takeoverFn` 内改用形参 `cfName` 构造路径，**明令禁止**引用 `configFileNameCopy`（并在实施时顺带删除该捕获与局部量，消除潜伏 UAF）；
2. 端口来源：`extractConfigFullPath(procs[i].commandLine)` → `readStandaloneInboundPort()`；失败则回退 `standaloneProxies_[indexId].socksPort`（内存值）；
3. 为此需把 `readStandaloneInboundPort` 的前向声明从 `:1371` 上移到 `startStandaloneProxy`（`:765`）之前；
4. 解析仍失败（`0`）→ 不写列（`updateSocksPort` 对 `<=0` 无操作）。

---

## 3. 改动清单

### Task 0：纯策略头（新文件）

- [x] 新建 `include/StandaloneAdoptionPolicy.h`（内容见 D7）
- [x] 新建 `tests/test_standalone_adoption_policy.cpp`，登记 CMake（`add_executable` + `target_include_directories` + `gtest_main gtest` + `RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests` + `add_test(NAME StandaloneAdoptionPolicyTest ...)`）
- [x] 用例：`ResolvePort_PrefersCmdline`、`ResolvePort_FallsBackToDirProbe`、`ResolvePort_FallsBackToHistory`、`ResolvePort_NoneWhenAllZero`、`ResolvePort_IgnoresNegative`、`PortSourceLabel_AllValues`

### Task 1：Schema 迁移（`src/ProxyRuntimeHistory.cpp`）

- [x] `migrateTable` 在 `pid` 的 ALTER（`:35-37`）之后追加 `ALTER TABLE proxy_runtime_history ADD COLUMN socks_port INTEGER`
- [x] 迁移失败时记录 `ERR`（当前 `sqlite3_exec` 返回值被忽略；列缺失会使 `getInProgressSessions` 的 `prepare` 整条失败并静默返回空，导致监控行丢失 pid/startedAt）
- [x] 更新列清单注释

### Task 2：切库后重跑迁移（`src/ui/AppController.cpp`）

- [x] `switchDatabase()`（`:160-208`）在 `setDb(db_)`（`:190`）之后调用 `db::models::ProxyRuntimeHistoryDAO::migrateTable(db_)`
- [x] 说明：`setDb()` 是纯赋值（`include/ProxyRuntimeHistory.h:42`），不重跑迁移；否则切到旧库后 `lookupSocksPort` 因缺列恒失败 → 该库所有游离进程被丢弃

### Task 3：DAO（`include/ProxyRuntimeHistory.h` + `.cpp`）

- [x] `updateSocksPort(...)`（非 const，与既有方法一致；返回语义见 D2）
- [x] `lookupSocksPort(...)`（双键降级见 D3）
- [x] `ProxyRuntimeHistoryItem` 追加 `int socksPort = 0;`（带注释），同步 `getInProgressSessions` 的契约注释
- [x] `getInProgressSessions` 的 SELECT 列清单（`:326-328`）追加 `socks_port`，用 `sqlite3_column_type(...) == SQLITE_NULL ? 0 : sqlite3_column_int(...)`

### Task 4：启动侧写入（`src/ui/AppController.cpp`）

- [x] `startStandaloneProxy`：`insertStart` 成功（`:1137`）后、`watch`（`:1153`）前调用 `updateSocksPort(rhId, socksPort, db_)`
- [x] 写入失败（返回 false）记 WARN，不中断启动
- [x] R6 接管路径按 D8 处理（含前向声明上移、删除悬垂捕获）

### Task 5：纳管流程重排（`src/ui/AppController.cpp`）

- [x] 端口解析块从 `:1673-1703` 整体上移到 `:1594` 之前，`adoptedConfigPath` 声明随之移动
- [x] `procStartedAt` 只求值一次（D4）
- [x] Step (c)：`lookupSocksPort(indexId, pid, procs[i].creationTime, db_)`
- [x] 用 `standalone_adoption::resolvePort(cmdlinePort, dirPort, historyPort)` 统一判定
- [x] `port == 0` → 按 D5 丢弃（含 `finalizeStop` 收尾、日志去重、`continue`）
- [x] 删除 `:1700-1703` 的无条件 WARN 与「以 0 纳管」
- [x] 成功路径日志 `source=<label>` + `pid`
- [x] 成功路径按 D6 回写 `updateSocksPort`（复用行与新建行两处）

### Task 6：测试（扩展既有文件）

- [x] 扩展 `tests/test_runtime_history_dao.cpp`（已注册为 `RuntimeHistoryDAOTest`，`CMakeLists.txt:1411-1424`）——**不新建文件**
      - `MigrateTable_AddsSocksPortColumn`（对照 `:369-375`）
      - `SocksPort_RoundTrip`
      - `SocksPort_UpdateReturnsFalseForUnknownId`
      - `SocksPort_IgnoresFinalizedRow`
      - `SocksPort_RejectsNonPositive`（返回 true 且不写）
      - `LookupSocksPort_ExactMatch` / `_DifferentPid_Zero` / `_DifferentStartedAt_Zero` / `_EmptyStartedAtMatchesPid` / `_Finalized_Zero` / `_NullPort_Zero` / `_LatestWins`
      - 扩展 `GetInProgressSessions_ReturnsPidAndStartedAt`（`:418-439`）断言 `socksPort`
- [x] 用例命名 `TEST_F(RuntimeHistoryDAOTest, ...)`；新增用例置于文件内匿名命名空间结束（`:465`）之前
- [x] 禁止 `auto`

---

## 4. 验证

1. `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`
2. `cmake --build build --parallel 8`
3. `ctest -R "RuntimeHistoryDAOTest|StandaloneAdoptionPolicyTest" -V`
4. `ctest -V`（全量；`XrayManagerStartupTest` 为既有基线失败）
5. 对 **diff 新增行**检查无 `auto`（`AppController.cpp` 既有大量 `auto`，整文件 grep 会误报）
6. **手工场景**
   - S1：启动独立代理 → 删配置 → 重启 GUI → `source=runtime-history`，端口正确
   - S2：启动独立代理 → 删配置 → 清空该行 `socks_port` → 重启 GUI → 首次 WARN 丢弃；30s 后同进程仅 DEBUG；监控无该行；`getRunningDurations` 不含该 indexId
   - S3：外部（非本应用）启动 xray → 本应用纳管（新建行）→ 删配置 → 重启 GUI → 仍能恢复端口（验证 D6）
   - S4：停止后重启代理 → 历史行 `socks_port` 更新为新端口

## 5. 风险与约束

| 编号 | 内容 | 处置 |
| :--- | :--- | :--- |
| R1 | **Task 4 与 Task 5 必须同批发布**：若只上线纳管侧，所有「删配置重启」从「端口 0」退化为「直接丢弃」 | 实施顺序固定 0→6，一次提交 |
| R2 | 本次改动前启动、且配置同时缺失的进程，行内无 `socks_port` → 被丢弃 | 既定代价（Q3 延后）；上线后新启动/新纳管的进程不受影响（D6） |
| R3 | `updateSocksPort` 与 `insertStart` 非原子，中间崩溃留下 NULL 行 | 已在 D2 声明；后果同 R2 |
| R4 | 纳管每 `checkIntervalMs`（默认 30s）重复执行，丢弃日志会刷屏 | D5-5 pid 去重集合 |
| R5 | `finalizeStop` 会累计真实运行时长 | 属正确记账；`exitCode=0` 不触发 crash_count |
| R6 | 丢弃后进程仍占用端口，用户点「启动」会触发 R1 冲突对话框 | 可接受恢复路径；WARN 文案已提示 |
| R7 | 三处并发调用纳管（`MainFrame.cpp:453`、`:979`、`AppController.cpp:203`），守卫无跨线程原子性 | 既有竞态，本方案不扩大；D5 的去重集合同样受 `standaloneMutex_` 保护 |
| R8 | `isPortListening` 相关改造（原 D7）不在本方案 | N5 单独立项 |

## 6. 文档登记

- [x] 实施完成后将本文件登记至 `docs/INDEX.md` §7.5（当前最后一行编号 55 → 本文为 56）
- [x] 同步 `docs/plans/project-plans-tracker.md` 的「近期文档引用」表（追加于表尾，日期 2026-10-20，类型 `spec`）
- [x] v1.0 / v1.1 / v1.2 标注为「已被 v1.3 取代」，不单独登记
