# 纳管独立代理进程的端口回退实施方案（历史表单一来源 + 失败丢弃）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 启动独立代理时把 SOCKS 端口写入 `proxy_runtime_history`；纳管游离进程时若配置文件不可用则从历史表取回端口；三层均失败则丢弃该进程（不纳管）。

**Architecture:** 历史表新增单一列 `socks_port`；纳管解析顺序为「命令行配置路径 → 已知目录探测 → 历史表端口 → 丢弃」。

**Tech Stack:** C++17 / SQLite（idempotent ALTER）/ GoogleTest / CMake + Ninja（MinGW GCC）

**依据文档：** `docs/specs/2026-10-20-Spec-StandaloneAdoptionPortFallback-v1.1.md`（被本文取代）

**约束：** 全栈禁止使用 `auto` 进行类型推导（AGENTS.md §1）；测试源码只允许放在 `tests/`。

---

## 0. 变更记录（相对 v1.1）

| 编号 | v1.1 | v1.2（owner 裁决 2026-10-20） |
| :--- | :--- | :--- |
| Q1 | 全失败时 `socksPort=0` 纳管 | **丢弃**（不纳管） |
| Q2 | 4 列（socks_port/api_port/backend/config_path） | **仅 `socks_port`**；`config_path` 无价值（文件存在时命令行/目录探测即可定位，文件删除时路径指向空） |
| Q3 | 实现 OS TCP 表兜底 | **延后**（与「全失败丢弃」的边界一致，且历史表已覆盖同一会话内的全部新进程） |

推论：`api_port` / `backend` 列同步取消；`StandaloneProxyInfo` 不新增字段。

---

## 1. 背景与目标

### 1.1 现状（已核实）

`AppController::adoptDanglingStandaloneProxies()`（`src/ui/AppController.cpp` 1545–1733）端口解析：

1. **Step (a)**：命令行 `-c` 全路径 → `readStandaloneInboundPort()`
2. **Step (b)**：探测 `<exeDir>\config\`、`<exeDir>\worker\config\`
3. 均失败 → WARN，随后**仍以 `socksPort = 0` 纳管**

`proxy_runtime_history` 现有列（`src/ProxyRuntimeHistory.cpp::migrateTable`）：`id, index_id, started_at, ended_at, exit_code, duration_ms, source, pid` —— 无运行参数列。

监控表格 `buildStandaloneMonitorRows()`（约 1426–1452 行）每次刷新从磁盘重读配置，读不到才回退内存值。

### 1.2 目标

| 编号 | 目标 | 验收方式 |
| :--- | :--- | :--- |
| G1 | 启动时写入 `socks_port` | `updateSocksPort()` 单测写入后可读回 |
| G2 | 纳管时从历史表取回端口 | `lookupSocksPort()` 单测；日志 `source=runtime-history` |
| G3 | 三层失败则丢弃且不残留状态 | 走查：无孤儿 watch key、无新建历史行、进程句柄已关闭 |
| G4 | 丢弃时给出可操作的日志 | 日志含 pid + 建议（手工结束或重新启动） |
| G5 | 监控表格在配置文件被删后仍显示正确端口 | 走查：磁盘值需通过监听校验，否则用历史表值 |

### 1.3 非目标

| 编号 | 非目标 | 说明 |
| :--- | :--- | :--- |
| N1 | OS TCP 表查询 | 延后（Q3） |
| N2 | 保存 config_path / backend / api_port | 无消费价值（Q2） |
| N3 | 反写/重建配置文件 | 后续需求 |
| N4 | `netstat` 子进程取证 | CMD 闪窗（AGENTS.md §3.1） |
| N5 | 对丢弃的进程做收尾（终止或标记） | 见 §5 R1 |

---

## 2. 设计决策

### D1：只存 `socks_port`

用途唯一且明确：**跨会话恢复端口**。其余运行参数（API 端口、backend、配置路径）都能从现有持久源推导，或在该场景下无意义，不写入。

### D2：写入采用「插入后补写」，不改 `insertStart` 签名

`insertStart` 有 3 个调用点（`AppController.cpp:1137 / 1240 / 1613`）且参与聚合事务。新增：

```cpp
// Backfills the SOCKS inbound port of an in-progress session (0 = unknown).
bool updateSocksPort(int64_t historyId, int socksPort, sqlite3* db = nullptr);
```

### D3：读取用专用查询

```cpp
// Returns the recorded SOCKS port of the matching in-progress session,
// or 0 when there is no match / NULL / invalid.
int lookupSocksPort(const std::string& indexId, int64_t pid,
                    const std::string& startedAt, sqlite3* db = nullptr);
