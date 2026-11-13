---
title: "bugfix: XrayManagerStartupTest 失败 = waitInstanceReady 未初始化 Winsock（socket() 返回 WSANOTINITIALISED）"
type: bugfix
status: completed
date: 2026-11-13
module: xray-manager
version: v1.0
---

# XrayManager::waitInstanceReady 未初始化 Winsock → API 就绪检查永远超时

## 1. 症状

全量 CTest 回归中 `XrayManagerStartupTest.RealXrayStartsBounded`（#47）失败：

```
[XrayInstance] Started successfully, socks=29501, api=29601
[XrayManager] API port 29601 not ready within timeout for instance 0
Expected equality of these values: 1 / actual(0)   // actualCount==0
Expected: (elapsedMs) < (3000), actual: ~6168
```

真实 xray 二进制正常启动（stdout 重定向日志 `Xray 26.3.27 started`），但 `mgr->start()` 返回 0。

## 2. 复现

`tests\test_xray_manager_startup.exe --gtest_filter=XrayManagerStartupTest.RealXrayStartsBounded` 连续 3 次**确定性失败**（每次均 5s 超时、actualCount=0）。

## 3. 根因（已确认）

`src/XrayManager.cpp` 的 `waitInstanceReady()` 使用原始 Winsock `socket()`/`connect()` 探测 API 端口，但**从不调用 `WSAStartup`**。

- 孤立测试 exe 进程中，没有任何代码事先初始化 Winsock → `socket()` 返回 `INVALID_SOCKET`（`WSANOTINITIALISED`）。
- `waitInstanceReady` 的 `if (sock != INVALID_SOCKET)` 恒假 → `connect()` 从未执行 → 纯轮询满 5s 超时返回 false → `start()` 报 `actualCount=0`。
- 生产环境"碰巧"正常：GUI 进程先由 `XrayApi::ensureWinsock()` / `DnsCache` / `Utils` 探测初始化了 Winsock，`waitInstanceReady` 依赖了这一副作用。任何未预初始化 Winsock 的调用方都会复现。

### 证据

| 检查 | 结果 |
|---|---|
| 测试窗口内 `Get-NetTCPConnection 29601` | `127.0.0.1:29601 Listen`（端口确实打开） |
| 测试窗口内外部 `TcpClient.Connect('127.0.0.1',29601)` | **成功**（端口可达） |
| `XrayManager.cpp` 中 `WSAStartup` | **无**（只有 `socket()`/`connect()`） |
| `tests\test_xray_manager_startup.cpp` 中 `WSAStartup` | **无** |
| 同库其他用 socket 的模块 | `XrayApi.cpp ensureWinsock()`、`Utils.cpp ensureWinsockStartedForProbe()`、`DnsCache.cpp` 均显式 `WSAStartup` |

**关键区分**：端口真实可连接（外部成功），但 `waitInstanceReady` 的 `connect()` 从未执行——排除配置/端口/xray 问题，指向 Winsock 未初始化。

## 4. 修复

`src/XrayManager.cpp`：新增 `ensureWinsock()`（**镜像 `XrayApi.cpp` 既有模式**：`static std::mutex` 互斥、仅缓存成功、失败记录错误码并重试），在 `waitInstanceReady` 入口调用；失败即快速返回（不烧超时）。

```cpp
static bool ensureWinsock() {
    static std::mutex mtx;
    static bool started = false;
    std::lock_guard<std::mutex> lock(mtx);
    if (started) return true;
    WSADATA wsa;
    const int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) { Logger::write("[XrayManager] ensureWinsock: WSAStartup failed, error=" +
                                 std::to_string(rc), LogLevel::ERR); return false; }
    started = true;
    return true;
}
```

## 5. 验收（证据）

- `RealXrayStartsBounded` 修复后 3/3 通过（`actualCount=1`，API 端口立即可用，无超时）。
- 全量 CTest：**72/72 通过（100%）**，0 失败；`XrayManagerStartupTest` 恢复绿色。
- UI 自动化 15/15 通过（验证重链后的 `bin\validproxy.exe` 端到端正常）。
- 该修复同时强化生产健壮性：`waitInstanceReady` 不再依赖调用方预先初始化 Winsock。

## 6. 说明

- 与 `2026-11-13-Bugfix-GuiFlashCrash-WxToolchainMismatch-v1.0.md` 是**两个独立根因**：前者 = wx DLL 跨 CRT 混链；本文件 = XrayManager 未初始化 Winsock。二者均在本次全量回归（`--clean-first` UCRT 重建）中暴露。
- 工具链侧：全量回归前 29 个 `0xC0000139` 失败为 `tests\` 下陈旧 msvcrt（GCC14.2）测试 exe 未被增量构建重链，`--clean-first` UCRT 重建后全部消除（详见 wx 工具链文档 §7）。
