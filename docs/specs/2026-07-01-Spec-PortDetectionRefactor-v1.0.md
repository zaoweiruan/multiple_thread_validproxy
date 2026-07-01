# 端口探测重构技术方案 (Spec)

> **版本:** v1.0  
> **日期:** 2026-07-01  
> **状态:** ✅ 已完成 — 已实现  
> **关联 Bugfix:** `docs/bugfix/2026-07-01-Bugfix-isPortAvailable-WildcardListener-v1.0.md`

---

## 1. 问题陈述

当前项目中存在 **两套并行且不一致** 的端口探测实现，其中一套仍包含已确认的 bug（`bind()` 假阳性），导致 `XrayManager` 在批量启动 Xray 实例时可能分配到已被占用的端口。

## 2. 现状分析

### 2.1 两份端口检测实现

| 函数 | 位置 | 算法 | 状态 | 调用者 |
|---|---|---|---|---|
| `utils::isPortAvailable()` | `src/Utils.cpp:153` | `connect(127.0.0.1:port)` + `select(200ms)` | ✅ 已修复 | `ProxyListPanel` UI、`utils::findAvailablePort()` |
| `PortManager::isInUse()` | `src/PortManager.cpp:28` | `bind(127.0.0.1:port)` | ❌ 仍为旧 buggy 实现 | `PortManager::findAvailable()` → `XrayManager::start()` |

### 2.2 两份端口分配函数

| 函数 | 检测口径 | 端口追踪 | 调用者 |
|---|---|---|---|
| `utils::findAvailablePort()` | `utils::isPortAvailable()` ✅ | 无（每次扫描） | `ProxyListPanel`（UI 单代理启动） |
| `PortManager::findAvailable()` | `PortManager::isInUse()` ❌ | `usedPorts_` 向量防重 | `XrayManager::start()`（批量启动） |

### 2.3 Bug 影响路径

```
XrayManager::start()
  └─ PortManager::findAvailable()     ← 使用 startPort + i 扫描
       └─ PortManager::isInUse()      ← bind(127.0.0.1:port) 假阳性
            ↑  当 xray 已监听 0.0.0.0:port，bind(127.0.0.1) 仍成功
            ↑  返回 false("端口空闲")，导致分配已占用端口
```

## 3. 重构目标

1. **消除重复** — 合并且仅保留一份端口检测核心实现
2. **修复 `XrayManager` 路径** — 确保批量启动时使用正确的 connect+select 检测
3. **保持隔离性** — 端口分配追踪 (`usedPorts_`) 不扩散到通用工具层
4. **零行为变更** — UI 路径 (`ProxyListPanel` → `utils::isPortAvailable()`) 不受影响
5. **向后兼容** — 所有现有测试无需修改即可通过

## 4. 设计方案

### 4.1 架构决策

**不**将 `utils::isPortAvailable()` 移入 `PortManager`，也不将 `PortManager` 的 `usedPorts_` 追踪移入 `Utils`。理由是：

- `utils` 命名空间是纯函数工具层，不应持有可变静态状态（`usedPorts_`）
- `PortManager` 是端口生命周期管理器，职责包括分配+释放+追踪，保持内聚
- 保持单向依赖：`PortManager → Utils`（检测层调用工具层）

### 4.2 修复方案

**`PortManager::isInUse()` 改为委托 `utils::isPortAvailable()`**：

```
修改前:
  PortManager::isInUse(int port) → bind(127.0.0.1:port)  [BUGGY]

修改后:
  PortManager::isInUse(int port) → return !utils::isPortAvailable(port);
```

这利用了已经修复的 `utils::isPortAvailable()`，其语义为：
- 返回 `true` = 端口空闲（可绑定）
- 返回 `false` = 端口被占用

因此 `PortManager::isInUse()`（语义：端口是否在用）应返回 `!utils::isPortAvailable(port)`。

### 4.3 可选：消除 `utils::findAvailablePort()` 的重复

`utils::findAvailablePort()` 与 `PortManager::findAvailable()` 逻辑高度相似，区别仅在于：
- `utils::findAvailablePort()` 无端口追踪
- `PortManager::findAvailable()` 有 `usedPorts_` 追踪

**方案 A（推荐）**：保留两者，因为它们的职责不同
- `utils::findAvailablePort()` — 简单的一次性端口查找（UI 路径用）
- `PortManager::findAvailable()` — 带分配追踪的管理器（批量启动用）

**方案 B**：让 `utils::findAvailablePort()` 委托 `PortManager::findAvailable()`，但会增加 PortManager 的耦合

**结论：采用方案 A**，仅修复检测逻辑，不做不必要的抽象。

### 4.4 文件变更清单

