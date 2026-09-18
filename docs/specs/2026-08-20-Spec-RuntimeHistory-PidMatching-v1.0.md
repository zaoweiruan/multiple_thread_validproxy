# Spec: ProxyRuntimeHistory 会话匹配引入 PID 因子 + 系统进程启动时间

- 日期: 2026-08-20
- 类型: Spec（技术方案）
- 模块: ProxyRuntimeHistoryDAO / ProcessInspector / ProcessExitListener / AppController
- 版本: v1.0
- 关联: `docs/bugfix/2026-08-17-Bugfix-StandaloneProxy-WatchLifecycle-v1.0.md`、`docs/specs/2026-08-13-Spec-ProxyScoring-History-v1.0.md`

---

## 1. 背景与调查结果

### 1.1 现状问题

悬垂代理进程纳管（`AppController::adoptDanglingStandaloneProxies`）复用 in-progress 会话时，**仅按 indexId 匹配**，无法区分"同一代理的不同进程实例"；且 `started_at` 取自纳管时刻（`utils::getCurrentTimestamp()`），而非进程真实启动时间，导致：

| 问题 | 后果 |
|------|------|
| **仅 indexId 匹配** | 进程崩溃后 GUI 重启，若旧会话未被 finalize，纳管会**复用旧会话**（starts 不 +1，duration 从纳管起算）——而崩溃重启后的新进程实例本应 +1；反之外部新启动进程若存在同名 in-progress 残留也会被误复用 |
| **started_at 不真实** | 会话 `started_at` 为纳管时刻；心跳 `elapsedMs` 亦从纳管时刻起算（`ProcessExitListener.cpp:34` `threadStartTime`）→ **纳管前已运行时间完全不计数**，单次会话时长失真 |
| **无 pid 记录** | 明细表 `proxy_runtime_history` 无进程实例标识，事后无法区分同 indexId 的不同启动 |

### 1.2 代码调查（现状）

| 位置 | 现状 |
|------|------|
| `proxy_runtime_history` 表（ProxyRuntimeHistory.cpp:16-23） | `id / index_id / started_at / ended_at / exit_code / duration_ms / source` —— **无 pid 列**；索引 `idx_runtime_history_index(index_id, started_at)`（L27-29） |
| `migrateTable`（L13-40） | `CREATE TABLE IF NOT EXISTS` + `ALTER TABLE ProfileExItem ADD COLUMN` 幂等模式（addCols 数组）——**新列可复用该模式** |
| `ProcessInfo`（ProcessInspector.h:13-16） | `{ DWORD pid; std::wstring commandLine; }` —— **无启动时间字段** |
| `ProcessInspector::enumerateByName`（ProcessInspector.cpp:162-197） | Toolhelp32Snapshot → `info.pid = pe.th32ProcessID` → OpenProcess 读命令行 —— **未获取 creation time**；已有 OpenProcess 句柄，扩展成本低 |
| `ProcessInspector::nowTimestamp / durationMsBetween`（L242-275） | 提供 `yyyy-MM-dd HH:mm:ss` 格式工具 —— **无 FILETIME→string 转换** |
| `insertStart`（ProxyRuntimeHistory.cpp:43-121） | 签名 `(indexId, startedAt, db=nullptr)`；事务内 INSERT detail + `start_count+1` —— **无 pid 参数** |
| `findInProgressHistory`（L257-275） | SQL `WHERE index_id=? AND ended_at IS NULL ORDER BY id DESC LIMIT 1` —— **仅 1 因子**；唯一调用点 AppController.cpp:1145 |
| `insertStart` 调用点 | ①L910 正常启动（`pi.dwProcessId` 可用）②L981 R6 takeover（`procs[i].pid` 可用）③L1147 悬垂纳管（`procs[i].pid` 可用）——**3 处均能拿到 pid** |
| 心跳基准（ProcessExitListener.cpp:34,56-59） | `threadStartTime = steady_clock::now()`（watch 调用时刻）；`elapsedMs = now - threadStartTime` —— **非进程启动时间起算** |
| 测试（test_runtime_history_dao.cpp） | `insertStart(A1, T0)` 两参调用约 15 处；辅助 `insertProxy/startCountOf/columnText/columnInt64` 可复用 |

---

## 2. 目标

1. **`findInProgressHistory` 判断因子升级为 `indexId + pid + started_at` 三因子**：精确匹配"同一进程实例"的未收尾会话，杜绝跨实例误复用。
2. **`started_at` 改为从系统获取进程启动时间**（`GetProcessTimes` creation time），使会话时间基准真实。
3. **配套修正心跳基准**：duration_ms 从进程真实启动起算（baseline + 监控增量），纳管前运行时间不再丢失。

