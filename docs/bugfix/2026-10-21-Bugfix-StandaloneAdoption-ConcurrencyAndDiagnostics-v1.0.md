# 纳管独立代理进程：并发重入、丢弃日志降级、OS 表不可区分三项缺陷

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:systematic-debugging to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修掉「纳管独立代理进程」链路遗留的三项缺陷，使纳管在并发场景下不再重复登记、丢弃事件不再被静默降级、活体端口校验能区分「表读不到」与「确实没监听」。

**Tech Stack:** C++17 / Win32 `GetExtendedTcpTable` / `std::atomic` / GoogleTest / CMake + Ninja（MinGW GCC）

**依据文档：** `docs/specs/2026-10-21-Spec-StandaloneAdoption-InstanceBoundPortFallback-v1.1.md`（v1.1 含 v1.1.1~v1.6 全部修订；本文档是其「已知残留」第 ②③④ 条的独立缺陷规格）

**约束：** 全栈禁止使用 `auto` 进行类型推导（AGENTS.md §1）；测试源码只允许放在 `tests/`。

---

## 0. 缺陷清单与优先级

| 编号 | 缺陷 | 性质 | 危害 | 优先级 |
| :--- | :--- | :--- | :--- | :--- |
| **D-A** | 纳管函数**无重入保护**，三个 detached 线程可并发进入 | 并发正确性 | watcher 泄漏 + 重复 `insertStartWithPort` + `start_count` 重复累加 + 多条 in-progress 行 | **高** |
| **D-B** | `discardedStandalonePids_` **只增不清**，pid 复用后丢弃日志永久降级 | 诊断可见性 | 「进程活着却没被监控」在 `file_level=WARN` 下完全不可见 | 中 |
| **D-C** | `osPorts` 为空时**无法区分**「TCP 表读不到」与「该 pid 确实无 LISTEN 端口」 | 判定精度 | 真正没监听的进程（配置错误/未 bind）无法被证伪 ⇒ 只能靠端口链其它层兜底 | 低（但收益明确） |

建议实施顺序：**D-A → D-B → D-C**（D-A 用一个 in-flight 标记即可解决，改动极小、收益最大；D-C 会改动公共判定函数签名，需连带重写策略单测）。

---

## 1. 现场证据与代码事实

### 1.1 D-A：纳管无重入保护

`AppController::adoptDanglingStandaloneProxies()`（声明 `src/ui/AppController.h:221`，实现 `src/ui/AppController.cpp:1676`）有 **3 个调用点**，全部**新建 detached 线程**、彼此无互斥：

| 调用点 | 时机 | 备注 |
| :--- | :--- | :--- |
| `src/ui/MainFrame.cpp:453` | `startMonitoring()` 启动时 | 带 `VALIDPROXY_NO_ADOPT=1` 环境变量门控（沙箱测试 app 必须禁纳管，否则 adoption 心跳写库 + `StandaloneProxyEvent` 驱动的 `refreshResults()` 同步读库会卡住主线程并挂起测试 app） |
| `src/ui/MainFrame.cpp:979` | `onProxyMonTimer`（30s 定时器）内 | 同样带该门控 |
| `src/ui/AppController.cpp:208-209` | `switchDatabase()` 内 | 紧接 `proxyWatchKeys_.clear();`（`:204`）之后；注释「Re-run dangling proxy adoption against the new database so any orphaned xray/sing-box processes are picked up and tracked」 |

**竞态窗口（读侧 → 写侧之间不是原子的）**：

- 读侧：`alreadyManaged` 计算于 `src/ui/AppController.cpp:1724-1735`（持 `standaloneMutex_` 读 `standaloneProxies_` / `proxyWatchKeys_`）→ `decideAdoptionRoute(alreadyManaged, startedHere)` 于 `:1756` → `route == Skip ⇒ continue` 于 `:1758`；
- 写侧：`exitListener_->watch(...)` 于 `:2120` → `StandaloneProxyInfo& info = standaloneProxies_[indexId];` 于 `:2173` → `proxyWatchKeys_[indexId] = key;` 于 `:2184`。