```

`WHERE index_id = ? AND pid = ? AND started_at = ? AND ended_at IS NULL
 AND socks_port IS NOT NULL ORDER BY id DESC LIMIT 1`

### D4：解析必须在 `OpenProcess` / `watch` / `insertStart` **之前**

否则丢弃时残留 watch 注册与新建历史行。`startedAt`（`processCreationTime`）不打开进程即可获得，可前置。

新流程：

```
indexId 判定 → 已在 standaloneProxies_/proxyWatchKeys_ 则跳过
  → 端口解析 (a)/(b)/(c)
  → 解析失败 → 丢弃（见 D5）
  → OpenProcess → 复用/新建历史行 → watch → 登记
```

### D5：丢弃的语义与边界

丢弃 = **不纳管、不监控、不建行**：

1. 已 `CloseHandle(hProcess)`（若已打开）——D4 前置后通常尚未打开；
2. 不注册 `exitListener_->watch`；
3. 不调用 `insertStart`（因此不产生新行）；
4. 记录 WARN：`[StandaloneProxy] Discarding dangling process pid=<pid> indexId=<id>: config missing and no runtime-history port; stop it manually or restart it.`；
5. 继续处理下一个进程（**不得** `return` 中断循环）。

**已知副作用**（需评审确认，见 R1）：若该进程此前由本应用启动，其**既有 in-progress 历史行**会保持 `ended_at IS NULL`。该行不会出现在浮动监控（监控按 `runtimeHistoryId` 关联），但会持续存在于 `getInProgressSessions()` 结果中，且下次 GUI 启动时该进程仍会被再次丢弃。

### D6：不新增 `StandaloneProxyInfo` 字段

端口仍存于内存态 `socksPort`；历史表只在纳管时读取一次。

### D7：监控表格采用「磁盘值需可验证」原则

现有 `row.socksPort = (diskPort > 0) ? diskPort : watched[j].socksPort;` 会信任任意同名文件。改为：

1. `diskPort > 0 && utils::isPortListening(diskPort)` → 采用磁盘值；
2. 否则按 `historyId` 取历史表 `socks_port`；
3. 仍为 0 → 回退内存值。

`isPortListening` 是无缓存的 LISTEN 判定（`include/Utils.h`），可排除「文件被篡改/陈旧副本」导致的错误端口。

---

## 3. 改动清单

### Task 1：Schema（`src/ProxyRuntimeHistory.cpp`）

- [ ] `migrateTable` 追加幂等 `ALTER TABLE proxy_runtime_history ADD COLUMN socks_port INTEGER`
- [ ] 更新列清单注释；说明旧行为 NULL（不参与匹配，不影响审计）

### Task 2：DAO（`include/ProxyRuntimeHistory.h` + `.cpp`）

- [ ] `bool updateSocksPort(int64_t historyId, int socksPort, sqlite3* db = nullptr);`
      仅更新 `ended_at IS NULL` 的行；`socksPort <= 0` 直接返回 true（无操作）
- [ ] `int lookupSocksPort(const std::string& indexId, int64_t pid, const std::string& startedAt, sqlite3* db = nullptr);`
- [ ] `ProxyRuntimeHistoryItem` 追加 `int socksPort = 0;`，`getInProgressSessions` 一并 SELECT（供 Task 4 使用）

### Task 3：启动侧写入（`src/ui/AppController.cpp`）

- [ ] `startStandaloneProxy`：`insertStart` 成功后 `historyDao_.updateSocksPort(rhId, socksPort, db_)`
- [ ] R6 接管重启路径（`insertStart` @1240）：同样补写该路径已解析的端口

### Task 4：纳管流程重排（`src/ui/AppController.cpp`）

- [ ] 端口解析上移至 `OpenProcess` 之前（D4）
- [ ] Step (c)：`lookupSocksPort(indexId, pid, startedAt, db_)`
- [ ] 解析失败：按 D5 丢弃 + WARN，`continue`
- [ ] 删除原「解析失败仍以 0 纳管」的路径与无条件 WARN
- [ ] 成功路径日志标注 `source=cmdline-config` / `source=dir-probe` / `source=runtime-history`

### Task 5：监控表格（`buildStandaloneMonitorRows`）

- [ ] 按 D7 改为「磁盘值（需 `isPortListening`）→ 历史表 → 内存值」
- [ ] 历史表值通过 `getInProgressSessions()` 已读取的字段获取，避免新增查询

### Task 6：测试（`tests/`）

- [ ] `tests/test_proxy_runtime_history.cpp`（新建 + 登记 CMake）
      `SocksPort.{RoundTrip, NullWhenNeverWritten, IgnoresFinalizedRow, RejectsNonPositive}`
- [ ] 纳管丢弃路径无可直接单测的入口（依赖 `AppController` + 真实进程表），**不新增** UI 层测试，改由走查 + 手工场景验证（见 §4.6）
- [ ] 禁止 `auto`

---

## 4. 验证

1. `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`
2. `cmake --build build --parallel 8`
3. `ctest -R ProxyRuntimeHistory -V`
4. `ctest -V`（全量；既有基线失败 `XrayManagerStartupTest` 允许保留）
5. `rg -n "\bauto\b" src/ProxyRuntimeHistory.cpp` 新增行零命中
6. **手工场景**
   - S1：启动独立代理 → 删配置文件 → 重启 GUI → 日志 `source=runtime-history`，端口正确
   - S2：启动独立代理 → 删配置文件 → **清空该行 `socks_port`** → 重启 GUI → 日志 `Discarding dangling process`，监控中无该行，进程仍在运行（预期）
   - S3：删配置文件后手工放一个同名不同端口的文件 → 监控端口不被误导（D7）

## 5. 风险与待评审项

| 编号 | 风险 | 缓解 / 待定 |
| :--- | :--- | :--- |
| R1 | **丢弃后进程不可见且其历史行长期 `ended_at IS NULL`** | 待评审：是否需要 `finalizeStop` 收尾（会污染运行时长统计）／是否仅在日志中提示 |
| R2 | **本次改动前启动的进程行内无 `socks_port`** → 必被丢弃 | 属 Q3 延后的既定代价；上线后新启动的进程不受影响 |
| R3 | 丢弃在循环中必须 `continue`，不得 `return` | 走查 + 代码注释标注 |
| R4 | 流程重排影响 R1 重复启动守卫 / R6 接管 | 守卫与接管逻辑不动，仅移动端口解析位置 |
| R5 | schema 变更 | 幂等 `ADD COLUMN`，旧行 NULL |
| R6 | `isPortListening` 只能证明「某进程监听该端口」，不能证明「是该 pid」 | 已足以排除陈旧文件；pid 级校验需 OS 层（Q3） |

## 6. 待评审结论项

- [ ] D5 的丢弃边界是否成立（尤其 R1 的历史行残留）
- [ ] Task 4 无自动化测试是否可接受
- [ ] D7 是否应纳入本方案，还是拆为独立缺陷修复
