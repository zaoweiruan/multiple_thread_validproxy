---
title: "feat: 订阅超时配置化"
type: spec
status: completed
date: 2026-06-03
origin: "Bug 修复后优化"
---

# 订阅超时配置化

## 需求

将订阅更新的连接超时和请求超时纳入 config.json 配置管理，支持用户自定义调整。

## 设计

### D1: AppConfig 结构添加订阅超时字段

```cpp
struct AppConfig {
    // ... existing fields ...
    
    // Subscription timeout configuration
    int subscription_connect_timeout_ms = 10000;  // Default: 10s
    int subscription_timeout_ms = 30000;          // Default: 30s
};
```

### D2: config.json schema 扩展

```json
{
    "subscription": {
        "priority_mode": "proxy_first",
        "check_auto_update_interval": false,
        "connect_timeout_ms": 10000,
        "timeout_ms": 30000
    }
}
```

### D3: SubitemUpdaterV2::fetchUrl/fetchUrlViaProxy 使用配置

- 从 ConfigReader 加载配置
- 使用配置的超时值代替硬编码

## 实施计划

| 步骤 | 文件 | 变更 |
|------|------|------|
| 1 | include/ConfigReader.h | 添加订阅超时字段到 AppConfig |
| 2 | src/ConfigReader.cpp | 解析订阅超时配置 |
| 3 | src/SubitemUpdaterV2.cpp | 使用配置的超时值 |
| 4 | bin/config.json | 添加订阅超时配置项 |
| 5 | tests/ | 验证配置解析正确 |

## 验证结果

### 构建验证
- 编译通过: `cmake -B build && cmake --build build --target validproxy-cli --parallel 8`

### 测试结果
```
100% tests passed, 0 tests failed out of 6
- CurlEasyHandleTest: Passed
- DedupTest: Passed (13 tests)
- LoggerTest: Passed (14 tests)
- ShareLinkTest: Passed (11 tests)
- ConfigGeneratorTest: Passed (2 passed, 1 skipped)
- DeleteSubscriptionTest: Passed (4 tests)
```

### 修改文件列表
- `include/ConfigReader.h` - 新增 `subscription_connect_timeout_ms` 和 `subscription_timeout_ms` 字段
- `src/ConfigReader.cpp` - 解析和保存订阅超时配置
- `src/SubitemUpdaterV2.cpp` - `fetchUrl()` 和 `fetchUrlViaProxy()` 使用配置的超时值
- `bin/test_config_empty.json` - 添加订阅超时配置项
- `bin/config.json` - 添加订阅超时配置项 (需同步)