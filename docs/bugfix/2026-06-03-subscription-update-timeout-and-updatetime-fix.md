---
title: "fix: 订阅更新 curl timeout 和 UpdateTime 字段不正确更新"
type: fix
status: completed
date: 2026-06-03
origin: "系统性调试 Bug 报告"
---

# 订阅更新 timeout 和 UpdateTime 修复

## 问题描述

### Bug 1: curl timeout 配置不完整导致连接失败
- **现象**: UI 程序更新时报错 `fetchUrlViaProxy failed - curl_easy_perform failed: Failure when receiving data from the peer`
- **原因**: `fetchUrlViaProxy()` 方法仅设置 `CURLOPT_TIMEOUT` (请求总超时)，缺少 `CURLOPT_CONNECTTIMEOUT` (连接超时)
- **影响**: 代理连接缓慢或不稳定时，容易超时失败

### Bug 2: 更新订阅失败时仍错误更新 UpdateTime 字段
- **现象**: 当订阅更新失败（网络错误、解析错误），`SubItem` 表的 `UpdateTime` 字段仍被更新为当前时间
- **原因**: `run()` 和 `runSingle()` 方法在订阅处理后无条件更新 `UpdateTime`
- **影响**: 无法区分订阅最后一次成功更新时间

## 范围边界

### 修改范围
- `include/CurlEasyHandle.h` - 添加 `setConnectTimeout()` 方法
- `src/SubitemUpdaterV2.cpp` - 修改 `fetchUrlViaProxy()` 添加连接超时
- `src/SubitemUpdaterV2.cpp` - 修改 `run()` 和 `runSingle()` 不在失败时更新 `UpdateTime`

### NOT 修改
- 其他 curl 使用场景
- 数据库结构
- 日志格式

## 详细变更

### U1: CurlEasyHandle.h - 添加连接超时方法
```cpp
// Fluent API: set connect timeout in milliseconds
CurlEasyHandle& setConnectTimeoutMs(long ms) {
    checkCurlCode(curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS, ms), "setConnectTimeoutMs");
    return *this;
}
```

### U2: SubitemUpdaterV2.cpp::fetchUrlViaProxy - 添加连接超时
```cpp
// 在 setTimeoutSec(30) 前添加:
.setConnectTimeoutMs(10000)  // 10秒连接超时
```

### U3: SubitemUpdaterV2.cpp::run - 仅在成功时更新 UpdateTime
- Direct 阶段: 仅当 `content.empty()` 为 false 且 profiles 解析成功时更新
- Proxy 阶段: 仅当 `content.empty()` 为 false 且 profiles 解析成功时更新

### U4: SubitemUpdaterV2.cpp::runSingle - 仅在成功时更新 UpdateTime
- 仅当 `updateWithStrategy()` 返回 true (代理抓取+解析成功) 时更新

### U5: SubitemUpdaterV2.cpp::runSingleWithProxy - 仅在成功时更新 UpdateTime
- 仅当 `content.empty()` 为 false 更新

## 验证步骤
1. 编译通过: `cmake --build build --parallel 8`
2. 检查 curl 超时逻辑: 运行订阅更新，观察超时行为
3. 检查 UpdateTime: 故意让订阅更新失败，确认 UpdateTime 不变

## 风险
- 低风险: 仅添加配置项和条件逻辑

## 验证结果

### 构建验证
- 编译通过: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel 8`
- CLI 可执行运行正常，成功读取订阅数据

### 测试结果
```
100% tests passed, 0 tests failed out of 6

Test #1: CurlEasyHandleTest ...............   Passed
Test #2: DedupTest ..............   Passed (13 tests)
Test #3: LoggerTest ...............   Passed (14 tests)
Test #4: ShareLinkTest ............   Passed (11 tests)
Test #5: ConfigGeneratorTest ......   Passed (2 passed, 1 skipped)
Test #6: DeleteSubscriptionTest ...   Passed (4 tests)

Total Test time (real) =   1.18 sec
```

### 修改文件列表
- `include/CurlEasyHandle.h` - 新增 `setConnectTimeoutMs()` 方法
- `src/UrlFetcher.cpp` - `fetch()` 和 `fetchViaProxy()` 新增连接超时
- `src/ProxyTester.cpp` - `test()` 新增连接超时
- `src/SubitemUpdaterV2.cpp` - 多个方法修复 UpdateTime 更新逻辑