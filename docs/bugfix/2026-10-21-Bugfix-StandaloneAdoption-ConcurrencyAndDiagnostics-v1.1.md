# 纳管独立代理进程：并发重入、丢弃日志降级、OS 表不可区分三项缺陷（v1.1）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:systematic-debugging to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复三项缺陷 —— D-A 纳管扫描无重入保护（并发正确性）、D-B 丢弃日志在 pid 复用后永久降级为 DEBUG（诊断可见性）、D-C 活体 TCP 表「读不到」与「确实没监听」不可区分（判定精度）。

**Tech Stack:** C++17 / Win32 `GetExtendedTcpTable` / `std::atomic` / `ui::ScopeGuard` / GoogleTest / CMake + Ninja（MinGW GCC）

**依据文档:** `docs/specs/2026-10-21-Spec-StandaloneAdoption-InstanceBoundPortFallback-v1.1.md`（v1.1–v1.6 纳管端口回落链路）

**约束:** 全栈禁止使用 `auto` 进行类型推导（AGENTS.md §1）；测试源码只允许放在 `tests/`。

---

## 0. 变更记录（v1.0 → v1.1）

v1.0 经**两份独立评审**（对抗性评审 FAIL_WITH_CHANGES + 可行性评审 FEASIBLE_WITH_CHANGES，均只读）后修订。逐项处置如下：

| 评审编号 | 问题 | v1.1 处置 |
| :--- | :--- | :--- |
| 对抗 B2 / feas B1 | D-C 的「`osPorts.empty() ⇒ Disproven`」会打断 **R6 接管路径**，接管端口被打成 0 | **D3 限定作用域**：「空 ⇒ Disproven」仅在**纳管 FullDetect 路径**启用；R6 调用点传 `readOk && !osPorts.empty()`，等价旧语义（见 §2 D3） |
| feas B2 / 对抗 B3 | `readOk` 把「族级失败」升级为「整次扫描不可读」，会静默关掉刚修好的 OS 回落层 | **D3 改为逐族可读性**：`v4ReadOk` / `v6ReadOk` 分族记录，整体 `readOk = v4ReadOk \|\| v6ReadOk`（见 §2 D3） |
| 对抗 B1 / feas B4 | D2 给出的代码片段自相矛盾（`operator[]` 会插入 ⇒ `firstDiscard` 恒 false） | **D2 给出唯一正确写法**（先 `find` → 比对 → `erase` → `insert().second`），并显式警告不得用 `operator[]` 判首次（见 §2 D2） |
| 对抗 B6 / feas B3 | D-A 无任何自动化验证；§4.2 判据不可观测（`file_level=REPORT` 反而更看不到 DEBUG） | **D1 抽出可单测的纯函数** `tryBeginAdoption(std::atomic<bool>&)` + 3 条用例；§4.2 步骤 1 改 `file_level=DEBUG` 并给出**正向**判据（见 §2 D1、§4.2） |
| 对抗 B4 / feas I2 | 规格称「`switchDatabase()` 未清 `standaloneProxies_`」为**假**；真实缺陷是 `:204` 的 `proxyWatchKeys_.clear()` **无锁**（数据竞争 / UB） | **D-A3** 把「`proxyWatchKeys_` 锁纪律统一 + clear 移入锁域」列为 D-A 的**第三个子缺陷**；订正 §1.1 证据（见 §1.1、§2 D-A3） |
| 对抗 B5 | D1 只互斥「扫描 vs 扫描」，未覆盖「扫描 vs 启动路径」的 `watchKey` 覆盖 ⇒ watcher 泄漏 | **D-A2 写侧复检**：把 `alreadyManaged` 判定下移到注册 `watch()` 之前的临界区内，已被登记则放弃本次纳管（见 §2 D-A2） |
| feas I4 | D1 重新发明既有 RAII 工具 | **D1 改用既有 `ui::ScopeGuard<std::atomic<bool>>`**（`include/ui/ScopeGuard.h`，本文件已用 7 次），删除自造守卫类（见 §2 D1） |
| feas I1 / 对抗 I5 | `proxyWatchKeys_` 四处访问锁纪律不一致 ⇒ 容器级数据竞争 | **D-A3** 统一 4 处加锁（见 §2 D-A3） |
| feas I3 | D1 的 RAII 理由错误（函数内无 `return`） | 理由订正为「未来新增早退路径时的健壮性」（`continue` 不退出函数） |
| feas I9 / 对抗 I4 | `isSameProcessInstance` 空串语义与同头 `checkInstanceIdentity` 相反；指纹为秒级精度 | **D2 改名 `isSameProcessFingerprint`** 并在注释中写明与 `checkInstanceIdentity` 的空串约定**相反**；登记秒级精度限制（见 §2 D2、§5 R10） |
| feas M3 | `inline` 只声明不定义 ⇒ header-only 单测链接错误 | **D2 给出完整函数体**（见 §2 D2） |
| feas I13 | `scanListeningTcpPorts` 若打日志 ⇒ `test_process_inspector` 链接失败（该 target 不链 Logger） | **D3 明确该函数不打日志**（见 §2 D3） |
| feas I12 | `TcpPortScan` 嵌套层级歧义 | **D3 裁决为类内嵌套** `ProcessInspector::TcpPortScan`（见 §2 D3） |
| feas I6 / 对抗 I9 | `readOk=false` 分支无接缝无用例；不存在的 pid 选取提示不严谨 | `readOk=false` 由策略层用例间接覆盖（明确登记）；不存在的 pid 用超出有效范围的常量 `0xFFFFFFFC`；补 `scanListeningTcpPorts(0)` 用例且 `readOk` 期望为 **false**（见 §3 Task 1、§5 R11） |
| 对抗 I10 / feas I10 | 「每轮开始按 pid 集合做差集清理」在当前结构下位置不成立 | **删除该可选项**，登记「集合随运行时长缓慢增长，未做容量治理」（见 §5 R12） |
| 对抗 I8 / feas 步骤 3 | §4.2 步骤 3/5 的预期不成立（运行中的 xray 仍在监听 ⇒ 不进 discard 分支） | **§4.2 重写步骤 3/5**（见 §4.2） |
| feas I8 / 对抗 I9 | `start_count` 实际恒 0，不能作验收信号 | **§4.2 步骤 6 改判「同一 pid 的 in-progress 行数」**（见 §4.2） |
| 对抗 I7 / feas I7、I16 | 生产库实测数字漂移（128 / 11 组 / in-progress 内仅 2 组） | **§1.1 数字订正为实测值并标注口径**，归因强度下调（见 §1.1） |
| 对抗 I1 | D1「跳过一轮」在监控定时器未启用时会变成整会话漏纳管 | **R1 措辞改为条件式**（见 §5 R1） |
| 对抗 I2 | D-C 落地后 discard 分支近乎不可达；discard WARN 文案与新判定理由不符 | **同步修改 discard 日志文案**，区分「链全空」与「被活体事实证伪」（见 §3 Task 3） |
| 对抗 I6 / feas I6 | tracker 表「无独立状态列」为假（数据行是 5 列，末列为状态） | **Task 6 按 5 列追加**（末列为状态）（见 §3 Task 6） |
| feas M2 / M4 / M7 | 测试风格：逐名 using 声明 + 非限定调用；测试名应 ASCII | Task 0 沿用既有 using 风格；新用例名全部 ASCII（见 §3 Task 0） |
| feas M5 / 对抗 M5 | `git diff -U0` 不覆盖已暂存改动 | **改用 `git diff HEAD -U0`**（见 §4.1） |
| feas M8 | §4.1 未给期望数字 | **补「全量 ctest 期望 54/56，失败项恰为 `XrayManagerStartupTest` + `UI_FLOATINGWIDGET`」**（见 §4.1） |
| feas M9 / 对抗 M7 | R3 措辞未限定「本会话自启动的代理走 BypassDetect 不受影响」 | **R3 措辞限定**（见 §5 R3） |
| feas I14 | §4.2 全程写生产库；切库路径无 `VALIDPROXY_NO_ADOPT` 门控 | **§4.2 步骤 0 要求先备份生产库**并注明切库无门控（见 §4.2） |
| 对抗 I3 / feas M | 破坏性签名变更的调用点清单缺失 | **Task 0 给出全部 20 处清单**（见 §3 Task 0） |

