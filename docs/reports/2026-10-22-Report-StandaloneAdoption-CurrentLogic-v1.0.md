# Report: 纳管独立代理进程（Standalone Proxy Adoption）当前完整逻辑

- 日期: 2026-10-22
- 类型: Report（现状逻辑说明 / 代码级行为基线）
- 模块: `AppController::adoptDanglingStandaloneProxies` 及其三条相邻路径（R6 接管 / 监控 / 启动），配套 `include/StandaloneAdoptionPolicy.h`、`include/ProxyRuntimeHistory.h`、`include/ProcessInspector.h`
- 版本: v1.0
- 代码基线: 工作区 HEAD `0bbc7f9` + 未提交的第七～第九阶段改动（缺陷规格 v1.1 / 历史表命中不 append / 配置↔库交叉核对 v1.8）
- 关键源文件: `src/ui/AppController.cpp`（3 4xx 行）、`include/StandaloneAdoptionPolicy.h`、`include/ProxyRuntimeHistory.h`、`src/ProxyRuntimeHistory.cpp`、`include/ProcessInspector.h`、`src/ProcessInspector.cpp`、`src/Utils.cpp`

> **触发**: 用户要求「给出当前纳管独立代理的逻辑」。本报告把散落在 4 份规格 / 3 份缺陷文档与代码中的行为**收敛为一份可核对的现状基线**，供后续改动、评审与排障直接引用；行号以本文档生成时的代码为准。

> **文档定位**: 本报告**只描述现状**，不含待实施方案。所有「为什么这样设计」的论证与改动方案见 §16 关联文档索引。

---

## 1. 概述

纳管（adopt）指的是：把**不是本应用启动**、但符合本应用命名约定的独立代理进程（`xray.exe` / `sing-box.exe`）收编为受管对象，使其出现在监控面板、纳入运行时历史、可被停止。

现行实现是一条约 30 秒执行一次的单线程流水线（`adoptDanglingStandaloneProxies`，定义于 `src/ui/AppController.cpp:1844`），全流程由 5 个串联环节构成：

```
① 枚举 + 身份识别（命令行 → indexId）
        ↓
② D5 实例闸门（句柄验明正身，防 pid 复用认领错进程）
        ↓
③ 三态路由（Skip / BypassDetect / FullDetect —— 决定要不要做活体检测）
        ↓
④ 端口链 + D2 闸门（历史表 → 命令行配置 → 目录探测 → 活体 OS，逐候选证伪）
        ↓
⑤ 配置 ↔ ProfileItem 交叉核对（诊断性，不相符 ⇒ 告警但仍纳管）
        ↓
   行身份判定（append-only）→ 端口写回 → watcher 登记 → 内存态 → 日志 → UI 事件
```

**核心不变量**（贯穿全文）：

| # | 不变量 | 含义 |
|---|---|---|
| I1 | **fail-open 优于显示错端口** | 只有被活体事实**证伪**的候选才被否决；「无法证实」一律放行 |
| I2 | **历史表 = append-only 会话日志 = 唯一信源** | 缺乏活体记录则**补齐**（append），**绝不 UPDATE 既有行**；命中则**复用 + 只刷新 duration** |
| I3 | **端口以链解出的为准** | 磁盘配置、目录副本、历史值都只是「候选」，最终写回的是经 D2 校验的链结果 |

---

## 2. 入口与并发守卫

### 2.1 三个调用点

| 位置 | 时机 | 门控 |
|---|---|---|
| `src/ui/MainFrame.cpp:453` | `startMonitoring()` 启动时 | 带 `VALIDPROXY_NO_ADOPT=1` 环境变量门控（沙箱 UI 自动化测试必须禁纳管，否则 adoption 心跳写库 + `StandaloneProxyEvent` 驱动的同步读库会卡住主线程） |
| `src/ui/MainFrame.cpp:979` | `onProxyMonTimer` 定时器（`proxy_process_monitor.check_interval_ms`，生产 **30000ms**） | 同上门控 |
| `src/ui/AppController.cpp:212` | `switchDatabase()` 内（切库后重跑纳管） | **无门控** |

> 周期不是 5s —— 生产 `bin/config.json` 的 `proxy_process_monitor.check_interval_ms = 30000`。

### 2.2 in-flight 互斥（`src/ui/AppController.cpp:1851`）

```cpp
if (!standalone_adoption::tryBeginAdoption(adoptInFlight_)) {
    Logger::write("[StandaloneProxy] Adoption scan already in progress; skipping this round",
                  LogLevel::DEBUG);
    return;
}
ScopeGuard<std::atomic<bool>> adoptGuard(adoptInFlight_);
```

- `tryBeginAdoption` = `!flag.exchange(true)`（纯函数，已单测：`AdoptionGate_AcquiresWhenFree` / `_RejectsWhenHeld` / `_ReleasesThenAcquires`）。
- 复位复用既有 `ui::ScopeGuard<T>`（`include/ui/ScopeGuard.h`，本文件已用它 7 处）。
- 并发轮次**直接跳过**（不排队、不阻塞）；因纳管是 30s 周期且幂等要求不强，跳过一轮优于并发执行。

