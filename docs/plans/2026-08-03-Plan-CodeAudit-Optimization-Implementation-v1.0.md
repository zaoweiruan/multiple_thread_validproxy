---
title: "refactor: XrayApi / ProxyBatchTester 代码审查优化实施计划"
type: refactor
status: terminated
date: 2026-08-03
origin: "docs/reports/2026-08-03-Report-CodeAudit-Optimization-v1.0.md"
---

# 代码审查优化实施计划：XrayApi / ProxyBatchTester / 支撑模块

> **状态**: ❌ terminated（2026-08-07 终止关闭）
> **关闭记录** (2026-08-07)：Phase 1-4（T1.1–T4.16，A1-A6/B1-B14/C1-C9/D1-D8/E1-E14）已全部落地，21/21 ctest 通过（见 `docs/plans/project-plans-tracker.md` 2026-08-04 记录）；Phase 5 规范清理尾项未完成——T5.1 全仓 `auto` 验收未达标（`src`/`include` 残留 15 处：DnsCache.cpp:44,55 / PortManager.cpp:68 / AppController.cpp:502,731,741 / AutoTaskManager.cpp:554,555 / RegionBatchResolver.cpp:51,107,261,331,388 / ToolbarIcons.h:185 / SubscriptionPanel.cpp:181；触碰文件部分 XrayApi.cpp 10 处 + CurlEasyHandle.h L149 已清理完毕），T5.2 日志等级复核无落地记录；剩余项按 §2 范围边界顺延、另行立项。
> **来源**: `docs/reports/2026-08-03-Report-CodeAudit-Optimization-v1.0.md`（只读审查报告，本计划将报告中的 6 HIGH + 14 MED 正确性 + 9 资源泄漏 + 8 性能 + 14 错误处理空洞转化为可执行任务）
> **约束**: C++17 / MinGW-GCC / Windows；全栈禁止 `auto` 类型推导；`Logger::write` 必须显式 `LogLevel`；所有变更文档先行、测试落 `tests/`

---

## 1. 问题描述

`docs/reports/2026-08-03-Report-CodeAudit-Optimization-v1.0.md` 通过三路并行审计，在批量测试核心链路（`ProxyBatchTester` → `XrayApi`/`ProxyTester`/`XrayManager`）发现：

- **6 个 HIGH 级崩溃/UB**：detach 后 UAF（A1）、gRPC status 从不解析导致错误当成功（A2）、protobuf 编码错位致 TLS/传输层静默丢失（A3）、XrayInstance 孤儿进程 + 句柄泄漏 + stop 竞态（A4）、PortManager/XrayManager 无锁并发（A5）、`lastResult_` 数据竞争（A6）。
- **14 个 MED 正确性缺陷**（B1–B14）与 **9 项资源泄漏**（C1–C9）、**8 项性能问题**（D1–D8）、**14 项错误处理空洞**（E1–E14）。

其中 A3 的编码错误已被 `tests/test_xray_api_direct.cpp` 固化——**测试通过反而掩盖 bug**，修复时必须同步修正测试期望字节。

本计划将上述发现按依赖关系拆分为 **5 个阶段、26 个可独立验收的任务**，供评审后按批次实施。

## 2. 范围边界

**修改（涉及文件）**：

| 文件 | 阶段 |
|---|---|
| `src/ProxyBatchTester.cpp` + `include/ProxyBatchTester.h` | 1、2、3、4、5 |
| `src/XrayApi.cpp` + `include/XrayApi.h` | 1、2、3、4、5 |
| `src/XrayInstance.cpp` + `include/XrayInstance.h` | 1、2、4 |
| `src/PortManager.cpp` + `include/PortManager.h` | 1、3、4 |
| `src/XrayManager.cpp` + `include/XrayManager.h` | 1、3、4 |
| `include/CurlEasyHandle.h` | 3、4、5 |
| `include/TestResultQueue.h` | 2、4 |
| `src/UrlFetcher.cpp` + `include/UrlFetcher.h` | 4 |
| `src/ProxyTester.cpp` + `include/ProxyTester.h` | 4 |
| `src/ConfigGenerator.cpp`（配合 E7，仅读取侧） | 2 |
| `tests/test_xray_api_direct.cpp` | 1（同步 A3 测试期望）、2 |
| `tests/`（新增测试） | 各阶段 |
| `docs/INDEX.md`、`docs/plans/project-plans-tracker.md` | 文档登记 |

**NOT 修改**：
- 不改变 CLI/GUI 入口、数据库 schema、订阅解析/去重/同步业务。
- 不做风格类重构（命名/格式化），仅清理触碰文件内的 `auto` 违规（T5.1）。
- 不引入新依赖库（不新增 curl_multi、不引入 gRPC C++ 库——维持手写 HTTP/2 帧协议）。
- 不改变 `XrayApi` 公开接口签名（保持 `addOutbound`/`addOutboundDirect` 等兼容）。

## 3. 批次划分与依赖关系

```
Phase 1 崩溃/UB (T1.1–T1.6)  ← 地基，全部后续依赖
   │
   ├─► Phase 2 正确性+数据完整性 (T2.1–T2.11)  ← 依赖 T1.1 的线程模型
   │         │
   │         └─► Phase 4 (T4.12 E7 依赖 T2.1 的 runInternal 统一)
   │
   ├─► Phase 3 性能 (T3.1–T3.5)  ← T3.1/T3.2 依赖 T1.1/T1.4 的生命周期；
   │         T3.3/T3.4 依赖 T1.5 的锁模型
   │
   └─► Phase 4 低危收尾 (T4.1–T4.16)  ← 大部分独立，可并行
                    │
                    └─► Phase 5 规范约束 (T5.1–T5.2)  ← 收尾统一清理
```

