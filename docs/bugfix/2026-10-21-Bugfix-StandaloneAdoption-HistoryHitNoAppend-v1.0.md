# 纳管独立代理进程：历史表命中时不应重复 append，应只刷新持续时间

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:systematic-debugging to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 纳管独立代理进程时，只要历史表中已存在该进程实例的会话行（in-progress 行），就复用该行并刷新其 `duration_ms`，**不再**关闭旧行 + 追加新行。

**Tech Stack:** C++17 / SQLite / GoogleTest / CMake + Ninja（MinGW GCC）

**依据文档:**
- `docs/specs/2026-10-21-Spec-StandaloneAdoption-InstanceBoundPortFallback-v1.1.md`（v1.4 append 语义 / v1.6 旁路检测路由）
- `docs/bugfix/2026-10-21-Bugfix-StandaloneAdoption-ConcurrencyAndDiagnostics-v1.1.md`（并发重入 / 丢弃日志 / OS 表可读性）

**约束:** 全栈禁止使用 `auto` 进行类型推导（AGENTS.md §1）；测试源码只允许放在 `tests/`。

---

## 0. 现场证据

### 0.1 用户指令（逐字）

> 修复：纳管独立代理时，如果从历史表命中，则不需要append，只修改持续时间

### 0.2 代码事实

两个「行身份块」结构完全相同（`src/ui/AppController.cpp`，现 3434 行）：

| 块 | 位置 | 触发条件 |
| --- | --- | --- |
| A. Bypass 块 | `:2086-2139` | `route == BypassDetect && alreadyManaged`（本次会话启动的代理） |
| B. 纳管主块 | `:2185-2224` | 纳管来的游离代理 |

两块的现行三分支行为：

| 情形 | 现行行为 | 问题 |
| --- | --- | --- |
| 三元组命中（`findInProgressHistory`） | 复用该行 | **不刷新 `duration_ms`** ⇒ 行上的运行时长停留在首次写入值 |
| 三元组不命中 + 宽匹配命中（`findInProgressHistoryLoose`） | `finalizeStop` 关掉旧行 + `insertStartWithPort` 追加新行 | **重复记录同一次物理启动** ⇒ 重复三元组、`start_count` 重复累加、旧行被提前结束 |
| 两者都不命中 | `insertStartWithPort` 追加新行 | 正确（「缺乏活体记录则补齐」保持不变） |

### 0.3 三元组失配的真实成因

`started_at` 的取值是三级兜底（`instanceStartedAt` → `procs[i].creationTime` → `utils::getCurrentTimestampFormatted()`）：

- 写入方（`startStandaloneProxy` / 纳管）与读取方（下一轮纳管）各自的「now」是**不同瞬间**；
- 一旦某轮两个创建时间都读不到 → 写入方写入 now 兜底值 → 下一轮读到真实创建时间 ⇒ **三元组必然失配**；
- 而 `findInProgressHistoryLoose`（`index_id + pid` 宽匹配）命中���那一行，**几乎必然是同一物理实例**（旧行 `started_at` = 写入时的 now）。

⇒ 因此「宽匹配命中」不应被当作「旧实例已死、新实例另起一行」，而应被当作「同一实例的会话行，只是 `started_at` 记录方式不同」。

### 0.4 后果

生产库 `bin/worker/guiNDB.db` 实测：`ended_at IS NULL` 孤儿行 **128** 条；重复三元组全表 **11 组**（其中 in-progress 内仅 **2** 组）；`start_count` 恒 0（`src/ProfileExItemDAO.cpp:287-290` + `:301` 的 `INSERT OR REPLACE` 归零机制，范围外）。

---

## 1. 根因（RCA）

**RCA-1**：`findInProgressHistoryLoose` 命中即被视为「旧实例」，触发「关闭旧行 + 追加新行」的补偿逻辑。该逻辑源于「append-only 会话日志」语义，但**未区分「旧实例确实结束」与「同一实例的 `started_at` 记录方式不同」**两种情形。

**RCA-2**：命中分支从不调用 `touchHeartbeat`，命中行的 `duration_ms` 停留在首次写入值 ⇒ 运行时长统计失真。

---

## 2. 修复方案

### D1：命中判定改为「三元组命中 或 宽匹配命中」

