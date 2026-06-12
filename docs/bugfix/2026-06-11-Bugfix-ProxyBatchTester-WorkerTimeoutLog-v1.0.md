# 2026-06-11-Bugfix-ProxyBatchTester-WorkerTimeoutLog-v1.0.md

## 摘要

批量测试超时等待日志增加 worker ID 与代理信息（indexid / address / port），将原模糊的 timeout 提示改为可定位的精确诊断信息。

## 问题描述

- `ProxyBatchTester::testProxiesMultiThreaded()` 在 5 秒 join 超时后，日志只输出 `[WARN] [ProxyBatchTester] Wait timeout, detaching thread to prevent hang`。
- 无法直接看出是哪一个 worker（Worker-1 / Worker-2 ...）超时，也无法看到该 worker 当前正在测哪个代理，排查残留数量偏差时需要人工翻代码。
- 属于可观测性缺口，不影响功能，但严重影响运维排障效率。

## 修复方案

1. 增加 `std::vector<int> workerCurrentProxyIndex_`，在每个 `workerThreadFunc()` 进入测试前、结束后分别更新为当前 proxy index。
2. 5 秒超时分支读取该 worker 的 `workerCurrentProxyIndex_[idx]` 对应到 `proxies_[]`，拼出 `indexid=xxx addr:port` 字符串拼进日志前缀。
3. 加 `workerStateMutex_` 保护 `workerCurrentProxyIndex_`，避免与 worker 线程自更新串行化竞争。

输出示例（修复后）：
```
[Worker-2][WARN] Wait timeout, detaching thread to prevent hang - indexid=abc123-01 1.2.3.4:1080
```

## 覆盖范围

- `src/ProxyBatchTester.cpp` 中 `testProxiesMultiThreaded()` 的超时分支与 `workerThreadFunc()` 的 proxy index 记录点。

## 修复细节

- 超时日志前缀：`"[Worker-" + idx + "][WARN] Wait timeout, detaching thread to prevent hang - " + proxyTag`
- `proxyTag` 规则：index valid 时输出 `indexid + space + address + ":" + port`，否则为 `[unknown proxy]`

## 关联

- 与零代理 early-return 修复同属 2026-06-11 Batch Test 稳定化系列。

## QA

- 构造一个占用 port 的卡死代理，跑单批次测试，验证 5s 后出现带 Worker # + addr 的 WARN。
- 确认 detach 会适当释放队列计数，后续批次不受影响。

## 记录时间

2026-06-11