**依赖要点**：
- T1.1（线程生命周期）优先于一切涉及 worker/join/计数器的任务（T2.1/T2.9/T3.1/T4.3/T4.15）。
- T1.3（protobuf 编码）必须与测试期望修正同批提交，否则测试继续固化错误字节。
- T2.1（runInternal 提取）合并解决 B1/D7/E13，避免同一代码区域被改两次。
- T3.2（端口就绪轮询）依赖 T1.4 的 `isRunning()`/进程存活验证语义。

## 4. 详细任务

> 每个任务含：**来源**（报告编号）、**位置**（核实后的文件:行号）、**问题**、**变更**、**验收**。

---

### Phase 1 — 崩溃/UB 修复（HIGH，必须优先）

#### T1.1 [A1] ProxyBatchTester 线程生命周期重构（废弃 async+detach）
- **位置**: `src/ProxyBatchTester.cpp` L43-49、L375-404、L35-57（析构）；`include/ProxyBatchTester.h` L74
- **问题**: `std::async` 限时 join 与主线程 `detach()` 竞争同一 `std::thread` 对象（任一路径抛异常 → terminate）；`fut.wait_for` 无效（future 析构阻塞）；detach 后 worker 仍访问 `proxyTester_`/`resultQueue_`/`proxies_`/`preGenConfigs_`，主线程随后 `stopAll()`/`delete proxyTester_` 构成 UAF；GUI 退出时 `curl_global_cleanup` 与分离线程并发即崩溃。
- **变更**:
  1. 删除 `std::async`+`detach` 模式；`workerThreadFunc` 循环内改为 `std::condition_variable` + `cancelRequested_` 协作退出。
  2. 主线程对 `workerThreads_` **无条件 join**；curl 阻塞调用经 `CurlEasyHandle` 的取消回调 + `CURLOPT_TIMEOUT_MS` 保证有界（配合 T3.5/T4.9）。
  3. 生命周期顺序固化：先 join 全部 worker → `resultQueue_.stop()` → `proxyTester_` 释放 → `stopAll()`。析构路径同样满足该顺序。
  4. `proxyTester_` 改 `std::shared_ptr<ProxyTester>`（或成员值对象）持有，杜绝析构时序依赖。
  5. 取消语义统一：`proxyTester_->test()` 同时接收 `externalCancel_`（当前只传 `&cancelRequested_`，外部取消在阻塞 curl 期间无效）。
- **验收**: 全量测试通过；ASAN 构建下运行批量测试无 use-after-free/数据竞争报告；GUI 关闭与批量测试并发不再崩溃；取消后主线程能在 ≤ joinTimeout 内返回。

#### T1.2 [A2] gRPC trailers grpc-status 解析
- **位置**: `src/XrayApi.cpp` L1310-1396（`grpcSendReceive`），波及三个 Direct 方法 L1401-1604
- **问题**: trailers 帧（L1373-1376）只检查 `END_STREAM`，从不解析 HPACK 载荷中的 `grpc-status`/`grpc-message`；EOF 时响应非空即 `return true`（L1321、L1346）。Xray 拒绝 AddOutbound 时经 trailers 返回非 0 status，代码却上报成功 → 重试循环永不触发、脏状态注入。
- **变更**: 解析 trailers 帧 HPACK 内容提取 `grpc-status`（非 0 视为失败，回填 `lastError_` 含 `grpc-message`）；EOF 且未收到 END_STREAM/完整消息时不得返回 true。
- **验收**: 新增单元测试：构造带 `grpc-status: 13`（INTERNAL）trailers 的响应，断言返回失败且 `lastError_` 含原因；现有 Direct 方法测试全绿。

#### T1.3 [A3] protobuf 编码修正 + 测试期望同步
- **位置**: `src/XrayApi.cpp` L999-1000（`security_type` 应为 varint 枚举却发字符串）、L986-989（`TransportConfig.protocol_name` 应为字段 1，却塞进字段 3）；`tests/test_xray_api_direct.cpp` L553-555、L570-572、L582-611（错误字节被固化为期望）
- **问题**: `security=="tls"` 被 `encodeString(3, "tls")` 发送 → wire type 不匹配，protobuf 当未知字段跳过 → **TLS 代理直连裸 TCP 必失败**；`protocolName`（"websocket" 等）塞进字段 3（repeated TypedMessage）→ **ws/grpc/kcp/http 传输配置静默丢失**。
- **变更**: 1) `security` 映射枚举值：`tls→2`、`reality→3`，用 `encodeVarintField(3, n)`；2) `transportConfig += encodeString(1, protocolName)`，删除字段 3 裸字符串；3) 同步修正测试期望字节——从"匹配当前输出"改为"匹配 Xray proto 规范"（`Xray-core/common/protocol` 与 `transport/internet/transport/config.proto`）。
- **验收**: 修正后测试断言逐字段核对：`security_type` 字段号 3 为 varint 值 2/3；`TransportConfig` 字段 1 为 protocol_name 字面量；54/54 测试通过；**必须同步比对 Xray proto 源码字段号**（`E:\eclipse_workspace\Xray-core`）。