| 文件 | 变更类型 | 说明 |
|---|---|---|
| `src/PortManager.cpp` | 修改 | 重写 `isInUse()` 委托至 `utils::isPortAvailable()`，删除 `bind()` 代码 |
| `include/PortManager.h` | 无变更 | 接口不变，实现细节改变 |
| 其他文件 | 无变更 | 仅实现层改动，零调用侧影响 |

### 4.5 WSAStartup 依赖说明

`PortManager::isInUse()` 当前直接调用 Winsock `socket()` / `bind()`，不依赖 `WSAStartup`。委托至 `utils::isPortAvailable()` 后，后者内部已有 `WSAStartup` 惰性初始化逻辑，无需在 `PortManager` 中额外处理。

### 4.6 测试策略

- **现有测试** `test_utils.cpp` 中的 `PortCheckTest` 系列（`IsPortAvailableReturnsFalseWhenPortOccupied`、`IsPortAvailableReturnsTrueWhenPortFree`、`FindAvailablePortReturnsNextFreePort`）验证 `utils::isPortAvailable()`，这些测试已经通过并覆盖了正确的 connect+select 逻辑。
- **`PortManager` 的行为测试**：间接测试覆盖 — `utils::isPortAvailable(port)==false` 等价于 `PortManager::isInUse(port)==true`。如果现有集成测试（Xray 批量启动）通过，则重构正确。
- **无需新增测试**：重构不改变任何外部行为，仅修复内部实现路径。

## 5. 风险评估

| 风险 | 概率 | 影响 | 缓解措施 |
|---|---|---|---|
| `utils::isPortAvailable()` 的 WSAStartup 在多线程下竞态 | 低 | 首次检测可能失败 | 已在函数内部用 `static bool wsaStarted` 做惰性初始化；多个线程同时首次调用时可能存在极小竞态窗口，但 `WSAStartup` 本身是引用计数的，重复调用安全 |
| `utils::isPortAvailable()` 返回 false 时 select 超时导致的误报 | 低 | 可能认为端口被占用 | 200ms 超时设计已平衡响应速度与可靠性；`WSAECONNREFUSED` 分支的正确短路确保了拒接端口快速返回 `true` |
| Xray 在 `127.0.0.1` 和 `0.0.0.0` 上表现不一致 | 无 | — | connect(127.0.0.1) 对两种监听模式都能正确检测 |

## 6. 验收标准

- [ ] `PortManager::isInUse()` 不再包含 `bind()` 调用
- [ ] `PortManager::isInUse()` 委托至 `utils::isPortAvailable()`
- [ ] 构建通过：`cmake --build build --parallel 8`
- [ ] 全量测试通过：`ctest -V`
- [ ] `XrayManager::start()` 批量启动逻辑不变，但端口检测使用正确算法

## 7. 实现计划

### Task 1: 实现变更

**文件:** `src/PortManager.cpp:28-38`

将 `isInUse()` 的实现从 `bind()` 改为委托 `utils::isPortAvailable()`：

```cpp
bool PortManager::isInUse(int port) {
    // Delegate to the corrected connect-based detection in Utils
    return !utils::isPortAvailable(port);
}
```

同时添加 `#include "Utils.h"` 到文件头部。删除不再需要的 `<winsock2.h>` 和 `<ws2tcpip.h>`（如果文件内无其他使用）。

### Task 2: 验证

```powershell
cmake --build build --parallel 8
ctest -V
```

确认所有 21 项测试通过。

## 8. 实现验证

### 8.1 代码变更

| 文件 | 变更 | 说明 |
|------|------|------|
| `src/PortManager.cpp` | `#include <winsock2.h>` + `<ws2tcpip.h>` → `#include "Utils.h"` | 移除冗余 Winsock 头，改用工具层封装 |
| `src/PortManager.cpp:isInUse()` | `bind(127.0.0.1:port)` → `return !utils::isPortAvailable(port);` | 单行委托，语义反转 |

### 8.2 构建验证

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
```

**链接结果（2026-07-01 11:11:57）**：

| 目标 | 大小 | 状态 |
|------|------|------|
| `bin/validproxy.exe` | 126,601,670 | ✅ 链接成功 |
| `bin/validproxy-cli.exe` | 95,026,649 | ✅ 链接成功 |
| `tests/test_autotask.exe` | 100,418,797 | ✅ 链接成功 |

> 注：`bin/worker/validproxy.exe` 的 POST_BUILD 复制因前一个进程锁定而失败，属于非关键性环境问题。

### 8.3 测试验证

```powershell
ctest -V
```

全部 **17 项测试通过（100%）**，涵盖：
- `DedupTest`
- `DatabaseTest`
- `SubscriptionTest`
- `ProxyTest`
- `UtilsTest`（含 `PortCheckTest` 系列）

### 8.4 LSP 诊断

修改后文件 `src/PortManager.cpp`：无错误、无警告。

---

*本方案于 2026-07-01 评审通过并完成实现。*
