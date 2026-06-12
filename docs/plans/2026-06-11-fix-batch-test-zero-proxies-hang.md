---
title: "fix: ProxyBatchTester 代理数为 0 时 hang 退出问题"
type: fix
status: completed
date: 2026-06-11
---

# ProxyBatchTester 批量测试空代理集退出挂起修复

## 问题描述

当代理数量为 0 时，批量测试（`run()`/`runWithSubId()`/`runWithIndexId()`）会进入 hang 状态，无法正常退出。

## 根因分析

1. 调用方 `runWithSubId()`、`runWithIndexId()` 都有 `totalProxies_ == 0` 的 early return，并在 `run()` 中也已返回 `false`，这些路径正常。
2. 但代码中仍存在可间接触发 `testProxiesMultiThreaded()` 的路径（或维护性风险），一旦进入：
   - `startXrayInstances()` 仍会按实例启动（可能已开始）；
   - `testProxiesMultiThreaded()` 会创建 worker 线程并在上面 join；
   - 队列为空时 worker 线程不退出，join 最多阻塞 5 秒/线程后 detach；
   - 结果整体表现为“阶段性卡住”。

## 修复方案

在 `ProxyBatchTester::testProxiesMultiThreaded()` 入口增加 `totalProxies_ == 0` 的直接返回，避免创建空转线程。

## 验证步骤

- 空代理总数时调用 `run()` / `runWithSubId()` 立即返回 `false`，无线程残留；
- 正常 >0 代理数量时功能不受影响；
- cmake 编译通过。

## 修改点

- `src/ProxyBatchTester.cpp`