#### T1.4 [A4] XrayInstance 进程生命周期加固
- **位置**: `src/XrayInstance.cpp` L67-74（AssignProcessToJobObject 失败）、L85-122（stop 无互斥 + 超时无 TerminateProcess）
- **问题**: 1) `CREATE_SUSPENDED` 子进程在 Assign 失败后从未 Terminate → 永久悬挂孤儿，`jobObject_` 泄漏；2) `stop()` 双线程并发双重 CloseHandle；3) `TerminateJobObject`+1s 等待后仍 `STILL_ACTIVE` 时从不 `TerminateProcess` → 进程存活、端口仍占，而 `stopAll` 已清端口追踪。
- **变更**: 1) Assign 失败路径：先 `TerminateProcess(pi.hProcess, 1)`，再关闭 `pi.hProcess`/`pi.hThread` 与 `jobObject_` 并置空；2) `start()/stop()` 共用 `std::mutex`；3) stop 超时后显式 `TerminateProcess(processHandle_, 1)` 再 `WaitForSingleObject`；4) `running_` 仅在确认进程退出/成功拉起后置位。
- **验收**: 模拟 Assign 失败（注入错误）断言无残留进程与句柄；并发 stop 压测（100 次双线程）无崩溃无句柄泄漏；ASAN 干净。

#### T1.5 [A5] PortManager / XrayManager 并发加锁
- **位置**: `src/PortManager.cpp` L6-25（`usedPorts_` 裸 vector）；`src/XrayManager.cpp` L51-110（`instances_`）
- **问题**: `findAvailable` 无同步 → push_back 与遍历并发迭代器失效；双线程可能返回同一端口。`instances_` 的 push_back/遍历/clear 无 mutex → 双 start 竞态、UI 轮询 `isRunning()` 与 `stopAll()` 并发读清空中的 vector。
- **变更**: `usedPorts_` 加 `std::mutex`，检查+登记在同一临界区；`instances_` 加专用 mutex（或复用 `instanceMutex_`），start 内对已运行实例去重/拒绝；`isRunning()`/`getInstanceCount()`/`getPortPairs()` 读路径同样加锁。
- **验收**: 新增并发单测：16 线程同时 `findAvailable` 500 次，断言无重复端口、无崩溃；`XrayManager` 并发 start/stopAll 压测通过。

#### T1.6 [A6] `lastResult_` / `lastIndexId_` 同步化
- **位置**: `src/ProxyBatchTester.cpp` L170-172、L271-272、L291-292、L320、L332-333；`include/ProxyBatchTester.h` L32（`getLastResult()` 无锁）
- **问题**: 最多 16 worker 并发写非原子成员，主线程无锁读 → 标准数据竞争。
- **变更**: 写入与 `getLastResult()` 均加 `workerStateMutex_`（拷贝返回）；或将 `lastResult_` 改 `std::atomic<std::shared_ptr<TestResult>>`。`lastIndexId_` 同批加锁或仅在主线程语义下明确文档化。
- **验收**: TSAN/ASAN 并发运行无数据竞争报告；`getLastResult()` 返回值为完整对象（拷贝语义）。

---

### Phase 2 — 正确性 + 数据完整性

#### T2.1 [B1 + D7 + E13] 三入口统一重构：runInternal + 状态重置
- **位置**: `src/ProxyBatchTester.cpp` L431-583（三入口）、L346-348（队列入队）、L17（构造清零）、L453-470/505-522/559-576（三段重复预生成）
- **问题**: 同一对象重复 `run()` 状态不重置（残留索引错位）；三个入口 + 预生成代码逐字重复三份，已现行为分叉（空结果 `run()` 返回 true vs `runWithSubId()` 返回 false）；取消提前 return 不计失败不入队导致对账失真。
- **变更**:
  1. 提取私有 `runInternal(const std::vector<db::models::Profileitem>& proxies)` + `preGenerateConfigs()`；三入口只负责数据准备（loadProxies / DAO 过滤）与返回值语义。
  2. `runInternal` 开头统一重置：清空 `proxiesQueue_`、清零 `successCount_/failedCount_/processedCount_`、清空 `workerCurrentProxyIndex_`、清空 `lastResult_`。
  3. 空结果语义统一为返回 false 并写 `lastResult_.errorMsg`（与现有 `runWithSubId` 对齐）；消除 L489-493 三重日志。
  4. 取消/异常提前返回路径统一走"计数失败 + 入队 + 写 lastResult_"收尾（E13）。
- **验收**: 新增单测：同一实例连续 run 两次（第二次代理集合不同）断言统计与结果无残留；三个入口空集合返回语义一致；日志无重复。

#### T2.2 [B2] preGenConfigs_ / proxies_ 索引绑定
- **位置**: `src/ProxyBatchTester.cpp` L140-144（只校验 `proxies_.size()`）、L176（直接下标 `preGenConfigs_[profileIdx]`）
- **问题**: 双向量分别构建存在潜伏越界；重复 run（T2.1 前）会错位。
- **变更**: 绑定单一结构体 `struct ProxyWorkItem { db::models::Profileitem proxy; config::XrayConfig config; };` 替代两个平行 vector；或边界双查（`profileIdx < preGenConfigs_.size()`）。
- **验收**: 越界注入测试（构造 `preGenConfigs_` 小于 `proxies_`）不崩溃、有防御日志。

