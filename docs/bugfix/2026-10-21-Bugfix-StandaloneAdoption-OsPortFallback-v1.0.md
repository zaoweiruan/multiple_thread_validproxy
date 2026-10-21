# 纳管独立代理进程端口回落：OS TCP 表回落层缺失导致存活进程被丢弃

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:systematic-debugging to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复「两个 xray 进程都在运行，却只监控其中一个」，并让纳管得到的端口是**进程实际在监听**的 SOCKS 端口（而非可能陈旧的历史/文件值），使周期探活不再对错误端口反复失败。

**Tech Stack:** C++17 / Win32 `GetExtendedTcpTable` + SOCKS5 握手探测 / GoogleTest / CMake + Ninja（MinGW GCC）

**依据文档：** `docs/specs/2026-10-20-Spec-StandaloneAdoptionPortFallback-v1.3.md`（v1.3 把「OS 层回落」标记为**延后**，本次补齐）

**约束：** 全栈禁止使用 `auto` 进行类型推导（AGENTS.md §1）；测试源码只允许放在 `tests/`。

---

## 0. 现场证据

| 项 | 值 |
| :--- | :--- |
| 进程 A | PID **13492**，indexId `5046338036629160457`，创建于 `2026-10-21 08:39:08`，监听 `127.0.0.1:10808` |
| 进程 B | PID **12776**，indexId `4949226207915085546`，创建于 `2026-10-21 08:39:14`，监听 `127.0.0.1:10809` |
| 配置文件 | `bin\worker\config\standalone_<indexId>-xray.json` **两个都不存在**（已被删除） |
| 历史表（`bin\worker\guiNDB.db`） | 行 316 = pid 13492：`socks_port=NULL`、`ended_at=2026-10-21 09:04:03`；行 317 = pid 12776：`socks_port=10810`、`ended_at=NULL` |
| 日志 | `bin\log\ui_20261021_090403.log` 唯一一行：`[WARN] [StandaloneProxy] Discarding dangling process pid=13492 indexId=5046338036629160457: config missing and no runtime-history port.` |

> 注：`bin\config.json` 的 `log.file_level = "WARN"`，所以 `REPORT` 级的「Adopted dangling process」行**不会落盘**；日志里只看到 WARN 的丢弃行是预期行为，不代表 12776 未被纳管。

## 1. 根因（RCA）

### 1.1 为什么 13492 不再被监控

`adoptDanglingStandaloneProxies()` 的端口解析只有三层：命令行配置文件 → 目录探测 → `proxy_runtime_history.socks_port`；三层全失败即**丢弃**该进程（v1.3 的 D5 决策）。

对 13492：配置文件已删（(a)(b) 失败）；历史行 316 的 `socks_port` 为 **NULL**（该行由**升级前**的二进制创建，旧版从不写 `socks_port` 列）⇒ (c) 失败 ⇒ **被丢弃**。

⇒ **运行时历史层无法覆盖升级前启动的进程**，而 v1.0 设计的 **OS TCP 表回落层被 v1.2/v1.3 延后**，于是「配置文件缺失 + 历史无端口」＝**丢弃一个健康存活进程**。这正是用户原始需求「回落到直接读取 xray 的 pid、监听端口等数据」尚未落地的那一层。

丢弃分支同时调用 `finalizeStop(...)` 把历史行 316 关闭（`ended_at=09:04:03`、`exit_code=0`、`duration_ms=1495000`＝`08:39:08→09:04:03` 精确吻合），使该 pid 后续连历史端口也查不到（`lookupSocksPort` 要求 `ended_at IS NULL`）。

### 1.2 周期探活为什么失败（用户的第二个问题）

**周期连通性测试不修改独立代理历史表。** `AppController::doTestOnlineProxies()`（`src/ui/AppController.cpp:2382-2430`）只调用 `exDao_.updateTestResult(indexId, latency, success, errorMsg)`，写入的是 `ProfileExItem`（delay / message / `consecutive_failures`），**从不触碰 `proxy_runtime_history`**。该表的写入点只有 4 个：`insertStart`（启动/纳管/接管）、`updateSocksPort`（端口登记）、`finalizeStop`（会话收尾）、`touchHeartbeat`（时长心跳）。13492 那行的关闭来自**纳管丢弃分支**，与探活失败无关。