---

## 1. 现场证据与代码事实

### 1.1 D-A：纳管扫描无重入保护（并发正确性，高优先级）

**调用点**（`adoptDanglingStandaloneProxies()`，声明 `src/ui/AppController.h:221`，实现 `src/ui/AppController.cpp:1676`）：

| 位置 | 时机 | 备注 |
| :--- | :--- | :--- |
| `src/ui/MainFrame.cpp:453` | `startMonitoring()` 启动时 | 带 `VALIDPROXY_NO_ADOPT=1` 门控 |
| `src/ui/MainFrame.cpp:979` | `onProxyMonTimer`（30s 定时器） | 同样带该门控 |
| `src/ui/AppController.cpp:208-209` | `switchDatabase()` 内 | **无**该门控 |

三处均**新起 detached 线程**，无互斥。

**竞态窗口**：读侧 `alreadyManaged` 计算于 `src/ui/AppController.cpp:1724-1735`（锁内）→ `:1756` `decideAdoptionRoute` → `:1758` `Skip ⇒ continue`；写侧 `watch()` 于 `:2120`、`standaloneProxies_[indexId]` 于 `:2173`、`proxyWatchKeys_[indexId]=key` 于 `:2184`。**窗口 ≈ 450 行**，其间含 D5 实例闸门、**活体检测（TCP 全表扫描 + 每候选一次 200ms SOCKS5 握手）**、OpenProcess、历史行读写、`watch()` 注册。

**三类子缺陷**：
1. **D-A1 扫描 vs 扫描**：两个线程同时通过「已在管」检查 ⇒ 重复注册 watcher ⇒ `proxyWatchKeys_[indexId]` 被覆盖 ⇒ 先前 watcher 线程泄漏（key 丢失无法 `unwatch`）；重复 `insertStartWithPort` ⇒ 同 pid 多条 in-progress 行。
2. **D-A2 扫描 vs 启动路径**：启动路径 `src/ui/AppController.cpp:1192` 写 `proxyWatchKeys_[indexId]`，与纳管路径 `:2184` 写同一 map 同一 key ⇒ 交错时启动路径的 key 被覆盖 ⇒ 同样泄漏。**D-A1 的 in-flight 标记不覆盖这一对**。
3. **D-A3 `proxyWatchKeys_` 锁纪律不一致 ⇒ 容器级数据竞争（UB）**：读 `:1732`（锁内）、`:2184`（锁内）、`:1439`（锁内）；写 `:1192`（启动路径，**无锁**）、`:1375`（R6，**无锁**）、`:204`（`clear()`，**无锁**）。`src/ui/AppController.h:317` 该成员**没有**「Guarded by standaloneMutex_」注释（而 `:320`、`:331` 都有）。`src/ui/AppController.cpp:200-203` 在锁内执行了 `standaloneProxies_.clear()`；`:204` 的 `proxyWatchKeys_.clear()` **在锁外** ⇒ 与纳管线程 `:1732`/`:2184` 并发改同一 `std::unordered_map`。