| 情形 | 新行为 |
| --- | --- |
| 三元组命中 | 复用该行 → `touchHeartbeat` 刷新 `duration_ms` → `updateSocksPort` 纠正端口 |
| 三元组不命中 + 宽匹配命中 | **复用该行** → `touchHeartbeat` 刷新 `duration_ms` → `updateSocksPort` 纠正端口；**不再 `finalizeStop`、不再 append** |
| 两者都不命中 | `insertStartWithPort` 追加新行（补齐活体记录，语义不变） |

> 注：`updateSocksPort` 在纳管主块中已有**无条件**调用（`watch()` 之后），故主块无需重复；Bypass 块需自行调用。

### D2：`duration_ms` 的计算基准

`touchHeartbeat` 写的是**绝对值** `duration_ms`。计算基准必须取**该历史行自身的 `started_at`**，而不是本轮的 `procStartedAt`：

- 三元组命中时两者相同；
- 宽匹配命中时本轮 `procStartedAt` 可能是 now 兜底值（与真实创建时间相差很大），用它算出的 duration 会是 0 或错误值；
- 因此需要从历史行取回 `started_at`。

⇒ 新增 DAO 方法 `getStartedAt(historyId, db)`（`SELECT started_at FROM proxy_runtime_history WHERE id = ?`）。

### D3：不变式

- 「历史表是纳管独立代理进程唯一信源、缺乏活体记录则补齐」**保持不变**（两者都不命中 ⇒ 仍 append）；
- 「写入历史表不是 update，应该是 append」**收敛为**：仅在**没有任何命中行**时 append；已有命中行时只做**属性更新**（`duration_ms` / `socks_port`），不新增行；
- 被复用的行**不得**被 `finalizeStop` 提前结束（否则该实例的运行时长统计被截断，且 `lookupSocksPort` 因 `ended_at IS NULL` 条件取不到）。

---

## 3. 改动清单

- [x] **Task 0**：`include/ProxyRuntimeHistory.h` + `src/ProxyRuntimeHistory.cpp` 新增
  `std::string getStartedAt(int64_t historyId, sqlite3* db = nullptr);`
  （`SELECT started_at FROM proxy_runtime_history WHERE id = ?`；未命中返回空串；无事务）
- [x] **Task 1**：`src/ui/AppController.cpp` 纳管主块（`:2185-2224`）改造：
  - 三元组命中 ⇒ 复用 + `touchHeartbeat`；
  - 三元组不命中 + 宽匹配命中 ⇒ 复用该行 + `touchHeartbeat`（**删除 `finalizeStop` 旧行**，**不 append**）；
  - 两者都不命中 ⇒ 保持 `insertStartWithPort`。
- [x] **Task 2**：`src/ui/AppController.cpp` Bypass 块（`:2086-2139`）同样改造（复用 + `touchHeartbeat` + `updateSocksPort` 纠正端口；移除原 `else if (historyPort != adoptedPort)` 分支，改为无条件纠正）。
- [x] **Task 3**：`tests/test_runtime_history_dao.cpp` 新增 `getStartedAt` 用例（返回存储值 / 未知 id 返回空串）。
- [x] **Task 4**：构建与测试（见 §4）。
- [x] **Task 5**：文档登记（见 §6）。

---

## 4. 验证