⇒ **窗口 ≈ :1735 → :2184（约 450 行）**，其间包含：

1. D5 实例闸门（`OpenProcess` + `GetProcessTimes` + `checkInstanceIdentity`）；
2. **活体检测**：`listeningTcpPorts`（`GetExtendedTcpTable` 全表扫描）+ 每个候选一次 **200ms** SOCKS5 握手；
3. `OpenProcess`（完整掩码 `PROCESS_QUERY_INFORMATION|SYNCHRONIZE|PROCESS_TERMINATE`）；
4. 历史行读写：`findInProgressHistory` / `findInProgressHistoryLoose` + `finalizeStop` / `insertStartWithPort`；
5. `watch()` watcher 注册。

**并发后果**：

- 两个线程可同时通过「已在管」检查 ⇒ **重复注册 watcher** ⇒ `proxyWatchKeys_[indexId]` 被后者覆盖 ⇒ **先前 watcher 泄漏**（其线程与回调仍存活，且 key 已丢失、无法 `unwatch`）；
- 同 pid **重复 `insertStartWithPort`** ⇒ 同 (indexId, pid, startedAt) 多条 in-progress 行 + `start_count` **重复累加**（`ProxyRuntimeHistory.cpp` 的 `UPDATE ProfileExItem SET start_count = start_count + 1` 无条件执行）；
- 生产库 `bin\worker\guiNDB.db` 已实测到 **10 组重复三元组**、**126 条 `ended_at IS NULL` 孤儿行**（成因之一，不能断言唯一成因）。

**v1.6 引入的回退风险**：v1.6 之前窗口内只有文件读（微秒级），v1.6 把**活体检测**放进窗口 ⇒ 窗口从微秒级放大到**数十~数百毫秒** ⇒ 触发概率显著上升。

**同类非原子点**：启动路径 `proxyWatchKeys_[indexId] = exitListener_->watch(`（`src/ui/AppController.cpp:1192`）；R6 接管路径 `auto& sp = standaloneProxies_[indexId];`（`:1359`）与 `proxyWatchKeys_[indexId] = exitListener_->watch(`（`:1375`）、`startedInfo.watchKey = proxyWatchKeys_[indexId];`（`:1439`）。

**附带发现（状态不一致）**：`switchDatabase()` 在 `:204` 只 `proxyWatchKeys_.clear()`，**未清** `standaloneProxies_` ⇒ 切库后旧库的代理条目仍在内存中，而其 watcher key 已被清空。这既放大了 D-A 的破坏面，也正是 v1.6「本次启动的代理旁路检测 + 补写历史表」要覆盖的场景。

### 1.2 D-B：`discardedStandalonePids_` 只增不清

- 声明：`src/ui/AppController.h:321` `std::unordered_set<DWORD> discardedStandalonePids_;`（全仓仅此一处）。
- 写入：**仅 2 处 insert**，**0 处 erase**：
  - `src/ui/AppController.cpp:1798` — D5 实例闸门判定 `Recycled`（pid 已复用）分支；
  - `src/ui/AppController.cpp:1987` — 端口链全空被丢弃分支。
- 用途：`.insert(pid).second` 取「是否首次丢弃该 pid」，据此决定日志级别（首次 **WARN**、后续 **DEBUG**）。因纳管每 `checkIntervalMs` 一周期（生产 `proxy_process_monitor.check_interval_ms = 30000`），不去重会刷屏。

**问题**：pid 被 OS 复用后，新进程的丢弃日志**永久停在 DEBUG**；而生产 `bin\config.json` 的 `log.file_level = "WARN"` ⇒ DEBUG 不落盘 ⇒ **用户完全看不到「进程活着却没被监控」**（对抗性评审 I6 指出的「静默丢弃」诊断黑洞）。