#### T2.3 [B3] XrayInstance start 存活验证
- **位置**: `src/XrayInstance.cpp` L76-81（固定 sleep 2s 即置 running_）
- **问题**: config 错时进程秒退，2s 后仍报成功；stop 在 2s 窗口内执行 → 孤儿进程 + 状态机错乱。
- **变更**: `ResumeThread` 后用 `WaitForSingleObject(processHandle_, 0)` / `GetExitCodeProcess` 验证存活（配合 T3.2 的 gRPC 端口就绪轮询，替代裸 sleep）；start/stop 共用状态 mutex（承接 T1.4）。
- **验收**: 注入错误 config（非法路径）断言 start 返回 false 且无孤儿进程；`running_` 与进程实际状态一致。

#### T2.4 [B4 + B9] parseServerAddr / grpcConnect 加固
- **位置**: `src/XrayApi.cpp` L1157-1181（端口解析）、L1183-1217（仅 AF_INET + inet_pton）
- **问题**: hostname（"localhost"）与 IPv6（`[::1]` 带方括号）永远连不上；`std::stoi("8080abc")` 吞尾缀、0/负/>65535 不校验 → `htons` 截断。
- **变更**: `getaddrinfo(AF_UNSPEC)` + 逐地址尝试；IPv6 去方括号；stoi 后校验剩余字符全数字且范围 `1..65535`。
- **验收**: 新增单测：`parseServerAddr` 对 `localhost:8080`、`[::1]:8080`、`1.2.3.4:8080`、`x:8080abc`、`x:0`、`x:70000` 的解析结果；`grpcConnect` 对 hostname 连接成功。

#### T2.5 [B5 + B14] servers 缺失 / 类型守卫
- **位置**: `src/XrayApi.cpp` L495、L497、L524、L1487-1489
- **问题**: 服务端类协议 servers 缺失/数组为空 → 空 protobuf 静默注入（freedom/blackhole 空结果合法，二者不可区分）；`servers[0].as_object()` 对非 object 元素抛 `boost::json::system_error`，L1489 调用处无 try/catch → 绕过重试与回退。
- **变更**: 协议校验改为 out-param 失败标志（或 `std::optional`）；类型守卫 `is_object()` + 失败置标志走 subprocess 回退。
- **验收**: 新增单测：servers 缺失、空数组、非 object 元素三种输入均返回失败且回退路径被调用。

#### T2.6 [B6 + B7] recv 循环加固（上限校验 + 跨帧重组）
- **位置**: `src/XrayApi.cpp` L1339-1368（flen/msgLen 解析）、L1369（response 被覆盖）
- **问题**: 服务端可控 `flen` 无上限（resize 抛 bad_alloc 无 catch → 崩溃）；`msgLen` 可宣称 4GB；消息跨多 DATA 帧时每帧覆盖 response → 大响应只剩最后一帧或全丢。
- **变更**: `flen` 上限 65536 超限断开；`msgLen` 与 `payload.size()-5` 严格相等校验（非法即失败）；按 `msgLen` 累加分片至收满再解析（引入 per-message 累积 buffer）。
- **验收**: 新增单测：模拟 3 帧分片传输 2KB 响应，断言重组完整；模拟 `flen=0xFFFFFF` / `msgLen=4GB` 断言快速失败不崩溃。

#### T2.7 [B8] HPACK 表修正
- **位置**: `src/XrayApi.cpp` L1078-1082（NAME_INDEX）、L1108-1118
- **问题**: 索引 57 是 `transfer-encoding` 而非 `te`；62/63（grpc-timeout/grpc-encoding）超出 61 条静态表范围 → 请求头实为 `transfer-encoding: trailers` 且缺 gRPC 强制的 `te: trailers`。
- **变更**: `te` 走完整字面量（`0x00 + varint(len) + "te"`），从 NAME_INDEX 删除 57/62/63；同时处理值长度 ≥127 的 Huffman 标志位隐患（值 >127 时按字面量长度分两段或改用非增量编码）。
- **验收**: 新增单测断言 `encodeHpack("te","trailers")` 输出 `00 02 74 65 08 74 72 61 69 6c 65 72 73`；gRPC 直连实测（wireshark 或 Xray 日志确认 `te: trailers`）。

#### T2.8 [B10] subprocess ReadFile 读超时
- **位置**: `src/XrayApi.cpp` L100-108（`runProcess` 读循环）
- **问题**: 子进程挂住不关 stdout 时"读空死等"，5s WFO 永不执行。
- **变更**: 读循环加 deadline（`PeekNamedPipe` 非阻塞读，或独立读线程 + `WaitForSingleObject(timeout)`），超时 `TerminateProcess`。
- **验收**: 注入挂死子进程（脚本 sleep 300s 不关句柄）断言 `runProcess` 在限定时间内返回失败。

#### T2.9 [B11] worker 兜底异常捕获
- **位置**: `src/ProxyBatchTester.cpp` L322-334（仅 catch `std::exception`）
- **问题**: `bad_alloc`/SEH 逃出线程 → terminate。
- **变更**: worker 主循环增加 `catch (...)` 兜底：计数失败 + 入队 + 日志 + 尝试一次 `removeOutboundDirect(tag)` 清理。
- **验收**: 注入 `throw 42` 于 worker 内部路径，断言不崩溃且失败被记录。

