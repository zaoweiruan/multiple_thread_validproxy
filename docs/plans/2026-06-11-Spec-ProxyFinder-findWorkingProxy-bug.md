---
title: "fix: ProxyFinder::findWorkingProxy 返回端口时未重新注入代理"
type: fix
status: completed
date: 2026-06-11
origin: "ProxyFinder.cpp 代码分析"
---

# ProxyFinder::findWorkingProxy Bug Fix Specification

## 问题描述

在 `src/ProxyFinder.cpp` 的 `findWorkingProxy()` 方法（lines 200-218）中，存在明确的逻辑缺陷：

1. **当前行为**: 方法测试所有代理后找到延迟最小的代理，但每次测试完成都会调用 `removeProxyFromXray()` 移除代理
2. **返回问题**: 返回的 `{socksPort, apiPort}` 指向的是 Xray 实例，但该实例内已经没有配置好的代理
3. **结果**: 调用方拿到端口后无法进行实际的代理请求，因为 Xray 配置文件中没有 outbound

## 范围边界

### 修改
- `src/ProxyFinder.cpp` - `findWorkingProxy()` 方法
- `src/ProxyFinder.cpp` - `removeProxyFromXray()` 方法（需添加清理逻辑）

### NOT 修改
- `findFirstWorkingProxy()` - 该方法测试成功后立即返回，代理还在 Xray 中（逻辑正确）
- `loadFallbackProxies()` - SQL 查询逻辑保持不变
- 其他模块不相关

## 详细变更

### U1: `findWorkingProxy()` - 重新注入最佳代理

**问题代码** (lines 200-218):
```cpp
// 找到对应的端口，但代理已被移除
for (size_t i = 0; i < validProxies.size(); ++i) {
    if (validProxies[i].indexId == best.indexId) {
        int workerIndex = i % manager_->getInstanceCount();
        XrayInstance* instance = manager_->getInstance(workerIndex);
        if (instance) {
            result = {instance->getSocksPort(), instance->getApiPort()};
            // 返回时代理未配置！
        }
        break;
    }
}
```

**修复后** (新增重新注入逻辑):
```cpp
// 找到对应的端口并重新注入代理
for (size_t i = 0; i < validProxies.size(); ++i) {
    if (validProxies[i].indexId == best.indexId) {
        int workerIndex = i % manager_->getInstanceCount();
        XrayInstance* instance = manager_->getInstance(workerIndex);
        if (instance) {
            currentSocksPort_ = instance->getSocksPort();
            currentApiPort_ = instance->getApiPort();
            // 重新注入最佳代理
            if (injectProxyToXray(best.indexId)) {
                result = {currentSocksPort_, currentApiPort_};
                Logger::write("[ProxyFinder] Best proxy re-injected successfully", LogLevel::INFO);
            }
        }
        break;
    }
}
```

### U2: `removeProxyFromXray()` - 当前为空实现

**当前状态** (line 361):
```cpp
void ProxyFinder::removeProxyFromXray() {
    // 暂时不清理
}
```

**建议**: 保留为空（因为 Xray API 中的 `removeOutbound("proxy")` 已在 `injectProxyToXray()` 开头调用），或改为重置端口状态：
```cpp
void ProxyFinder::removeProxyFromXray() {
    // Xray API 层面已移除 outbound，此处重置端口跟踪
    currentSocksPort_ = -1;
    currentApiPort_ = -1;
}
```

## 验证步骤

1. 编译通过（`cmake --build build --parallel 8`）
2. 静态分析无警告
3. 手动测试 `-FMIN` CLI 命令，验证返回代理可用

## 风险

- **低风险**: 修改不影响其他方法逻辑
- **注意**: `injectProxyToXray()` 内部已调用 `removeOutbound()`，重复移除可能导致竞态，但概率极低（500ms 后才移除）

## 文件变更列表

- `src/ProxyFinder.cpp` - 修复 findWorkingProxy() 返回前重新注入代理