**影响面**：**纯诊断性**。该集合**不参与任何路由决策**（丢弃判定完全由 `route` + 端口链结果决定）⇒ 危害是「故障可见性下降」，不是「行为错误」。

### 1.3 D-C：`osPorts` 为空 ⇒ `Unknown` 放行，无法区分两种「空」

判定链（`include/StandaloneAdoptionPolicy.h:90-112`，`classifyPortCandidate`）：

```
candidate <= 0 || candidate > 65535              => Disproven
excludedPort > 0 && candidate == excludedPort     => Disproven   (gRPC api 端口)
osPorts.empty()                                  => Unknown      ("No live evidence at all: nothing can be disproven.")
!containsPort(osPorts, candidate)                => Disproven   ("The pid does not listen on this port -> stale.")
containsPort(socksPorts, candidate)              => Verified
否则                                                => Unverified
```

`ProcessInspector::listeningTcpPorts(DWORD pid)`（`src/ProcessInspector.cpp:308`）的实现：先 `GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0)` **探测缓冲大小**（须返回 `ERROR_INSUFFICIENT_BUFFER`），再取表并过滤 `dwOwningPid == pid && dwState == MIB_TCP_STATE_LISTEN`，端口经 `localPortToHost()` 换算（v4 段 `:317-337`，v6 段紧随其后）。

⇒ **两次 `GetExtendedTcpTable` 失败时与「该 pid 确实没有 LISTEN 端口」在 API 层返回完全相同的结果——空 vector**（`ports` 初始化为空，失败时直接 return）。于是：

- 情形 1：**表读不到**（权限/瞬时失败/容器环境）⇒ `osPorts` 空 ⇒ 当前实现按 `Unknown` 放行；
- 情形 2：**表读到了，但该 pid 确实没在监听**（配置错误导致 xray 未 bind / 进程已僵死）⇒ `osPorts` 同样为空 ⇒ **也被按 `Unknown` 放行**。

**为什么必须 fail-open（不能改成「空 ⇒ 证伪」）**：若把空一律当证伪，则 TCP 表**任何一次**读取失败 ⇒ 所有层候选全部被证伪 ⇒ 所有健康代理被丢弃 ⇒ **精确复现本次要修的缺陷**（对抗性评审 B1 / 可行性评审 G10 的共同结论）。

**取舍原则**：「看不见」比「显示错端口」危害更大 —— 看不见 = 杀不掉 + 监控丢失；错端口 = 探活报错、人能看出来。

**彻底修法（本次要做）**：让活体扫描**携带读取状态**，把两种「空」分开：

- `readOk == false`（表读不到）⇒ 维持 `Unknown` 放行（fail-open，不变）；
- `readOk == true` 且 `ports` 为空 ⇒ **该 pid 确实没有 LISTEN 端口** ⇒ 所有外部来源候选（历史/配置/目录）**被证伪** ⇒ `Disproven` ⇒ 丢弃。

`readOk == true` 的判定依据是「第一次 `GetExtendedTcpTable` 探测缓冲返回 `ERROR_INSUFFICIENT_BUFFER`」—— 该返回值说明**表可枚举**，后续取表失败才置 `readOk = false`。

---

## 2. 修复方案

### D1：纳管加 in-flight 互斥（解决 D-A）

引入一个**进程级、单实例**的 in-flight 标记：

```cpp
// src/ui/AppController.h
private:
  // Guards against concurrent entries into adoptDanglingStandaloneProxies().
  // The adoption scan is spawned on detached threads from three places
  // (startup, the 30s monitor timer and switchDatabase), and the
  // "already managed" check at the top of the scan is NOT atomic with the
  // watcher registration at the bottom (the live OS probe in between takes
  // tens to hundreds of milliseconds). Two concurrent scans would register
  // the same proxy twice: the second watch() would overwrite the first
  // watch key (leaking the first watcher) and would append a duplicate
  // in-progress history row (double counting start_count).
  // A periodic, idempotent-by-nature scan may simply skip one round.
  std::atomic<bool> adoptInFlight_{ false };
```

