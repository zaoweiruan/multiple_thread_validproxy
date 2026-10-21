# 纳管独立代理进程的端口回退实施方案（历史表优先 + OS 兜底）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 启动独立代理时把监控所需的关键运行参数落到 `proxy_runtime_history`；纳管游离进程时按「配置 → 历史表配置路径 → 历史表端口 → OS TCP 表」四层解析，确保配置文件丢失时 `socksPort` 仍然有效。

**Architecture:** 历史表由「纯审计表」扩展为「会话审计 + 实例运行参数」；写入点在启动/接管的成功路径，读取点在纳管与监控表格。

**Tech Stack:** C++17 / SQLite（idempotent ALTER）/ Windows API（`GetExtendedTcpTable`）/ GoogleTest / CMake + Ninja（MinGW GCC）

**依据文档：** `docs/specs/2026-10-20-Spec-StandaloneAdoptionOsFallback-v1.0.md`（v1.0，被本文取代）

**约束：** 全栈禁止使用 `auto` 进行类型推导（AGENTS.md §1）；测试源码只允许放在 `tests/`；`iphlpapi` 已由 `CMakeLists.txt` 顶层 `link_libraries(iphlpapi)` 全局链接。

---

## 0. 背景与目标

### 0.1 现状（已核实）

`AppController::adoptDanglingStandaloneProxies()`（`src/ui/AppController.cpp` 1545–1733）的端口解析顺序：

1. **Step (a)**：命令行 `-c` 全路径 → `readStandaloneInboundPort()`
2. **Step (b)**：探测 `<exeDir>\config\`、`<exeDir>\worker\config\`
3. 均失败 → 仅一条 WARN，随后**仍以 `socksPort = 0` 纳管**

`proxy_runtime_history` 现有列（`src/ProxyRuntimeHistory.cpp::migrateTable`）：

```
id, index_id, started_at, ended_at, exit_code, duration_ms, source, pid
```

**无任何运行参数列**。三处副作用：

- 纳管时端口只能靠猜目录或放弃；
- 监控表格 `buildStandaloneMonitorRows()`（约 1426–1452 行）**每次刷新都从磁盘重读配置文件**取端口，文件被删后只能回退到内存值（重启即丢）；
- R6 接管重建进程时需要 `backend` 等信息，目前只能从 `config.json` 的 `use_singbox` 反推。

`StandaloneProxyInfo`（`src/ui/AppController.h:35`）当前持有 `configPath / configFileName / socksPort / runtimeHistoryId / startedAt`，**没有 `apiPort` 与 `backend`**。

### 0.2 目标

| 编号 | 目标 | 验收方式 |
| :--- | :--- | :--- |
| G1 | 启动时把监控关键参数写入历史表 | `updateRuntimeParams()` 单测：4 列写入后可按三元组读回 |
| G2 | 纳管优先用历史表的配置路径与端口 | 日志标注 `source=runtime-history` |
| G3 | 历史表无记录时回落 OS TCP 表 | `listeningTcpPorts` + `pickSocksListeningPort` 单测 |
| G4 | 四层全失败时行为明确且无资源泄漏 | 走查：不残留孤儿 watch key / 半成品历史行 |
| G5 | 监控表格在配置文件被删后仍显示端口 | 走查：磁盘读取失败时改用历史表端口 |

### 0.3 非目标（本次不做）

| 编号 | 非目标 | 归属 |
| :--- | :--- | :--- |
| N1 | 回退路径反写/重建配置文件 | 后续需求 |
| N2 | 通过 `netstat` 子进程取证 | 不采用（CMD 闪窗，AGENTS.md §3.1） |
| N3 | SOCKS5 握手探测精化端口选择 | 后续增强（见 R1） |
| N4 | 历史表保存配置全文 / 入站列表 | 冗余，配置路径已足够 |
| N5 | 用历史表替代 `StandaloneProxyInfo` 内存态 | 内存态仍是同会话权威，历史表服务于跨会话 |

---

## 1. 设计决策

### D1：历史表新增 4 个运行参数列

| 列 | 类型 | 写入点 | 消费方 |
| :--- | :--- | :--- | :--- |
| `socks_port` | INTEGER | start / takeover / adopt 成功 | 纳管回退、监控表格兜底 |
| `api_port` | INTEGER | start / takeover 成功 | R6 接管与后续健康探测（当前 standalone 未存，属预留） |
| `backend` | TEXT | start / takeover 成功（`xray` / `singbox`） | 接管重启时选可执行文件与配置模板 |
| `config_path` | TEXT | start / adopt 成功（绝对路径） | 监控表格复读配置、纳管回退第 (b) 层 |

**不写入**：`exe_path`（由 `backend` + 可执行目录推导）、监听地址、配置全文。

语义上这些列描述**一次进程实例**，与 `(index_id, pid, started_at)` 三元组一致，不会因同一 indexId 多次启动而混淆。

### D2：`socks_port` 是本次需求的核心，`config_path` 是更高价值的一层

纳管时若历史行有 `config_path`，直接按该路径读配置即可——这**严格优于**现有的「猜两个目录」：它是启动时真正使用的那份文件，不存在同名陈旧副本问题。因此把 `config_path` 作为第 (b) 层放在端口列之前。

四层顺序：

```
(a) 命令行 -c 全路径        → 读配置取端口
(b) 历史表 config_path      → 读配置取端口          ← 新增
(c) 历史表 socks_port       → 直接用                ← 新增
(d) OS TCP 表按 pid         → 筛选监听端口          ← 新增
（目录探测仅在 (b) 无记录时作为旧行的兼容路径保留）
```

### D3：写入采用「插入后补写」，不改 `insertStart` 签名

`insertStart` 有 3 个调用点（`AppController.cpp:1137 / 1240 / 1613`）且参与聚合事务，改签名会波及 R6 接管路径。新增：

```cpp
// Backfills the runtime parameters of an in-progress session.
bool updateRuntimeParams(int64_t historyId, int socksPort, int apiPort,
                         const std::string& backend,
                         const std::string& configPath,
                         sqlite3* db = nullptr);
