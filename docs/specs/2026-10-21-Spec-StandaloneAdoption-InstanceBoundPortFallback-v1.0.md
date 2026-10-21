# 纳管独立代理进程的端口回退实施方案 v1.0（实例级闸门 + 历史表优先 + OS 活体校验）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 纳管游离的独立代理进程时，端口来源按「运行时历史 → 命令行配置文件 → 目录探测 → OS 活体」逐层回落，**每一层的候选都必须先经 OS 活体事实校验**；并用「句柄前移 + 创建时间一致性」闸门保证纳管的是**当前这个进程实例**。

**Architecture:** 纳管流程重排为「枚举 → 识别 → 已在管跳过 → **OpenProcess（句柄前移）** → **实例闸门** → **端口解析（历史 → 命令行 → 目录 → OS，逐层 OS 校验）** → 历史行/监听注册」；OS 事实（该 pid 的 LISTEN 端口集合 + SOCKS5 握手应答子集）**一次查询、层层复用**；监控路径（`getWatchedStandaloneMonitors`）同步改为同一条链，消除「纳管算对、监控期被磁盘陈旧副本推翻」的残留缺口。

**Tech Stack:** C++17 / SQLite（`proxy_runtime_history.socks_port` 已在库）/ Win32（`GetExtendedTcpTable` + SOCKS5 握手探测）/ GoogleTest / CMake + Ninja（MinGW GCC）

**依据文档：** `docs/bugfix/2026-10-21-Bugfix-StandaloneAdoption-OsPortFallback-v1.0.md`（上一阶段：OS 层作为**首选来源**已实施）；`docs/specs/2026-10-20-Spec-StandaloneAdoptionPortFallback-v1.3.md`（历史表作为**回落来源**的上一版方案）

**约束：** 全栈禁止使用 `auto` 进行类型推导（AGENTS.md §1）；测试源码只允许放在 `tests/`；文档先行，改完登记 `docs/INDEX.md` 与 `docs/plans/project-plans-tracker.md`。

---

## 0. 变更记录

| 版本 | 内容 |
| :--- | :--- |
| v1.0 | 首版。经现场 RCA（pid 13492 被丢弃 / pid 12776 端口陈旧 10810）与用户多轮裁决收敛：①端口来源改为**历史表优先**（历史 > 命令行 > 目录探测 > OS）；②新增**实例闸门**（句柄前移 + 创建时间一致性校验）；③**关键洞**：OS 校验必须作用于**每一层**的候选，而非只作用于 OS 层自己；④**降级规则**「无法校验 ⇒ 放行」；⑤**监控路径同步**修复残留缺口。 |

---

## 1. 背景与根因

### 1.1 现场证据（已核实，行号为当前工作区）