入口处（`adoptDanglingStandaloneProxies()` 的**最开头**，在任何枚举之前）：

```cpp
    if (adoptInFlight_.exchange(true)) {
        // Another adoption scan is already running. Skipping one round is
        // harmless: the scan is periodic and the running one will pick up
        // the same processes.
        Logger::write("[StandaloneProxy] Adoption scan already in progress; skipping this round",
                      LogLevel::DEBUG);
        return;
    }
```

**复位必须覆盖所有退出路径**（函数内有多个 `continue` 与 `return`）⇒ **禁止手写 `adoptInFlight_.store(false)`**，改用 **RAII 守卫**（局部结构体，析构时复位）：

```cpp
// 匿名 namespace 内
class AdoptionInFlightGuard {
public:
    explicit AdoptionInFlightGuard(std::atomic<bool>& flag) : flag_(flag) {}
    ~AdoptionInFlightGuard() { flag_.store(false); }
    AdoptionInFlightGuard(const AdoptionInFlightGuard&) = delete;
    AdoptionInFlightGuard& operator=(const AdoptionInFlightGuard&) = delete;
private:
    std::atomic<bool>& flag_;
};
```

用法：

```cpp
    if (adoptInFlight_.exchange(true)) { /* ... */ return; }
    AdoptionInFlightGuard inFlightGuard(adoptInFlight_);
```

**不在本规格范围内**：把启动路径（`:1192`）与 R6 接管路径（`:1359`/`:1375`）也纳入同一把锁 —— 那两条路径由 `startStandaloneProxy` / watcher 线程驱动，与纳管扫描的并发概率与破坏面不同，另行评估。

### D2：丢弃记录改为带实例指纹的映射（解决 D-B）

把 `std::unordered_set<DWORD> discardedStandalonePids_;` 换成**带创建时间指纹**的映射，使「pid 被复用」可被识别：

```cpp
// src/ui/AppController.h
  // Pids of standalone proxies that were discarded by the adoption scan,
  // mapped to the process creation time observed at the time of the discard.
  // The creation time is the instance fingerprint: when the OS recycles the
  // pid, the fingerprint changes and the record must be reset so the new
  // instance logs at WARN again instead of being permanently downgraded to
  // DEBUG (which is invisible when log.file_level == WARN).
  std::unordered_map<DWORD, std::string> discardedStandalonePids_;
```

配套一个**纯函数**（便于单测，也可被监控路径复用）：

```cpp
// include/StandaloneAdoptionPolicy.h
namespace standalone_adoption {
// True when the recorded discard belongs to the very same process instance
// that is being inspected now. An empty recorded timestamp means the record
// was written before the fingerprint was known -> treat it as "same" so the
// log level does not flap.
inline bool isSameProcessInstance(const std::string& recordedCreatedAt,
                                  const std::string& currentCreatedAt);
}
```

判定规则：
- `recordedCreatedAt` 为空 ⇒ 视为同一实例（保守：维持现有降级行为）；
- `currentCreatedAt` 为空 ⇒ 视为同一实例（无法证伪）；
- 两者都非空且相等 ⇒ 同一实例（继续降级为 DEBUG）；
- 两者都非空且不等 ⇒ **新实例**（重置记录，下一次丢弃重新打 WARN）。

两处 insert 点（`:1798`、`:1987`）改为「先查后写」：

```cpp
    const bool sameInstance =
        standalone_adoption::isSameProcessInstance(recordedCreatedAt, currentCreatedAt);
    if (!sameInstance) {
        discardedStandalonePids_[pid] = currentCreatedAt;   // 新实例：重置
    }
    const bool firstDiscard = discardedStandalonePids_.find(pid) == discardedStandalonePids_.end();
```