#### T2.10 [B12 + B13] tls.Config 字段号复核 + xhttp/splithttp 传输编码
- **位置**: `src/XrayApi.cpp` L721-766（潜伏）、L935-991（传输配置）；`config/StreamSettingsBuilder.cpp` L18-69
- **问题**: tls.Config min/maxVersion 字段号需对照 proto 复核（当前 builder 不产出故潜伏）；xhttp 无编码分支、splithttp 只写 protocol_name、settings 全丢 → 传输静默退化裸 TCP。
- **变更**: 对照 `E:\eclipse_workspace\Xray-core\transport\internet\tls\config.proto` 核实全部字段号；补 xhttp/splithttp settings 编码；无法编码的协议强制走 subprocess 回退（不静默降级）。
- **验收**: 新增 xhttp/splithttp 编码单测；tls 字段号与 proto 源码逐一比对记录。

#### T2.11 [E1 + E2] TestResultQueue flush 可靠性
- **位置**: `include/TestResultQueue.h` L88-92（回调返回值忽略）、L107-118（flushBatch 同）、L76-105（flush 线程无 try/catch）
- **问题**: flush 回调（`dao.updateTestResultBatch`，DB busy 返回 false）返回值在 flush 线程、`stop()` 排空、`flushBatch()` 三处全部忽略 → 一批 50 条结果永久丢失；回调异常逃出线程 → terminate。
- **变更**: 1) 回调返回 false 时保留 batch 带退避重试（上限后落盘日志 + ERR 记录 indexid 列表）；2) 回调包 `try/catch(...)`，catch 内保留 batch 重试 + 日志，绝不外抛；3) 队列满提供背压（可配置最大缓冲，超限直接降级为同步写）。
- **验收**: 新增单测：回调连续返回 false → batch 不丢、重试 N 次后落日志；回调抛异常 → 不 terminate、数据保留。

---

### Phase 3 — 性能优化

#### T3.1 [D1 + D2] 固定睡眠删除 + 条件退避
- **位置**: `src/ProxyBatchTester.cpp` L276-281（10×10ms=100ms 空转）、L208-212（成功 2×10ms）、L233-237（重试 5×10ms）
- **问题**: 每代理固定 100ms 空转（53,837 代理/16 worker ≈ 5.6 分钟/轮）；成功路径无条件睡眠按代理累积。
- **变更**: 删除 100ms 空转（取消响应由 L284 + curl 超时保证）；成功路径零睡眠；仅 gRPC 返回"未就绪"类错误时指数退避（10ms→50ms→200ms）。
- **验收**: 同数据集批量测试墙钟时间不增反降（对照 `docs/reports/2026-07-31` 的 88s 基线）；取消响应时间无明显劣化。

#### T3.2 [D3] gRPC 端口就绪轮询替代固定 sleep + 实例复用
- **位置**: `src/XrayInstance.cpp` L79（2s 固定 sleep）、`src/XrayManager.cpp` L74（500ms sleep）、`src/ProxyBatchTester.cpp` L107-108（warmup 2s）
- **问题**: 16 实例 ≈ 40s 纯等待；每次 `run()` 全量重启重复该延迟。
- **变更**: 启动后轮询 gRPC api 端口就绪（`grpcConnect` 尝试 + 短超时，上限 5s）替代固定 sleep；`startXrayInstances` 前检查 `xrayManager_` 现有实例 `isRunning()`，已运行实例复用不重启。
- **验收**: 实测 16 实例启动耗时从 ~40s 降至 ≤8s；`run()` 连续调用第二次不重建已运行实例。

#### T3.3 [D4] PortManager 数据结构优化
- **位置**: `src/PortManager.cpp` L6-25（线性遍历 O(n²)）、回绕后重扫已检区间
- **变更**: `usedPorts_` 改 `std::unordered_set<int>`（配合 T1.5 的 mutex）；回绕后跳过已检区间。
- **验收**: 端口分配压测（10 万次 findAvailable）耗时显著下降；无重复端口。

#### T3.4 [D5 + D6] connect / recv 超时加固
- **位置**: `src/XrayApi.cpp` L1183-1217（connect 无超时，不可达地址最长 ~21s）、L1310-1396（每次 recv 粒度 5s × MAX_FRAMES=200）
- **问题**: 阻塞 connect 不受 `SO_RCVTIMEO` 影响；整次调用最坏 200×5s；SETTINGS ACK send 失败被忽略、RST_STREAM/PING 未处理。
- **变更**: connect 前设 `SO_SNDTIMEO` 或非阻塞 connect + select（3-5s）；整次调用设绝对 deadline；收到 RST_STREAM 立即失败；SETTINGS ACK send 失败记为错误。
- **验收**: 不可达地址单次调用 ≤5s 返回；注入 RST_STREAM 断言立即失败。

#### T3.5 [D8] curl connect 超时调小
- **位置**: `include/CurlEasyHandle.h` L104-120（取消仅依赖 XFERINFO 回调，卡 connect/DNS 时无回调）
- **变更**: `perform()` 内显式设 `CURLOPT_CONNECTTIMEOUT_MS` 为 `min(timeoutMs_, 3000)`；DNS 卡顿场景取消延迟 = connect 超时。
- **验收**: 模拟不可达 IP + 取消 → 3s 内返回；现有 CurlEasyHandleTest 全绿。

---

### Phase 4 — 低危收尾（资源、错误语义、日志）

