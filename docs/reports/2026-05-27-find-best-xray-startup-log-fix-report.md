---
title: "修复: 工具栏查找最佳代理 + Xray 自启动 + 日志进度显示"
type: report
status: completed
date: 2026-05-27
origin: "UI 增强与 ProxyFinder 完善"
---

# 技术报告: 查找最佳代理功能修复

## 问题概述

本次修复解决了 GUI 中"查找代理"功能的三个问题：

| # | 问题 | 根因 |
|---|------|------|
| P1 | 工具栏"查找"按钮只查找**首个可用代理**而非**最佳代理** | `onToolFind` 委托给了 `onMenuFindProxy`（查首个），而非 `onMenuFindBest`（查最优） |
| P2 | 从 GUI 触发"查找代理/最佳代理"时，Xray 未自动启动，导致 `ProxyFinder` 直接返回失败 | `AppController::doFindFirstProxy` / `doFindBestProxy` 获取 `XrayManager` 后未调用 `start()` |
| P3 | `ProxyFinder` 使用 `std::cout` 输出日志，不会显示在 GUI 日志窗口 | 所有进度输出走 `std::cout`，而非 `Logger::write()`，且日志中缺少代理协议类型信息 |

## 解决方案架构

### P1 修复: 工具栏按钮行为变更

**文件**: `MainFrame.cpp`

将工具栏"查找"按钮从委托"查找首个代理"改为委托"查找最佳代理"：

```cpp
// 旧: tb->AddTool(..., "查找");
// 新: tb->AddTool(..., "查找最佳代理");

// 旧: onMenuFindProxy(event);
// 新: onMenuFindBest(event);
```

语义对齐：用户单击"查找"应找到延迟最低的代理而非随机的首个可用。

### P2 修复: 查找前自动启动 Xray

**文件**: `AppController.cpp`（两个方法，相同逻辑）

在 `doFindFirstProxy()` 和 `doFindBestProxy()` 中，获取 `XrayManager` 后，检查实例数是否为 0，如是则自动启动：

```cpp
// 在创建 ProxyFinder 之前插入:
if (manager->getInstanceCount() == 0) {
    int started = manager->start(
        config_.xray_workers,
        config_.xray_start_port,
        config_.xray_api_port
    );
    if (started == 0) {
        wxQueueEvent(wxHandler,
            new StatusUpdateEvent(0, "ERR:Failed to start Xray instances"));
        return;
    }
}
```

这消除了用户在调用查找功能前需要手动启动 Xray 的前提条件。

### P3 修复: ProxyFinder 日志改革 + 协议显示

#### 3a. 添加 configType 字段

**文件**: `ProxyFinder.h`

```cpp
struct FallbackProxy {
    std::string indexId;
    std::string address;
    int socksPort;
    int delay;
    std::string configType;   // 新增: 1=VMess, 3=SS, 5=VLESS...
};

static std::string configTypeToProtocol(const std::string& ct);
```

#### 3b. SQL 查询增加 ConfigType 列

**文件**: `ProxyFinder.cpp` - `loadFallbackProxies()`

```sql
-- 旧: SELECT pi.IndexId, pi.Address, pi.Port, pi.PreSocksPort, COALESCE(pe.Delay, 999999) AS Delay
-- 新: SELECT pi.IndexId, pi.Address, pi.Port, pi.PreSocksPort, COALESCE(pe.Delay, 999999) AS Delay, pi.ConfigType
```

添加 `sqlite3_column_text(stmt, 5)` 读取 `proxy.configType`。

#### 3c. 协议转换实现

```cpp
std::string ProxyFinder::configTypeToProtocol(const std::string& ct) {
    if (ct == "1") return "VMess";
    if (ct == "3") return "Shadowsocks";
    if (ct == "4") return "SOCKS";
    if (ct == "5") return "VLESS";
    if (ct == "6") return "Trojan";
    if (ct == "7") return "Hysteria2";
    if (ct == "8") return "TUIC";
    if (ct == "9") return "WireGuard";
    if (ct == "10") return "HTTP";
    return "Unknown(" + ct + ")";
}
```

#### 3d. 全部日志迁移

所有 14 处 `std::cout` 调用替换为 `Logger::write()`，日志格式统一：

```
旧: ProxyFinder: Testing proxy 3/10 (indexId=xxx, socks=10808)
新: [ProxyFinder] Testing 3/10: 1.2.3.4:1080 (VLESS) socks=10808
```

日志前缀统一为 `[ProxyFinder]`，每条记录包含 `address:port (Protocol)`。

#### 3e. 日志级别调整

两处 `LogLevel::WARN`（取消搜索）降为 `LogLevel::INFO`，避免在 GUI 日志中误标为警告。

#### 3f. TestResult 初始化修复

```cpp
// 旧: TestResult result = {false, -1, ""};
// 新: TestResult result = {};
```

零初始化更安全，避免遗漏字段。

## 变更清单

| 文件 | 修改类型 | 说明 |
|------|----------|------|
| `include/ProxyFinder.h` | 结构体扩展 | 添加 `configType` 字段 + `configTypeToProtocol()` 声明 |
| `src/ProxyFinder.cpp` | 核心重写 | 实现协议转换、SQL 增加列、替换所有 `std::cout` 为 `Logger::write`、统一日志格式 |
| `src/ui/AppController.cpp` | 添加 Xray 自启动 | `doFindFirstProxy` / `doFindBestProxy` 中添加 `manager->start()` |
| `src/ui/MainFrame.cpp` | 工具栏修复 | 提示改为"查找最佳代理"，委托改为 `onMenuFindBest` |

## 事件流

```
用户点击工具栏"查找最佳代理"
    ↓
MainFrame::onToolFind()
    ↓ (改为委托 onMenuFindBest)
MainFrame::onMenuFindBest()
    ↓
controller_->findBestProxyAsync(this)
    ↓
std::thread → doFindBestProxy()
    ↓
XrayManager::getInstance(...)
    ↓
检查 getInstanceCount() == 0?
    ├─ 是 → manager->start(...) 启动 Xray 实例
    └─ 否 → 跳过
    ↓
ProxyFinder::findWorkingProxy()
    ↓ 遍历代理，每个输出:
    [ProxyFinder] Testing 3/10: 1.2.3.4:1080 (VLESS) socks=10808
    ↓
Logger::write() → LogPanel / TestPanel 实时显示
    ↓
找到最佳代理 → StatusUpdateEvent("FOUND:...")
    ↓
MainFrame 接收 → proxyPanel_->selectProxyByIndexId()
    ↓
代理行高亮选中
```

## 验证结果

```
Build: cmake --build build --parallel 8
Result: ✅ SUCCESS (0 errors)

Tests: ctest -V
Result: ✅ 3/3 passed
  - CurlEasyHandleTest: ✅
  - DedupTest: ✅ (11/11)
  - ProfileitemTest: ✅ (3/3)
```

## 文件变更统计

| 文件 | 新增行 | 删除行 |
|------|--------|--------|
| `include/ProxyFinder.h` | 4 | 0 |
| `src/ProxyFinder.cpp` | 70 | 41 |
| `src/ui/AppController.cpp` | 20 | 0 |
| `src/ui/MainFrame.cpp` | 2 | 2 |
| **合计** | **93** | **45** |

净变化: **+48 行**