---

## 3. 设计

### 3.1 数据模型变更

**`proxy_runtime_history` 新增 `pid` 列（幂等迁移）**：

```cpp
// migrateTable() 内新增（模式同 addCols）
const char* addPidSql =
    "ALTER TABLE proxy_runtime_history ADD COLUMN pid INTEGER";
sqlite3_exec(db, addPidSql, nullptr, nullptr, nullptr);
```

- 旧行 `pid = NULL`：三因子查询 `pid = ?` 不命中 NULL → **旧 in-progress 行不再被复用**（保留为审计记录，无正确性问题；聚合仅在 finalizeStop 累加，老行无人收尾即不累加）。
- 索引策略：现有 `(index_id, started_at)` 保留；三因子查询为等值 + `ORDER BY id DESC LIMIT 1`，数据量级小（standalone 会话），不新增索引（YAGNI）。

### 3.2 ProcessInspector 扩展

**ProcessInfo 增加启动时间字段**：

```cpp
// include/ProcessInspector.h
struct ProcessInfo {
    DWORD pid = 0;
    std::wstring commandLine;
    std::string creationTime;   // "yyyy-MM-dd HH:mm:ss"，读取失败为空
};
```

**`enumerateByName` 填充 creationTime**（复用已有 OpenProcess 句柄）：

```cpp
// src/ProcessInspector.cpp enumerateByName() 内
if (h) {
    std::wstring cmdLine;
    if (readCommandLine(h, cmdLine)) {
        info.commandLine = cmdLine;
    }
    info.creationTime = processCreationTime(h);   // GetProcessTimes
    CloseHandle(h);
}
```

**新增静态方法**（头文件声明 + 实现）：

```cpp
// 从进程句柄取创建时间，格式 "yyyy-MM-dd HH:mm:ss"；失败返回空串
static std::string processCreationTime(HANDLE processHandle);
// 从 pid 取创建时间（内部 OpenProcess + processCreationTime）；失败返回空串
static std::string processCreationTime(DWORD pid);
// FILETIME → "yyyy-MM-dd HH:mm:ss"（本地时区）；纯函数，可单测
static std::string fileTimeToString(const FILETIME& ft);
```

实现要点（Windows）：

```cpp
std::string ProcessInspector::processCreationTime(HANDLE h) {
    FILETIME create{0}, exit{0}, kernel{0}, user{0};
    if (!GetProcessTimes(h, &create, &exit, &kernel, &user)) {
        return std::string();
    }
    return fileTimeToString(create);
}

std::string ProcessInspector::fileTimeToString(const FILETIME& ft) {
    // FILETIME(100ns since 1601) → time_t
    ULARGE_INTEGER ui;
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    const long long secsSince1601 = static_cast<long long>(ui.QuadPart / 10000000ULL);
    const long long unixSecs = secsSince1601 - 11644473600LL;   // 1601→1970 偏移
    std::time_t t = static_cast<std::time_t>(unixSecs);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf);
}
```

### 3.3 DAO 签名变更

**`insertStart`**（ProxyRuntimeHistory.cpp:43 / .h:48）：

```cpp
int64_t insertStart(const std::string& indexId, const std::string& startedAt,
                    int64_t pid, sqlite3* db = nullptr);
```

- INSERT 增加 pid 列：
  ```sql
  INSERT INTO proxy_runtime_history (index_id, started_at, pid, source)
  VALUES (?, ?, ?, 'standalone')
  ```
- 事务与 `start_count+1` 逻辑不变。

**`findInProgressHistory`**（ProxyRuntimeHistory.cpp:257 / .h:68）：

```cpp
int64_t findInProgressHistory(const std::string& indexId, int64_t pid,
                              const std::string& startedAt, sqlite3* db = nullptr);
```

```sql
SELECT id FROM proxy_runtime_history
WHERE index_id = ? AND pid = ? AND started_at = ? AND ended_at IS NULL
ORDER BY id DESC LIMIT 1
```

三因子语义：

| 因子 | 作用 |
|------|------|
| `index_id` | 代理归属 |
| `pid` | **进程实例**：崩溃重启/外部新启 → 不同 pid → 不命中 → 新建会话（starts +1，正确） |
| `started_at` | **实例身份二次校验**：pid 被系统复用（极小概率）时靠启动时间区分；同时保证复用的是同一实例的同一生命周期 |

### 3.4 调用点改造（AppController.cpp）

三处 `insertStart` / 一处 `findInProgressHistory` 统一改为"系统启动时间 + pid"：

**① 正常启动（L910）**：