#### T4.1 [C1 + E10] removeOutbound subprocess 退出码校验
- **位置**: `src/XrayApi.cpp` L200-216（CreateProcessA 成功即 SUCCESS）、L151（addOutbound 凭输出含 "adding" 判成功）
- **变更**: `removeOutbound` 复用 `runProcess` 式管道捕获，检查退出码与 WFO 结果，失败写 `lastError_`；`addOutbound` 以退出码为准（"adding" 仅作已知场景特判）。
- **验收**: 注入子进程返回非 0 退出码，断言方法返回 false 且 `lastError_` 非空。

#### T4.2 [C2] 删除冗余确认进程与无谓 sleep
- **位置**: `src/XrayApi.cpp` L143-172（addOutbound 额外起 lso 确认进程 + 200ms sleep）
- **变更**: 删除确认进程与 200ms sleep；行为由退出码 + 输出判定取代。
- **验收**: subprocess 回退路径进程创建次数从最多 4 次降至 ≤2 次；功能回归测试通过。

#### T4.3 [C3 + C4] stopAll RAII 保证 + 析构幂等
- **位置**: `src/ProxyBatchTester.cpp` L351-358/L475/527/581（异常路径不执行 stopAll）、L35-57（析构不负责 stopAll）
- **变更**: 用 RAII scope guard 保证 `testProxiesMultiThreaded` 任何退出路径执行 `stopAll()`（含线程创建失败/resultQueue_.start 抛异常）；析构中（线程安全退出后）幂等 `stopAll()` 兜底。
- **验收**: 注入线程创建失败异常，断言 xray 进程与端口全部释放；析构后 `Get-Process xray` 无残留。

#### T4.4 [C5] PortManager::freePort 新增 + start 失败归还
- **位置**: `src/PortManager.cpp` + `src/XrayManager.cpp` L70-77
- **变更**: 新增 `freePort(int)` API；`instance->start()` 失败路径调用归还端口；`stopAll` 仍走 `clearPorts`。
- **验收**: 模拟 start 失败（config 写盘失败）后 `usedPorts_` 不单调增长；重试可复用原端口。

#### T4.5 [C6] CurlEasyHandle move 转移 cancelFlag_
- **位置**: `include/CurlEasyHandle.h` L26-39
- **变更**: move 构造/赋值同步转移 `cancelFlag_` 并置空源。
- **验收**: 新增单测：move 后目标对象 `perform()` 取消生效、源对象 `cancelFlag_` 为空。

#### T4.6 [C7 + E8] UrlFetcher SSL 校验 + 返回语义 + 配置超时
- **位置**: `src/UrlFetcher.cpp` L7-50（`setSslVerifyPeer(false)`/`setSslVerifyHost(false)`、10s 硬编码、失败返回 `""` 与空 body 不可区分）
- **变更**: `fetch()`（订阅源抓取）恢复 TLS 校验开启；代理测试路径如需关闭显式注释说明；返回改 `std::optional<std::string>`（失败 `nullopt`）；超时从 `config_` 传入。
- **验收**: 新增单测：自签证书服务器 → `fetch` 失败返回 `nullopt`；空 body 返回 `""`（二者可区分）；调用方（SubitemUpdaterV2 等）适配新签名后全量测试通过。

#### T4.7 [C8] WSAStartup 同步与生命周期
- **位置**: `src/XrayApi.cpp` L1143-1151
- **变更**: `static bool started` 加锁（`std::once_flag` 或原子 CAS）；进程级单例不要求 WSACleanup（文档化）。
- **验收**: 多线程首次并发调用 `ensureWinsock` 无竞态。

#### T4.8 [C9] typeUrl 统一命名
- **位置**: `src/XrayApi.cpp` L1452（`xray.core.proxy.outbound.Config`）vs L648（`xray.proxy.outbound.Config`）
- **变更**: 统一为 `xray.proxy.outbound.Config`（或统一常量表），消除错误命名陷阱。
- **验收**: grep 断言两处一致。

#### T4.9 [E3] timeoutMs_ 下限兜底
- **位置**: `src/ProxyTester.cpp` L24-25 + `include/CurlEasyHandle.h` L55-58（`timeoutMs_=0` → 超时禁用）
- **变更**: 构造/设置处 `max(timeoutMs_, 1000)` 兜底；CLI/config 读取侧对 ≤0 值归一化。
- **验收**: 传入 0/负值断言实际超时 ≥1000ms；现有测试全绿。

#### T4.10 [E4] writeCallback 异常边界 + setopt 检查
- **位置**: `include/CurlEasyHandle.h` L96-100（`append` 抛 bad_alloc 穿过 C 回调栈）、L109-120（NOPROGRESS/XFERINFO 三个 setopt 返回值未查）
- **变更**: `writeCallback` 内 try/catch 返回 0（CURLE_WRITE_ERROR）；setopt 走 `checkCurlCode`。
- **验收**: 注入内存分配失败（测试钩子）断言 `perform()` 抛异常而非 terminate。

#### T4.11 [E5 + E6] 错误分类与 lastError_ 语义
- **位置**: `src/XrayApi.cpp` L1319-1350/L1392-1396/L1509-1523（recv 超时与断连归一类）、L254-256 + 26 处赋值点（成功不清空 lastError_）
- **变更**: 区分 `WSAETIMEDOUT`；EOF 前必须收完整消息；streamSettings/mux 解析失败降级 WARN + 显式回退 subprocess；**每个公开方法成功路径清空 `lastError_`**。
- **验收**: 新增单测：成功调用后 `getLastError()` 为空；超时 vs 断连错误消息可区分。