| 事实 | 证据 |
| :--- | :--- |
| 两个 xray 进程都在运行 | pid **13492**（indexId `5046338036629160457`，创建 08:39:08，监听 `127.0.0.1:10808`）、pid **12776**（indexId `4949226207915085546`，创建 08:39:14，监听 `127.0.0.1:10809`） |
| 两个 `standalone_*.json` 配置文件**均已删除** | `Test-Path` → MISSING |
| 上一阶段修复后 13492 已被纳管、端口正确 | 新增历史行：pid 13492 → `socks_port=10808`、`ended_at=NULL`；pid 12776 的 `socks_port` 由陈旧的 `10810` **纠正为 `10809`** |
| 改动前 13492 被丢弃 | 日志 `[WARN] [StandaloneProxy] Discarding dangling process pid=13492 ...: config missing and no runtime-history port.` |
| 12776 的历史端口 10810 是**陈旧值** | 来自 `bin\config\` 的旧副本（2026-09-02 端口漂移缺陷：同一 indexId 在 `bin\config` 与 `bin\worker\config` 两套目录端口不同），实际监听 10809 |

### 1.2 根因

1. **来源层覆盖不到「升级前创建的进程」**：v1.3 的三层链（命令行 → 目录探测 → 历史表）在配置文件被删时只剩历史表；而历史表的 `socks_port` 列是**本版本新增**的，旧二进制创建的进程行 `socks_port IS NULL` ⇒ 全部落空 ⇒ 丢弃。v1.0 设计的 OS 层回落被 v1.2/v1.3 延后（上一阶段才补上，且当时是「OS 首选」）。
2. **丢弃会关闭历史行**（`finalizeStop`）⇒ 该 pid 后续连历史端口都查不到（`lookupSocksPort` 要求 `ended_at IS NULL`）⇒ 自愈路径被切断。
3. **陈旧端口被直接采纳**：历史表/磁盘层返回的端口**从不与活体进程核对**。12776 的 10810 被写入 `socks_port` 后，周期探活（`doTestOnlineProxies`，`src/ui/AppController.cpp:2382-2430`）每 5s 打错端口 ⇒ 永久失败。

> 关键澄清（已核实）：**周期探活失败不修改 `proxy_runtime_history`**。探活只写 `ProfileExItem`（`exDao_.updateTestResult` → delay / message / `consecutive_failures`）。`proxy_runtime_history` 的写入点仅 4 个：`insertStart`、`updateSocksPort`、`finalizeStop`、`touchHeartbeat`。13492 那行是被**纳管丢弃分支**关闭的，与探活无关。

### 1.3 目标

| 编号 | 目标 | 验收方式 |
| :--- | :--- | :--- |
| G1 | 端口来源按「历史表 → 命令行配置 → 目录探测 → OS」逐层回落，首个**通过校验**的候选被采用 | 纯策略单测 + 纳管实机验证 |
| G2 | **每一层**的候选都经 OS 活体校验（历史层的陈旧端口不得被直接采纳） | 纯策略单测（候选 ∈ osPorts ∧ socksPorts）+ 实机（12776 端口由 10810 纠正为 10809） |
| G3 | 纳管的是**当前进程实例**：句柄前移 + 创建时间一致性校验，不一致即放弃 | 代码走查 + 实机（重启 xray 后旧 pid 记录不被误纳管） |
| G4 | OS 事实**一次查询、层层复用**（不做 N 次 `GetExtendedTcpTable`） | 代码走查（单次 `listeningTcpPorts` + 单次握手扫描） |
| G5 | **无法校验 ⇒ 放行**（OS 表读不到 / 握手全超时不得否掉候选） | 纯策略单测 |
| G6 | 监控路径与纳管路径使用**同一条链**（消除「纳管算对、监控期被磁盘陈旧副本推翻」） | 代码走查 + 实机（磁盘有陈旧副本时监控端口仍为活体端口） |
| G7 | 存量 `socks_port` 行在纳管时被核对一次（零额外成本：历史表本已查） | 代码走查 + 实机 |

### 1.4 非目标（本次不做）

| 编号 | 非目标 | 归属 |
| :--- | :--- | :--- |
| N1 | 监控表格**显示**端口的磁盘值校验（原 v1.2 的 D7 独立缺陷） | 已被 G6 覆盖大半，剩余纯显示诉求单开 |
| N2 | 进程级「实例化」管理（同一 indexId 多实例并存） | 架构级，另议 |
| N3 | 跨机器/远程纳管 | 本项目为单机工具 |
| N4 | 把 `api_port`/`backend`/`config_path` 也持久化到历史表 | 上一版 v1.1 已裁决收窄为仅 `socks_port` |
| N5 | 用 `GetExtendedTcpTable` 区分「查表失败」与「无监听」（返回读取状态） | 现阶段按「无法校验 ⇒ 放行」处理，必要时再加 |

---

## 2. 设计决策

### D1：端口来源链 = 历史表 → 命令行配置 → 目录探测 → OS 活体

**决策。** 采纳用户裁决「历史表优先」。理由：

- **历史表是实例级证据**：行绑定 `(indexId, pid, started_at)`，记录的是「这个进程实例被真正启动时的端口」，不会被之后的文件改写/删除作废。
- **命令行/目录探测是文件级证据**：读的是「此刻磁盘上的配置」，可能已被外部改写或指向另一个 indexId 的旧副本。
- **OS 活体是事实而非来源**：它回答「现在到底哪个端口在听」，因此在下一层（D2）中被用作**校验器**而不是首选来源；只有在前面所有层都没有候选时，它才作为最后的候选来源。

**接口影响（破坏性）。** `include/StandaloneAdoptionPolicy.h` 的 `resolvePort` 签名从
`resolvePort(int osPort, int historyPort, int cmdlinePort, int dirProbePort)` 改为
`resolvePort(int historyPort, int cmdlinePort, int dirProbePort, int osPort)`；
`enum class PortSource` 成员顺序随之调整，新增 `PortSource::OsTcpTable` 保持存在（作为最后一层来源标签）。

### D2：OS 活体校验必须作用于**每一层**的候选（本方案的关键洞）

**决策。** 候选端口 `c` 的合法性判据：

```
valid(c) = (1 <= c <= 65535)
         ∧ (osPorts 为空 ⇒ true)                      // 无法校验 ⇒ 放行
         ∧ (c ∈ osPorts)
         ∧ (socksPorts 非空 ⇒ c ∈ socksPorts
            else ⇒ osPorts.size() == 1)               // 唯一候选放宽