---

## 3. 身份识别：命令行是唯一身份判据

| 步骤 | 代码位置 | 动作 | 不满足时 |
|---|---|---|---|
| 3.1 枚举 | `AppController.cpp:1860` | `ProcessInspector::enumerateByName("xray.exe")` / `("sing-box.exe")` ⇒ 每条记录含 `{pid, commandLine, creationTime}` | — |
| 3.2 命令行 | — | `commandLine` 非空 | 空 ⇒ **跳过**（管理员启动的 xray 读不到 PEB 命令行） |
| 3.3 配置文件名 | — | `extractConfigFileName(commandLine)` ⇒ 形如 `standalone_<indexId>-xray.json` | 空 ⇒ **跳过**（不是本应用的独立代理） |
| 3.4 indexId | — | 剥 `standalone_` 前缀 + `-xray.json` / `-singbox.json` 后缀 | 空 ⇒ 跳过 |

**为什么 indexId 只能来自命令行**：它是身份判据。若改成「从历史表按 pid 反查」，机器上**任意** xray 进程都会被当成独立代理纳管。（历史表反查只能作**兜底**，用于救回命令行为空的高权限进程，且须校验该行 `ended_at IS NULL`。）

**枚举侧一次拿全三项**：`CreateToolhelp32Snapshot` → `Process32FirstW/NextW` 匹配 `szExeFile` → `info.pid = pe.th32ProcessID`；随后 `OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ)` 读 PEB 命令行与 `GetProcessTimes` 创建时间，读完即 `CloseHandle`。

---

## 4. D5 实例闸门：认领前先验明正身

枚举是**快照**；到真正纳管时（其间含数百毫秒的活体检测）该 pid 可能已死并被 OS 复用 ⇒ 直接纳管会**认领错进程**（端口错、探活失败、还会误杀）。

实现（`src/ui/AppController.cpp:1960-2030` 附近）：

```cpp
HANDLE probeHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, procs[i].pid);
if (probeHandle) {
    instanceStartedAt = proc::ProcessInspector::processCreationTime(probeHandle);
    CloseHandle(probeHandle);
}
const standalone_adoption::InstanceCheck identity =
    standalone_adoption::checkInstanceIdentity(procs[i].creationTime, instanceStartedAt);
```

| 判定 | 条件 | 处置 |
|---|---|---|
| **Match** | 两侧都读到且相等 | 继续纳管 |
| **Recycled** | 两侧都读到但不等（pid 被复用） | **放弃纳管**（不写历史行、不建 watcher、不 bump `start_count`）+ 按 pid+创建时间指纹去重的日志（首次 WARN、后续 DEBUG，文案含 `enumeratedCreatedAt=` / `handleCreatedAt=`） |
| **Unverifiable** | 任一侧为空（`GetProcessTimes` 失败 / `OpenProcess` 失败） | **降级放行** + DEBUG ——「读不到创建时间」不是「不匹配」的证据，不能误杀健康代理 |

**为什么探针用 `PROCESS_QUERY_LIMITED_INFORMATION`**：枚举读命令行已需 `PROCESS_QUERY_INFORMATION | PROCESS_VM_READ`（更严），故闸门**永不强于枚举本身** ⇒ 不会出现「枚举接受了、闸门却打不开」的新一类静默丢弃。真正纳管时后面另开完整掩码句柄。

**位置约束**：闸门必须排在「查历史表 / 拿端口」**之前**。

**精度限制**：创建时间格式为 `"%Y-%m-%d %H:%M:%S"`（秒级、本地时区，`ProcessInspector::fileTimeToString`）⇒ 同秒内发生的 pid 复用不可区分。

---

## 5. 三态路由：决定要不要做活体检测

路由决策在 `standaloneMutex_` 锁内取三个事实（`AppController.cpp:1936` 调 `decideAdoptionRoute`）：

| 事实 | 来源 |
|---|---|
| `alreadyManaged` | `standaloneProxies_` 或 `proxyWatchKeys_` 命中 indexId |
| `memoryPort` | `standaloneProxies_[indexId].socksPort` |
| `startedHere` + `sessionPort` | `sessionStartedProxies_`（pid ⇒ `SessionStartedProxy{socksPort, creationTime}`）**且创建时间与枚举值一致**才算「本次启动」 |

| 路由 | 条件 | 行为 |
|---|---|---|
| **Skip** | 已在管 **且** 非本次启动 | `continue`（不重复登记） |
| **BypassDetect** | 本会话由本应用启动 | **完全跳过活体检测**（不查 TCP 表、不做 SOCKS5 握手）；端口取 历史表 → 内存 → 会话启动登记；职责收窄为「确保历史表有该会话行」 |
| **FullDetect** | 其余（纳管来的外来游离进程） | 走完整端口链 + D2 闸门 |