#### T4.12 [E7] 预生成失败跳过而非空配置重试
- **位置**: `src/ProxyBatchTester.cpp` 预生成块（`push_back(XrayConfig{})`）+ `src/ConfigGenerator.cpp` L28-46（失败抛异常）
- **变更**: 预生成失败记录索引（跳过或直接入队失败结果），worker 不再对空 JSON 白做 3 次 gRPC 重试；错误信息改为真实原因。
- **验收**: 构造坏代理（空 id）断言跳过且失败统计正确；日志显示真实错误而非 "XRAY_ERROR"。

#### T4.13 [E9] XrayInstance atomic + CreateProcessW
- **位置**: `src/XrayInstance.cpp` L47-58（`"` 路径解析错乱）、L124-126（`running_` 裸 bool）
- **变更**: `running_` 改 `std::atomic<bool>`；`CreateProcessA` → `CreateProcessW`（或路径 `"` 转义校验）。
- **验收**: 含引号路径构造断言启动行为正确；TSAN 下 `isRunning()` 无竞态。

#### T4.14 [E11] 错误路径记录 GetLastError
- **位置**: `src/XrayApi.cpp` L41-42、L48-51、L76-80、L117-119
- **变更**: CreatePipe/CreateProcessA 失败路径 `lastError_` 拼入 `GetLastError()` 系统错误码。
- **验收**: 注入 CreateProcessA 失败断言 `lastError_` 含错误码。

#### T4.15 [E12] 计数器原子化
- **位置**: `src/ProxyBatchTester.cpp` L140-144/L166-169/L266-269/L301-312/L327-330（`queueMutex_` 包裹 atomic 的 `processedCount_++` + 三个计数器混用队列互斥）；`printSummary` L413-414 无锁读
- **变更**: `successCount_/failedCount_` 改 `std::atomic<int>`；去除多余锁包裹；`load()` 读取。抽取 `recordResult(bool success)` 辅助方法消灭五处重复计数逻辑（配合 T2.1）。
- **验收**: 计数与汇总在并发下一致；无死锁。

#### T4.16 [E14] 网络恢复日志风暴收敛
- **位置**: `src/ProxyBatchTester.cpp` L418-429（`waitForNetworkRecovery` 每 500ms 每 worker 打 WARN）
- **变更**: 日志降级 TRACE 或仅首/末各一次；可选的等待上限。
- **验收**: 断网场景下日志量显著下降；恢复/取消行为不变。

---

### Phase 5 — 规范约束清理

#### T5.1 禁用 `auto` 违规清理
- **位置**: 已核实 25 处生产代码违规（`src/XrayApi.cpp` 10 处、`src/utils/DnsCache.cpp` 2、`src/AutoTaskManager.cpp` 2、`src/RegionBatchResolver.cpp` 5、`src/ui/AppController.cpp` 3、`src/ui/SubscriptionPanel.cpp` 1、`src/ui/ToolbarIcons.h` 1、`include/CurlEasyHandle.h` L149）+ 测试目录 ~81 处
- **变更**: 本计划触碰文件内的 `auto` 全部替换为显式类型（`CurlEasyHandle.h` L149、`XrayApi.cpp` 10 处优先）；其余文件单独立项或在触碰时顺手清理。新增代码禁止 `auto`。
- **验收**: 全仓 `grep -rn "\bauto\b" src include` 生产代码 0 违规（字符串/注释除外）；构建通过。

#### T5.2 日志等级规范复核
- **位置**: 本次变更新增/修改的所有 `Logger::write`
- **变更**: 统一显式 `LogLevel`；错误路径用 `ERR`、边界用 `WARN`、汇总用 `REPORT`（对照 `docs/plans/DEV-PROCESS.md` 等级规范）。
- **验收**: 变更文件内无省略 LogLevel 的调用（grep 复核）。

---

## 5. 测试策略

- **新增/修改测试文件**（全部落 `tests/`，Google Test `TEST_F`）：
  - `tests/test_xray_api_direct.cpp`：A3 期望字节修正（**最高优先，随 T1.3 同批**）；新增 A2（trailers status）、B4（地址解析）、B6/B7（分帧/上限）、B8（HPACK）、B9（端口）、B10（读超时）、B12/B13（tls/xhttp 编码）、E6（lastError_ 语义）。
  - `tests/test_proxy_batch_components.cpp` 或新建 `test_proxy_batch_optimization.cpp`：T1.1（线程生命周期/取消）、T1.6（lastResult_ 同步）、T2.1（重复 run 状态重置/空结果语义）、T2.2（越界防御）、T2.9（catch... 兜底）、T4.3（异常路径 stopAll）、T4.15（计数器一致性）。
  - `tests/test_curl_easy_handle.cpp`（或并入现有 CurlEasyHandleTest）：T3.5、T4.5、T4.10。
  - 新建 `tests/test_port_manager_concurrency.cpp`：T1.5 端口并发、T4.4 freePort。
  - 新建 `tests/test_test_result_queue.cpp`：T2.11 flush 可靠性。
- **回归红线**: `ctest -V` 全量通过；`test_xray_api_direct` 54/54（修正后按新期望重算）。

## 6. 验证步骤（PowerShell）