```

其中 `osPorts` = 该 pid 的全部 LISTEN 端口（`ProcessInspector::listeningTcpPorts(pid)`），`socksPorts` = 其中对 SOCKS5 问候（`05 01 00`）应答 `05 xx` 的子集（`utils::respondsToSocks5Greeting`）。

**为什么这是关键洞。** 上一阶段把 OS 层当作**首选来源**，于是 12776 的陈旧历史端口 10810 虽然被 OS 层覆盖成 10809，但**历史层本身仍没有校验**——一旦历史层排在 OS 之前（用户裁决），若无本条规则，10810 会被直接采纳，探活继续失败。**校验必须覆盖每一层，而不只是产生候选的那一层。**

**逐层语义。** 各层产出的候选按链顺序检查，**首个通过校验的候选被采用**；某层候选未通过 ⇒ 继续下一层（而不是立即丢弃）。只有「所有层都没有候选」或「所有候选都未通过校验」时才丢弃。

**排除 API 端口。** 候选还应排除 gRPC api 入站端口（`config_.proxy.api_base_port > 0 ? : 10085`）。注意：实测 10085 根本未监听（xray standalone 未启用 api inbound），故该排除是防御性的；SOCKS5 握手本身已能把 api 入站区分开（api 返回 HTTP 而非 `05`）。

### D3：OS 事实一次查询、层层复用

**决策。** 纳管循环内**每个 pid 只做一次** `listeningTcpPorts(pid)` 与**一次**握手扫描（对 `osPorts` 中每个候选各探一次），结果缓存到局部变量，供：①D2 校验历史层候选、②校验命令行层候选、③校验目录探测层候选、④作为 OS 层自身的候选来源。监控路径（`getWatchedStandaloneMonitors`）同理，每个在管代理一次。

**理由。** `GetExtendedTcpTable` 是全表扫描（v4 + v6 各一次），每层各查一次会把每进程的 OS 查询放大 3–4 倍；纳管每 `checkIntervalMs`（默认 30s）周期执行，放大直接乘在稳态 CPU 上。

### D4：实例闸门（句柄前移 + 创建时间一致性校验）

**决策。** 纳管循环的顺序调整为：

```
枚举 xray.exe / sing-box.exe
  → 跳过空命令行
  → 解析 configFileName → 派生 indexId
  → 跳过已在管（standaloneProxies_ / proxyWatchKeys_）
  → OpenProcess(PROCESS_QUERY_INFORMATION|SYNCHRONIZE|PROCESS_TERMINATE)   // ← 句柄前移
  → instanceStartedAt = ProcessInspector::processCreationTime(hProcess)    // 句柄级权威值
  → 若 procs[i].creationTime 非空 且 != instanceStartedAt ⇒ CloseHandle + continue   // 实例闸门
  → 端口解析（历史 → 命令行 → 目录 → OS，逐层 OS 校验）                       // 无候选 ⇒ 丢弃
  → findInProgressHistory / insertStart（用 instanceStartedAt）
  → exitListener_->watch(...)
  → 写入 standaloneProxies_ / proxyWatchKeys_