> ⚠️ 实施时必须保证「首次丢弃 ⇒ WARN」这一语义不被破坏：推荐写法是**先判断是否新实例**，新实例时先 `erase` 再 `insert`，然后用 `insert(...).second` 的返回值判定是否首次。

**附加清理（可选，同批做）**：每轮纳管开始时，用本轮 `enumerateByName` 得到的 pid 集合，对 `discardedStandalonePids_` 做**差集清理**（进程已不存在的 pid ⇒ erase），避免集合随运行时间无限增长。

### D3：活体扫描携带读取状态（解决 D-C）

**API 变更**（`include/ProcessInspector.h` / `src/ProcessInspector.cpp`）：

```cpp
  // Result of a live TCP table scan for one pid.
  // `readOk` distinguishes "the table could not be enumerated" from
  // "the table was read successfully and this pid has no LISTEN socket".
  // The distinction matters: a failed scan must NOT disprove any candidate
  // (that would drop healthy proxies), while a successful scan with an
  // empty result DOES disprove every candidate coming from a file or from
  // the history table.
  struct TcpPortScan {
    bool readOk = false;
    std::vector<int> ports;
  };

  // Scans the local TCP tables (IPv4 + IPv6) for LISTEN sockets owned by
  // `pid`. `readOk` is false when the table could not be enumerated.
  static TcpPortScan scanListeningTcpPorts(DWORD pid);

  // Backwards-compatible wrapper: returns only the ports.
  static std::vector<int> listeningTcpPorts(DWORD pid);
```

`scanListeningTcpPorts` 的实现要点（`src/ProcessInspector.cpp`，现有 `listeningTcpPorts` 的 v4/v6 两段逻辑照搬）：

- 第一次 `GetExtendedTcpTable(nullptr, &size, ...)` 返回 `ERROR_INSUFFICIENT_BUFFER` ⇒ **表可枚举**，继续；
- 第二次（带缓冲）调用成功 ⇒ `readOk = true`；
- 任一族的第二次调用失败 ⇒ `readOk = false`（**保守放行**）；
- 过滤 `dwOwningPid == pid && dwState == MIB_TCP_STATE_LISTEN`，端口经 `localPortToHost()` 换算，去重升序。

**判定链扩展**（`include/StandaloneAdoptionPolicy.h`）：`classifyPortCandidate` 新增 `osFactsReadable` 参数：

```cpp
  // Classifies one candidate port against the live facts about its process.
  // `osFactsReadable` is true when the TCP table could be enumerated for the
  // process. When it is false nothing can be disproven -> Unknown (pass).
  // When it is true and the pid owns no LISTEN socket at all, every candidate
  // is stale -> Disproven.
  inline PortVerdict classifyPortCandidate(int candidate,
                                            const std::vector<int>& osPorts,
                                            const std::vector<int>& socksPorts,
                                            int excludedPort,
                                            bool osFactsReadable);
```

判定顺序（在现有基础上插入一条）：

```
candidate <= 0 || candidate > 65535            => Disproven
excludedPort > 0 && candidate == excludedPort   => Disproven
!osFactsReadable                                => Unknown    (表读不到：放行)
osPorts.empty()                                => Disproven  (表读到了，该 pid 确实没在监听：证伪)
!containsPort(osPorts, candidate)              => Disproven
containsPort(socksPorts, candidate)            => Verified
否则                                             => Unverified
```

**连带影响（必须一并处理）**：

1. `resolveChainPort` 需透传 `osFactsReadable`；
2. 纳管路径（`src/ui/AppController.cpp` FullDetect 路由）改用 `scanListeningTcpPorts` 的 `readOk`；
3. R6 接管路径（`:1310`/`:1316` 取证处）同步改用；
4. 策略单测 `tests/test_standalone_adoption_policy.cpp` 全部 `classifyPortCandidate` / `resolveChainPort` 用例需补参数并新增「表读得到且该 pid 无监听 ⇒ Disproven」用例；
5. `tests/test_process_inspector.cpp` 新增「`scanListeningTcpPorts` 对无监听 pid 返回 `readOk=true` + 空 ports」用例。

