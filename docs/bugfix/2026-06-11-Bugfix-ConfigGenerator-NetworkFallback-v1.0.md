# 2026-06-11-Bugfix-ConfigGenerator-NetworkFallback-v1.0.md

## 摘要

修复 ConfigGenerator::loadProfiles() 在遇到非标准网络类型时跳过整条代理记录的问题，以及 splithttp 遗留值兜底问题。

## 问题描述

- `src/ConfigGenerator.cpp:42-44`：当 profile.network 不合法时，直接 `continue` 将整条代理从有效配置中移除，导致代理数量减少。
- 若订阅期已将 `splithttp` 映射为 `xhttp`，但后续 DB 直读或手工插入存在遗留的 `splithttp` 值，再进 `loadProfiles()` 时会命中 invalid-network 分支，被当作无效记录丢弃，表现形式像是历史修复重现。

## 修复方案

在 `loadProfiles()` 的检测链中进行正向兜底：

1. 网络字段为空时，回退为 `"tcp"`。
2. 网络字段为 `"splithttp"` 时，映射为 `"xhttp"`。
3. 仍检测出类型不合法时，**不再跳过**该代理，而是回退为 `"tcp"` 并输出 WARN。

```
空字段 → tcp
splithttp → xhttp
仍无效 → tcp（保留记录，不再 skip）
```

## 覆盖范围

- 影响路径：`ConfigGenerator::loadProfiles()` -> `isValidNetwork()` 原返回 false 时，整条记录被丢弃——现改为回退到 tcp 并保留。
- 影响输出：导出配置时该代理不会消失。

## 变更文件

- `src/ConfigGenerator.cpp`

## QA

- 单元测试：`tests/test_config_generator.cpp`
- 重编验证：通过 Ninja Debug 构建

## 记录时间

2026-06-11