```

**闸门判据与理由。** `procs[i].creationTime` 是**枚举时刻**读到的值（`enumerateByName` 内部 `OpenProcess` + `GetProcessTimes`），而 pid 在枚举与后续操作之间**可能被系统回收并复用**给另一个进程。句柄上的创建时间才是「我们打开的这个实例」的权威标识；两者不一致 ⇒ 我们枚举到的是一个已经死掉、pid 已被复用的旧记录 ⇒ 绝不能纳管、不能写历史行、不能建 watcher。

**副产物（消除历史包袱）。** 闸门让 `startedAt` 权威化：`procStartedAt` 不再需要「`creationTime` 为空 ⇒ 用当前秒级时间兜底」这条退化路径（该兜底会使 `(indexId, pid, started_at)` 三元组与写入方的值失配 ⇒ `lookupSocksPort` 假阴性）。因此：

- `startedAt` 一律取 `instanceStartedAt`（句柄级，权威）；
- `ProxyRuntimeHistoryDAO::lookupSocksPort` 的「`startedAt` 为空 ⇒ 降级为 `(index_id, pid)` 双键」宽匹配路径**可以退休**（不再是必需的兜底），但**保留**不删（对存量脏数据仍是无害的宽松匹配）。

**精度说明。** 创建时间字符串为秒级（`yyyy-MM-dd HH:mm:ss`），故「同一秒内 pid 复用」无法区分。该窗口极小，且复用后新进程通常不是 `xray.exe`/`sing-box.exe`，风险可接受。**不做**更高精度的比较（需要 `FILETIME` 精确值，超出本次范围）。

### D5：降级规则 —— 「无法校验 ⇒ 放行」

**决策。** 当 `osPorts` 为空（查表失败、或该 pid 确实没有 LISTEN 端口）时，**不否决**任何层的候选，直接放行。

**理由。** 两类失败的代价不对称：

| 情形 | 放行（信任历史/文件） | 否决（丢弃进程） |
| :--- | :--- | :--- |
| OS 表确实读不到（权限/瞬时失败） | 端口可能陈旧 ⇒ 探活失败，但代理**可见** | 代理**从监控中消失** ⇒ 用户无法停止它、下次启动还会撞 R1 冲突对话框 |
| 该进程确实无 LISTEN（已死/非代理） | 探活失败但可见 | 丢弃更干净 |

「可见但探活失败」远优于「不可见」。故默认放行，只有在**有活体事实且候选与之矛盾**时才否决（即 `osPorts` 非空但候选 ∉ `osPorts`）。

**唯一候选放宽。** 若 `osPorts` 非空但 `socksPorts` 为空（全部握手超时/无应答），且 `osPorts.size() == 1`，则该唯一端口放行——避免握手超时误杀健康代理。若 `osPorts.size() > 1` 且 `socksPorts` 为空，则不自动放宽（无法区分），交由后续层或丢弃处理。

### D6：写入端不变式

**决策。** 确立不变式：**今后写入 `proxy_runtime_history.socks_port` 的值必须已被证实**。

| 写入路径 | 证实方式 |
| :--- | :--- |
| `startStandaloneProxy`（UI 启动） | `waitForPort(socksPort, ...)` 已确证端口在监听 |
| R6 接管路径 | 本方案要求同样经 D2 校验（见 Task 1 附带项） |
| 纳管路径 | 本方案的 D2 校验 |

**推论。** 新写入的行可直接被后续纳管取用，无需二次校验的**必要性**降低；但存量行（写入时未经证实、且由旧二进制创建）仍需核对一次 —— 核对成本为零，因为历史表本就要查。

### D7：监控路径必须同步（残留缺口）

**决策。** `getWatchedStandaloneMonitors()`（`src/ui/AppController.cpp:1412-1525`）当前用**磁盘配置**重读端口并**覆盖**内存值：

```cpp
row.socksPort = (diskPort > 0) ? diskPort : watched[j].socksPort;   // :1485
```

而周期探活直接使用行值（`src/ui/AppController.cpp:2421` `int port = mon.socksPort;`）。⇒ 磁盘存在陈旧副本时，**纳管阶段算对的端口在监控期又被推翻**，探活继续失败。

**改动。** 监控路径改为与纳管路径**同一条链**：

1. 历史表 `socks_port`（`byId` join 得到的行 → `hit->second->socksPort`）——但注意：纳管时已把校验后的端口写回历史行，故历史层值本身已是核实值；
2. 命令行/磁盘配置（`configPath` → 两目录探测）；
3. OS 活体（`listeningTcpPorts(pid)` + SOCKS5 握手）——**对磁盘层候选同样做 D2 校验**。

**实施要点。** `row.pid` 当前在 `:1497`（`byId.find(watched[j].historyId)`）才赋值，**晚于**端口块（`:1468-1486`）。要按 pid 做 OS 校验，必须把 `byId` 查表**上移**到端口块之前（或把端口块下移到 `:1504` 之后）。`pid == -1`（历史行缺失）⇒ 无法做 OS 校验 ⇒ 按 D5 只用文件层（放行语义）。

**不做。** 不改「监控表格显示哪一列」；只改**端口取值链**。

### D8：顺带一致性 —— `startStandaloneProxy` 用句柄取创建时间

**决策。** `src/ui/AppController.cpp:1145` 已有 `pi.hProcess`，却又调 `processCreationTime(pi.dwProcessId)` 重新 `OpenProcess` 取创建时间。改为用句柄取同一值（`processCreationTime(pi.hProcess)`），少一次 `OpenProcess` 并消除「枚举后进程已重启 ⇒ 取到新实例创建时间」的竞态。

### D9：纯策略抽取（可单测）

**决策。** 判定逻辑抽到纯头文件 `include/StandaloneAdoptionPolicy.h`（先例 `src/ui/UnifiedMonitorRows.h`），新增：

```cpp
// 端口是否属于该 pid 的 LISTEN 集合
inline bool isPortOwnedByProcess(int candidate, const std::vector<int>& osPorts);
// 端口是否属于 SOCKS5 应答子集
inline bool isSocksAnsweringPort(int candidate, const std::vector<int>& socksPorts);
// D2 的完整判据（含 D5 放行与唯一候选放宽）
inline bool validatePortCandidate(int candidate,
                                  const std::vector<int>& osPorts,
                                  const std::vector<int>& socksPorts);
