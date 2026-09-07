updated: 2026-08-31
title: "bugfix: isPortAvailable 通配符端口误判为空闲 (wildcard false-free)"
type: bugfix
status: completed
---

# Bugfix: isPortAvailable 通配符端口误判为空闲 (wildcard false-free)

- 日期: 2026-08-31
- 模块: `src/Utils.cpp::isPortAvailable` (+ 调用方 `src/PortManager.cpp::isInUseUnlocked`)
- 严重度: 高（多 Xray 实例可能共享同一 SOCKS 入站端口，引发启动竞态 / "failed to listen"）
- 关联现象: 代理池启动 `failed to listen*`、空闲端口被重复分配给多个实例

## 一、根因 (Root Cause)

`utils::isPortAvailable(port)` 原实现用 `bind()` 探测**仅绑定 127.0.0.1（IPv4）**：

```cpp
sockaddr_in addr{};
addr.sin_family = AF_INET;
addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // 127.0.0.1
addr.sin_port = htons(static_cast<u_short>(port));
bool available = (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
```

真实服务（xray SOCKS5 入站）以 `listen()` 监听 **通配符地址 0.0.0.0 / [::]**，
而不是 127.0.0.1。

**Windows 套接字语义**：在同一端口上，对「特定地址（127.0.0.1）」的 `bind` **允许与**
已存在的「通配符（0.0.0.0）监听」**共存**——通配符监听接受发往任意本地地址的连接，而
特定地址 bind 仅占用 127.0.0.1 这一具体端点，二者不冲突。于是当某端口已被 xray 以
`0.0.0.0` 占用时，探测 bind 到 `127.0.0.1` 仍成功 → `isPortAvailable` 错误地返回 `true`
（假「空闲」）。

**现场证据**：`netstat -ano | findstr :10808` 显示 xray PID 13920 同时持有
`0.0.0.0:10808` 与 `[::]:10808`，而 `isPortAvailable(10808)` 返回 free。

此外，仅探测 IPv4 也会漏报以 `[::]`（IPv6 通配符）监听的端口。

## 二、首稿尝试：通配符 bind —— 已被实机证伪（重要教训）

最初按“把探测地址从 127.0.0.1 改成通配符 0.0.0.0/[::]”实施了**首稿**：

- 新增 `probeWildcardTcp(family, port)`：`AF_INET` bind `0.0.0.0`、`AF_INET6` bind `[::]`
  + `IPV6_V6ONLY=1`，任一地址族 `WSAEAFNOSUPPORT` 视为非阻塞返回 `true`。
- `isPortAvailable = probeWildcardTcp(AF_INET,port) && probeWildcardTcp(AF_INET6,port)`。
- 目标是让占据 `0.0.0.0:10808` 的 xray 使探针 bind 失败 → 判为占用。

**实机证伪**（`temp/probe_*.exe` 对 PID 13920 持有 `0.0.0.0:10808` 的实况探测）：
无论**普通 bind** 还是 **`SO_EXCLUSIVEADDRUSE` 独占 bind**，在 `0.0.0.0:10808` 上
**都成功返回了 true（即可用）**。结论：Windows 上一个「通配符地址的 bind」被允许与
已存在的「通配符或特定地址的监听」**共存**，`bind()` 根本探测不到跨进程的通配符监听。
首稿的单测之所以“通过”，是因为单测在**同一进程内** bind+listen，触发了**进程内**冲突；
而目标场景（xray 是**另一进程**）不冲突——即 **单测是假阳性（false pass）**。
首稿方案因此**被整体废弃**。

## 三、终版修复 (Final Fix)：GetExtendedTcpTable 枚举系统 TCP 表

放弃 `bind()`，改为**枚举系统 TCP 端点表**（`netstat` 同源的权威数据）：

- 新增静态辅助 `static bool isPortOccupiedInTable(int port)`：
  - 依次对 `AF_INET`（`MIB_TCPTABLE_OWNER_PID`）与 `AF_INET6`
    （`MIB_TCP6TABLE_OWNER_PID`）调用 `GetExtendedTcpTable(..., TCP_TABLE_OWNER_PID_ALL)`：
    先以 `nullptr` 查得所需缓冲区大小（`ERROR_INSUFFICIENT_BUFFER`），再分配
    `std::vector<unsigned char>` 并取回全表。
  - 遍历 `dwNumEntries`，若任一表项 `ntohs(dwLocalPort)==port` 且
    `dwState ∈ {MIB_TCP_STATE_LISTEN, MIB_TCP_STATE_TIME_WAIT}` → 判为已占用。
