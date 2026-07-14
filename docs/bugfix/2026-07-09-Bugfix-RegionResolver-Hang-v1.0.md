# Bugfix: RegionBatchResolver Hang ("假死") at Late Stage

**日期**: 2026-07-09
**版本**: 1.0

## 症状

批量地区解析操作在后期（~60-74 项处理后）进入"假死"状态：
- UI 停止响应进度更新
- 45 秒后用户强制关闭应用
- 日志中无 "Completed: X / 74 resolved" 输出
- 从 `AppController` 析构函数日志 (11:04:03) 确认 5s 线程 join 超时

## 日志分析

11:02:54 — 开始解析 74 个代理，4 个 worker
11:02:54~11:03:13 — 正常处理，DNS 解析 + cURL 请求 api.ipinfo.io
11:03:13 — 最后一条日志为 IP 地址 API 响应
11:03:13~11:03:58 — **45 秒完全静默**
11:03:58 — 用户强制关闭，触发 cancelTest → AppController 析构

## 根因分析

### 根因 1: `getaddrinfo()` 无限阻塞

`DnsCache::resolve()` 直接调用 `getaddrinfo()`，该调用在 DNS 服务器不可达时可能阻塞 20-60 秒。无超时机制。4 个 worker 处理 55+ 个域名时，最坏情况总阻塞时间可达：
```
(55 域名 × 20s 超时) / 4 workers ≈ 275 秒 → 远超用户等待阈值
```

### 根因 2: 缓冲互斥锁在 SQLite 期间未释放

`flushRegionBuffer()` 在持有 `bufferMutex_` 期间执行 SQLite `BEGIN/INSERT/COMMIT`：
- Worker 1 累积满 50 项 → 持锁执行 SQLite（~100-500ms）
- Worker 2/3/4 等待 `bufferMutex_` → 全部串行化
- `BATCH_FLUSH_SIZE=50` 只触发一次，但该次阻塞决定了响应性

### 根因 3: Worker 退出无日志

Worker 线程完成队列处理时无声退出（`break` 无日志），使得诊断冻结问题变得困难。

## 修改内容

### `src/utils/DnsCache.cpp`

1. **DNS 超时** — 使用 `std::async` + `wait_for(10s)` 包装 `getaddrinfo()`
2. **线程安全的 WSAStartup** — 使用 `std::call_once` 替代静态 bool（修复多线程竞态条件）
3. **DNS 结果日志** — 成功（`DEBUG` 级别）/ 超时（`WARN` 级别）

### `src/RegionBatchResolver.cpp`

1. **缓冲刷新解耦** — `needFlush` 标志在锁内判断，`flushRegionBuffer()` 在锁外调用
2. **消除竞态条件** — `flushRegionBuffer()` 中 `regionBuffer_.empty()` 检查移入互斥锁内
3. **Worker 退出日志** — 添加 "Worker X exiting (cancelled/queue empty)" 日志（`DEBUG` 级别）
4. **刷新日志增强** — `flushBuffer` 日志输出批量大小及缓冲中剩余项数（`INFO` 级别）

## 验证

-   18/18 ctest 全部通过
-   编译无警告
-   LSP 诊断无错误

## 涉及文件

| 文件 | 修改说明 |
|------|----------|
| `src/utils/DnsCache.cpp` | DNS 超时机制 + WSAStartup 线程安全 |
| `src/RegionBatchResolver.cpp` | 缓冲互斥锁优化 + Worker 日志增强 |
