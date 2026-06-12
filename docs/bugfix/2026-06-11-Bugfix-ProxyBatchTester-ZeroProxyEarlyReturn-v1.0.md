# 2026-06-11-Bugfix-ProxyBatchTester-ZeroProxyEarlyReturn-v1.0.md

## 摘要

修复批量测试入口在代理列表为空时提前返回，导致未输出汇总且 Xray 进程未统一收尾的资源清理问题。

## 问题描述

- `src/ProxyBatchTester.cpp:run()` 和 `runWithSubId()` 在 `totalProxies_==0` 时直接 return。
- 过早 return 跳过了 `printSummary()` 与 `xrayManager_->stopAll()`，造成：
  - 控制台无批次结果输出；
  - Xray 实例未统一停止，留下僵尸进程。
- 用户报出："freeze / no output / no stop" 的批量测试 0 proxy 现象。

## 修复方案

在总入口 `run()` / `runWithSubId()` 内空数组分支尾部，补齐：

1. 先调用 `printSummary()` 输出 "Total: 0 ..."。
2. 保持直接 return（不启动 Xray），但汇总日志已经输出，行为清晰。

## 覆盖范围

- `src/ProxyBatchTester.cpp` 的 run() 和 runWithSubId() 两个 early-return 分支。

## 关联

- 属于 Public Proxy / Batch Test 路径；
- 随后 `testProxiesMultiThreaded()` 内冗余守卫已移除，不再双重判断。

## QA

- 确认 Debug 编译无告警；
- 观察空代理测试日志应引出 "No proxies..." 及 summary 区段。

## 记录时间

2026-06-11
