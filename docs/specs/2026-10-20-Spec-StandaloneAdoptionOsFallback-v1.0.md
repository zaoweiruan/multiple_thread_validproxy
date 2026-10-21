# 纳管独立代理进程的 OS 级端口回退实施方案

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 当纳管（adopt）一个游离的独立代理进程（xray / sing-box）时，若磁盘上的配置文件不存在或不可读，则直接从操作系统 TCP 连接表读取该进程的 pid 与监听端口，使 `StandaloneProxyInfo.socksPort` 仍然有效。

**Architecture:** 在既有 `proc::ProcessInspector`（进程内省模块）中新增 `listeningTcpPorts(DWORD pid)`（OS 查询）与 `pickSocksListeningPort(...)`（纯函数候选筛选）；`AppController::adoptDanglingStandaloneProxies()` 在现有两步配置解析（命令行全路径 → 已知目录探测）都失败后，调用该回退路径，并保留空 `configPath`。

**Tech Stack:** C++17 / Windows API（`GetExtendedTcpTable`，AF_INET + AF_INET6）/ GoogleTest / CMake + Ninja（MinGW GCC）

**依据文档：** `docs/specs/2026-10-20-Spec-XrayApi-AdoPayloadNormalization-v1.0.md`（同目录风格基线）

**约束：** 全栈禁止使用 `auto` 进行类型推导（AGENTS.md §1）；测试源码只允许放在 `tests/`；`iphlpapi` 已由 `CMakeLists.txt` 顶层 `link_libraries(iphlpapi)` 全局链接，无需新增依赖。

---

## 0. 背景与目标

### 0.1 问题回顾

`src/ui/AppController.cpp` 的 `adoptDanglingStandaloneProxies()`（约 1545-1733 行）按以下顺序解析被纳管进程的 SOCKS 入站端口：

1. **Step (a)**：从命令行 `-c` 提取配置文件全路径（`ProcessInspector::extractConfigFullPath`），读取 `inbounds[].port` / `listen_port`。
2. **Step (b)**：回退探测 `<exeDir>\config\` 与 `<exeDir>\worker\config\` 下的同名配置。
3. 两者都失败时仅打印 `[StandaloneProxy] Could not resolve inbound port from ...` 的 WARN，随后仍以 `socksPort = 0` 纳管进程。

后果：进程已被纳管（生命周期监控、历史记录均正常），但端口为 0 ⇒ 监控对话框/日志无法显示端口，UI 也无法对该成员发起连通性测试；用户只能手工停止并重启该代理。

配置缺失的真实场景：用户手工删除/重命名 `config\standalone_<indexId>-xray.json`、从旧工作副本（`bin\worker`）启动后目录结构调整、配置文件被安全软件隔离等。此时进程仍在监听，OS 连接表是唯一可靠的事实来源。

### 0.2 目标

| 编号 | 目标 | 验收方式 |
| :--- | :--- | :--- |
| G1 | 配置不可用时，从 OS TCP 表按 pid 取得监听端口 | `ProcessInspector::listeningTcpPorts(pid)` 单测：本进程临时监听端口可被枚举到 |
| G2 | 从多个监听端口中选出 SOCKS 入站端口 | `ProcessInspector::pickSocksListeningPort` 纯函数单测覆盖空/单值/排除 API 端口/多候选 |
| G3 | 纳管流程接入回退且日志可区分来源 | 代码走查 + 日志文案 `resolved from OS TCP table (config file missing)` |
| G4 | 回退路径不改变 `configPath` 语义 | 回退成功时 `adoptedConfigPath` 保持为空，避免 UI 误认为配置存在 |

### 0.3 非目标（本次不做）

| 编号 | 非目标 | 归属 |
| :--- | :--- | :--- |
| N1 | 回退路径反写/重建配置文件 | 后续需求 |
| N2 | 通过 `GetExtendedTcpTable` 之外的手段（如 `netstat` 子进程）取证 | 不采用（避免 CMD 窗口闪烁，见 AGENTS.md §3.1） |
| N3 | sing-box 多入站（mixed/vless）场景下的精确协议识别 | 以 SOCKS5 握手探测为后续增强 |

---

## 1. 设计决策

### D1：端口查询放在 `ProcessInspector`，而非 `Utils`

`Utils.cpp` 已有 port→pid 方向的 `logPortOccupants`，但语义是「端口占用诊断」；pid→监听端口属于「进程内省」，与 R1 重复启动守卫、R6 自动接管同属 `ProcessInspector` 的职责边界。放在一起便于后续 R1/R6 复用。

### D2：新增纯函数承担候选筛选

OS 查询结果不可控（xray 同时监听 SOCKS 与 API 入站，sing-box 可能有多个入站），必须把「如何选」与「如何查」分离，前者才能无进程依赖地单测。

筛选规则（确定性、可解释）：

1. 丢弃 `<= 0` 或 `> 65535` 的非法值；
2. 丢弃等于 `primaryExcludedPort` / `secondaryExcludedPort`（>0 时生效）的候选——调用方传入配置中的 API 端口及其默认值 10085；
3. 若剩余为空 → 返回 0；
4. 若存在 `>= 1024` 的候选，仅在其中选择（排除系统保留端口噪声）；
5. 返回最小值。

已知局限：若剩余仍多于一个（例如 API 端口经过 `findAvailablePort` 偏移），可能选中错误端口。此情形在 `Logger` 中以 DEBUG 输出完整候选列表，便于定位；后续可用 SOCKS5 握手（`0x05 0x01 0x00` → 期望 `0x05`）精化，列在 N3。

### D3：字节序转换不引入 `<winsock2.h>`

`ProcessInspector.h` 公开包含 `<windows.h>`；在 `.cpp` 内再引入 `<winsock2.h>` 存在包含顺序风险。`dwLocalPort`（`MIB_TCPROW_OWNER_PID` / `MIB_TCP6ROW_OWNER_PID`）仅低 16 位有效且为网络序，故直接手工转换：

```cpp
static int networkPortToHost(DWORD rawPort) {
    DWORD low = rawPort & 0xFFFFu;
    return static_cast<int>(((low & 0xFFu) << 8) | ((low >> 8) & 0xFFu));
}
```

### D4：不改变纳管成功/失败判定

回退失败（进程无监听端口）时维持现状：仍纳管并 `socksPort = 0`，仅保留原有 WARN。新增行为只提升端口可解析率，不引入新的失败分支。

---

## 2. 改动清单

### Task 1：`include/ProcessInspector.h`

- [ ] 新增 `static std::vector<int> listeningTcpPorts(DWORD pid);`
- [ ] 新增 `static int pickSocksListeningPort(const std::vector<int>& candidates,
      int primaryExcludedPort = 0, int secondaryExcludedPort = 0);`
- [ ] 两者均补齐与文件既有风格一致的 `//` 文档注释（含 “Pure/unit-testable.” 标注）