> `startedByThisSession` 优先于 `alreadyManaged`：本会话启动的代理即使已在管，也仍需在切库后把会话行补回新库。

**三个清理点**（按 pid erase `sessionStartedProxies_`，防 pid 复用误认）：启动路径 NotifyFn、纳管路径 NotifyFn、`stopStandaloneProxy`。

---

## 6. 端口链 + D2 闸门

### 6.1 活体取证（每进程只做一次，全链复用）

`AppController.cpp:2059-2075`：

```cpp
const proc::ProcessInspector::TcpPortScan scan =
    proc::ProcessInspector::scanListeningTcpPorts(procs[i].pid);   // {readOk, ports}
const int apiPort = (config_.proxy.api_base_port > 0) ? config_.proxy.api_base_port : 10085;
const std::vector<int> osCandidates =
    standalone_adoption::buildOsCandidates(scan.ports, apiPort);   // 排除 api 端口
std::vector<int> socksPorts;
for (...) { if (utils::respondsToSocks5Greeting(*it, 200)) { socksPorts.push_back(*it); } }
const std::vector<int> orderedOsCandidates =
    standalone_adoption::orderOsCandidatesBySocks(osCandidates, socksPorts);
```

- `scanListeningTcpPorts` 逐族（AF_INET / AF_INET6）独立探测→取表→过滤 `dwOwningPid == pid && dwState == MIB_TCP_STATE_LISTEN`；**整体 `readOk = v4Ok || v6Ok`**；`pid == 0` ⇒ `readOk = false`；**不打日志**（`test_process_inspector` 目标不链 Logger）。
- `respondsToSocks5Greeting(port, 200ms)`：仅连 **IPv4 回环**，发 `05 01 00`，要求 `reply[0] == 0x05 && reply[1] != 0xFF`（`05 FF` = 不接受任何方法 ⇒ 判为非 SOCKS）。
- `utils::isPortListening(port)` **不区分归属，不可用于校验**；`utils::logPortOccupants(port)` 是端口→pid 的既有诊断工具。

### 6.2 候选链（按优先级装配）

| 序 | 来源 | 取法 | `PortSource` 标签 | 代码位置 |
|---|---|---|---|---|
| Tier1 | **运行时历史表** | `lookupSocksPort(indexId, pid, creationTime)` | `runtime-history` | `:2082` |
| Tier2 | **命令行配置** | `extractConfigFullPath(commandLine)` + 读 `inbounds[0].port`（xray）/ `listen_port`（sing-box） | `cmdline-config` | `:2092-2094` |
| Tier3 | **目录探测** | **仅** `<exeDir>\config\<configFileName>` | `dir-probe` | `:2114` |
| Tier4 | **活体 OS** | `orderedOsCandidates` 每个端口 | `os-tcp-table` | `:2059` |