```

并在 `resolvePort` 之上加一层「逐层校验后取值」：

```cpp
// 按链顺序取首个通过校验的候选；全部落空返回 port=0
inline PortResolution resolveValidatedPort(int historyPort, int cmdlinePort,
                                           int dirProbePort, int osPort,
                                           const std::vector<int>& osPorts,
                                           const std::vector<int>& socksPorts);
```

`AppController` 侧只负责「取事实」（`listeningTcpPorts` / `respondsToSocks5Greeting` / `lookupSocksPort` / `readStandaloneInboundPort`）与「落库」，判定全部走纯函数 ⇒ 100% 可单测。

### D10：丢弃语义（收窄）

**决策。** 丢弃条件收窄为：①所有层都没有候选端口；或 ②`osPorts` 非空（活体事实可得）且**所有层候选都未通过校验**。丢弃时沿用既有语义：关闭该 pid 的残留历史行（`findInProgressHistoryLoose` + `finalizeStop`）、按 pid 去重日志（首次 WARN / 后续 DEBUG）、`continue`（不建句柄外的任何资源）。

---

## 3. 改动清单

### Task 0：纯策略头扩展 + 单测

- [ ] `include/StandaloneAdoptionPolicy.h`：
  - [ ] `enum class PortSource` 调整为 `{ None, RuntimeHistory, CmdlineConfig, DirProbe, OsTcpTable }`（**破坏性**：既有 `resolvePort` 参数顺序随之改变）
  - [ ] `resolvePort(int historyPort, int cmdlinePort, int dirProbePort, int osPort)`（首个 >0 优先，顺序 = 历史 → 命令行 → 目录 → OS）
  - [ ] 新增 `isPortOwnedByProcess` / `isSocksAnsweringPort` / `validatePortCandidate`（D2 + D5 判据）
  - [ ] 新增 `resolveValidatedPort(...)`（逐层校验后取值）
  - [ ] `portSourceLabel()` 覆盖全部枚举值
- [ ] `tests/test_standalone_adoption_policy.cpp` 扩展为下列用例（既有 15 例按新签名重写）：
  - [ ] `ResolvePort_PrefersHistory` / `PrefersCmdlineOverDirProbe` / `FallsBackToOs` / `NoneWhenAllZero` / `IgnoresNegative`
  - [ ] `ValidatePortCandidate_RejectsPortNotOwnedByProcess`
  - [ ] `ValidatePortCandidate_RejectsNonSocksWhenSocksSetKnown`
  - [ ] `ValidatePortCandidate_AcceptsSocksAnsweringPort`
  - [ ] `ValidatePortCandidate_AllowsSingleCandidateWhenHandshakeEmpty`（唯一候选放宽）
  - [ ] `ValidatePortCandidate_RejectsMultipleCandidatesWhenHandshakeEmpty`
  - [ ] `ValidatePortCandidate_PassesThroughWhenOsPortsEmpty`（无法校验 ⇒ 放行）
  - [ ] `ValidatePortCandidate_RejectsOutOfRange`
  - [ ] `ResolveValidatedPort_SkipsInvalidHistoryFallsToCmdline`
  - [ ] `ResolveValidatedPort_ReturnsZeroWhenAllRejected`
  - [ ] `ResolveValidatedPort_PrefersHistoryWhenValid`
  - [ ] `PortSourceLabel_AllValues`

### Task 1：纳管流程重排（`src/ui/AppController.cpp`）

- [ ] `adoptDanglingStandaloneProxies()`：把 `OpenProcess`（当前 `:1750`）**前移**到「已在管跳过」（`:1627`）之后、端口解析之前
- [ ] 闸门：句柄取 `instanceStartedAt = proc::ProcessInspector::processCreationTime(hProcess)`；与 `procs[i].creationTime` 非空且不等 ⇒ `CloseHandle(hProcess)` + `continue`（**不写历史行、不建 watcher、不记 discarded**）
- [ ] 端口解析块（当前 `:1629-1713`）改为链：历史表 → 命令行 → 目录探测 → OS；**每一层候选**先过 `validatePortCandidate`
- [ ] OS 事实一次查询：`osPorts = proc::ProcessInspector::listeningTcpPorts(pid)`；`socksPorts` = 其中 `utils::respondsToSocks5Greeting(p, 200)` 为真者；两者**只取一次**并复用于全部层
- [ ] 历史层候选改为取 `historyDao_.lookupSocksPort(indexId, pid, instanceStartedAt, db_)`（用权威创建时间；空值时 DAO 内部降级双键）
- [ ] 逐层取值改用 `standalone_adoption::resolveValidatedPort(...)`
- [ ] 丢弃分支（当前 `:1714-1744`）按 D10 收窄条件；日志文案补充「哪些层有候选但被 OS 否决」的可诊断信息（`source` 标签 + 候选值）
- [ ] `procStartedAt`（当前 `:1761-1764`）改用 `instanceStartedAt`，**删除**秒级兜底
- [ ] 纳管成功路径：把校验后的端口写回历史行 `updateSocksPort(rhId, adoptedPort, db_)`（存量行核对 ⇒ G7）
- [ ] R6 接管路径（`:1256-1287`）：其端口候选同样过 `validatePortCandidate`（D6 不变式）

### Task 2：监控路径同步（`src/ui/AppController.cpp`）

- [ ] `getWatchedStandaloneMonitors()`：把 `byId` 查表（当前 `:1494-1504`）**上移**到端口块（`:1468-1486`）之前，使 `row.pid` 在端口解析时可用
- [ ] 端口取值改为同一条链：历史表 `socks_port`（`hit->second->socksPort`）→ 磁盘配置（`configPath` → 两目录探测）→ OS 活体；**磁盘层候选同样过 `validatePortCandidate`**
- [ ] `pid == -1`（历史行缺失）⇒ 无法做 OS 校验 ⇒ 仅用磁盘层（放行语义）
- [ ] 保留 `row.socksPort` 的最终赋值语义（探活消费该字段，`:2421`）

### Task 3：顺带一致性（`src/ui/AppController.cpp`）

- [ ] `startStandaloneProxy`：`:1145` 的 `processCreationTime(pi.dwProcessId)` 改为 `processCreationTime(pi.hProcess)`

### Task 4：测试

- [ ] `tests/test_standalone_adoption_policy.cpp`：按 Task 0 的用例清单重写/扩展
- [ ] `tests/test_process_inspector.cpp`：既有 `ListeningTcpPorts_*` 用例保持；如需补「进程退出后端口从集合中消失」用例则新增
- [ ] `tests/test_utils.cpp`：既有 `Socks5GreetingTest` 保持
- [ ] 全部用例禁止 `auto`；新增代码禁止 `auto`（AGENTS.md §1）
- [ ] 登记 CMake：`StandaloneAdoptionPolicyTest`（既有），必要时补 `ProcessInspectorTest` / `UtilsTest` 的 `ws2_32` 链接

### Task 5：文档登记

- [ ] 登记 `docs/INDEX.md` §7.5（specs 表，编号顺延）
- [ ] 同步 `docs/plans/project-plans-tracker.md`「近期文档引用」表（追加于表尾，日期 2026-10-21，类型 `spec`）

---

## 4. 验证

```powershell
# 1. 配置 + 构建（Debug / Ninja）
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8

