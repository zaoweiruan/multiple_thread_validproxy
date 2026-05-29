# 2026-04-16-subitem-updater-v2-optimization.md

## 优化背景

当前 SubitemUpdaterV2 在更新多个订阅时，每个订阅都会重复查找代理并频繁启停 Xray 实例，效率低下。

## 优化目标

1. **减少代理查找次数**: 多个订阅只查找一次可用代理
2. **复用 Xray 实例**: 更新过程中保持 Xray 运行，更新完成后统一关闭
3. **失败不重试**: 预存代理失败后不再重新查找

## 优化方案

### 1. 代理预查找逻辑

```cpp
bool SubitemUpdaterV2::run() {
    // 1. 获取订阅列表
    auto enabledSubs = subDao.getEnabledSubscriptions();
    if (enabledSubs.empty()) return false;
    
    // 2. 根据策略决定是否需要预查找代理
    int proxySocksPort = -1;
    int proxyApiPort = -1;
    Strategy strategy = parseStrategy(config_.priority_mode);
    
    if (strategy == Strategy::ProxyFirst && !enabledSubs.empty()) {
        // 用第一个订阅的 URL 作为测试目标，找到一个可用代理
        auto result = getProxyPorts(enabledSubs[0].url);
        proxySocksPort = result.first;
        proxyApiPort = result.second;
        log("INFO: Pre-found working proxy, socks=" + std::to_string(proxySocksPort));
    }
    
    // 3. 遍历处理所有订阅
    for (const auto& sub : enabledSubs) {
        // 尝试使用预存的代理，不重复查找
        if (proxySocksPort > 0) {
            content = fetchUrlViaProxy(sub.url, proxySocksPort);
            if (!content.empty()) {
                // 成功
            }
        }
        // 如果失败，不重试，直接标记失败
    }
    
    // 4. 完成后统一释放 Xray 实例
    releaseProxyPorts();
}
```

### 2. updateWithStrategy 修改

- 移除 `releaseProxyPorts()` 调用
- Xray 生命周期由 `run()` 统一管理

### 3. runSingle 保持不变

- 单订阅模式保持原有逻辑

## 数据流

```
run()
  ├── 获取订阅列表
  ├── Strategy == ProxyFirst? 
  │     └── getProxyPorts(firstSubUrl) → 找到可用代理
  │           └── keep Xray running
  ├── for each subscription
  │     └── fetchUrlViaProxy(url, savedSocksPort)
  │           └── parseSubscription() → updateProfileItems()
  │                 └── (失败不重试)
  └── releaseProxyPorts() ← 完成后统一关闭
```

## 验收标准

- [x] 多个订阅只启动一次 Xray 实例
- [x] 用第一个订阅的 URL 测试代理
- [x] 预存代理失败后不重新查找
- [x] 更新完成后统一关闭 Xray
- [x] runSingle 保持原有逻辑