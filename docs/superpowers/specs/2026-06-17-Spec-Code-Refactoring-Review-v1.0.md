# 代码重构方案评审

**日期**: 2026-06-17
**版本**: v1.0
**状态**: 待评审

## 1. 执行摘要

本文档汇总代码库中发现的重构候选点，包括死代码、重复逻辑、冗余头文件、过大函数及职责超出的文件。

## 2. 死代码 (Dead Code)

| # | 文件路径 | 行数 | 描述 |
|---|----------|------|------|
| DC-1 | `src/ProxyFinder_temp.cpp` | 86 | 备份/临时文件，与ProxyFinder.cpp重复内容 |
| DC-2 | `src/ProxyFinder_part1.cpp` | 86 | 碎片备份文件，与ProxyFinder.cpp重复 |
| DC-3 | `src/ProxyTester.cpp:9-10` | 2 | 空析构函数 `~ProxyTester()` |
| DC-4 | `src/ProxyFinder.cpp:366-368` | 3 | `removeProxyFromXray()` 仅含注释 `//暂时不清理` |

## 3. 重复逻辑 (Duplicated Logic)

| # | 重复位置 | 描述 |
|---|----------|------|
| DD-1 | `ConfigGenerator.cpp:52-80` + `SubitemUpdaterV2.cpp:51-65` | `isValidNetwork()` 功能重复，后者注释注明"mirrors ConfigGenerator::isValidNetwork" |
| DD-2 | `ShareLink.cpp:485-499` + `ProxyFinder.cpp:23-34` | 代理类型枚举转字符串映射重复 |
| DD-3 | `ProxyFinder.cpp:243` + `ProxyTester.cpp:19` | socks5代理URL构造重复 |

## 4. 冗余头文件 (Redundant Includes)

| # | 文件 | 行数 | 描述 |
|---|------|------|------|
| RI-1 | `include/ProxyFinder.h:5` | 1 | `#include <curl/curl.h>` 冗余，应使用CurlEasyHandle.h |
| RI-2 | `include/CurlEasyHandle.h:8` | 1 | `#include <utility>` 未使用 |

## 5. 过大函数 (Large Functions)

| # | 文件 | 函数 | 行数 | 建议拆分 |
|---|------|------|------|----------|
| LF-1 | `SubitemUpdaterV2.cpp` | `parseSubscription()` | ~400 | 拆分为 `parseVmess()`, `parseVless()`, `parseSs()` 等 |
| LF-2 | `AppController.cpp` | `doTestSubscription()`, `doFindFirstProxy()` | 50-80行 | 提取公共线程管理逻辑 |
| LF-3 | `ProxyBatchTester.cpp` | `workerThreadFunc()` | 175 | 拆分为 `prepareProxyConfig()`, `recordTestResult()` |
| LF-4 | `ConfigGenerator.cpp` | `buildStreamSettings()` | 153 | 按网络类型拆分 (tcp/ws/grpc/xhttp/kcp) |
| LF-5 | `ConfigReader.cpp` | `load()` | 481 | 按配置节提取解析方法 |

## 6. 职责超出 (Excessive Responsibility Files)

| # | 文件 | 行数 | 责任数量 |
|---|------|------|----------|
| ER-1 | `SubitemUpdaterV2.cpp` | 2438 | 订阅获取、代理解析(vmess/vless/ss/trojan/hysteria)、去重、数据库同步、Xray配置生成、URL获取 |
| ER-2 | `AppController.cpp` | 1004 | 配置管理、数据库切换、订阅操作、代理加载、测试、查找、导出、去重、同步、Xray配置生成、网络监控、工作线程编排 |
| ER-3 | `MainFrame.cpp` | 978 | UI构造、菜单/工具栏/状态栏、事件处理、网络监控、托盘图标、数据库切换 |

## 7. 重构建议

### 7.1 清理优先 (低风险)
1. 删除 `ProxyFinder_temp.cpp` 和 `ProxyFinder_part1.cpp`
2. 移除空析构函数和注释掉的代码
3. 清理冗余头文件

### 7.2 去重优化 (中风险)
1. 合并 `isValidNetwork()` 到共享工具
2. 合并代理类型字符串映射到单一位置
3. 提取公共URL构造助手

### 7.3 大型文件重构 (高风险)
1. `SubitemUpdaterV2.cpp` → `SubscriptionFetcher`, `ProxyParser`, `Deduplicator`, `DatabaseSync`
2. `ConfigGenerator.cpp` → 按网络类型拆分 `buildStreamSettings()`
3. 业务逻辑与UI逻辑解耦

## 8. 风险评估

| 级别 | 操作 | 风险 |
|------|------|------|
| 低 | 清理死代码、冗余头文件 | 无功能影响 |
| 中 | 逻辑去重 | 需要单元测试验证 |
| 高 | 大型文件重构 | 需完整集成测试 |