```

补写是启动注解，不参与聚合事务。

### D4：读取用专用查询，不复用 `findInProgressHistory`

```cpp
struct RuntimeParams {
  int socksPort = 0;
  int apiPort = 0;
  std::string backend;
  std::string configPath;
};
bool lookupRuntimeParams(const std::string& indexId, int64_t pid,
                         const std::string& startedAt,
                         RuntimeParams& out, sqlite3* db = nullptr);
```

`WHERE index_id = ? AND pid = ? AND started_at = ? AND ended_at IS NULL
 ORDER BY id DESC LIMIT 1`，三键与既有三元组一致。

### D5：纳管流程必须**先解析端口，再注册监听**

现状是「OpenProcess → 历史行 → `watch()` → 解析端口」。若解析后丢弃进程，`watch` 注册与历史行都会残留。改为先解析、后注册。`startedAt`（`processCreationTime`）不需要打开进程即可获得。

### D6：「均失败则丢弃」不采用，保留端口 0 的可见纳管

丢弃会把仍在监听端口的进程变成**不可见孤儿**：不进浮动监控、不能停止、端口不释放。现状（可见、可终止）严格优于丢弃。若要支持「不纳管」，应以配置开关实现，而非默认行为。**需 owner 裁决**（§5 Q1）。

### D7：字节序转换不引入 `<winsock2.h>`

`ProcessInspector.h` 公开包含 `<windows.h>`，再引入 `<winsock2.h>` 有包含顺序冲突风险；`dwLocalPort` 低 16 位手工转换即可。

---

## 2. 改动清单

### Task 1：Schema 迁移（`src/ProxyRuntimeHistory.cpp`）

- [ ] `migrateTable` 追加 4 条幂等 `ALTER TABLE ... ADD COLUMN`（`socks_port INTEGER`、`api_port INTEGER`、`backend TEXT`、`config_path TEXT`）
- [ ] 更新列清单注释与迁移说明（旧行为 NULL，不影响审计）

### Task 2：DAO（`include/ProxyRuntimeHistory.h` + `.cpp`）

- [ ] `updateRuntimeParams(...)`：仅更新 `ended_at IS NULL` 的行；`socksPort/apiPort <= 0` 写 0；空字符串写 NULL
- [ ] `lookupRuntimeParams(...)`：无记录返回 false，输出零值
- [ ] `ProxyRuntimeHistoryItem` 可选：追加 4 字段并在 `getInProgressSessions` 中读取（供监控表格使用）

### Task 3：启动侧写入（`src/ui/AppController.cpp`）

- [ ] `startStandaloneProxy`：`insertStart` 成功后调用 `updateRuntimeParams(rhId, socksPort, apiPort, backend, configPath, db_)`
- [ ] R6 接管重启路径（`insertStart` 于 1240 行）：同样补写（端口与 backend 取该路径已有变量）
- [ ] `StandaloneProxyInfo` 增加 `apiPort`、`backend` 字段并在此赋值（供同会话读取，避免查库）

### Task 4：OS 查询（`include/ProcessInspector.h` + `src/ProcessInspector.cpp`）

- [ ] `static std::vector<int> listeningTcpPorts(DWORD pid);` —— AF_INET / AF_INET6，`dwOwningPid == pid && dwState == MIB_TCP_STATE_LISTEN`，排序去重
- [ ] `static int pickSocksListeningPort(const std::vector<int>& candidates, int primaryExcludedPort = 0, int secondaryExcludedPort = 0);`
      规则：丢弃非法值 → 排除 API 端口及默认 10085 → 空则 0 → 优先 `>= 1024` → 取最小值
- [ ] 禁用 `auto`

### Task 5：纳管流程重排（`src/ui/AppController.cpp`）

- [ ] 按 D5 把端口解析上移到 `OpenProcess` 之前
- [ ] (b)(c) 历史表：`lookupRuntimeParams` → 先试 `configPath` 读文件，再试 `socksPort`
- [ ] (d) OS 表：`listeningTcpPorts` + `pickSocksListeningPort(ports, apiPort, 10085)`
- [ ] 日志来源互斥且含 pid：`source=cmdline-config` / `source=runtime-history` / `source=os-tcp-table`
- [ ] 全部失败：保留原 WARN，按 D6 以 `socksPort = 0` 纳管
- [ ] 纳管新建历史行时补写已知参数（`configPath` 为空则写 NULL）

### Task 6：监控表格（`src/ui/AppController.cpp` `buildStandaloneMonitorRows`）

- [ ] 磁盘复读失败时，按 `historyId` 取历史表 `socks_port`，再回退内存值（顺序：磁盘 → 历史表 → 内存）
- [ ] **磁盘值可信性校验**：仅当 `utils::isPortListening(diskPort)` 为真时采用磁盘值；否则视为「文件被手工删除后重建/篡改」，改用历史表端口。防止同名陈旧文件把监控端口改错（属既有缺陷，本文顺带修正）

### Task 7：测试（`tests/`）

- [ ] `tests/test_process_inspector.cpp`：`PickSocksListeningPort.*`（空/单值/排除 API/排除次选/优先非特权/过滤非法）+ `ListeningTcpPorts.*`（回环监听可枚举、未知 pid 返回空）
- [ ] `tests/test_proxy_runtime_history.cpp`（新建，登记 CMake）：`RuntimeParams.{RoundTrip, NullWhenNeverWritten, IgnoresFinalizedRow, RejectsNonPositive, EmptyConfigPathIsNull}`
- [ ] 用例显式类型，禁止 `auto`

---

## 3. 验证

1. `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`
2. `cmake --build build --parallel 8`
3. `ctest -R "ProcessInspector|ProxyRuntimeHistory" -V`
4. `ctest -V`（全量；既有基线失败 `XrayManagerStartupTest` 允许保留）
5. `rg -n "\bauto\b" src/ProcessInspector.cpp src/ProxyRuntimeHistory.cpp` 新增行零命中
6. 手工场景：启动独立代理 → 删配置文件 → 重启 GUI → 日志 `source=runtime-history`；清空该行 4 列 → 日志 `source=os-tcp-table`；两者皆无 → `socksPort=0` + WARN

## 4. 风险

| 编号 | 风险 | 缓解 |
| :--- | :--- | :--- |
| R1 | API 端口被 `findAvailablePort` 偏移，OS 层可能误选 | 双排除 + 优先非特权端口 + DEBUG 打全量候选；N3 后续握手探测 |
| R2 | 旧进程无新列 → 历史表层失效 | 由 OS 层覆盖，这正是保留两层的理由 |
| R3 | schema 变更影响既有 DB | 4 条幂等 `ADD COLUMN`，旧行 NULL，不破坏审计 |
| R4 | 流程重排引入 watch/历史行顺序回归 | 「解析失败也不改变纳管结果」；R1 守卫与 R6 接管路径逻辑不动 |
| R5 | 历史表角色从审计扩展为运行参数，长期可能腐化 | 只存「无法从其他持久源推导」的 4 项；配置全文等一律拒绝（N4） |

## 5. 待 owner 裁决

| 编号 | 议题 | 方案 A（推荐） | 方案 B |
| :--- | :--- | :--- | :--- |
| Q1 | 四层全失败时 | `socksPort = 0` 纳管（可见、可终止） | 丢弃（进程变不可见孤儿） |
| Q2 | 列范围 | 4 列（socks_port / api_port / backend / config_path） | 仅 socks_port + config_path |
| Q3 | OS 层 | 本轮实现（覆盖旧进程） | 延后（旧进程场景仍失败） |