**风险与降级**：`readOk = true` 且端口为空 ⇒ 该 pid 确实没在监听 ⇒ 丢弃是**期望行为**（进程在但服务不可用）；但若某台机器上 `GetExtendedTcpTable` 因环境原因**始终**返回「表可枚举但内容为空」，则会把所有代理证伪 ⇒ 丢弃。缓解：Windows 上 `GetExtendedTcpTable` 对本机 IPv4 表不会返回空表（系统至少有多个 LISTEN socket），该风险可接受；若评审要求更保守，可改为「仅当该 pid 在**进程列表**中存在且 `osPorts` 为空时证伪」。

---

## 3. 改动清单

### Task 0：策略头扩展（纯函数，可单测）

- `include/StandaloneAdoptionPolicy.h`
  - 新增 `inline bool isSameProcessInstance(const std::string& recordedCreatedAt, const std::string& currentCreatedAt);`
  - `classifyPortCandidate` 增加 `bool osFactsReadable` 参数（**破坏性签名变更**）；
  - `resolveChainPort` 增加 `bool osFactsReadable` 参数（**破坏性签名变更**）。
- `tests/test_standalone_adoption_policy.cpp`（⚠️ 该文件**无** `using namespace standalone_adoption;`，一律写全限定名）
  - 改写全部受签名影响的既有用例；
  - 新增用例：
    - `Classify_DisprovenWhenTableReadableAndPidHasNoListener`（表可读 + `osPorts` 空 ⇒ Disproven）
    - `Classify_UnknownWhenTableUnreadable`（`osFactsReadable=false` ⇒ Unknown 放行）
    - `Classify_UnknownWhenFactsUnreadableAndOsPortsEmpty`（表不可读 ⇒ 即使 ports 为空也放行）
    - `ResolveChain_DisprovesAllWhenTableReadableAndPidHasNoListener`
    - `ResolveChain_PassesThroughWhenTableUnreadable`
    - `Instance_同一实例判定_同时间戳` / `Instance_同一实例判定_记录为空` / `Instance_新实例判定_时间戳不同` / `Instance_同一实例判定_当前时间为空`

### Task 1：活体扫描返回读取状态

- `include/ProcessInspector.h`：新增 `struct TcpPortScan { bool readOk = false; std::vector<int> ports; };` 与 `static TcpPortScan scanListeningTcpPorts(DWORD pid);`（`listeningTcpPorts` 保留为包装）。
- `src/ProcessInspector.cpp`：按 D3 要点实现 v4 + v6 两段扫描。
- `tests/test_process_inspector.cpp`：
  - 新增 `ScanListeningTcpPorts_ReportsReadOkForLiveProcess`（自建回环监听 socket + `GetCurrentProcessId()` ⇒ `readOk == true` 且 ports 含该端口）；
  - 新增 `ScanListeningTcpPorts_ReportsReadOkButEmptyForNonListeningPid`（取一个必定不存在的 pid ⇒ `readOk == true` + `ports` 空）。⚠️ 需注意：若测试机上恰好有该 pid 在监听会偶发失败 ⇒ 用一个刚 `OpenProcess` 失败的超大 pid 值以规避。

### Task 2：纳管加 in-flight 互斥（D-A）

- `src/ui/AppController.h`：新增成员 `std::atomic<bool> adoptInFlight_{ false };`（需 `#include <atomic>`）。
- `src/ui/AppController.cpp`：
  - 匿名 namespace 内新增 `AdoptionInFlightGuard`；
  - `adoptDanglingStandaloneProxies()` 入口加 `exchange(true)` 判定 + RAII 守卫；
  - 并发命中时打 DEBUG 日志后 `return`。