- **目录探测单目录化**：只探 `<exeDir>\config\`，**不探** `<exeDir>\worker\config\`（那属 `bin\worker\validproxy.exe` 另一套安装的目录，探测它 = 跨安装取端口 = 2026-09-02 记录的「两套 config 目录端口漂移」缺陷类别）。被 `worker\validproxy.exe` 启动的进程由 **Tier2 命令行绝对路径**覆盖。
- 命中 Tier2/Tier3 时记录 `adoptedConfigPath`（供 §7 交叉核对使用）。

### 6.3 D2 闸门判定（`classifyPortCandidate`，纯内存比较、零额外 I/O）

判定**短路顺序**：

| 序 | 条件 | 判定 | 处置 |
|---|---|---|---|
| 1 | `candidate <= 0` 或 `> 65535` | `Disproven` | 顺延到链上下一来源 |
| 2 | `== excludedPort`（api 端口，默认 10085） | `Disproven` | 顺延 |
| 3 | `!osFactsReadable`（TCP 表读不到） | `Unknown` | **放行**（无法证伪） |
| 4 | `osFactsReadable && osPorts.empty()`（该 pid 确实没监听） | `Disproven` | 顺延 |
| 5 | `c ∉ osPorts` | `Disproven` | 顺延 —— **活体事实证伪**，陈旧端口被拦下 |
| 6 | `c ∈ socksPorts`（握手应答 SOCKS5） | `Verified` | 采纳 |
| 7 | 其余（在听但握手不可判定） | `Unverified` | 放行 + WARN |

`resolveChainPort` 沿链取**第一个非 `Disproven`** 的候选；全被否 ⇒ 端口 0 ⇒ 进入 §8 丢弃分支。

> **「空表 ⇒ 证伪」只在纳管 FullDetect 路径启用**：R6 接管触发于旧进程退出的瞬间，新进程常**尚未 bind** ⇒ 若那里也证伪，接管端口会被打成 0。故 R6 传 `takeoverOsFactsReadable = readOk && !osPorts.empty()`（等价旧的 `Unknown` 放行语义）。

### 6.4 BypassDetect 的端口取值

BypassDetect 路由**不做任何校验**（端口是本应用自己配的，且启动路径已由 `waitForPort` 证实）：

```cpp
adoptedPort = standalone_adoption::resolveBypassPort(historyPort, memoryPort, sessionPort);
```

取值优先级：**历史表 → 内存 → 会话启动登记**；首个 `> 0` 优先。

---

## 7. 配置 ↔ ProfileItem 交叉核对（诊断性，非门禁）

位置：**端口解析之后、丢弃判定之前**（`AppController.cpp:2188-2201`）。

| 步骤 | 动作 |
|---|---|
| 7.1 前置 | 仅当 `adoptedConfigPath` 非空才做（配置文件读不到 ⇒ 跳过） |
| 7.2 读配置快照 | `readStandaloneProxySnapshot(configPath, cfgSnap)`（`:1736`）—— 定位 `tag == "proxy"` 的出站（退路 `outbounds[0]`）⇒ `protocol`；`settings.vnext[0]` 取 address / port / `users[0].id`，或 `settings.servers[0]` 取 address / port / `password`（trojan）；`streamSettings.wsSettings` / `xhttpSettings`.`host`；`streamSettings.realitySettings`.`publicKey`。**必比项任一取不到 ⇒ 返回 false ⇒ 跳过比对**（不报假不一致） |
| 7.3 读库快照 | `ProfileitemDAO recordDao(db_); recordDao.getByIndexId(indexId)`（`std::optional`）⇒ configtype→protocol、address、port、id→userId、requesthost、publickey |
| 7.4 比对 | `compareProxyRecord(cfgSnap, dbSnap)` ⇒ `Match` / `Mismatch` / `Unverifiable` |
| 7.5 告警 | `Mismatch` ⇒ 拼不一致字段列表 ⇒ 按 **(indexId, 字段集)** 去重 ⇒ **首次 WARN / 后续 DEBUG**：「Configuration of pid=… indexId=… does not match the current database record (…); the running proxy still uses the older definition. Restart the proxy to apply the new one.」 |

**比对项分两类**：

| 类别 | 字段 | 规则 |
|---|---|---|
| 必比 | protocol、address、port、userId | 任一为空 ⇒ **Unverifiable**（**不是** Mismatch，避免「取不到」被误报成「不一致」）；否则逐项比较，不一致即记字段名 |
| 可选 | requestHost、publicKey | 仅在**双方都非空**时比较（单边缺失不判不一致） |

**为什么不相符不阻断纳管**：
1. **不相符是常态** —— 用户在 GUI 改了 address / uuid 但**没重启**独立代理 ⇒ 磁盘配置是旧的、DB 是新的 ⇒ 此时丢掉正在服务的代理，会让它从监控里消失（违背不变量 I1）。
2. **不相符恰是最有价值的信息** —— 它是**唯一**能发现「正在跑的代理用的是旧配置」的信号（活体层只能证明本地端口，无法证明出站节点是否已过期）。
3. **丢弃语义已被收窄** —— 丢弃只发生在「身份不明」与「端口链全空」两种情况（见 §8）。

⇒ 纳管 REPORT 日志追加 `record=match|mismatch|unverifiable`。

### 7.1 配置 JSON ↔ ProfileItem 关键项映射

| 语义 | 配置 JSON 路径 | ProfileItem 列 |
| :--- | :--- | :--- |
| 服务器地址 | `outbounds[proxy].settings.vnext[0].address`（trojan：`settings.servers[0].address`） | `address` |
| 服务器端口 | `...vnext[0].port` | `port` |
| 用户标识 / 密码 | `...vnext[0].users[0].id`（trojan：`...servers[0].password`） | `id` |
| 协议 | `outbounds[proxy].protocol` | `configtype` |
| 请求 Host | `...streamSettings.wsSettings.host` / `xhttpSettings.host` | `requesthost` |
| Reality 公钥 | `...streamSettings.realitySettings.publicKey` | `publickey` |
| Reality shortId | `...streamSettings.realitySettings.shortId` | `shortid` |
| SNI | `...streamSettings.tlsSettings.serverName` / `realitySettings.serverName` | `sni` |

⚠️ **本地 SOCKS 端口不在 ProfileItem 里** —— 它由 standalone 模板的 `inbounds[0].port` 生成（`ConfigBuilder::buildStandaloneConfig`）⇒ `ProfileItem.port` 是**远端**端口，**不可**与本地端口混用。

⚠️ standalone 配置的 `outbounds` 顺序固定为 **[proxy(tag="proxy"), direct, block]**；sing-box 分支 JSON 形状不同 ⇒ 当前快照**仅覆盖 xray**（sing-box ⇒ 恒 `Unverifiable`）。

---

## 8. 丢弃语义：只有两种情况

| 触发 | 说明 |
|---|---|
| ① **身份不明** | D5 闸门判 `Recycled`（pid 被复用，枚举到的进程已消失） |
| ② **端口链全空** | 四个来源都没给出可用端口（含全部被 D2 证伪） |

**动作**：记 `discardedStandalonePids_`（pid ⇒ 创建时间指纹）⇒ 日志（首次 WARN、后续 DEBUG）⇒ `continue`。

**明确不做**：
- 不写历史行
- 不建 watcher
- 不进内存态（`standaloneProxies_`）
- **不对活体进程 `finalizeStop`** —— 关掉活体行会让下一轮必然三元组失配 ⇒ 重复 append + `start_count` 重复累加（这是 2026-10-21 缺陷 #102 的根因之一）

**指纹去重的正确写法**（`operator[]` 会执行插入，**不可**用于判定「首次」）：

```cpp
std::unordered_map<DWORD, std::string>::iterator recIt = discardedStandalonePids_.find(pid);
const bool sameInstance = (recIt == discardedStandalonePids_.end()) ||
    standalone_adoption::isSameProcessFingerprint(recIt->second, fingerprint);