**生产库实测**（`bin/worker/guiNDB.db`，2026-10-21 只读）：`ended_at IS NULL` = **128** 行；全表重复 `(index_id,pid,started_at)` 三元组 = **11 组**，但**限定 `ended_at IS NULL` 的仅 2 组** ⇒ 多数重复来自已结束的历史记录，与并发纳管**不同源**，归因强度须下调。另有一条**与并发无关**的重复成因：`findInProgressHistory` 三元组为精确字符串相等（`src/ProxyRuntimeHistory.cpp:306-309`），而 `started_at` 有三级兜底（`src/ui/AppController.cpp:2084-2091`），某轮走「now」兜底后下一轮读到真实创建时间 ⇒ 失配 ⇒ 关闭旧行 + 追加新行。

### 1.2 D-B：`discardedStandalonePids_` 只增不清（诊断可见性，中优先级）

- 声明 `src/ui/AppController.h:321` `std::unordered_set<DWORD> discardedStandalonePids_;`；**仅 2 处 insert、0 处 erase**：`src/ui/AppController.cpp:1798`（D5 闸门 `Recycled` 分支）、`:1987`（端口链全空分支）。
- 用途：`.insert(pid).second` 取「是否首次丢弃该 pid」⇒ 日志级别（首次 WARN、后续 DEBUG）。纳管每 30s 一周期（`proxy_process_monitor.check_interval_ms=30000`），不去重会刷屏。
- 问题：pid 被 OS 复用后，新进程的丢弃日志**永久停在 DEBUG**；生产 `log.file_level="WARN"` ⇒ DEBUG 不落盘 ⇒ **「进程活着却没被监控」完全不可见**。
- 影响面：**仅诊断性**，不参与路由决策。
- 指纹来源：`instanceStartedAt`（句柄 `GetProcessTimes`）优先，其次 `procs[i].creationTime`（枚举预算）。二者均为**秒级**字符串（`src/ProcessInspector.cpp:377-395`）⇒ 同秒复用不可区分（登记为 §5 R10）。

### 1.3 D-C：活体 TCP 表「读不到」与「确实没监听」不可区分（判定精度，低优先级）

- 判定链 `include/StandaloneAdoptionPolicy.h:90-112`：`osPorts.empty() ⇒ Unknown`（放行）。
- `ProcessInspector::listeningTcpPorts(DWORD pid)`（`src/ProcessInspector.cpp:308`）：v4 段 `:316-337`、v6 段 `:340-361` **各自独立**「探测 → 取表 → 过滤」；任一段第二步失败只丢该族；两族失败与「该 pid 确实无 LISTEN」都返回**空 vector**。
- 必须 fail-open 的理由：若改成「空 ⇒ 证伪」，TCP 表**任何一次**读取失败 ⇒ 所有层候选全部被证伪 ⇒ 所有健康代理被丢弃 ⇒ **精确复现 v1.6 刚修掉的缺陷**（`docs/INDEX.md:488` bugfix #100）。取舍原则：「看不见」比「显示错端口」危害更大。
- 本缺陷的正解不是「更强的证伪」，而是**让调用方知道表到底读没读到**（`readOk`），再由调用方按场景决定是否启用「空 ⇒ 证伪」。

---

## 2. 修复方案

### D-A1：纳管扫描 in-flight 互斥（可单测）

在 `src/ui/AppController.h` 新增 `std::atomic<bool> adoptInFlight_{false};`（`#include <atomic>` 已存在，`src/ui/AppController.h:9`）。

`src/ui/AppController.cpp` 纳管函数入口：

```cpp
    // Only one adoption sweep may run at a time: the sweep registers
    // watchers and appends history rows, and two concurrent sweeps would
    // duplicate both (the second one overwrites proxyWatchKeys_[indexId],
    // leaking the first watcher). A sweep that finds the flag set simply
    // returns; the next 30s timer tick picks the work up.
    if (!standalone_adoption::tryBeginAdoption(adoptInFlight_)) {
        Logger::write("[StandaloneProxy] Adoption scan already in progress; "
                      "skipping this round", LogLevel::DEBUG);
        return;
    }
    ScopeGuard<std::atomic<bool>> adoptGuard(adoptInFlight_);
```

- 守卫用**既有** `ui::ScopeGuard<std::atomic<bool>>`（`include/ui/ScopeGuard.h:16-30`；`src/ui/AppController.cpp:9` 已 include、`:13` 已 `using ui::ScopeGuard;`；本文件已在 `:2537/2562/2588/2634/2674/2727/2733` 用同型模式 7 次）。**不新增守卫类**。
- 理由订正：`adoptDanglingStandaloneProxies()` 当前**一个 `return` 都没有**（4 处 `return` 全在 lambda 内），`continue` 不退出函数 ⇒ 守卫并非必需；保留它是为了**未来新增早退路径时的健壮性**。
- `tryBeginAdoption` 为**可单测的纯函数**（见 D-A1 的策略头声明）。

