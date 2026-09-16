# Spec: 独立代理日志级别细化（LogLevelRefinement）

**日期**: 2026-09-16  
**类型**: Spec（功能调整）  
**模块**: 独立代理 / 日志  
**版本**: v1.0

---

## 1. 背景与动机

独立代理周期探活上线后，`REPORT` 级别日志存在**重复与噪音**：

1. `[ProxyListPanel] refreshResults called`（REPORT）——每次列表刷新都输出，高频噪音
2. 手动启动一个独立代理会输出 **3 条 REPORT**：
   - `AppController.cpp` L1303 `[StandaloneProxy] Started <id> on SOCKS5 :<port>`
   - `ProxyListPanel.cpp` L702 `[UI] Standalone proxy started: <id> on SOCKS5 127.0.0.1:<port>`
   - `ProxyListPanel.cpp` L714 `[UI] Standalone proxy started: <id> on port <port>`
3. 悬垂进程纳管会输出 **2 条 REPORT**：
   - `AppController.cpp` L1695 `[StandaloneProxy] Adopted dangling process pid=...`
   - `ProxyListPanel.cpp` L714（纳管事件同样触发 started 分支）

用户诉求：**REPORT 只保留三类成功事件各一条**——独立代理启动、测试成功、悬垂进程纳管。

## 2. 目标

- `refreshResults` 日志降为 TRACE
- 独立代理启动成功 → 全局仅 1 条 REPORT（AppController 业务层）
- 测试成功（手工开启后首次连通性验证 + 手动测试全成功）→ 各 1 条 REPORT；周期探活全成功保持 DEBUG（防刷屏）
- 悬垂进程纳管成功 → 全局仅 1 条 REPORT（AppController 业务层）
- 代理停止 → 降为 DEBUG（不在三类保留事件内）

## 3. 改动清单

### 3.1 `src/ui/ProxyListPanel.cpp`

| 行 | 现状 | 改为 | 说明 |
|----|------|------|------|
| L192 | `[ProxyListPanel] refreshResults called` REPORT | **TRACE** | 高频刷新噪音 |
| L702 | `[UI] Standalone proxy started: <id> on SOCKS5 127.0.0.1:<port>` REPORT | **TRACE** | 与 AppController L1303 重复；业务层保留 |
| L714 | `[UI] Standalone proxy started: <id> on port <port>` REPORT | **TRACE** | 与 L1303 重复；且 adopt 也触发（纳管重复） |
| L719 | `[UI] Standalone proxy stopped: <id>` REPORT | **DEBUG** | 停止不在三类保留事件内 |

### 3.2 `src/ui/AppController.cpp`

| 行 | 现状 | 改为 | 说明 |
|----|------|------|------|
| L1093 | `[StandaloneProxy] Connectivity test PASSED for <id> on SOCKS5 :<port>` DEBUG | **REPORT** | 手工开启后首次测试成功 |
| L2228-2235 | 探活总结 `Online proxies test finished: ...`：失败 REPORT / 全成功 DEBUG | 失败 REPORT（不变）；**手动全成功（!silent）REPORT**；周期全成功（silent）DEBUG（不变） | 手动测试成功可见，周期探活防刷屏 |

### 3.3 保持不变的 REPORT（各场景唯一一条）

| 场景 | 位置 | 日志 |
|------|------|------|
| ① 独立代理启动 | `AppController.cpp` L1303 | `[StandaloneProxy] Started <id> on SOCKS5 :<port>` |
| ② 手工开启后首次测试 | `AppController.cpp` L1093 | `[StandaloneProxy] Connectivity test PASSED ...` |
| ② 手动测试全成功 | `AppController.cpp` L2232 分支 | `Online proxies test finished: total=... failed=0` |
| ③ 悬垂进程纳管 | `AppController.cpp` L1695 | `[StandaloneProxy] Adopted dangling process pid=...` |

## 4. 行为对照（最终）

| 事件 | REPORT 日志数（改动前 → 后） |
|------|------|
| 手动启动独立代理 | 3 → 2（Started + Connectivity PASSED，分属「启动」「首次测试」两类） |
| 手动测试在线代理全成功 | 0 → 1 |
| 周期探活全成功 | 0（DEBUG）→ 0（DEBUG，不变） |
| 探活有失败 | 1（不变） |
| 悬垂进程纳管 | 2 → 1 |
| 代理停止 | 1 → 0（DEBUG） |
| refreshResults | 每条 1（REPORT）→ 0（TRACE） |

## 5. 非目标

- 不改变失败路径日志（WARN/ERR 保持）
- 不改变探活失败阈值 WARN（`[OnlineProbe] test failed` 保持 WARN）
- 不改变其他异步操作（批量测试/订阅更新等）的 report 语义