```cpp
const std::string startedAt =
    proc::ProcessInspector::processCreationTime(pi.dwProcessId);
const std::string safeStartedAt =
    startedAt.empty() ? utils::getCurrentTimestamp() : startedAt;
int64_t rhId = historyDao_.insertStart(
    indexId, safeStartedAt, static_cast<int64_t>(pi.dwProcessId), db_);
```

（`pi` 为 `PROCESS_INFORMATION`，`pi.dwProcessId` 可用；creationTime 失败时回退当前时间——进程刚由本进程创建，二者几乎一致。）

**② R6 auto-takeover（L981）**：

```cpp
const std::string newStartedAt =
    procs[i].creationTime.empty() ? utils::getCurrentTimestamp() : procs[i].creationTime;
int64_t newRhId = historyDao_.insertStart(
    indexId, newStartedAt, static_cast<int64_t>(procs[i].pid), db_);
```

**③ 悬垂纳管（L1145-1149）**：

```cpp
const std::string procStartedAt =
    procs[i].creationTime.empty() ? utils::getCurrentTimestamp() : procs[i].creationTime;
int64_t rhId = historyDao_.findInProgressHistory(
    indexId, static_cast<int64_t>(procs[i].pid), procStartedAt, db_);
if (rhId < 0) {
    rhId = historyDao_.insertStart(
        indexId, procStartedAt, static_cast<int64_t>(procs[i].pid), db_);
}
```

（L1154 的 `startedAt` 变量与 watcher `EnumerateFn` 一并使用 `procStartedAt`。）

### 3.5 心跳基准修正（配套）

**目标**：`duration_ms` 从进程真实启动起算（含纳管前已运行时间）。

**`ProcessExitListener::watch` 增加 baseline 参数**：

```cpp
// ProcessExitListener.h 签名追加（默认 0 = 行为不变）
WatchKey watch(..., HeartbeatFn heartbeatFn, int heartbeatIntervalMs,
               int64_t baselineElapsedMs = 0);
```

```cpp
// ProcessExitListener.cpp
w->baselineElapsedMs = baselineElapsedMs;
w->threadStartTime = std::chrono::steady_clock::now();
...
// L56-59 心跳
const auto elapsedMs =
    w->baselineElapsedMs +
    std::chrono::duration_cast<std::chrono::milliseconds>(
        now - w->threadStartTime).count();
```

**AppController 3 处 watch 调用传 baseline**（纳管/接管/正常启动均适用）：

```cpp
const long long baselineMs =
    procStartedAt.empty()
        ? 0LL
        : proc::ProcessInspector::durationMsBetween(procStartedAt,
                                                    utils::getCurrentTimestamp());
```

- 正常启动路径 baseline≈0（进程刚创建），R6/悬垂路径 baseline = 纳管时刻 − 进程启动时间（即纳管前已运行时长）。
- **语义影响**：`finalizeStop` 累加 `total_runtime_ms += durationMs` 将包含纳管前时间 → 历史聚合更真实（与"从系统获取启动时间"目标一致）。

---

## 4. 测试计划（TDD）

### 4.1 DAO 测试（tests/test_runtime_history_dao.cpp）

现有 `insertStart(A1, T0)` 两参调用约 15 处 → 统一改为 `insertStart(A1, T0, 1001)`（pid 常量）。新增用例：

```cpp
// MigrateTable 幂等 + pid 列可写
TEST_F(RuntimeHistoryDAOTest, MigrateTable_AddsPidColumn) {
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  insertProxy(A1);
  int64_t id = dao.insertStart(A1, T0, 1001);
  EXPECT_GT(id, 0);
  EXPECT_EQ(columnInt64("SELECT pid FROM proxy_runtime_history WHERE id = ?", id), 1001);
}

// 三因子精确命中
TEST_F(RuntimeHistoryDAOTest, FindInProgressHistory_ExactMatch) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t id = dao.insertStart(A1, T0, 1001);
  int64_t found = dao.findInProgressHistory(A1, 1001, T0);
  EXPECT_EQ(found, id);
}

// 同 indexId 不同 pid → 不命中（崩溃重启场景：新实例应新建会话）
TEST_F(RuntimeHistoryDAOTest, FindInProgressHistory_DifferentPid_ReturnsMinusOne) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  dao.insertStart(A1, T0, 1001);
  EXPECT_EQ(dao.findInProgressHistory(A1, 2002, T0), -1);
}

// 同 pid 不同 started_at → 不命中（pid 复用场景）
TEST_F(RuntimeHistoryDAOTest, FindInProgressHistory_DifferentStartedAt_ReturnsMinusOne) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  dao.insertStart(A1, T0, 1001);
  EXPECT_EQ(dao.findInProgressHistory(A1, 1001, T1), -1);
}

// 已收尾行不参与复用
TEST_F(RuntimeHistoryDAOTest, FindInProgressHistory_Finalized_ReturnsMinusOne) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t id = dao.insertStart(A1, T0, 1001);
  dao.finalizeStop(id, T1, 0, 5000);
  EXPECT_EQ(dao.findInProgressHistory(A1, 1001, T0), -1);
}

// 多条 in-progress（异常态）取最新
TEST_F(RuntimeHistoryDAOTest, FindInProgressHistory_LatestWins) {
  insertProxy(A1);
  db::models::ProxyRuntimeHistoryDAO dao(db_);
  int64_t id1 = dao.insertStart(A1, T0, 1001);
  int64_t id2 = dao.insertStart(A1, T1, 1001);
  EXPECT_EQ(dao.findInProgressHistory(A1, 1001, T1), id2);
  EXPECT_NE(id1, id2);
}
```