- `isPortAvailable(port)` 返回 `!isPortOccupiedInTable(port)`（保留静态 `WSAStartup`）。
- **LISTEN** 捕获真实服务（如 xray 的 `0.0.0.0` / `[::]` SOCKS 入站，即本次 10808
  通配符假阴性）；**TIME_WAIT** 捕获刚关掉的套接字（如被 kill 的 xray outbound，
  即历史 10810 情形）——**两处历史痼疾一次覆盖**。
- 新增 `#include <iphlpapi.h>` + `#include <vector>`；`CMakeLists.txt` 在 `project()`
  后新增 `link_libraries(iphlpapi)`（所有编译 `src/Utils.cpp` 的目标均链接 iphlpapi.lib）。

`findAvailablePort` 与 `PortManager::isInUseUnlocked`（`!isPortAvailable(port)`）
**无需改动逻辑**，自动受益；`PortManager.cpp` 中过时的「via connect()」注释已同步更新。

## 四、验证 (Verification, TDD + 实机)

1. **RED**：在 `tests/test_utils.cpp` 新增 3 个回归测试（先写、构建、运行确认失败）：
   - `PortCheckTest.OccupiedOnWildcardReportedUnavailable`：bind `0.0.0.0:19878` → 期望 `isPortAvailable(19878)==false`（旧代码返回 true → FAIL）
   - `PortCheckTest.OccupiedOnIpv6ReportedUnavailable`：bind `[::]:19879` → 期望 `false`（无 IPv6 时 GTEST_SKIP）
   - `PortCheckTest.FindAvailablePortSkipsWildcardOccupied`：bind `0.0.0.0:19880` → 期望 `findAvailablePort(19880, 10) != 19880`
   - 修复前运行结果：`[ FAILED ] 3 tests`。
2. **GREEN**：实施终版修复后运行 `.\tests\test_utils.exe --gtest_filter=PortCheckTest.*`
   → **6/6 PASS**（含上述 3 个新用例 + 既有 3 个 PortCheck 用例）。
3. **实机跨进程探测**（关键，首稿在此被证伪）：对 PID 13920 持有 `0.0.0.0:10808`
   LISTENING 的实况运行 `temp/probe_table.exe`（`isPortOccupiedInTable` 的独立自拷贝）：
   ```
   port 10808 occupied=1 (available=0)   <- CORRECT：跨进程通配符监听被识别
   port 10810 occupied=0 (available=1)   <- 正确（当前无 TIME_WAIT）
   port 65000 occupied=0 (available=1)   <- 正确
   ```
   旧 `bind()` 方案在此返回 `available=1`（误判），终版正确返回 `0`。
4. **部署**：`bin/worker/validproxy.exe` 原为 8/24 旧二进制（AGENTS §4.1 禁止构建期自动
   拷贝），已手动用 `bin/validproxy.exe`（11:48:34 终版）覆盖，确保 GUI 运行时走新逻辑。
5. **回归**：`PortManagerTest` 6/6 PASS；`test_standalone_proxy_pool`、
   `test_subscription_parser` 待全量复跑确认。

## 五、影响范围与边界

- 行为变化：`isPortAvailable` 现在对「通配符监听」（含其它进程/其它 xray 实例）正确返回
  `false`，`PortManager` 分配端口时会跳过该端口，消除多实例同端口冲突；同时保留
  TIME_WAIT 判定（历史 10810 修复的延续）。
- 与 `bind()` 方案的本质差异：读 OS 权威 TCP 表而非依赖套接字 bind 冲突判定，不受
  “同端口特定/通配符地址共存”这一 Windows 语义影响。
- 未引入新的公共 API，未改动 `findAvailablePort` 签名/语义。
- 已知限制：依赖 Windows `GetExtendedTcpTable`（iphlpapi），仅限 Windows 目标平台
  （本项目即 wxMSW/MinGW，无跨平台负担）；LISTEN 之外处于其他状态（如 ESTABLISHED）的
  端点不判占用。