### D-A2：写侧复检（覆盖「扫描 vs 启动路径」）

把「是否已被登记」的判定**下移到注册 `watch()` 之前的临界区**：加锁后再次检查 `standaloneProxies_` / `proxyWatchKeys_`，若已被其它路径登记 ⇒ `CloseHandle(hProcess)` + `continue`（放弃本次纳管），并把 `watch()` 一并移入该临界区，使「查—写」原子。

这样 D-A1 的 in-flight 标记只负责「扫描 vs 扫描」，而「扫描 vs 启动/R6」由写侧复检统一覆盖。

### D-A3：`proxyWatchKeys_` 锁纪律统一

`src/ui/AppController.cpp` 的 4 处无锁访问全部纳入 `standaloneMutex_`：

| 行 | 现状 | 处置 |
| :--- | :--- | :--- |
| `:1192` | 启动路径写 `proxyWatchKeys_[indexId]`（启动路径已持 `standaloneMutex_`，`:777`） | 确认已在锁域；若否则补锁 |
| `:1375` | R6 路径写 `proxyWatchKeys_[indexId]`（**无锁**） | 纳入锁域 |
| `:204` | `switchDatabase()` 中 `proxyWatchKeys_.clear()`（**无锁**） | 移入 `:200-203` 所在的锁域 |
| `:1439` | 读 `proxyWatchKeys_[indexId]`（锁内） | 保持 |

并在 `src/ui/AppController.h:317` 为该成员补上「Guarded by standaloneMutex_」注释。

### D-B：丢弃记录携带实例指纹

`src/ui/AppController.h`：`std::unordered_set<DWORD> discardedStandalonePids_;` ⇒ `std::unordered_map<DWORD, std::string> discardedStandalonePids_;`（pid → 创建时间指纹）；`<unordered_set>` 若无其它用途可删。

策略头新增（**必须给出函数体**，该头为 header-only 且 `test_standalone_adoption_policy` 只链 gtest）：

```cpp
// Compares two process creation-time fingerprints.
//
// NOTE: the empty-string convention is the OPPOSITE of checkInstanceIdentity:
// here an empty fingerprint means "no evidence" and is treated as the SAME
// instance (so the log level does not flap), while checkInstanceIdentity
// treats an empty timestamp as Unverifiable (identity cannot be proven).
inline bool isSameProcessFingerprint(const std::string& recorded,
                                     const std::string& current) {
    if (recorded.empty() || current.empty()) {
        return true;
    }
    return recorded == current;
}
```

两处丢弃点（`src/ui/AppController.cpp:1798`、`:1987`）改为（**在 `standaloneMutex_` 保护下**）：

```cpp
    std::unordered_map<DWORD, std::string>::iterator recIt =
        discardedStandalonePids_.find(pid);
    const bool sameInstance =
        (recIt == discardedStandalonePids_.end()) ||
        standalone_adoption::isSameProcessFingerprint(recIt->second,
                                                       currentCreatedAt);
    if (!sameInstance) {
        // A different process now owns this pid -> drop the stale fingerprint
        // so the next discard is reported as a first discard again.
        discardedStandalonePids_.erase(pid);
    }
    // NOTE: operator[] would insert, so it must NOT be used to detect the
    // first discard. insert().second is true only when the key was absent.
    const bool firstDiscard =
        discardedStandalonePids_.insert(pid, currentCreatedAt).second;
```

⇒ 同实例 ⇒ `firstDiscard=false` ⇒ DEBUG（去重，不刷屏）；新实例 ⇒ `erase` 后 `insert` 成功 ⇒ `firstDiscard=true` ⇒ **WARN 重新出现**。

**删除** v1.0 的「按 pid 集合做差集清理」可选项（枚举发生在循环体内，函数开始处拿不到本轮 pid 集合）；改为登记「集合随运行时长缓慢增长，未做容量治理」（§5 R12）。

同步修改 discard 日志文案，区分「链全空」与「被活体事实证伪」两种原因。

### D-C：活体扫描携带读取状态（逐族）

**`src/ProcessInspector.h` / `src/ProcessInspector.cpp`** 新增（**类内嵌套**，裁决层级）：

```cpp
class ProcessInspector {
public:
    // Result of a live TCP scan of one pid.
    //   readOk - the OS TCP table could be enumerated (at least one of the
    //             IPv4 / IPv6 families was read successfully). When false the
    //             scan carries NO evidence and nothing can be disproven.
    //   ports  - every LISTEN port owned by the pid (may be empty even when
    //             readOk is true: the pid may simply not listen).
    struct TcpPortScan {
        bool readOk = false;
        std::vector<int> ports;
    };

    // Scans the OS TCP table for the pid and reports whether the table
    // could be read at all. Per address family: the probe only sizes the
    // buffer, the buffered call must return NO_ERROR for the family to
    // count as readable. A table that grew between the two calls
    // (ERROR_INSUFFICIENT_BUFFER) is retried once.
    static TcpPortScan scanListeningTcpPorts(DWORD pid);

    // Convenience wrapper kept for existing callers.
    static std::vector<int> listeningTcpPorts(DWORD pid);
};
```