# 2. 定向测试
ctest -R "StandaloneAdoptionPolicyTest|ProcessInspectorTest|RuntimeHistoryDAOTest|UtilsTest" -V

# 3. 全量回归
ctest

# 4. 禁用 auto 自查（新增/修改行）
git diff -U0 | Select-String -Pattern "^\+" | Select-String -Pattern "\bauto\b"
```

**实机验收（Windows 生产现场）**

1. 启动 `bin\validproxy.exe`，观察日志出现两个 `[StandaloneProxy] Adopted dangling process pid=... on SOCKS5 :...`（REPORT 级，注意 `bin\config.json` 的 `log.file_level="WARN"` 会屏蔽 REPORT ⇒ 需临时把 `file_level` 调低或直接查库）。
2. 查库 `proxy_runtime_history`：`pid` 列 = 13492 / 12776，`socks_port` 分别 = 10808 / 10809（**不是陈旧的 10810**），`ended_at IS NULL`。
3. 制造陈旧端口：把 `bin\config\standalone_4949226207915085546-xray.json` 写成另一个端口（如 10877），重启 GUI ⇒ 监控与探活仍用 10809（G6）。
4. 制造实例不同：重启 xray（复用 pid 的极端场景难以人工构造 ⇒ 以代码走查 + 闸门单测替代）。

**已知基线失败**：`XrayManagerStartupTest`（需真实就绪 xray）；`UI_FLOATINGWIDGET` 为已知并行 flaky（`WindowFromPoint` z-order 竞态，`hitCount 20 != probeCount 25`），单跑 2/3 通过，非回归。

---

## 5. 风险与约束

| 编号 | 风险 | 缓解 |
| :--- | :--- | :--- |
| R1 | 「无法校验 ⇒ 放行」可能放行陈旧端口 ⇒ 探活持续失败 | 可接受：代理仍**可见**，用户可手动停止；优于「不可见」 |
| R2 | 握手探测有 200ms 超时 × 候选数 ⇒ 纳管周期耗时上升 | `osPorts` 通常 1–2 个；且 OS 事实一次查询（D3）已避免重复表扫描 |
| R3 | 闸门依赖秒级创建时间 ⇒ 同秒 pid 复用无法区分 | 窗口极小；且复用者通常非 xray/sing-box；如需可升级为 FILETIME 精确比较 |
| R4 | `lookupSocksPort` 三元组改为用权威 `instanceStartedAt` ⇒ 与**旧**行（由秒级兜底写入）失配 | DAO 内部「`startedAt` 为空 ⇒ 双键」降级保留；但旧行的 `started_at` 非空 ⇒ 仍可能失配 ⇒ **存量行须靠 OS 校验兜底**（D2 覆盖），不依赖历史层命中 |
| R5 | 监控路径上移 `byId` 查表可能影响 `row.pid` / `row.startedAt` 赋值顺序 | 查表上移不改变取值语义，仅改变执行顺序；需在代码走查中确认 `:1494-1504` 的两分支（命中 / 未命中）完整迁移 |
| R6 | `resolvePort` 签名破坏性变更 ⇒ 其它调用点编译失败 | 编译期即可暴露；既有调用点仅纳管一处（`resolveValidatedPort` 内部再调用） |
| R7 | 端口校验若过严 ⇒ 健康代理被丢弃 | D5 放行 + 唯一候选放宽；且丢弃只发生在「有活体事实且候选矛盾」时 |
| R8 | 纳管每 `checkIntervalMs` 周期执行 ⇒ 校验逻辑稳态开销 | 每次每进程 1 次 TCP 表查询 + ≤N 次握手（N 通常 1–2） |

---

## 6. 文档登记

- [ ] `docs/INDEX.md` §7.5（specs 表）登记本文
- [ ] `docs/plans/project-plans-tracker.md`「近期文档引用」表追加本文（表尾，日期 2026-10-21，类型 `spec`，状态 `📋 spec 定稿（未实施）`）
- [ ] 实施完成后回填本文 Task 勾选状态并在 `docs/INDEX.md` 更新为 `✅ 已完成`