```powershell
# 1. 配置并构建 Debug（每阶段完成即执行）
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8

# 2. 全量测试（每阶段）
ctest -V

# 3. 专项测试（按阶段）
ctest -R "XrayApiDirectTest" -V        # Phase 1/2 XrayApi
ctest -R "ProxyBatch|PortManager|TestResultQueue|CurlEasyHandle" -V

# 4. ASAN 构建（Phase 1 完成后必跑）
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build --parallel 8 && ctest -V

# 5. 静态分析（全部完成后）
.\scripts\run_static_analysis.ps1

# 6. 性能回归（Phase 3 完成后）
# 使用 test/guindb.db 全量批量测试，对照 docs/reports/2026-07-31 基线
```

**人工验证项**：
- 带 TLS 代理 gRPC 直连实测连通（A3 修复效果）。
- GUI 批量测试 + 关闭并发操作无崩溃（A1 修复效果）。
- 批量测试后 `Get-Process xray` 无残留进程、端口全部释放（A4/C3/C4 修复效果）。

## 7. 文件变更列表

| 文件 | 变更任务 |
|---|---|
| `include/ProxyBatchTester.h` | T1.1、T1.6、T2.1、T2.2、T4.15 |
| `src/ProxyBatchTester.cpp` | T1.1、T1.6、T2.1、T2.2、T2.9、T3.1、T3.2、T4.3、T4.12、T4.15、T4.16 |
| `include/XrayApi.h` | T1.2/T1.3（如签名不变则无改动） |
| `src/XrayApi.cpp` | T1.2、T1.3、T2.4、T2.5、T2.6、T2.7、T2.8、T2.10、T3.4、T4.1、T4.2、T4.7、T4.8、T4.11、T4.14 |
| `include/XrayInstance.h` | T1.4、T4.13 |
| `src/XrayInstance.cpp` | T1.4、T2.3、T3.2、T4.13 |
| `include/PortManager.h` | T1.5、T4.4 |
| `src/PortManager.cpp` | T1.5、T3.3、T4.4 |
| `include/XrayManager.h` | T1.5 |
| `src/XrayManager.cpp` | T1.5、T3.2、T4.4 |
| `include/CurlEasyHandle.h` | T3.5、T4.5、T4.10、T5.1 |
| `include/TestResultQueue.h` | T2.11 |
| `include/UrlFetcher.h` + `src/UrlFetcher.cpp` | T4.6 |
| `src/ProxyTester.cpp` | T4.9 |
| `tests/*` | 见 §5 测试策略 |
| `docs/INDEX.md`、`docs/plans/project-plans-tracker.md` | 文档登记 |

## 8. 风险

| 风险 | 等级 | 缓解 |
|---|---|---|
| **A3 测试期望修正**：改字节断言可能漏改导致误判（测试过宽/过窄） | 高 | 逐字段对照 `E:\eclipse_workspace\Xray-core` proto 源码；修正后立即实测带 TLS 代理连通性 |
| **T1.1 线程重构回归**：无条件 join 在极端情况下拉长退出时间（原 detach 是有意的"防火墙"） | 中 | curl 有界化（T3.5/T4.9）+ 取消链保证；保留动态 joinTimeout 计算（cpp:363-373）作为硬上限 |
| **T3.2 实例复用**：复用语义改变 `run()` 行为（端口/配置残留） | 中 | 仅复用 `isRunning()` 且配置未变（baseDir 相同）的实例；复用前校验 api 端口 gRPC 可达 |
| **T4.6 UrlFetcher 签名变更**：`std::string` → `std::optional<std::string>` 波及调用方 | 中 | 先全仓检索调用方（SubitemUpdaterV2/ProxyFinder/导入路径）再改；同批适配 |
| **T2.11 flush 重试**：DB busy 重试可能引入无限等待 | 中 | 重试上限 + 落盘日志 + indexid 列表（不可静默吞） |
| **性能回归**：睡眠删除后 Xray 未就绪即注入 | 中 | T3.2 端口就绪轮询先行/并行；保留 gRPC 重试循环兜底 |
| 多任务触碰同一文件（XrayApi.cpp/ProxyBatchTester.cpp） | 中 | 按依赖分批串行执行；每阶段独立提交并跑全量测试 |

## 9. 评审要点（供 Reviewer）

1. **批次边界**是否合理（Phase 1 是否真正独立于 Phase 2/3 可先行落地）。
2. **T1.3 测试期望修正策略**：是否同意"以 Xray proto 规范为准"替代"匹配当前输出"。
3. **T2.1 合并 B1/D7/E13 的范围**是否过大，是否需要拆分。
4. **T3.2 实例复用**的语义变更是否可接受。
5. 每阶段验收标准是否可量化、可执行。

## 10. 关联文档

- 审计报告: `docs/reports/2026-08-03-Report-CodeAudit-Optimization-v1.0.md`
- 性能基线: `docs/reports/2026-07-31-Report-XrayApi-gRPC-vs-Subprocess-Performance-v1.0.md`
- 批量测试效率重构（A-F 六阶段）: `docs/specs/2026-07-28-Spec-BatchTestingEfficiency-v1.0.md`
- 近期 gRPC 字段修复: `docs/bugfix/2026-07-29-Bugfix-XrayApi-gRPC-AddOutboundDirect-Fields-v1.0.md`
- 开发流程规范: `docs/plans/DEV-PROCESS.md`