### 4.1 构建与自动化测试

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
cd build
ctest -R "StandaloneAdoptionPolicyTest|RuntimeHistoryDAOTest|ProcessInspectorTest|UtilsTest" -V
ctest -V
```

- 期望：定向 **4/4 全绿**；全量 **54/56**，失败项恰为既有基线 `XrayManagerStartupTest` + 已知并行 flaky `UI_FLOATINGWIDGET`。
- `auto` 红线：`git diff HEAD -U0 | Select-String '^\+.*\bauto\b'` ⇒ **0 行**（未跟踪新文件须单独 grep）。

### 4.2 实机验收

1. 启动 `bin\validproxy.exe`，确认纳管日志出现且 `source=` / `verdict=` 正常。
2. 连续观察 **两个以上** 30s 纳管周期：同一 `(indexId, pid)` 在 `proxy_runtime_history` 中**不得**新增行（`ended_at IS NULL` 的行数不增长）。
3. 查库确认命中行的 `duration_ms` 随时间增长（`SELECT id, started_at, duration_ms, ended_at FROM proxy_runtime_history WHERE pid = ? AND ended_at IS NULL`）。
4. 切库到空库 ⇒ 纳管**应 append** 一条新行（两者都不命中的分支仍生效）。
5. 验收前备份 `bin\worker\guiNDB.db`（生产库会被写入）。

---

## 5. 风险与边界

- **R1**：`touchHeartbeat` 对已 `finalizeStop` 的行是 no-op（仍返回 true）⇒ 不会误改历史行。
- **R2**：复用宽匹配行 ⇒ 该行的 `started_at` 仍是旧记录（可能与真实创建时间不同）⇒ 三元组在后续轮次仍可能失配，但每次都走宽匹配复用 ⇒ **不再产生新行**（本缺陷修复的核心收益）。
- **R3**：宽匹配复用意味着「同一 pid 的旧实例行」不会被关闭 ⇒ 若 pid 确实被复用，新实例会沿用旧行。缓解：`proxy_runtime_history.pid` 记录实例身份，配合 D5 实例闸门（`checkInstanceIdentity`）在纳管入口先做 pid 复用判定；本方案不改该闸门。
- **R4**：`duration_ms` 的基准取历史行 `started_at` ⇒ 若该值为空（`getStartedAt` 返回空），则 duration 记 0（保守，不虚报）。
- **N1**（非目标）：`start_count` 恒 0 的归零机制（`src/ProfileExItemDAO.cpp`）。
- **N2**（非目标）：生产库 128 条存量孤儿行的数据治理。
- **N3**（非目标）：切库时旧 watcher 跨库 `finalizeStop` 与 `db_` 无同步改写。

---

## 6. 文档登记

- `docs/INDEX.md` §9.1 缺陷修复表新增编号 **102**（当前末号 101）。
- `docs/plans/project-plans-tracker.md`「近期文档引用」表**表尾追加**一行（数据行 5 列，末列为状态）。
- 本文档追加「实施记录」附录。


---

## 附录：实施记录（v1.0，已落地）

### 7.1 代码改动表

| # | 文件 | 改动 |
| --- | --- | --- |
| 1 | `include/ProxyRuntimeHistory.h` | 新增 `std::string getStartedAt(int64_t historyId, sqlite3* db = nullptr);` |
| 2 | `src/ProxyRuntimeHistory.cpp` | 实现 `getStartedAt`（单条 SELECT，无事务；未知 id 返回空串） |
| 3 | `src/ui/AppController.cpp` 纳管主块 | 三元组命中或宽匹配命中 ⇒ 复用该行 + `touchHeartbeat` 刷新 `duration_ms`；不再 `finalizeStop` 旧行、不再 append；两者都不命中才 `insertStartWithPort` |
| 4 | `src/ui/AppController.cpp` Bypass 块 | 同上改造；`updateSocksPort` 改为无条件执行 |
| 5 | `tests/test_runtime_history_dao.cpp` | 新增 2 例：`GetStartedAt_ReturnsStoredValue` / `GetStartedAt_UnknownIdReturnsEmpty` |

### 7.2 验证结果

- 构建 `cmake --build build --parallel 8` ⇒ **exit=0**（无新增警告）
- 定向 ctest（在 `build/` 目录）`ctest -R "StandaloneAdoptionPolicyTest|RuntimeHistoryDAOTest|ProcessInspectorTest|UtilsTest"` ⇒ **4/4 全绿**
- 全量 `ctest` ⇒ **55/56**（唯一失败 `XrayManagerStartupTest`，既有基线；`UI_FLOATINGWIDGET` 本轮通过）
- `auto` 检查：`git diff HEAD -U0 -- '*.cpp' '*.h'` ⇒ **0**

### 7.3 剩余残留（不在本缺陷范围）

- 生产库存量 128 条 `ended_at IS NULL` 孤儿行与 11 组重复三元组（历史数据，需另案治理）
- `start_count` 恒 0（`ProfileExItem` 的 `INSERT OR REPLACE` 归零机制）
- 切库时旧 watcher 跨库 `finalizeStop` 与 `db_` 无同步改写
- 创建时间指纹秒级精度（同秒 pid 复用不可区分）