# Evidence — TIME_WAIT 不阻塞 Windows 上的重新绑定

> **文档类型**：Evidence（证据归档，非 Spec）
> **模块**：端口探测 / Windows TCP 语义
> **版本**：v1.0
> **日期**：2026-10-12
> **关联**：`docs/specs/2026-10-12-Spec-StandalonePortImmediateRebind-v1.0.md` §2.1 R1 / §11.2

---

## 1. 为什么需要这份归档

`src/Utils.cpp` 的 `isPortOccupiedInTable` 把 `MIB_TCP_STATE_TIME_WAIT` 与 `MIB_TCP_STATE_LISTEN` 同等判为「端口占用」，该判定（含其上方注释断言「lingering sockets from a just-killed connection must block a new listen()」）已写入生产代码约两年，经历了 bugfix #17 / #73 / #74 三轮改动而未被质疑。

本实验用最小可复现步骤证明：**在 Windows 上，仅存在 TIME_WAIT 端点时，同一端口可以立即重新 `bind()` 成功，且不需要 `SO_REUSEADDR`。**

这与 BSD/macOS 的行为相反，是把 Windows 语义与通用 TCP 教条混淆导致的缺陷。由于该结论直接推翻了生产注释，证据必须留在仓库内，而不是只存在于 `%TEMP%`。

---

## 2. 实验环境与前置测量

| 项 | 实测值 | 说明 |
| --- | --- | --- |
| 操作系统 | Windows（win32） | — |
| `TcpTimedWaitDelay` | `0x1e` = **30 秒** | `reg query "HKLM\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters" /v TcpTimedWaitDelay`；Windows 默认 120s，本机被调优为 30s |
| 实验端口 | **54323** | 一次性选定，避免与任何服务冲突 |
| 工具 | PowerShell + `System.Net.Sockets` | 脚本见 §3 |

`TcpTimedWaitDelay=30s` 意味着：若 TIME_WAIT 真的阻塞 bind，缺陷的窗口是 **30 秒**而非 2 分钟 —— 解释了为什么用户感知到的是「等很久但最终会好」。

---

## 3. 实验脚本（`tw5.ps1`，52 行，逐行归档）

```powershell
$ErrorActionPreference = 'Stop'
$PORT = 54323

function Snap([int]$p) {
  $t = Get-NetTCPConnection -LocalPort $p -ErrorAction SilentlyContinue
  if ($t) { $t | Select-Object LocalAddress,LocalPort,RemoteAddress,RemotePort,State | Format-Table -AutoSize | Out-String } else { '(no rows)' }
}
function TryBind([string]$addr, [int]$mode, [string]$label) {
  $ip = if ($addr -eq 'wild') { [System.Net.IPAddress]::Any } else { [System.Net.IPAddress]::Loopback }
  $s = [System.Net.Sockets.Socket]::new([System.Net.Sockets.AddressFamily]::InterNetwork, [System.Net.Sockets.SocketType]::Stream, [System.Net.Sockets.ProtocolType]::Tcp)
  try {
    if ($mode -eq 1) { $s.SetSocketOption([System.Net.Sockets.SocketOptionLevel]::Socket, [System.Net.Sockets.SocketOptionName]::ReuseAddress, $true) }
    if ($mode -eq 2) { $s.SetSocketOption([System.Net.Sockets.SocketOptionLevel]::Socket, [System.Net.Sockets.SocketOptionName]::ExclusiveAddressUse, $true) }
    $s.Bind((New-Object System.Net.IPEndPoint($ip, $PORT)))
    "  $label => BIND-OK"
    $s.Close()
  } catch {
    $e = $_.Exception.InnerException; if (-not $e) { $e = $_.Exception }
    "  $label => BIND-FAIL : " + $e.Message
  }
}

$server = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, $PORT)
$server.Start()
$client = [System.Net.Sockets.TcpClient]::new()
$client.Connect('127.0.0.1', $PORT)
$ns = $server.AcceptSocket()
Start-Sleep -Milliseconds 150
$buf = New-Object byte[] 4
$client.GetStream().Write([byte[]]@(1,2,3,4),0,4)
[void]$ns.Receive($buf)

# server = active closer -> server side must enter TIME_WAIT
$ns.Close()
Start-Sleep -Milliseconds 150
$client.Close()
# NOW destroy the LISTEN socket too; only TIME_WAIT rows must remain
$server.Stop()
Start-Sleep -Milliseconds 600

$snap = Snap $PORT
"snapshot after listener destroyed (only TIME_WAIT should remain):"
$snap
"TIME_WAIT present: " + [bool]($snap -match 'TimeWait') + " | LISTEN present: " + [bool]($snap -match 'Listen')

"bind probes with ONLY TIME_WAIT rows present:"
TryBind 'wild' 0 'wildcard / no flags'
TryBind 'loop' 0 'loopback / no flags'
TryBind 'wild' 1 'wildcard / SO_REUSEADDR only'
TryBind 'wild' 2 'wildcard / SO_EXCLUSIVEADDRUSE only'
TryBind 'loop' 1 'loopback / SO_REUSEADDR only'
"done"
```