if (!sameInstance) { discardedStandalonePids_.erase(pid); }
firstDiscard = discardedStandalonePids_.insert_or_assign(pid, fingerprint).second;
```

---

## 9. 纳管主体

### 9.1 句柄与写侧复检

```cpp
HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE,
                               FALSE, procs[i].pid);        // :2377
if (!hProcess) { continue; }
```

- 必须含 `PROCESS_TERMINATE` ⇒ 否则 `stopStandaloneProxy()` / `shutdownStandaloneProxies()` 杀不掉被纳管的孤儿进程（`TerminateProcess` 报 `ERROR_ACCESS_DENIED`），孤儿会永久占着端口。
- **写侧复检**（`:2393-2405`）：活体检测耗时数百毫秒，其间用户可能已启动同一代理、另一轮扫描也可能已纳管它，两边都会为同一 indexId 注册 watcher。故在**写任何历史行之前**、锁内复查 `standaloneProxies_` / `proxyWatchKeys_`，命中则 `CloseHandle` + 放弃本次纳管 ⇒ 保证「查—写」原子，杜绝 `watchKey` 被覆盖导致的 watcher 线程泄漏。

### 9.2 started_at 三级兜底（`:2423-2430`）

```
instanceStartedAt（D5 闸门句柄值，权威）
    → procs[i].creationTime（枚举值）
    → utils::getCurrentTimestampFormatted()（秒级当前时间）
```

兜底的唯一目的是**不写空串**；它**不会**让 `(indexId, pid, started_at)` 三元组匹配上 —— 写入方的「now」与读取方的「now」本就是不同瞬间，必然失配。兜底值必须是可被 `durationMsBetween` 解析的日期格式（不能是 epoch 秒）。

### 9.3 行身份判定：append-only 语义

`:2431-2470`（纳管主块）与 `:2317-2360`（Bypass 块）结构一致：

```
① rhId = findInProgressHistory(indexId, pid, startedAt)        // 三元组精确匹配
② 若未命中：rhId = findInProgressHistoryLoose(indexId, pid)      // 宽匹配（indexId+pid）
③ 若仍未命中：rhId = insertStartWithPort(indexId, startedAt, pid, adoptedPort)
                                                                  // APPEND 新行并携带端口
④ 复用行（②或①命中）时：
     rowStartedAt = getStartedAt(rhId)                           // 该行自身的 started_at
     durationBase = rowStartedAt.empty() ? procStartedAt : rowStartedAt
     touchHeartbeat(rhId, durationMsBetween(durationBase, now)) // 只刷新 duration_ms
```

**为什么宽匹配也算命中**：`started_at` 有三级兜底，写入方与读取方的「now」是不同瞬间 ⇒ 三元组失配是常态；而宽匹配命中（indexId + pid 且 `ended_at IS NULL`）几乎必然就是**同一物理实例** ⇒ 应当复用，而不是关掉旧行再 append 一条（那会造成重复行 + `start_count` 重复累加 + 孤儿行堆积）。

**复用路径绝不 `finalizeStop`、绝不 append**；`updateSocksPort` 改为**无条件执行**（被复用的行可能端口为 NULL 或陈旧）。

### 9.4 端口写回（`:2521`）

```cpp
if (!historyDao_.updateSocksPort(rhId, adoptedPort, db_)) {
    Logger::write("[StandaloneProxy] Failed to record socks_port for " + indexId, LogLevel::WARN);
}
```

⇒ **以链解出的端口为准写回**（不变量 I3）；`updateSocksPort` 对 `socksPort <= 0` 是 no-op、0 行变更返回 false。

### 9.5 watcher 登记与收尾（`:2475` 起）

```cpp
const proc::ProcessExitListener::WatchKey key =
    exitListener_->watch(hProcess, indexId, rhId, /*callbacks*/ …);