但探活**确实一直失败**，原因是另一个缺陷：12776 的历史端口是 **10810**，而它实际监听 **10809**。10810 来自 `bin\config\` 目录下的**陈旧配置副本**（10810）与 `bin\worker\config\` 的实际配置（10809）不一致——这正是 2026-09-02 记录过的「同一 indexId 两套 config 目录导致端口漂移」缺陷。纳管采用历史值 10810 后，周期探活每 5s 打一次 10810 ⇒ 永远失败。

⇒ **历史层与文件层都可能给出陈旧端口；只有 OS TCP 表描述的是"活着的进程"**。因此 OS 层必须**优先**于文件层/历史层，而不是「最后兜底」。

## 2. 修复方案

### D1：新增 OS 回落层并置于**最高**优先级

端口解析新顺序（**owner 裁决：运行时历史提到配置文件之前**）：

1. **OS TCP 表**（`GetExtendedTcpTable` 按 `dwOwningPid` 过滤 `LISTEN`）+ SOCKS5 握手确认 → 活进程的真实 SOCKS 端口；
2. **运行时历史**（`proxy_runtime_history.socks_port`，按 `(indexId, pid, startedAt)` 匹配）—— 该行绑定的是**这一进程实例**，记录的就是它启动时用的端口，不会被后来编辑过的配置文件带偏，因此优先级高于文件层；
3. 命令行 `-c` 配置文件端口——文件是**现在**读的，可能已描述了另一个端口；
4. `<exeDir>\config\` / `<exeDir>\worker\config\` 目录探测端口——最不可靠：可能命中另一个目录里的陈旧副本（2026-09-02 端口漂移缺陷）；
5. 全部失败 → 丢弃（此时进程确实没有可识别的 SOCKS 监听端口，丢弃是合理的）。

### D2：SOCKS5 握手作为多监听端口的判别器

xray/sing-box 独立代理除 SOCKS 入站外还可能监听 gRPC api 端口（`api_base_port`，默认 10085）。对每个候选端口发 `05 01 00`，收到 `05` 才认定为 SOCKS 入站；api 端口不会应答 `05`，从而被自然排除。**没有候选端口应答时，OS 层返回 0**，继续走文件/历史层（宁可用声明端口，也不用错误端口）。

### D3：纯策略头扩展（保持可单测）

`include/StandaloneAdoptionPolicy.h`：

- `enum class PortSource { None, OsTcpTable, CmdlineConfig, DirProbe, RuntimeHistory }`；
- `resolvePort(osPort, historyPort, cmdlinePort, dirProbePort)`：首个 >0 优先（OS > 运行时历史 > 命令行配置 > 目录探测；owner 裁决把运行时历史提到文件层之前）；
- `selectOsListeningPort(candidates, socksAnswering, excludedPort)`：纯函数，剔除非法/排除端口 → 取 `socksAnswering` 交集的最小值 → 否则 0；
- `portSourceLabel()` 增加 `"os-tcp-table"`。

## 3. 实施步骤

- [x] `include/ProcessInspector.h` + `src/ProcessInspector.cpp`：新增 `static std::vector<int> listeningTcpPorts(DWORD pid);`（v4+v6、仅 `MIB_TCP_STATE_LISTEN`、去重升序）；`ntohs` 需要 `<winsock2.h>`，必须置于 `<windows.h>` 之前
- [x] `include/Utils.h` + `src/Utils.cpp`：新增 `bool respondsToSocks5Greeting(int port, int timeoutMs = 200);`（回环连接 + `05 01 00` → 期望 `05`；一次性 `WSAStartup`）
- [x] `include/StandaloneAdoptionPolicy.h`：按 D3 扩展
- [x] `src/ui/AppController.cpp`：纳管流程插入 OS 层（先于历史/命令行/目录层），并把 `resolvePort` 参数顺序改为 OS > 历史 > 命令行 > 目录探测；日志 `source=` 标签随之更新
- [x] `tests/test_standalone_adoption_policy.cpp`：更新既有 6 例签名 + 新增 OS 层优先级、`selectOsListeningPort` 规则用例
- [x] `tests/test_process_inspector.cpp`：新增「本进程监听端口可被查出」用例
- [x] `tests/test_utils.cpp`：新增 SOCKS5 握手判定用例（真实监听 socket）
- [x] `CMakeLists.txt`：`test_process_inspector` 补 `-lws2_32`
- [x] 构建 0 error + `ctest -R "RuntimeHistoryDAOTest|StandaloneAdoptionPolicyTest|ProcessInspectorTest|UtilsTest" -V` + 全量 `ctest`
- [x] 登记 `docs/INDEX.md` §9.1（第 100 行）与 `docs/plans/project-plans-tracker.md` 表尾

## 4. 风险

| 编号 | 风险 | 缓解 |
| :-- | :-- | :-- |
| R1 | SOCKS 握手探测在纳管循环内产生 I/O（每候选 200ms 上界） | 纳管每 `checkIntervalMs`（默认 30s）执行一次，候选通常 1 个；超时上界 200ms |
| R2 | 入站协议非 socks/mixed（如纯 http）时握手不响应 | OS 层返回 0，行为退回文件/历史层，不产生错误端口 |
| R3 | 进程同时监听 SOCKS 与 api 端口 | 由 D2 握手判别器区分；api 端口被排除 |
| R4 | 端口探测期间进程退出 | 握手失败 → OS 层 0 → 走后续层；`OpenProcess` 失败则跳过 |

## 5. 验证

- 构建：`cmake --build build --parallel 8` → 0 error
- 单测：`RuntimeHistoryDAOTest`、`StandaloneAdoptionPolicyTest`、`ProcessInspectorTest`、`UtilsTest` 全绿
- 全量：`ctest`（既有基线失败 `XrayManagerStartupTest`；`UI_FLOATINGWIDGET` 为已知并行 flaky）
