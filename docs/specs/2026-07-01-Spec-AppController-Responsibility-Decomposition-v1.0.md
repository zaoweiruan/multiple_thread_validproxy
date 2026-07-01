---
title: AppController 职责边界重构方案
date: 2026-07-01
type: spec
status: draft
version: v1.0
---

## 背景

`AppController` 当前承担过多职责：
- UI事件调度 (subscription/proxy async operations)
- 数据库协调 (switchDatabase, loadSubscriptions, loadProxies)
- Xray 代理生命周期管理 (startStandaloneProxy, stopXray)
- 测试流程编排 (testSubscriptionAsync, testSingleProxyAsync)
- 查找流程编排 (findFirstProxyAsync, findBestProxyAsync)
- 同步流程编排 (syncDatabasesAsync)
- 自动任务编排 (runAutoTaskAsync)
- 网络监控暴露 (getNetworkMonitor, restartNetworkMonitor)

## 当前状态

**文件**: `src/ui/AppController.h/cpp` (1170 行)
**重复代码统计**:
| 重复逻辑 | 出现次数 | 说明 |
|---------|---------|------|
| 线程防护 `if(workerThread_.joinable())...` | 11 | async 方法入口相同 |
| `struct ResetGuard` RAII | 9 | do* 方法相同 |

**依赖模式**: 直接持有 `sqlite3* db_`, `config::AppConfig config_`, `NetworkMonitor netMon_`
**执行方式**: **全部异步**（订阅更新、代理测试、查找、同步），同步查找仅用于 CLI 模式 (`findFirstProxy`/`findBestProxy`)

### 异步执行模型

| 方法 | 线程模式 | 说明 |
|------|---------|------|
| `updateSubscriptionAsync` | workerThread_ + isRunning_ 重入防护 | detach 模式，异常后自动重置 isRunning_ |
| `test*Async` | workerThread_ + isRunning_ 重入防护 | detach 模式，ResetGuard RAII 确保 isRunning_ 重置 |
| `find*Async` | workerThread_ + isRunning_ 重入防护 | 同步查找仅供 CLI 使用 |
| `syncDatabasesAsync` | workerThread_ + isRunning_ 重入防护 | |
| `runAutoTaskAsync`/`resumeAutoTaskAsync` | workerThread_ + isRunning_ 重入防护 | 设置 progress callback |

**代码坏味道**: 11 个 async 方法重复相同线程防护逻辑；9 个 do* 方法重复 `ResetGuard` RAII 定义

## 重构目标

### 阶段 0 - 前置工具提取 (推荐优先)

| 工具类 | 职责 | 来源方法 |
|-------|------|--------|
| **AsyncOperationGuard** | 封装 `workerThread_.joinable() + isRunning_` 防护，`reject(handler, msg)`、`resetRunning()` | 11 个 async 入口方法 |
| **ScopeGuard** | RAII 重置 `isRunning_`，模板类 `ResetGuard(flag)` | 9 个 do* 方法 |

### 阶段 1 - 职责分解 (5 个服务)

| 新服务 | 职责 | 来源于 AppController 方法 |
|-------|------|------------------------|
| **SubscriptionOrchestrator** | 订阅管理 + 异步更新 | loadSubscriptions, updateSubscriptionAsync, updateAllSubscriptionsAsync, importSubscription, deleteSubscription |
| **ProxyOrchestrator** | 代理列表 + 测试编排 | loadProxies, loadProxiesAsync, testSubscriptionAsync, testSingleProxyAsync, testAllProxiesAsync, cancelTest |
| **StandaloneProxyManager** | 独立代理生命周期 | startStandaloneProxy, getStandaloneSocksPort, getRunningStandaloneIds, stopXray |
| **DatabaseCoordinator** | 数据库切换/查询 | switchDatabase, loadSubscriptions, loadProxies, updateSubscriptionEnabled, updateSubitem |
| **NetworkMonitorCoordinator** | 网络监控控制 | getNetworkMonitor, restartNetworkMonitor, netMonEnabled_ |

### 接口设计

```cpp
// SubscriptionOrchestrator
class SubscriptionOrchestrator {
public:
    void updateAsync(const std::string& subId, wxEvtHandler* handler);
    void updateAllAsync(wxEvtHandler* handler);
    bool import(const std::string& url);
    std::vector<db::models::Subitem> getAll();
};

// ProxyOrchestrator  
class ProxyOrchestrator {
private:
    sqlite3* db_;
    config::AppConfig config_;
    std::unique_ptr<XrayManager> xrayManager_;
    std::atomic<bool>* cancel_;
public:
    void testAsync(const std::string& subId, wxEvtHandler* handler);
    void testOneAsync(const std::string& indexId, wxEvtHandler* handler);
    void testAllAsync(wxEvtHandler* handler);
    void cancel();
};

// StandaloneProxyManager
class StandaloneProxyManager {
private:
    sqlite3* db_;
    config::AppConfig config_;
    std::mutex mutex_;
    std::unordered_map<std::string, StandaloneProxyInfo> proxies_;
public:
    bool start(const std::string& indexId, int overridePort = 0);
    int getSocksPort(const std::string& indexId) const;
    std::vector<std::string> getRunningIds() const;
    void stopAll();
};
```

## 工作量估计

| 阶段 | 任务 | 预估 |
|------|------|------|
| 0 | 提取 AsyncOperationGuard/ScopeGuard 工具类 | 3h |
| 1 | 分析现有方法划分边界 | 2h |
| 2 | 创建 SubscriptionOrchestrator + 测试 | 4h |
| 3 | 创建 ProxyOrchestrator + 测试 | 4h |
| 4 | 创建 StandaloneProxyManager + 测试 | 3h |
| 5 | 创建 DatabaseCoordinator + 测试 | 3h |
| 6 | 迁移 AppController 调用点 | 4h |
| 7 | 删除旧方法 + 清理 | 2h |

**总计**: ~21 小时 (2-3 天)

## 风险点

1. **线程同步**: workerThread_ 单线程模型需保留，确保异步方法互斥
2. **XrayManager 单例**: 生命周期由 AppController 管理，StandaloneProxyManager 暂时不接管
3. **事件传递**: wxEvtHandler 传递给各 orchestration 服务需保持一致性