```

登记成功后写 `standaloneProxies_[indexId]`（indexId / configFileName / configPath / socksPort / processHandle / running / managed / runtimeHistoryId / watchKey / startedAt）与 `proxyWatchKeys_[indexId] = key`。

### 9.6 收尾输出

1. **REPORT 日志**：`"[StandaloneProxy] Adopted dangling process pid=… indexId=… on SOCKS5 :… source=… verdict=… record=… config=…"`
2. `exDao_.updateStartupTime(indexId, db_)` —— 写入 `ProfileExItem.message`（`+<startup time>`）
3. `wxQueueEvent(topWindow_, new StandaloneProxyEvent(indexId, "", 0, /*started=*/true, startedAt))` —— 刷新历史 / 健康列

---

## 10. 三条相邻路径的差异

| 路径 | 端口来源 | D2 判定 | 历史表 | 内存回填 |
|---|---|---|---|---|
| **纳管 FullDetect** | 完整四级链 | ✅ 全链逐候选 | 三元组 / 宽匹配复用，否则 append | 写 `standaloneProxies_` |
| **纳管 BypassDetect**（本次启动） | 历史 → 内存 → 会话启动登记 | ❌ 零校验 | 三元组 / 宽匹配复用，否则 append | 不重写（已在管） |
| **R6 接管**（旧进程退出 → 接管新进程） | 新进程 cmdline 配置 → 旧进程内存端口 → 新进程 OS 候选 | ✅ 全链（但「空表 ⇒ 证伪」**关闭**） | `insertStartWithPort` 追加会话行 | 回填 `socksPort` / `startedAt` / `configPath` |
| **监控路径** `getWatchedStandaloneMonitors()` | 历史(已核实) → 内存(已核实) → 磁盘(未核实) → 0 | ❌ **零 socket 调用** | 只读 | 只读 |
| **启动路径** `startStandaloneProxy` | 配置文件 + `waitForPort` 证实 | ❌ | `insertStartWithPort` | 写 `standaloneProxies_` + `sessionStartedProxies_` |

### 10.1 R6 接管路径（`AppController.cpp:1244-1400`）

- 触发时机：被监控的旧进程退出瞬间（`ProcessExitListener` 的 `shouldFinalize`）⇒ 新进程常尚未 bind。
- 端口来源顺序：新进程命令行配置 → **旧进程内存端口**（须被新进程活体事实证伪）→ 新进程 OS 候选。
- `takeoverOsFactsReadable = takeoverScan.readOk && !takeoverOsPorts.empty()` ⇒ 保留旧的「放行」语义。
- 句柄掩码已补 `PROCESS_TERMINATE`（否则接管后杀不掉）。
- 接管成功后**回填** `sp.socksPort` / `sp.startedAt` / `sp.configPath`（此前只更新 processHandle / running / runtimeHistoryId ⇒ 探活会继续打旧端口）。

### 10.2 监控路径（`getWatchedStandaloneMonitors`）

- **零 socket 调用** —— 该函数在 **UI 线程**被调用（`StandaloneFloatingWidget` 的 wxTimer），200ms 握手会周期性冻结 UI。
- 端口优先序：**历史(已核实) → 内存(已核实) → 磁盘(未核实) → 0**。原实现是「磁盘优先」（`row.socksPort = (diskPort > 0) ? diskPort : watched[j].socksPort`）⇒ 磁盘上存在陈旧副本时会把已算对的端口推翻（现场：12776 实听 10809，陈旧副本写 10810）。
- `byId` 历史行查表已上移到端口计算块之前，以取该行的 `pid` / `socksPort`。

### 10.3 启动路径（`startStandaloneProxy`）

- 端口由 `waitForPort(socksPort, test_timeout_ms * 3, pi.hProcess)` 证实。
- 成功后登记 `sessionStartedProxies_[pi.dwProcessId] = {socksPort, creationTime}` 并置 `info.startedByThisSession = true`。
- 三个保活/终止分支（端口未就绪、端口就绪后闪崩、连通性失败但用户选择保留）都**不**清除该登记（进程仍存活）；只有真正杀掉进程的三个分支才 erase。

---

## 11. 关键 API 清单

### 11.1 `include/StandaloneAdoptionPolicy.h`（header-only、纯函数、`namespace standalone_adoption`）

| 符号 | 作用 |
|---|---|
| `PortSource{None,RuntimeHistory,CmdlineConfig,DirProbe,OsTcpTable}` | 端口来源标签 |
| `PortVerdict{Disproven,Unknown,Unverified,Verified}` | D2 四态判定 |
| `PortCandidate{port,source}` / `PortResolution{port,source,verdict}` | 候选 / 解析结果 |
| `containsPort` | 集合包含 |
| `classifyPortCandidate(c,osPorts,socksPorts,excludedPort,osFactsReadable)` | **D2 核心判定**（§6.3） |
| `buildOsCandidates(osPorts,excludedPort)` | OS 候选（排除 api 端口、去重升序） |
| `orderOsCandidatesBySocks(candidates,socksPorts)` | 应答 SOCKS5 的排前 |
| `resolveChainPort(candidates,osPorts,socksPorts,excludedPort,osFactsReadable)` | 沿链取第一个非 Disproven |
| `portSourceLabel` / `portVerdictLabel` | 日志标签 |
| `InstanceCheck{Match,Recycled,Unverifiable}` + `checkInstanceIdentity` + `instanceCheckLabel` | **D5 闸门**（§4） |
| `tryBeginAdoption(std::atomic<bool>&)` | **in-flight 互斥**（§2.2） |
| `isSameProcessFingerprint(recorded,current)` | 丢弃指纹比对（空串 ⇒ 同实例，与 `checkInstanceIdentity` 的空串约定**相反**） |
| `AdoptionRoute{Skip,BypassDetect,FullDetect}` + `decideAdoptionRoute` + `resolveBypassPort` + `adoptionRouteLabel` | **三态路由**（§5） |
| `RecordVerdict{Match,Mismatch,Unverifiable}` + `ProxyRecordSnapshot` + `RecordCompareResult` + `compareProxyRecord` + `recordVerdictLabel` | **配置↔库交叉核对**（§7） |

### 11.2 `include/ProxyRuntimeHistory.h`（`proxy_runtime_history` DAO）

| 方法 | 语义 |
|---|---|
| `migrateTable(db)` | 幂等 `ALTER TABLE … ADD COLUMN socks_port INTEGER` |
| `insertStart` / `insertStartWithPort` | **append** 语义；`socksPort > 0` 绑 int、否则绑 NULL；同事务 bump `ProfileExItem.start_count` |
| `finalizeStop` | 写 `ended_at` / `exit_code` / `duration_ms` + 聚合 |
| `touchHeartbeat(historyId, durationMs)` | `UPDATE … SET duration_ms = ? WHERE id = ? AND ended_at IS NULL` ⇒ 写**绝对值**、仅 in-progress 行、无事务、已 finalize 行 no-op 但返回 true |
| `findInProgressHistory(indexId,pid,startedAt)` | 三元组**精确字符串相等**（`ORDER BY id DESC LIMIT 1`） |
| `findInProgressHistoryLoose(indexId,pid)` | 宽匹配（indexId+pid），只返回 id |
| `lookupSocksPort(indexId,pid,startedAt)` | 三元组或 (index_id,pid) 双键；要求 `ended_at IS NULL` 且 `socks_port IS NOT NULL` |
| `updateSocksPort(historyId, socksPort)` | 单条 UPDATE、无事务；`socksPort <= 0` 为 no-op；0 行变更返回 false |
| `getStartedAt(historyId)` | `SELECT started_at … WHERE id = ?`；行不存在返回空串 |
| `getInProgressSessions(db)` | `SELECT id,index_id,started_at,pid,socks_port … WHERE ended_at IS NULL ORDER BY started_at` ⇒ **可按 pid 反查 indexId，零新 DAO 代码** |

### 11.3 `include/ProcessInspector.h`

`enumerateByName`（快照枚举，一次拿 pid+commandLine+creationTime）、`extractConfigFileName` / `extractConfigFullPath`、`listeningTcpPorts(pid)`、`scanListeningTcpPorts(pid)`（`TcpPortScan{readOk,ports}`，类内嵌套）、`processCreationTime(HANDLE)/(DWORD)`、`fileTimeToString`、`matchesConfig`、`setEnumeratorForTesting`（测试接缝）。

---

## 12. 关键生产库 / 磁盘事实（实测）

生产库 = `bin\worker\guiNDB.db`（`bin\config.json` 的 `database.path`）。

| 事实 | 数值 |
|---|---|
| `proxy_runtime_history` 中 `ended_at IS NULL` 的孤儿行 | **128** 条 |
| 重复 `(index_id, pid, started_at)` 三元组 | 全表 **11** 组（其中 in-progress 内仅 2 组） |
| `ProfileExItem.start_count` | **恒 0**（`ProfileExItemDAO.cpp:287-290/:301` 的 `INSERT OR REPLACE` 归零机制）⇒ **不能作为验收信号** |

| 磁盘事实 | 说明 |
|---|---|
| `bin\config\standalone_4949226207915085546-xray.json` 仍存在，`inbounds[0].port = 10810` | **陈旧副本**（该进程实听 10809）⇒ 目录探测层会给出错误端口，靠 D2 的 OS 证伪拦下 |
| `bin\worker\config\` 存在但**为空** | 12776 的命令行指向该目录 ⇒ 命令行层取不到端口 |
| `bin\config.json`：`log.file_level = WARN`、`console_level = REPORT` | **REPORT 级日志不落盘** ⇒ 判断「是否纳管」要看 DB，不能看日志文件 |
| `proxy_process_monitor.check_interval_ms = 30000` | 纳管周期 30s（非 5s） |

---

## 13. 已知残留（均在既有规格的非目标范围）

| # | 残留 | 影响 |
|---|---|---|
| R1 | 生产库 128 条 `ended_at IS NULL` 孤儿行 + 11 组重复三元组 + `start_count` 恒 0 | 污染 `getInProgressSessions()` join 与「运行中」幻影统计；`start_count` 无法作验收信号 |
| R2 | 切库时旧 watcher 的 `finalizeStop` 会用成员 `db_` 写到**新库** | 跨库误伤（需 id 碰撞）；`db_` 指针本身存在数据竞争 |
| R3 | 创建时间指纹为**秒级** | 同秒内 pid 复用不可区分（彻底修需 `processCreationTimeTicks(HANDLE)`，用 FILETIME tick） |
| R4 | `discardedStandalonePids_` 无容量治理 | 集合随运行时间缓慢增长 |
| R5 | 纳管线程是 detached | 应用退出时可能访问已析构的 `this` / 已关闭的 `db_` |
| R6 | **sing-box 配置快照未实现** | sing-box 代理恒 `Unverifiable`（不比对、不告警，也不误报） |
| R7 | 「配置已陈旧」仅日志可见 | 无 UI 标记（悬浮窗未加列） |

---

## 14. 验证基线

| 项 | 结果 |
|---|---|
| `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug` + `cmake --build build --parallel 8` | **exit = 0**（无 error） |
| 定向 ctest（**在 `build/` 目录执行**）：`ctest -R "StandaloneAdoptionPolicyTest\|RuntimeHistoryDAOTest\|ProcessInspectorTest\|UtilsTest"` | **4/4 全绿** |
| 全量 `ctest` | **55/56**（唯一失败 `XrayManagerStartupTest`，既有基线，需真实就绪 xray；`UI_FLOATINGWIDGET` 为已知并行 flaky） |
| `git diff HEAD -U0 -- '*.cpp' '*.h' \| Select-String '^\+.*\bauto\b' -CaseSensitive` | **0**（C++ 新增/修改行无 `auto` 类型推导） |

ctest 目标名 ≠ 源文件名：`RuntimeHistoryDAOTest` / `StandaloneAdoptionPolicyTest` / `ProcessInspectorTest` / `UtilsTest`。

---

## 15. 已知陷阱清单（改动本模块前必读）

1. **`operator[]` 会插入** ⇒ `discardedStandalonePids_` 判「首次」必须用 `insert_or_assign(...).second`，不能用 `operator[]` + `find`。
2. **`isSameProcessFingerprint` 与 `checkInstanceIdentity` 的空串约定相反**：前者「空 ⇒ 同实例」（避免日志级别抖动），后者「空 ⇒ 无法证伪」（避免误杀）。二者不可混用。
3. **「空表 ⇒ 证伪」只在纳管 FullDetect 启用**；R6 必须传 `readOk && !osPorts.empty()`，否则接管端口被打成 0。
4. **监控路径禁止 socket 调用**（UI 线程 + wxTimer）⇒ 200ms 握手会造成周期性 UI 冻结。
5. **目录探测只探 `<exeDir>\config\`**；`worker\config` 属另一套安装，探测它会跨安装取端口。
6. **`start_count` 恒 0**，不能作验收信号；改判「同一 pid 的 in-progress 行数」。
7. **REPORT 级日志在生产 `file_level=WARN` 下不落盘** ⇒ 验收纳管结果要看 DB。
8. **切库路径无 `VALIDPROXY_NO_ADOPT` 门控** ⇒ UI 自动化沙箱里切库会触发真实纳管。
9. **`getStartedAt` 返回空串 ⇒ duration 记 0**（基准回退到本轮 `procStartedAt`）。

---

## 16. 关联文档索引

| 文档 | 覆盖范围 |
|---|---|
| `docs/specs/2026-10-21-Spec-StandaloneAdoption-InstanceBoundPortFallback-v1.1.md` | 端口回退主规格（v1.0 → v1.6 全部实施依据，含附录 A/B/C） |
| `docs/bugfix/2026-10-21-Bugfix-StandaloneAdoption-OsPortFallback-v1.0.md` | OS TCP 表回落层（原始需求「配置文件不存在 ⇒ 回落到直接读取 xray 的 pid、监听端口」） |
| `docs/bugfix/2026-10-21-Bugfix-StandaloneAdoption-ConcurrencyAndDiagnostics-v1.1.md` | 并发重入 D-A / 丢弃日志降级 D-B / OS 表不可区分 D-C（两份独立评审后修订） |
| `docs/bugfix/2026-10-21-Bugfix-StandaloneAdoption-HistoryHitNoAppend-v1.0.md` | 历史表命中不 append、只刷新持续时间 |
| `docs/bugfix/2026-10-21-Bugfix-StandaloneAdoption-ConfigRecordCrossCheck-v1.0.md` | 配置 ↔ ProfileItem 交叉核对（诊断性） |
| `docs/specs/2026-10-20-Spec-StandaloneAdoptionPortFallback-v1.3.md` | 运行时历史表回落层（v1.3） |
| `docs/reports/2026-09-16-Report-StandalonePool-ConfigSemantics-v1.0.md` | `proxy_process_monitor` / `standalone_pool` 配置语义与 UI 暴露面 |
| `docs/INDEX.md` §9.1 | Bug 修复记录（编号 101 / 102 / 103） |

---

*本报告为现状基线，随代码演进需同步修订；行号以 2026-10-22 工作区状态为准。*