**逐族规则**：
- 对每一族（AF_INET / AF_INET6）：探测 `GetExtendedTcpTable(nullptr,&size,...)` ⇒ 仅用于定尺；随后带缓冲调用 ⇒ 返回 `NO_ERROR` ⇒ 该族 `readOk = true`（**哪怕 0 条**）；返回 `ERROR_INSUFFICIENT_BUFFER`（表增长）⇒ **重试一次**（重新定尺再取表），仍失败 ⇒ 该族 `readOk = false`；其它错误 ⇒ 该族 `readOk = false`。
- 整体 `readOk = v4ReadOk || v6ReadOk`（**至少一族成功**即可读）⇒ 单族失败不会关闭整个 D2 闸门。
- `pid == 0` 早退 ⇒ `readOk = false`（无 pid ⇒ 无事实）。
- **该函数不打日志**（`test_process_inspector` target 不链 Logger，`CMakeLists.txt:1459-1460`；打日志会链接失败）。

**策略头签名变更（破坏性，不保留重载、不加默认参数）**：

```cpp
inline PortVerdict classifyPortCandidate(int candidate,
                                         const std::vector<int>& osPorts,
                                         const std::vector<int>& socksPorts,
                                         int excludedPort,
                                         bool osFactsReadable);
inline PortResolution resolveChainPort(
    const std::vector<PortCandidate>& candidates,
    const std::vector<int>& osPorts,
    const std::vector<int>& socksPorts,
    int excludedPort,
    bool osFactsReadable);
```

判定顺序（新增两条）：

```
candidate <= 0 || candidate > 65535           => Disproven
excludedPort > 0 && candidate == excludedPort  => Disproven
!osFactsReadable                               => Unknown   (表读不到 ⇒ 不可证伪 ⇒ 放行)
osFactsReadable && osPorts.empty()            => Disproven (表读到了 ⇒ 该 pid 确实没监听)
!containsPort(osPorts, candidate)             => Disproven
containsPort(socksPorts, candidate)           => Verified
否则                                            => Unverified
```

**作用域**：
- **纳管 FullDetect 路径**：传 `osFactsReadable = readOk` ⇒ 启用「空 ⇒ Disproven」。
- **R6 接管路径**：传 `osFactsReadable = readOk && !osPorts.empty()` ⇒ **等价旧语义**（空表 ⇒ 不可证伪 ⇒ 放行），避免「新进程尚未 bind」窗口把接管端口打成 0。

### D-A1 的策略头声明（可单测）

```cpp
// Tries to acquire the adoption in-flight flag.
// Returns true when this caller acquired it (and MUST release it exactly
// once), false when another sweep already holds it.
// Pure/unit-testable (the atomic lives on the caller's side).
inline bool tryBeginAdoption(std::atomic<bool>& flag) {
    return !flag.exchange(true);
}
```

---

## 3. 改动清单（Task 0-6）

### [ ] Task 0：策略头扩展 + 单测

1. `include/StandaloneAdoptionPolicy.h`：
   - `#include <atomic>`（新函数需要）。
   - `classifyPortCandidate` / `resolveChainPort` **破坏性**新增 `bool osFactsReadable` 参数（`resolveChainPort` 内部透传给 `classifyPortCandidate`）。
   - 新增 `tryBeginAdoption(std::atomic<bool>&)`（**带函数体**）。
   - 新增 `isSameProcessFingerprint(const std::string&, const std::string&)`（**带函数体**）。
   - 补 `#include <string>`（已有则不重复）。
2. **签名变更调用点清单（共 20 处，必须逐条改）**：
   - 生产 2：`src/ui/AppController.cpp:1339-1343`（R6）、`:1958-1959`（纳管）。
   - 头文件内部 1：`include/StandaloneAdoptionPolicy.h:183-184`（`resolveChainPort` 内部调 `classifyPortCandidate`）。
   - 测试 17：`tests/test_standalone_adoption_policy.cpp` 的 `:53,54,55,64,72,81,89,96`（classify）+ `:179,204,223,235,256,270,284,300,309`（resolveChain）。
     - `:72` / `:235` / `:270` 传 **`false`**（保持 Unknown 语义）；其余传 **`true`**。
3. 新增用例（**ASCII 名**，沿用该文件**逐名 using 声明**风格 `tests/test_standalone_adoption_policy.cpp:18-28`）：
   - `Classify_UnknownWhenFactsUnreadable`
   - `Classify_DisprovenWhenTableReadableAndPidHasNoListener`
   - `Classify_UnknownWhenFactsUnreadableAndOsPortsEmpty`
   - `ResolveChain_DisprovesAllWhenTableReadableAndPidHasNoListener`（断言 `port == 0` **且** `source == PortSource::None`）
   - `ResolveChain_PassesThroughWhenTableUnreadable`
   - `Instance_SameFingerprint_IsSame`
   - `Instance_DifferentFingerprint_IsNotSame`
   - `Instance_EmptyFingerprint_IsSame`
   - `AdoptionGate_AcquiresWhenFree` / `AdoptionGate_RejectsWhenHeld` / `AdoptionGate_ReleasesThenAcquires`

### [ ] Task 1：`scanListeningTcpPorts` + 测试

1. `include/ProcessInspector.h`：新增类内嵌套 `struct TcpPortScan { bool readOk = false; std::vector<int> ports; };` 与 `static TcpPortScan scanListeningTcpPorts(DWORD pid);`。
2. `src/ProcessInspector.cpp`：按 §2 D-C 的逐族规则实现；`listeningTcpPorts` 改为调用它并只返回 `.ports`。
3. `tests/test_process_inspector.cpp` 新增 3 例：
   - `ScanListeningTcpPorts_ReportsReadOkForLiveSocket`（复用既有 `openLoopbackListener()` 夹具 + `GetCurrentProcessId()` ⇒ `readOk == true` 且 ports 含该端口）
   - `ScanListeningTcpPorts_ReportsReadOkAndEmptyForUnusedPid`（用 `0xFFFFFFFC` ⇒ `readOk == true` 且 ports 空）
   - `ScanListeningTcpPorts_ReportsNotReadableForZeroPid`（`pid == 0` ⇒ `readOk == false`）