### Task 2：`src/ProcessInspector.cpp`

- [ ] 匿名命名空间内新增 `networkPortToHost(DWORD)`；
- [ ] 实现 `listeningTcpPorts`：`GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0)` 取尺寸 → `std::vector<unsigned char>` 缓冲 → `PMIB_TCPTABLE_OWNER_PID`，对 `dwOwningPid == pid && dwState == MIB_TCP_STATE_LISTEN` 收集；AF_INET6 同理（`PMIB_TCP6TABLE_OWNER_PID`）；结果 `std::sort` + `std::unique`；
- [ ] 实现 `pickSocksListeningPort`（纯函数，见 D2）；
- [ ] 禁止 `auto`。

### Task 3：`src/ui/AppController.cpp`

- [ ] 在 Step (b) 之后、现有 WARN 之前插入 Step (c)：

```cpp
// Step (c): OS-level fallback — the config file may be missing or
// unreadable, but the process is still listening. Query the TCP table
// by pid so the adopted member keeps a usable SOCKS port.
if (adoptedPort <= 0) {
    const int apiPort = config_.proxy.api_base_port > 0
        ? config_.proxy.api_base_port : 10085;
    std::vector<int> listeningPorts =
        proc::ProcessInspector::listeningTcpPorts(procs[i].pid);
    adoptedPort = proc::ProcessInspector::pickSocksListeningPort(
        listeningPorts, apiPort, 10085);
    if (adoptedPort > 0) {
        Logger::write("[StandaloneProxy] Resolved SOCKS5 port from OS TCP "
                      "table (config file missing) pid="
                      + std::to_string(procs[i].pid) + " port="
                      + std::to_string(adoptedPort),
                      LogLevel::REPORT);
    } else {
        Logger::write("[StandaloneProxy] Could not resolve inbound port from "
                      + configFileName, LogLevel::WARN);
    }
}
```

- [ ] 删除原位置无条件 WARN（并入上块），确保两条消息互斥；
- [ ] `adoptedConfigPath` 在回退路径保持空字符串。

### Task 4：`tests/test_process_inspector.cpp`

- [ ] `PickSocksListeningPort.EmptyReturnsZero`
- [ ] `PickSocksListeningPort.SingleCandidate`
- [ ] `PickSocksListeningPort.ExcludesApiPort`
- [ ] `PickSocksListeningPort.ExcludesSecondaryApiPort`
- [ ] `PickSocksListeningPort.PrefersNonPrivileged`
- [ ] `PickSocksListeningPort.FiltersInvalidRange`
- [ ] `ListeningTcpPorts.FindsLoopedBackListener`（`WSAStartup` + `bind` 到 127.0.0.1:0 + `listen`，断言 pid 的枚举集合包含该端口；系统调用失败时 `GTEST_SKIP()`）

---

## 3. 验证

1. `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`
2. `cmake --build build --parallel 8`
3. `ctest -R ProcessInspector -V`
4. `ctest -V`（全量，既有失败基线 `XrayManagerStartupTest` 允许保留）
5. 代码走查：`rg -n "auto " src/ProcessInspector.cpp` 新增行零命中

## 4. 风险

| 编号 | 风险 | 缓解 |
| :--- | :--- | :--- |
| R1 | API 端口偏移导致选中错误端口 | 双排除 + DEBUG 候选列表；N3 后续握手探测 |
| R2 | `GetExtendedTcpTable` 在权限受限环境失败 | 返回空集合，行为退回现状（socksPort=0） |
| R3 | IPv6 与 IPv4 同时监听同端口 | 结果去重，不产生重复候选 |