**设计要点**（这是实验有效性的核心，缺一不可）：

1. **制造“真”TIME_WAIT**：必须让**服务端**作为 active closer 先 `Close` 已建立连接的 socket（`$ns.Close()`），TIME_WAIT 才会落在服务端持有 `$PORT` 的那一侧；若让客户端先关，TIME_WAIT 会落在客户端的临时端口上，与 `$PORT` 无关。
2. **先传数据再关**：`Write` + `Receive` 确保连接真正进入 ESTABLISHED，而非刚 accept 就被拆。
3. **务必销毁 LISTEN 套接字**：`$server.Stop()`。若只关连接而不停监听器，快照里会有 `Listen` 行 —— 那时任何探针的 FAIL 都来自 LISTEN，与 TIME_WAIT 无关（tw3/tw4 两次失败正是栽在这里）。
4. **600ms 等待**：让内核完成状态迁移，随后用 `Get-NetTCPConnection` 只读快照、断言快照里 **只有 TimeWait 行、没有 Listen 行**，才认为前置条件成立。



---

## 4. 原始输出（逐字归档）

```
snapshot after listener destroyed (only TIME_WAIT should remain):

LocalAddress LocalPort RemoteAddress RemotePort  State
127.0.0.1        54323 127.0.0.1          55584 TimeWait

TIME_WAIT present: True | LISTEN present: False
bind probes with ONLY TIME_WAIT rows present:
  wildcard / no flags                  => BIND-OK
  loopback / no flags                  => BIND-OK
  wildcard / SO_REUSEADDR only         => BIND-OK
  wildcard / SO_EXCLUSIVEADDRUSE only  => BIND-OK
  loopback / SO_REUSEADDR only         => BIND-OK
done
```

**前置条件已满足**（`TIME_WAIT present: True | LISTEN present: False`）⇒ 此后每一个 `BIND-OK` 都只能归因于「仅存在 TIME_WAIT 端点」这一状态，不存在 LISTEN 干扰。

---

## 5. 结论

### 5.1 主结论

**在 Windows 上，TIME_WAIT 端点不阻塞对同一端口的新 `bind()`**，且：

- **不需要 `SO_REUSEADDR`**（`wildcard / no flags` 与 `loopback / no flags` 均 BIND-OK）；
- 加 `SO_REUSEADDR` 或 `SO_EXCLUSIVEADDRUSE` 也不改变该结论；
- 通配地址（`IPAddress::Any`）与 loopback（`IPAddress::Loopback`）行为一致。

这与 BSD/macOS 的行为**相反**：在 BSD 上 TIME_WAIT 端点会让不带 `SO_REUSEADDR` 的 bind 返回 `EADDRINUSE`。把 BSD 教条套到 Windows 上，正是 `src/Utils.cpp` 那段错误注释的来源。

### 5.2 由此推导的缺陷判定

`src/Utils.cpp:200-201`（IPv4）/ `:226-227`（IPv6）的

```cpp
st == MIB_TCP_STATE_LISTEN || st == MIB_TCP_STATE_TIME_WAIT
```

**过严且不符合内核事实**：