### [ ] Task 2：纳管并发加固（D-A1 / D-A2 / D-A3）

1. `src/ui/AppController.h`：新增 `std::atomic<bool> adoptInFlight_{false};`；`discardedStandalonePids_` 改 map（见 Task 3）；`proxyWatchKeys_` 补「Guarded by standaloneMutex_」注释。
2. `src/ui/AppController.cpp` 纳管函数入口：加 `tryBeginAdoption` + `ScopeGuard<std::atomic<bool>>`。
3. **写侧复检**：把「是否已被登记」判定下移到 `watch()` 之前的临界区；`watch()` 移入该临界区。
4. `proxyWatchKeys_` 四处访问统一加锁（`:1192` 启动路径、`:1375` R6、`:204` clear、`:1439` 读）。

### [ ] Task 3：丢弃记录携带指纹（D-B）

按 §2 D-B 改两处丢弃点 + 改 map 类型 + 同步 discard 日志文案（区分「链全空」与「被活体事实证伪」）。

### [ ] Task 4：纳管 / R6 接入 `readOk`（D-C）

1. 纳管路径：`ProcessInspector::TcpPortScan scan = ProcessInspector::scanListeningTcpPorts(pid);` ⇒ `osPorts = scan.ports`、`osFactsReadable = scan.readOk` ⇒ `resolveChainPort(..., osFactsReadable)`。
2. R6 路径：`resolveChainPort(..., scan.readOk && !scan.ports.empty())`（保旧语义）。

### [ ] Task 5：构建与测试

### [ ] Task 6：文档登记

- `docs/INDEX.md` §9.1 bugfix 表**新增编号 101**（当前末号 100，`docs/INDEX.md:488`）；行格式 `| 101 | [docs/bugfix/<file>.md](./bugfix/<file>.md) | **<中文摘要>** — <细节> | <状态> | — |`；状态 `✅ 已完成`。
- `docs/plans/project-plans-tracker.md`「近期文档引用」表**表尾追加一行**，**5 列**（末列为状态：`✅ completed`；表头 `docs/plans/project-plans-tracker.md:18` 缺第 5 列，可顺手补 `| 状态 |`）。

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

- 定向 4 个 ctest 目标**全部通过**。
- 全量 ctest **期望 54/56**，失败项恰为 `XrayManagerStartupTest`（既有基线，需真实就绪 xray）+ `UI_FLOATINGWIDGET`（已知并行 flaky，`WindowFromPoint` z-order 竞态）。
- `auto` 红线检查（**覆盖已暂存改动**）：`git diff HEAD -U0 | Select-String '^\+.*\bauto\b'` ⇒ 0；未跟踪新文件须单独 grep。
- 本次**不新增测试文件**（改既有 `test_standalone_adoption_policy.cpp` / `test_process_inspector.cpp`）⇒ **无需改 `CMakeLists.txt`**；ctest 名沿用 `StandaloneAdoptionPolicyTest` / `ProcessInspectorTest`。

### 4.2 实机验收

> **前置**：验收会写生产库 `bin/worker/guiNDB.db`（`bin/config.json` 的 `database.path`）⇒ **先备份**。切库路径**没有** `VALIDPROXY_NO_ADOPT` 门控（`src/ui/AppController.cpp:208-210`）⇒ 不要在 UI 自动化测试运行时切库。

| 步 | 操作 | 判据（正向、可判定） |
| :--- | :--- | :--- |
| 0 | 备份 `bin/worker/guiNDB.db`；临时把 `bin/config.json` 的 `log.file_level` 改为 **`DEBUG`**（**不是 REPORT**：`TRACE=0<DEBUG=1<INFO=2<REPORT=3<WARN=4<ERR=5`，过滤条件是 `level >= fileLevel_`，`REPORT` 比 `DEBUG` **更严格**，会把 DEBUG 滤掉；且 GUI 模式已 `Logger::setConsoleEnabled(false)`，`src/main_gui.cpp:91`） | 验收后改回 `WARN` |
| 1 | 启动 `bin/validproxy.exe`，手动改数据库路径触发切库 | 日志中出现**恰好 1 次**「Adoption scan already in progress」（证明 in-flight 命中）；此后连续 3 个 30s 周期**不应**再出现（证明未误杀正常周期） |
| 2 | 制造一个**活着但无 LISTEN 端口**的 `standalone_<indexId>-xray.json` 进程（例如配置 inbound 端口已被占用导致 bind 失败） | 日志出现 **WARN** 级 discard（不是 DEBUG）⇒ D-B 的「首次打 WARN」生效；随后 30s 内**不应**重复出现同 pid 的 WARN |
| 3 | 杀掉该进程并让 OS 复用同一 pid，再制造同样的「活着但无 LISTEN」进程 | 日志**再次出现 WARN**（不是 DEBUG）⇒ 指纹比对生效（pid 复用的实例被当作新实例） |
| 4 | 让一个**只监听 api、不监听 socks** 的 xray 被纳管 | 该进程被**丢弃**并打 WARN，且**不影响**本会话自启动代理的纳管（BypassDetect 路由不受 D-C 影响） |
| 5 | 外部重启一个独立代理，触发 R6 接管 | 接管后历史行 `socks_port` **仍等于配置文件端口**（不是 0）⇒ R6 未被 D-C 回归 |
| 6 | 查库：`SELECT COUNT(*) FROM proxy_runtime_history WHERE ended_at IS NULL AND pid = ?` | 同一 pid 的 in-progress 行**只有 1 条**（`start_count` 当前恒 0，**不可**作为判据） |

