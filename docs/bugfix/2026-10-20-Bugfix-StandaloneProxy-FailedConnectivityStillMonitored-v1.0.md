# 2026-10-20 Bugfix — 独立代理连通性验证失败后仍纳入监控

> **类型**：Bugfix（控制流）
> **范围**：`src/ui/AppController.cpp` `startStandaloneProxy()` 单个分支
> **触发指令**：「对独立代理测试连通性失败后，依然需要纳入监控」
> **关联**：`docs/bugfix/2026-08-21-Bugfix-StandaloneProxy-PortExternalConfigDir-v1.0.md`（端口解析）、`docs/bugfix/2026-08-17-Bugfix-StandaloneProxy-WatchLifecycle-v1.0.md`（退出监听生命周期）

---

## 1. 现象

用户在系统中同时启动了两个独立代理进程，`validproxy` 的监控面板只显示其中一个。

现场（2026-10-20 11:56）`Get-CimInstance Win32_Process` 确认两个进程均存活：

```
"E:\v2rayN-windows-64\bin\xray\xray.exe" run -c "...\bin\worker\config\standalone_5046338036629160457-xray.json"   PID 15140
"E:\v2rayN-windows-64\bin\xray\xray.exe" run -c "...\bin\worker\config\standalone_5854986468419022565-xray.json"   PID 7328
```

两份配置都位于 `bin\worker\config\`，命令行完整路径可被 `ProcessInspector::extractConfigFullPath()` 正确解析（路径仅含 `[A-Za-z0-9 \\ / : . - _]`，无空格/中文），因此**不是命令行 → 配置目录解析问题**。

`bin/worker/log/ui_20261020_115618.log` 前两行给出直接证据：

```
[2026-10-20 11:57:29] [WARN] [StandaloneProxy] Connectivity test FAILED for 5647031224044510199 on SOCKS5 :10809
[2026-10-20 11:57:33] [WARN] [StandaloneProxy] Proxy left unmanaged (connectivity failed) for 5647031224044510199
```

即：该进程构造与启动均成功，只是**出网连通性验证失败**，随后被标记为「不托管」。

---

## 2. 根因

`AppController::startStandaloneProxy()`（`src/ui/AppController.cpp`）在启动进程并写入
`standaloneProxies_[indexId]` 后，同步执行 R2 连通性验证。验证失败且用户选择「保留进程」时，原实现：

```cpp
// wxNO / wxCANCEL → leave the process alive but unmanaged: no history row,
// no exit-listener watch, no monitoring.
info.running = false;
info.managed  = false;
return true;   // 直接返回，跳过后续 tracking
```

后果链：

1. `getWatchedStandaloneMonitors()`（`:1373`）Pass 1 过滤条件为 `running && managed`（`:1390`）→ 该进程**不产生监控行**；
2. `getRunningStandaloneCount()`（`:1357`）只统计 `running == true` → 状态栏计数少 1；
3. `return true` 跳过了后续 `historyDao_.insertStart()` 与 `exitListener_->watch()` → 无运行历史、无退出监听；
4. `adoptDanglingStandaloneProxies()`（`:1545`）对已存在于 `standaloneProxies_` 的 `indexId` 直接 `continue`（`:1584`）→ 该进程**永远不会被重新纳管**。

因此「两个存活进程 → 只监控一个」的真实原因是：其中一个进程连通性验证失败后被移出监控，而非进程识别/配置目录解析失败。

### 2.1 历史溯源：该行为于 2026-08-19 `a51d173` 引入

用户反馈「以前可以托管」，历史记录证实这是一次回归：`a51d173` 之前并不存在连通性验证，**启动即纳管**。

**引入点**

| 提交 | 时间 | 说明 |
| :--- | :--- | :--- |
| `a51d173` | **2026-08-19 10:13:34 +0800** | `feat(controller): add getRunningStandaloneCount()` — **首次引入 unmanaged 语义**（AppController.cpp +553/−42） |
| `ebd025b` | 2026-09-07 09:08:59 +0800 | 「添加悬浮框、弹窗、代理池功能，临时备份」— 仅新增「端口就绪后闪崩」分支（unmanaged 4 处 → 5 处），属延续而非重新引入 |

**改动前（`a51d173^`）——无条件纳管**

`startStandaloneProxy()` 尾部直线执行，无任何验证门控：

```cpp
CreateProcessA(...)
StandaloneProxyInfo info{ indexId, configPath, socksPort, pi.hProcess, /*running=*/true };
standaloneProxies_[indexId] = std::move(info);
CloseHandle(pi.hThread);
exDao.updateStartupTime(indexId);
Logger::write("[StandaloneProxy] Started " + indexId + " on SOCKS5 :" + ...);
return true;
```

**证据链**

1. `git grep -n "unmanaged" a51d173^ -- src/ui/AppController.cpp` → **零匹配**（该语义当时不存在）。
2. `git grep -n "ConnectivityVerifier\|waitForPort\|Connectivity test" a51d173^` → **零匹配**；这些符号全部由 `a51d173` 首次引入（`ProxyConnectivityVerifier.h` :23、`waitForPort` :838、`verify` :862）。
3. `git grep -n "insertStart\|exitListener_->watch" a51d173^` → **零匹配**；运行时历史 + 退出监听纳管链路同样由 `a51d173` 首次引入（`insertStart` :899/:963/:1122、`exitListener_->watch` :906/:979/:1131）。
4. `a51d173` 中 unmanaged 出现 4 处（`:833` 注释、`:857` 端口未就绪、`:881` 注释、`:886` 连通性失败）；HEAD 为 6 处，增量来自 `ebd025b` 的闪崩分支与后续注释。

**一句话归因**

`a51d173` 在**同一个提交**里做了两件事：(a) 引入运行时历史 + 退出监听这套「纳管」链路；(b) 引入连通性验证。问题在于纳管动作被放在验证**之后**（`:899+`），而验证失败分支在 `:886` 就 `return true` 提前返回 —— 它自称成功、却跳过了自己的下游纳管代码。由此 `running=false` 同时充当「不监控」开关与「永不被重新纳管」的锁（`adoptDanglingStandaloneProxies` 见 indexId 已存在即 `continue`）。

本次修复正是把纳管动作移回验证之外，仅保留「启动时间戳写入」（`updateStartupTime`）这一项的成功门控。

---

## 3. 修复

仅修改 `if (!ok)` 分支中「保留进程」的路径（`wxNO` / `wxCANCEL`）：

- 删除 `info.running = false; info.managed = false;` 与提前 `return true;`；
- 记录 WARN「kept alive but unverified; tracking it anyway」；
- 与成功路径共用后续正常 tracking 流程（运行历史 + 退出监听 + 启动事件）。

成功路径的 `updateStartupTime()` 仍只在连通性通过时执行，`updateTestResult(indexId, -1, false, ...)` 仍记录失败结果，语义不变。

**保持不变**（按指令范围）：

- 「端口从未就绪」与「端口就绪后闪崩」两个分支维持原有 unmanaged 行为——进程已处于异常终态，不属于「连通性验证失败但仍在运行」。

---

## 4. 验证

1. 构建：`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug` + `cmake --build build --parallel 8`（C++17，禁止 `auto`）。
2. 全量：`ctest -V`。
3. 现场判据：两个独立代理进程同时存活时，`getRunningStandaloneCount()` 返回 2，浮动监控面板出现两行；日志出现
   `Proxy kept alive but unverified; tracking it anyway for <indexId>`，且不再出现 `Proxy left unmanaged (connectivity failed)`。