### 4.2 ProcessInspector 测试（tests/test_process_exit_listener.cpp 或独立）

```cpp
// FILETIME → string 纯函数（1601 epoch → 本地时间）
TEST(ProcessInspectorTimeTest, FileTimeToString_Valid) {
  FILETIME ft{};
  ULARGE_INTEGER ui;
  // 2026-08-20 00:00:00 UTC 的 100ns 计数（用已知值构造）
  ui.QuadPart = 134285760000000000ULL;   // 2026-08-20 00:00:00 UTC 附近
  ft.dwLowDateTime = ui.LowPart;
  ft.dwHighDateTime = ui.HighPart;
  std::string s = proc::ProcessInspector::fileTimeToString(ft);
  EXPECT_FALSE(s.empty());
  EXPECT_EQ(s.size(), 19u);
}

// 当前进程 creation time 非空
TEST(ProcessInspectorTimeTest, ProcessCreationTime_CurrentProcess_NonEmpty) {
  std::string s = proc::ProcessInspector::processCreationTime(GetCurrentProcess());
  EXPECT_FALSE(s.empty());
  EXPECT_EQ(s.size(), 19u);
}
```

### 4.3 ProcessExitListener baseline 测试

```cpp
// 模拟 heartbeat：baseline + 监控增量
// （复用现有 watcher 测试模式，注入 FakeProcess 句柄与短 interval）
TEST(ProcessExitListenerTest, Heartbeat_IncludesBaseline) {
  // watch(..., heartbeatFn, 30, /*baselineElapsedMs=*/60000)
  // 触发一次 WAIT_TIMEOUT 后断言 heartbeatFn 收到 elapsedMs >= 60000
}
```

### 4.4 回归

- `ctest --test-dir build -V` 全量 30 项 + 新增项通过；
- 既有 `ProcessExitListenerTest` / `ProcessInspectorTest` / `RuntimeHistoryDAOTest` 无回归。

---

## 5. 验收标准

- [ ] 悬垂纳管：外部新启动进程（无 in-progress 会话）→ `insertStart`，starts +1，`started_at` = 系统启动时间
- [ ] 悬垂纳管：GUI 崩溃重启纳管同一进程（同 pid + 同 started_at 的 in-progress 会话存在）→ 复用，starts 不 +1
- [ ] 同 indexId 新 pid（崩溃重启后新实例）→ 不复用旧会话，新建会话 starts +1
- [ ] Runtime 列 = 历史 total_runtime_ms + 当前会话 duration_ms（duration_ms 含纳管前运行时间）
- [ ] 构建 0 error；新增 + 回归测试全绿

---

## 6. 影响范围与风险

| 项 | 影响/风险 | 缓解 |
|----|-----------|------|
| `insertStart` 签名变更 | 3 处调用点 + ~15 处测试调用需批量更新 | 全部调用点可获取 pid；测试用统一 pid 常量 |
| `findInProgressHistory` 签名变更 | 1 处调用点（悬垂纳管） | 同步修改 |
| 老数据 pid NULL | 旧 in-progress 行不再被复用（审计保留） | 无正确性问题；如需清理可后续单独立项 |
| pid 复用竞态 | 进程崩溃后 pid 被系统复用（同 started_at 概率极低） | started_at 作为第二校验因子兜底 |
| 心跳 baseline | duration_ms 含纳管前时间，finalizeStop 聚合值变大 | 与"系统启动时间"目标一致，属预期语义修正 |
| `GetProcessTimes` 失败 | creationTime 空串 | 回退 `utils::getCurrentTimestamp()`（正常启动路径误差毫秒级） |