---

## 5. 风险与边界

| # | 风险 | 缓解 |
| :--- | :--- | :--- |
| R1 | in-flight 标记导致「跳过一轮」。若 `proxy_process_monitor.enabled` 与 `independent_probe.enabled` **均为 false**，定时器不启动 ⇒ 被跳过的切库纳管**本会话内不会补做**，进程直到应用重启才被纳管 | 生产配置两者均 true（`bin/config.json`）⇒ 成立；关闭监控的用户不成立 ⇒ 该情形登记为已知限制 |
| R2 | 守卫未复位导致纳管永久停摆 | 用 `ScopeGuard` 覆盖全部退出路径；当前函数无 `return`，守卫是为未来早退路径的健壮性 |
| R3 | D-C 落地后「所有代理被丢弃」 | 措辞限定：**所有外来悬垂**代理在「表可读且该 pid 无监听」时被丢弃；**本会话自启动的代理走 BypassDetect 路由**（`include/StandaloneAdoptionPolicy.h:313-322`、`src/ui/AppController.cpp:1943-1956`）**不受影响** |
| R4 | 破坏性签名变更漏改 | Task 0 给出全部 20 处清单；`rg` 复核 |
| R5 | D-A 的写侧复检可能把合法纳管「挤掉」 | 复检只在「已被登记」时放弃，且被登记者已由其它路径接管 ⇒ 无覆盖损失 |
| R6 | 启动 / R6 路径未共享 in-flight 标记 | 由**写侧复检**统一覆盖（纳管在注册 watcher 前会复查） |
| R7 | 生产库存量脏数据（128 条孤儿行、11 组重复三元组）使绝对值判据必然失败 | §4.2 步骤 6 判「同一 pid 的 in-progress 行数」，不判绝对值；存量治理为**非目标** |
| R8 | 旧 watcher 的 `finalizeStop` 回调用成员 `db_` 把旧库行终结到新库 | 既有缺陷，与本次正交；§4.2 步骤 1 可能实测到它 ⇒ 不要误判为本次回归 |
| R9 | 外部拉起的 xray 在 bind 之前被纳管扫描命中 ⇒ 首次被误判丢弃 | 丢弃分支**故意不关闭历史行** ⇒ 30s 后自愈；本会话自启动的代理不受影响 |
| R10 | 创建时间指纹为**秒级** ⇒ 同秒内 pid 复用不可区分 | 登记为已知限制；可选改进：新增 `processCreationTimeTicks(HANDLE)` 返回 FILETIME tick |
| R11 | `readOk == false` 分支在本机无法稳定制造 | 由策略层 `Classify_UnknownWhenFactsUnreadable` 用例间接覆盖；不引入新的测试接缝 |
| R12 | `discardedStandalonePids_` map 随运行时长缓慢增长 | 未做容量治理；条目数上界 = 曾被丢弃过的不同 pid 数，实际极小 |

### 非目标

| # | 非目标 |
| :--- | :--- |
| N1 | 生产库 128 条孤儿行与 `start_count` 恒 0 的数据治理 |
| N2 | `switchDatabase` 的整体状态一致性重构（旧 watcher 跨库 `finalizeStop`、`sqlite3_close(oldDb)` 与 `db_` 无同步改写 ⇒ UAF 风险） |
| N3 | TCP 表快照缓存（`utils` 侧已有 250ms 快照机制，`ProcessInspector` 未复用） |
| N4 | 纳管循环的枚举结构重构（当前枚举在循环体内，无法在函数开始处拿到本轮 pid 集合） |
| N5 | 秒级指纹升级为 FILETIME tick |

---

## 6. 文档登记

- `docs/INDEX.md` §9.1 bugfix 表新增编号 **101**。
- `docs/plans/project-plans-tracker.md`「近期文档引用」表尾追加 1 行（5 列，末列为状态）。
- 本文件末尾追加「实施记录」附录（改动表 / 验证结果 / 剩余残留）。

---

## 附录：实施记录（v1.1，已落地）

### A.1 代码改动