- 验证要点：`switchDatabase()` 与 30s 定时器**可能**在纳管进行中触发切库 ⇒ 切库那一次纳管会被跳过（**可接受**：下一轮 30s 定时器会补上；需在 §4 实机验收中确认切库后下一周期能纳管）。

### Task 3：丢弃记录带实例指纹（D-B）

- `src/ui/AppController.h`：`std::unordered_set<DWORD> discardedStandalonePids_;` → `std::unordered_map<DWORD, std::string> discardedStandalonePids_;`（注释说明用途与实例指纹）。
- `src/ui/AppController.cpp`：两处 insert 点（`:1798`、`:1987`）改为「先判新实例 → 重置 → 再用 `insert().second` 判定首次」；两处均需传入当前进程的创建时间（`procs[i].creationTime`）。
- 可选：每轮纳管开始时按本轮 pid 集合做差集清理。

### Task 4：纳管 / R6 路径接入 `readOk`（D-C）

- `src/ui/AppController.cpp`：
  - 纳管路径取证处改用 `ProcessInspector::scanListeningTcpPorts(pid)`，把 `readOk` 透传给 `resolveChainPort`；
  - R6 接管路径取证处（`:1310`/`:1316`）同步改用。

### Task 5：构建与测试

- `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`（若未配置）
- `cmake --build build --parallel 8` ⇒ 必须 **0 error**
- 在 `build/` 目录执行 `ctest -R "StandaloneAdoptionPolicyTest|RuntimeHistoryDAOTest|ProcessInspectorTest|UtilsTest" -V` ⇒ 必须全绿
- 在 `build/` 目录执行全量 `ctest` ⇒ 已知基线：`XrayManagerStartupTest` 失败（需真实就绪 xray）、`UI_FLOATINGWIDGET` 为已知并行 flaky（单跑多数通过）

### Task 6：文档登记

- `docs/INDEX.md` §9.1（`bugfix` 表）新增一行，编号 **101**；
- `docs/plans/project-plans-tracker.md`「近期文档引用」表**表尾追加**一行（该表**无独立状态列**，状态写进「说明」列末尾）；
- 规格文档追加「实施记录」附录（改动表 / 验证结果 / 剩余残留）。

---

## 4. 验证

