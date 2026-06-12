# 2026-06-12-Bugfix-ProxyBatchTester-Worker0-JoinTimeout-v1.0.md

## 摘要

修复批量测试中首条代理（Worker-0）始终触发 "Wait timeout, detaching thread" WARN 告警的问题。

## 问题描述

- 每次批量测试，第一条代理（indexid=5841116306214803185, snapp.ir:80）输出警告：
  `[Worker-0][WARN] Wait timeout, detaching thread to prevent hang`
- 后续所有 Worker 无此告警。
- `src/ProxyBatchTester.cpp:290` 的 join 超时硬编码为 `5000ms`。

## 根因分析

Worker 线程的 join 是**顺序执行**的，Worker-0 最先被 join，具备最短的 wall-clock 运行时间。而每个代理完成所需的流程包括：

1. removeOutbound × 2 及中间 sleep（~500ms）
2. addOutbound 及重试机制（~1-2s）
3. sleep 300ms
4. `proxyTester_->test()` — cURL 测试耗时可达 `test_timeout_ms`（默认 5000ms）

合计最长可达 **7-8s** >> 硬编码的 5s 超时。Worker-1 及后续线程在 main thread 等待 Worker-0 的 5s 期间已继续执行，因此不会超时。

## 修复方案

将 join 超时改为动态计算：`test_timeout_ms + 5000ms`（多出的 5000ms 覆盖 API 调用与 sleep 开销）。

```cpp
// Before:
if (fut.wait_for(std::chrono::milliseconds(5000)) != std::future_status::ready) {

// After:
int joinTimeoutMs = config_.test_timeout_ms + 5000;
if (fut.wait_for(std::chrono::milliseconds(joinTimeoutMs)) != std::future_status::ready) {
```

## 覆盖范围

- `src/ProxyBatchTester.cpp:290` — `testProxiesMultiThreaded()` 的 join 超时计算。

## 影响分析

- 正: 首条代理不再误超时，批量测试行为一致。
- 副: 若 `test_timeout_ms` 被设为极大值（如 120s），join 等待将相应延长。但 cURL 自身已受 `test_timeout_ms` 约束，不会额外挂起。

## QA

- Debug 编译无告警；
- 全量 8 项 CTest 通过（100%）；
- 后续代理流程不受影响。

## 记录时间

2026-06-12