| # | 文件 | 改动 |
| :--- | :--- | :--- |
| 1 | `include/StandaloneAdoptionPolicy.h` | 新增 `#include <atomic>`；`classifyPortCandidate` / `resolveChainPort` **破坏性**新增 `bool osFactsReadable` 参数（`resolveChainPort` 内部透传）；新增 `tryBeginAdoption(std::atomic<bool>&)`（带函数体）；新增 `isSameProcessFingerprint(const std::string&, const std::string&)`（带函数体，注释写明与 `checkInstanceIdentity` 的空串约定相反） |
| 2 | `include/ProcessInspector.h` | 新增类内嵌套 `struct TcpPortScan { bool readOk = false; std::vector<int> ports; };` 与 `static TcpPortScan scanListeningTcpPorts(DWORD pid);` |
| 3 | `src/ProcessInspector.cpp` | 新增匿名 namespace 内 `TcpFamilyRead` / `readTcpTableFamily()`（探测仅定尺、带缓冲调用须 `NO_ERROR`；`ERROR_INSUFFICIENT_BUFFER` 重试一次）；`scanListeningTcpPorts()` **逐族**记录可读性，整体 `readOk = v4ReadOk \|\| v6ReadOk`；`listeningTcpPorts()` 改为包装；`pid == 0` ⇒ `readOk=false`；**不打日志**（`test_process_inspector` 不链 Logger） |
| 4 | `src/ui/AppController.h` | `discardedStandalonePids_` 由 `unordered_set<DWORD>` 改为 `unordered_map<DWORD, std::string>`（pid → 创建时间指纹）；新增 `std::atomic<bool> adoptInFlight_{false}`；`proxyWatchKeys_` 补「Guarded by standaloneMutex_」注释 |
| 5 | `src/ui/AppController.cpp` | 纳管函数入口加 `tryBeginAdoption` + `ScopeGuard<std::atomic<bool>>`（复用既有 `ui::ScopeGuard`，不新增守卫类）；**写侧复检**（OpenProcess 之后、写历史行之前，在锁内复查「已被登记」则 `CloseHandle` + 放弃纳管）；`proxyWatchKeys_.clear()` 移入 `switchDatabase()` 的锁域；R6 路径 `proxyWatchKeys_[indexId]` 写入改为「先取 watch key、再在锁内赋值」；两处丢弃点改用指纹比对（`find` → 比对 → `erase` → `insert_or_assign().second`）；discard 日志文案区分「表可读但无监听」与「无活体事实」 |
| 6 | `src/ui/AppController.cpp`（活体取证） | 纳管 FullDetect 路径改用 `scanListeningTcpPorts()`，`osFactsReadable = scan.readOk` ⇒ 启用「空 ⇒ Disproven」；R6 路径传 `readOk && !ports.empty()` ⇒ **保旧语义**（新进程尚未 bind 时不证伪配置端口） |
| 7 | `tests/test_standalone_adoption_policy.cpp` | 17 处旧调用点补第 5 参（`:72/:235/:270` 传 `false`，其余 `true`）；新增 11 例：`Classify_UnknownWhenFactsUnreadable` / `Classify_DisprovenWhenTableReadableAndPidHasNoListener` / `Classify_UnknownWhenFactsUnreadableAndOsPortsEmpty` / `ResolveChain_DisprovesAllWhenTableReadableAndPidHasNoListener` / `ResolveChain_PassesThroughWhenTableUnreadable` / `Instance_SameFingerprint_IsSame` / `Instance_DifferentFingerprint_IsNotSame` / `Instance_EmptyFingerprint_IsSame` / `AdoptionGate_AcquiresWhenFree` / `AdoptionGate_RejectsWhenHeld` / `AdoptionGate_ReleasesThenAcquires`；补 `#include <atomic>` |
| 8 | `tests/test_process_inspector.cpp` | 新增 3 例：`ScanListeningTcpPorts_ReportsReadOkForLiveSocket` / `ScanListeningTcpPorts_ReportsReadOkAndEmptyForUnusedPid`（pid `0xFFFFFFFC`）/ `ScanListeningTcpPorts_ReportsNotReadableForZeroPid` |

> 注：`unordered_map` 没有两参 `insert`，实现用 `insert_or_assign(pid, fingerprint).second` 判定「首次」（`operator[]` 会插入，**不可**用于判定首次）。

### A.2 验证结果

| 项 | 结果 |
| :--- | :--- |
| `cmake --build build --parallel 8` | **exit=0**，0 error |
| `ctest -R "StandaloneAdoptionPolicyTest\|RuntimeHistoryDAOTest\|ProcessInspectorTest\|UtilsTest"`（在 `build/`） | **4/4 全绿**（Policy 41 / RuntimeHistoryDAO 45 / ProcessInspector 14 / Utils） |
| 全量 `ctest` | **54/56**（96%）；失败项恰为 `XrayManagerStartupTest`（既有基线，需真实就绪 xray）+ `UI_FLOATINGWIDGET`（已知并行 flaky） |
| `auto` 红线 | `git diff HEAD -U0` 新增行 = **0**；未跟踪新文件单独 grep = **0** |
| 文档登记 | `docs/INDEX.md` §9.1 新增编号 **101**；`docs/plans/project-plans-tracker.md` 表尾追加 1 行（5 列，末列状态 `✅ completed`） |

### A.3 剩余残留（本次范围外）

- 生产库 `proxy_runtime_history` **128 条 `ended_at IS NULL` 孤儿行** + 重复三元组 11 组（其中 `ended_at IS NULL` 内仅 2 组）⇒ 数据治理为独立缺陷。
- `start_count` 实际恒 0（`ProfileExItemDAO` 的 `INSERT OR REPLACE` 归零机制掩盖了 `UPDATE ... start_count+1`）⇒ 不能作验收信号。
- 旧 watcher 的 `finalizeStop` 回调在切库后会把旧库行终结到新库（`db_` 无同步改写）⇒ 数据竞争/UAF 风险，属独立缺陷。
- 创建时间指纹为**秒级** ⇒ 同秒内 pid 复用不可区分（可选改进：`processCreationTimeTicks(HANDLE)` 返回 FILETIME tick）。
- `discardedStandalonePids_` 随运行时长缓慢增长，未做容量治理。