### 4.1 构建与单测

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
# 在 build/ 目录执行
ctest -R "StandaloneAdoptionPolicyTest|RuntimeHistoryDAOTest|ProcessInspectorTest|UtilsTest" -V
ctest -V
```

- `git diff -U0 | Select-String '^\+.*\bauto\b'` ⇒ **0**（⚠️ `git diff -U0` **不覆盖未纳入版本控制的新文件**，新增文件须单独 grep）；
- 新增/修改文件中不得出现 `auto` 类型推导（AGENTS.md §1）。

### 4.2 实机验收

| 步骤 | 操作 | 期望判据 |
| :--- | :--- | :--- |
| 1 | 启动 `bin\validproxy.exe`，确认纳管日志 | `source=` / `verdict=` 正常；**不得**出现「Adoption scan already in progress」刷屏（DEBUG 级） |
| 2 | 切库（GUI 内切换数据库） | 切库当次的纳管**可能被跳过**（in-flight 命中）⇒ 等待**下一个 30s 周期**后，数据库中应出现对应 indexId 的 in-progress 行（`socks_port` 正确） |
| 3 | 手动启动一个 `xray` 并删掉其配置文件 | 纳管日志出现 WARN（**不是** DEBUG）⇒ 确认「首次丢弃打 WARN」语义未被破坏 |
| 4 | 让 OS 复用 pid（需回收大量进程，实验环境不易构造） | **以单测覆盖为准**（`isSameProcessInstance` 的「时间戳不同 ⇒ 新实例 ⇒ 重置为 WARN」用例） |
| 5 | 停掉一个已纳管 xray 的 SOCKS 监听（不可行则跳过） | 纳管时 `osPorts` 空 + `readOk=true` ⇒ 该进程被丢弃并打 WARN ⇒ 验证 D-C 生效 |
| 6 | 确认数据库 | `proxy_runtime_history` 中**同一 pid 不应出现两条 in-progress 行**；`start_count` 不应因纳管而重复累加 |

> 注：生产 `bin\config.json` 的 `log.file_level = "WARN"` ⇒ `REPORT` 级「Adopted」日志不落盘；验收 1/2 需临时把 `file_level` 调为 `REPORT` 才能看到，**验收后应改回**。

---

## 5. 风险与边界

| 编号 | 风险 | 缓解 |
| :--- | :--- | :--- |
| R1 | in-flight 标记导致**纳管被连续跳过**（例如切库与定时器恰好同时） | 纳管是 30s 周期任务，跳过一轮代价极小；`switchDatabase` 后下一周期必补 |
| R2 | in-flight 标记因异常/未覆盖的返回路径**未复位** ⇒ 纳管永久停摆 | 必须用 **RAII 守卫**而非手写复位（覆盖 `continue`/`return`/异常） |
| R3 | D-C 的「表可读且该 pid 无监听 ⇒ 证伪」若在某环境恒成立 ⇒ 所有代理被丢弃 | 该分支只在**表成功枚举**后触发；验收步骤 5 需人工确认；必要时可降级为「仅当该 pid 在进程列表中存在时证伪」 |
| R4 | `classifyPortCandidate` / `resolveChainPort` 签名破坏性变更 ⇒ 既有调用点/单测全部需改 | Task 0/4 一次性改完；`rg` 确认无遗漏调用点 |
| R5 | `discardedStandalonePids_` 从 set 变 map ⇒ 两处 insert 点的语义变化引入「首次判定」回归 | 用 `isSameProcessInstance` 单测锁定语义；验收步骤 3 人工确认 WARN 仍打 |
| R6 | D-A 只保护纳管函数本身，启动/R6 路径仍非原子 | 明确列为**非目标**（另案评估） |
| R7 | 生产库存量 126 条 `ended_at IS NULL` 孤儿行与 `start_count` 恒 0 **不会**被本规格修复 | 明确列为**非目标**（数据治理另案） |

### 非目标（N）

| 编号 | 非目标 | 理由 |
| :--- | :--- | :--- |
| N1 | 清理生产库存量孤儿行 / 修复 `start_count` 恒 0 | 数据治理问题，与本规格三项缺陷正交 |
| N2 | 把启动路径（`:1192`）与 R6 路径（`:1359`/`:1375`）纳入同一把锁 | 触发路径与破坏面不同，另案评估 |
| N3 | 为 `GetExtendedTcpTable` 增加缓存（参考 `utils` 侧 TCP 快照） | 性能优化，非正确性缺陷 |
| N4 | `switchDatabase()` 的 `standaloneProxies_` / `proxyWatchKeys_` 状态一致性重构 | 已有 v1.6 旁路检测覆盖主要后果；完整重构另案 |
| N5 | `discardedStandalonePids_` 的长期容量治理 | 差集清理已足够；更彻底的方案（LRU）属过度设计 |

---

## 6. 文档登记

- `docs/INDEX.md` §9.1 `bugfix` 表：新增编号 **101**，指向本文档；
- `docs/plans/project-plans-tracker.md`「近期文档引用」表：表尾追加一行（日期 2026-10-21 / 类型 bugfix / 路径 `docs/bugfix/2026-10-21-Bugfix-StandaloneAdoption-ConcurrencyAndDiagnostics-v1.0.md` / 说明含三项缺陷与实施状态）；
- 本文档在实施后追加「实施记录」附录（改动表 / 验证 / 剩余残留）。