- 它让 `isPortAvailable(port)` 在端口仅残留 TIME_WAIT 时返回 `false`，持续 `TcpTimedWaitDelay`（本机 30s）；
- 于是 `PortManager::findAvailable` / `StandaloneProxyPool::resolvePoolPorts` **每次调用都跳过刚释放的端口**并顺延到下一个；
- 用户看到的现象（「端口被占用、提示顺延、等很久」）是**策略性跳过**，不是内核阻塞 —— 内核从头到尾都允许立即重绑。

### 5.3 附带实测（对本 Spec 的 Q1「通配 vs 127」同样有结论价值）

| 已有端点状态 | bind `0.0.0.0:P` | bind `127.0.0.1:P` |
| --- | --- | --- |
| 存在 LISTEN（无标志） | **BIND-FAIL** | **BIND-FAIL** |
| 存在 LISTEN + `SO_REUSEADDR`（xray/Go 情形） | BIND-OK（漏报） | BIND-OK（漏报） |
| 存在 LISTEN + `SO_EXCLUSIVEADDRUSE` | BIND-FAIL（可识别） | 未单测（预期 FAIL） |
| **仅存在 TIME_WAIT** | **BIND-OK** | **BIND-OK** |
| 仅存在 LISTEN 且只绑 loopback | BIND-OK（通配探针漏报） | 未单测（预期 FAIL） |

- **`SO_REUSEADDR` 与 `SO_EXCLUSIVEADDRUSE` 同时设置 → bind 直接失败 `WSAEINVAL`（「提供了一个无效的参数」）**，二者互斥。
- 存在 `LISTEN + SO_REUSEADDR` 时通配与 loopback 的 bind **均成功** ⇒ 这正是 bugfix #73「改 bind 通配地址探测」方案被实机证伪的根因：xray/Go 的监听 socket 带 `SO_REUSEADDR`，bind 探针对它系统性漏报。
- ⇒ Windows 上 `bind()` / `connect()` 都不是可信的占用判定，**内核 TCP 端点表（`GetExtendedTcpTable`）才是**。

---

## 6. 实验迭代史（tw3 → tw4 → tw5，记录失败以免重犯）

| 版本 | 结果 | 教训 |
| --- | --- | --- |
| `tw3.ps1` | 两次运行错误：① `$ns.Read` 不存在（`Socket` 用 `Receive`）；② `New-Object System.Net.Sockets.Socket (...)` 参数解析失败 | 改用 `[System.Net.Sockets.Socket]::new(AddressFamily, SocketType, ProtocolType)` |
| `tw3.ps1`（修正后） | 跑通但**结果无效** ：探针执行时 `$server.Stop()` 尚未被调用，快照里 `Listen` 行仍在 | 「loopback 失败 / wildcard 成功」是 LISTEN 造成的，与 TIME_WAIT 无关。**探针前必须断言 LISTEN 已消失** |
| `tw4.ps1` | 成功制造出真正的 TIME_WAIT 行，但探针仍在 Listen 存活期间执行 | 同上；附带发现 `SO_REUSEADDR` + `SO_EXCLUSIVEADDRUSE` 互斥（`WSAEINVAL`） |
| `tw5.ps1` | **有效**：先 `$server.Stop()` 销毁监听器，再等 600ms、断言 `TIME_WAIT present: True \| LISTEN present: False`，然后才做探针 | 即本文件 §3 归档的脚本 |

**核心方法论**：任何「某状态是否阻塞 bind」的结论，都必须先**独立验证**该状态确实存在于内核表中、且其它候选状态确实不存在。否则测到的是别的状态的效果。

---

## 7. 归档位置与复现方式

- 脚本原位置：`C:\Users\dsm\AppData\Local\Temp\opencode\tw5.ps1`（52 行，2258 字节，`%TEMP%` 不随仓库分发）
- 本仓库归档：本文档 §3
- 复现：在 Windows PowerShell 中直接执行 §3 脚本；预期输出见 §4。若输出与 §4 不一致（例如出现 `BIND-FAIL`），先检查 `TIME_WAIT present / LISTEN present` 两个布尔值是否为 `True / False`。
