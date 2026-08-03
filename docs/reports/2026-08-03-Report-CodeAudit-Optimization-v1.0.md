# 代码审查优化方案报告：XrayApi / ProxyBatchTester / 支撑模块

**日期**: 2026-08-03
**类型**: 代码审查报告（只读审查，未改动代码）
**模块**: XrayApi (gRPC + subprocess) / ProxyBatchTester / XrayManager / XrayInstance / PortManager / ProxyTester / ConfigGenerator / UrlFetcher / CurlEasyHandle
**版本**: v1.0
**状态**: 待评审（供人工决策后进入实施）

---

## 1. 审查范围与方法

| 项 | 说明 |
|---|---|
| **审查对象** | `include/XrayApi.h` + `src/XrayApi.cpp`（1608 行，gRPC 直连 + subprocess 回退双路径）；`include/ProxyBatchTester.h` + `src/ProxyBatchTester.cpp`（多线程测试引擎，A-F 六阶段重构后）；支撑模块（ProxyTester / XrayManager / XrayInstance / PortManager / ConfigGenerator / UrlFetcher / CurlEasyHandle） |
| **方法** | 3 个并行 explore 审计代理全文通读，交叉核对调用链（ProxyBatchTester → XrayApi/ProxyTester/XrayManager、XrayManager → XrayInstance/PortManager）与测试期望（`tests/test_xray_api_direct.cpp`）、`config/StreamSettingsBuilder.cpp` JSON 键名来源 |
| **分类** | ① 正确性 bug ② 资源泄漏 ③ 性能问题 ④ 错误处理空洞；严重级别 HIGH / MED / LOW |
| **边界** | 跳过风格类问题（命名/格式化）；未发现问题的类别已明确说明 |

---

## 2. 崩溃 / UB 级（HIGH，必须修）

### A1. [HIGH] detach 后 UAF —— 三份审计共同确认的头号问题
- **位置**：`src/ProxyBatchTester.cpp` L43-49、L382-404（`std::async` 限时 join + `t.detach()`）
- **问题**：
  1. `std::async` 包装 `join` 与主线程同时操作同一 `std::thread` 对象——任一分支必然出错：async 先 join 成功 → 主线程 `detach()` 抛 `std::system_error`，析构路径抛出即 `terminate`；主线程先 detach → async 内 `join()` 抛异常，进程在 `fut` 析构时异常终止。
  2. `fut.wait_for(5s)` 超时无效——`std::async` 返回的 future 析构会阻塞到任务完成，detach 不能阻止析构卡死在 curl 阻塞上，"防止挂死"目标未实现。
  3. detach 后线程仍访问 `proxyTester_->test()`（L296）、`resultQueue_.enqueue`（L317）、`proxies_`/`preGenConfigs_`/`config_`/`db_`；主线程随后 `resultQueue_.stop()`（L407）、`xrayManager_->stopAll()`、析构 `delete proxyTester_`（L56）——全部是 use-after-free；GUI 退出时 `CurlGlobalGuard` 执行 `curl_global_cleanup()` 与分离线程 `curl_easy_perform` 并发即崩溃。
- **修复**：废弃 `std::async`+detach 模式。worker 内用 `condition_variable` + `cancelRequested_` 协作退出，主线程**无条件 join**（配合 curl 进度回调使阻塞有界）；或将 `ProxyTester` 改 `shared_ptr` 持有 + 完成回调，所有清理动作（stopAll/delete/resultQueue_.stop）等待全部 worker 退出后再执行。

### A2. [HIGH] gRPC status/trailers 从不解析，错误被当成功
- **位置**：`src/XrayApi.cpp` L1310-1396（`grpcSendReceive`），波及三个 Direct 方法 L1401-1604
- **问题**：trailers 帧（L1373-1376）只检查 `END_STREAM` 标志，从不读取 HPACK 载荷里的 `grpc-status` / `grpc-message`；`recv` 遇 EOF 时只要 `response` 非空就 `return true`（L1321、L1346）。Xray 拒绝 AddOutbound（如 protobuf 编码错误）时经 trailers 返回 `grpc-status != 0`，但代码上报成功 → `ProxyBatchTester` 重试循环（L217-239）永不触发，测试框架把"未注入"当成"已注入"，后续连通性测试基于脏状态。
- **修复**：解析 trailers 帧 HPACK 内容提取 `grpc-status`，非 0 视为失败并回填 `lastError_`；EOF 且未收到 END_STREAM/完整消息时不得返回 true。

### A3. [HIGH] protobuf 编码错误：TLS 安全层与传输协议静默丢失
- **位置**：
  - `src/XrayApi.cpp` L999-1000（`encodeStreamConfig`）：`Config.security_type`（proto 字段 3）是 `SecurityType` 枚举（LEGACY=0/AUTO=1/TLS=2/REALITY=3，varint），代码却 `encodeString(3, "tls")` 发送 length-delimited 字符串——wire type 不匹配 → protobuf 当未知字段跳过 → **带 TLS 的代理直连裸 TCP 必失败**。
  - `src/XrayApi.cpp` L986-989（TransportConfig）：真实 proto `{ string protocol_name = 1; TypedMessage settings = 2; repeated TypedMessage extra_settings = 3; }`，代码把 `protocolName`（如 "websocket"）塞进字段 3（repeated TypedMessage，裸字符串反序列化时非法 tag），字段 1 空缺 → **ws/grpc/kcp/http 传输配置静默丢失，回落裸 TCP**。
  - `tests/test_xray_api_direct.cpp`（L582-611、L570-572、L553-555）把错误格式固化为测试期望——**测试通过反而掩盖 bug**。
- **修复**：`security=="tls"→2`、`=="reality"→3` 用 `encodeVarintField(3,...)`；`transportConfig += encodeString(1, protocolName)` 删字段 3 裸字符串；**同步修正测试期望字节**（从"匹配当前输出"改为"匹配 Xray proto 规范"）。

### A4. [HIGH] XrayInstance 进程生命周期：孤儿进程 + 句柄泄漏 + stop 竞态
- **位置**：`src/XrayInstance.cpp` L67-74、L85-122
- **问题**：
  1. `AssignProcessToJobObject` 失败路径：`CREATE_SUSPENDED` 创建的 xray.exe 从未 `TerminateProcess` → 永久悬挂孤儿；`jobObject_` 句柄未关闭未置空 → 泄漏。
  2. `stop()` 无互斥：双线程并发 stop 对同一 `jobObject_`/`processHandle_` 双重 `CloseHandle`（第二次可能关掉被系统复用的任意句柄）。
  3. `TerminateJobObject` + 1s 等待后 `GetExitCodeProcess` 仍 `STILL_ACTIVE` 时从不 `TerminateProcess` → 进程存活、端口仍占，而 `XrayManager::stopAll` 的 `PortManager::clearPorts()` 已清端口追踪 → 下次 start 复用冲突。
- **修复**：失败分支先 `TerminateProcess(pi.hProcess, 1)` 再关双句柄与 jobObject_ 并置空；stop 加 mutex，超时后显式 `TerminateProcess(processHandle_, 1)` 再等待；clearPorts 前确认进程确已退出。

### A5. [HIGH] PortManager / XrayManager 无锁并发
- **位置**：`src/PortManager.cpp` L6-25（`usedPorts_` 裸 `std::vector<int>`）；`src/XrayManager.cpp` L51-110（`instances_`）
- **问题**：`findAvailable` 无同步——push_back 与 `for (int used : usedPorts_)` 遍历并发 → 迭代器失效 UB；双线程同时通过检查且都未 push_back 前返回同一端口 → 双实例绑定同一 socks/api 端口。`instances_` 的 push_back/遍历/clear 均无 mutex：双 start 竞态 + UI 轮询 `isRunning()` 与 `stopAll()` 并发读清空中的 vector。
- **修复**：`usedPorts_` 加 `std::mutex`，检查+登记放入同一临界区；`instances_` 加专用 mutex，start 内对已运行实例去重/拒绝。

### A6. [HIGH] `lastResult_` 数据竞争（UB）
- **位置**：`src/ProxyBatchTester.cpp` L170-172、L271-272、L291-292、L320、L332-333；读方 `include/ProxyBatchTester.h` L32（`getLastResult()`）
- **问题**：`lastResult_` 为非原子非互斥普通成员，`run()` 路径下最多 16 个 worker 并发写，主线程随后无锁读——标准数据竞争。`runWithIndexId()` 因单 worker 侥幸安全。
- **修复**：写入/读取加 `workerStateMutex_`（或独立 mutex），`getLastResult()` 同样加锁拷贝；或改 `std::atomic<std::shared_ptr<TestResult>>`。

---

## 3. 正确性 bug（MED）

| # | 位置 | 问题 | 修复 |
|---|---|---|---|
| **B1** | `ProxyBatchTester.cpp` L346-348、L432-433 | 同一对象重复 `run()` 状态不重置：`proxiesQueue_` 残留 + 计数器（L17 仅构造清零）累积 → 残留索引与新 `proxies_`/`preGenConfigs_` 错位，测错代理/统计错乱 | 三个入口开头统一重置：清空队列、清零计数器、清空 `workerCurrentProxyIndex_` |
| **B2** | `ProxyBatchTester.cpp` L140-144 | 边界只校验 `proxies_.size()`，L176 直接 `preGenConfigs_[profileIdx]`（现两 vector 同循环构建恒等，属潜伏越界） | 双向量同查；或绑定单一结构体 `{Profileitem p; XrayConfig cfg;}` 消除错位 |
| **B3** | `XrayInstance.cpp` L76-81 | `start()` 固定 sleep 2s 即置 `running_=true`，不验证进程存活（config 错时进程秒退、gRPC 端口未起）；stop 在 2s 窗口内执行 → 进程变无 job 管理的孤儿，状态机错乱 | `WaitForSingleObject(handle, 0)`/`GetExitCodeProcess` 验证存活；start/stop 共用状态 mutex |
| **B4** | `XrayApi.cpp` L1189-1216 | `grpcConnect` 仅 `AF_INET` + `inet_pton`：hostname（如 "localhost"）与 IPv6（`parseServerAddr` L1166 保留方括号 `[::1]`）永远连不上 | `getaddrinfo(AF_UNSPEC)` + IPv6 去方括号 |
| **B5** | `XrayApi.cpp` L495、L593、L1487-1489 | 服务端类协议 servers 缺失/数组为空/无认证 SOCKS/HTTP → 空 protobuf 静默注入空配置（freedom/blackhole 空结果合法，二者不可区分） | 改 `std::optional<std::string>` 或 out-param 失败标志；失败回退 subprocess |
| **B6** | `XrayApi.cpp` L1339-1368 | 服务端可控 `flen` 无上限（`resize` 抛 bad_alloc 无 catch → 崩溃）、`msgLen` 可宣称 4GB、uint32/int 混算 | `flen` 限上限（如 65536）超限断开；`msgLen` 与 `payload.size()-5` 严格相等校验 |
| **B7** | `XrayApi.cpp` L1355-1371 | 消息跨多个 HTTP/2 DATA 帧时每帧直接覆盖 `response`，分帧场景 `payload.size() >= 5+msgLen` 不成立 → 大响应（多 outbound 的 ListOutbounds）只剩最后一帧或全丢 | 按 msgLen 累加分片收满再解析 |
| **B8** | `XrayApi.cpp` L1078-1113 | HPACK 静态表索引错：索引 57 是 `transfer-encoding` 而非 `te`，62/63（grpc-timeout/grpc-encoding）超出 61 条静态表范围 → 请求头是 `transfer-encoding: trailers` 且缺 gRPC 强制的 `te: trailers` | `te` 走完整字面量（`0x00 + varint(len) + "te"`），删除 NAME_INDEX 中 57/62/63 |
| **B9** | `XrayApi.cpp` L1157-1181 | 端口解析：`std::stoi("8080abc")` 吞尾缀；0/负/>65535 不校验 → `htons` 截断 | stoi 后校验剩余字符与 `1..65535` 范围 |
| **B10** | `XrayApi.cpp` L100-108 | subprocess 路径 `ReadFile` 无超时（子进程挂住不关 stdout 时"读空死等"），5s WFO 根本执行不到 | 读循环加 deadline（PeekNamedPipe/非阻塞读或独立读线程），超时 TerminateProcess |
| **B11** | `ProxyBatchTester.cpp` L322-334 | worker 只 catch `std::exception`，`bad_alloc`（L317 enqueue 分配失败）/SEH 逃出线程 → terminate | 加 `catch(...)` 兜底：计数失败 + 入队 + 日志 + 尝试一次 `removeOutboundDirect(tag)` 清理 |
| **B12** | `XrayApi.cpp` L721-766（潜伏） | tls.Config 字段号与 wire type 需复核（min/maxVersion 是 uint32 varint 非字符串，真实编号 5/6；当前 `StreamSettingsBuilder` L18-69 不产出该字段故潜伏） | 对照 `Xray-core/transport/internet/tls/config.proto` 核实全部字段号 |
| **B13** | `XrayApi.cpp` L935-991 | xhttp 传输无分支 → 传输配置静默丢失退化为裸 TCP；splithttp 只写 protocol_name、settings 全丢 | 补 xhttp/splithttp settings 编码，或无法编码时强制走 subprocess 回退 |
| **B14** | `XrayApi.cpp` L497、L524 | `servers[0].as_object()` 元素非 object 时抛 `boost::json::system_error`，L1489 调用处无 try/catch → 异常冒泡到 worker catch（L322）绕过重试与回退 | 类型守卫（`is_object()`）+ 失败置标志走回退 |

---

## 4. 资源泄漏

| # | 位置 | 问题 | 修复 |
|---|---|---|---|
| **C1** | `XrayApi.cpp` L200-216 | `removeOutbound`（subprocess）CreateProcessA 成功即返回 true 并打 "SUCCESS"：不取退出码、不读输出、WFO 结果忽略；`STARTF_USESTDHANDLES` + 全 NULL 句柄使子进程 stdout/stderr 错误不可见；`ProxyFinder` L348 依赖其清理 → 静默失败污染后续注入 | 复用 `runProcess` 式管道捕获 + 检查退出码与 WFO 结果，失败写 `lastError_` |
| **C2** | `XrayApi.cpp` L143-172、L212 | subprocess 每次调用起进程，`addOutbound` 还额外起第二个"确认"进程（L165-170）+ 200ms sleep；每条代理回退路径最多 4 次进程创建（各 1.7-2.3s） | 删冗余确认进程与无谓 sleep |
| **C3** | `ProxyBatchTester.cpp` L351-358、L475/527/581 | `testProxiesMultiThreaded` 中途抛异常（线程创建失败/`resultQueue_.start()` 抛）→ `stopAll()` 不执行：xray 子进程与端口泄漏；局部 joinable `threads` 异常展开析构 → terminate | RAII scope guard 保证 `stopAll()` 任何退出路径执行；threads 改成员 + try/catch 清理后重抛 |
| **C4** | `ProxyBatchTester.cpp` L35-57 | 析构不负责 `stopAll()`（注释"由 release() 管理"），但 `run()` 在 startXrayInstances 成功后异常提前退出时无任何路径 stopAll → xray/端口持续占用 | 析构中（线程安全退出后）幂等 `stopAll()`，或与 C3 守卫合并 |
| **C5** | `PortManager.cpp` + `XrayManager.cpp` L70-77 | `findAvailable` 已登记端口，`instance->start()` 失败（config 写盘/CreateProcessA/Assign 失败）端口不归还 → 重试时 `usedPorts_` 单调增长、分配基线不断上移 | start 失败路径调 `PortManager::freePort(port)`（需新增 API） |
| **C6** | `include/CurlEasyHandle.h` L26-39 | move 构造/赋值只搬 `curl_`，`cancelFlag_` 留在旧值 → 取消标志静默丢失/错位 | move 时同步转移 `cancelFlag_` 并清源 |
| **C7** | `include/UrlFetcher.h` + `UrlFetcher.cpp` L7-50 | `setSslVerifyPeer(false)`/`setSslVerifyHost(false)` → `fetch()`（订阅源抓取）与 `fetchViaProxy` 均无 TLS 校验，中间人可注入恶意订阅 | `fetch()` 保持校验开启；代理测试路径如需关闭显式注释说明 |
| **C8** | `XrayApi.cpp` L1143-1151 | `WSAStartup` 无 `WSACleanup` 配对 + `static bool started` 无同步（低危，WSAStartup 本身线程安全） | 进程级单例可接受；bool 加锁 |
| **C9** | `XrayApi.cpp` L1452 vs L648 | 回退 typeUrl 前后不一致：`xray.core.proxy.outbound.Config` vs `xray.proxy.outbound.Config`（当前均不在 supportedTypeUrls L1460-1469 → 均走 subprocess，行为无害，属错误命名陷阱） | 统一命名 |

---

## 5. 性能优化

| # | 位置 | 问题 | 修复 |
|---|---|---|---|
| **D1** | `ProxyBatchTester.cpp` L276-281 | 每代理固定 100ms（10×10ms）纯空转睡眠，注释理由"cancellation responsiveness"站不住（真正取消检查 L284 已存在，curl 有 `test_timeout_ms` 超时）；53,837 代理/16 worker ≈ 5.6 分钟/轮 | 删除；取消响应由 L284 + curl 超时保证 |
| **D2** | `ProxyBatchTester.cpp` L208-212、L233-237 | 成功 2×10ms / 重试 5×10ms 无条件固定睡眠按代理累积 | 改为条件退避：仅 gRPC 返回"未就绪"类错误时指数退避，成功路径零睡眠 |
| **D3** | `XrayInstance.cpp` L79 + `XrayManager.cpp` L74 | 每实例固定 2s+500ms sleep，16 实例 ≈ 40s 纯等待，且每次 `run()` 都全量重启重复该延迟 + warmup 2s（L107-108） | 轮询 gRPC 端口就绪替代固定 sleep；`startXrayInstances` 前检查 `isRunning()` 复用实例 |
| **D4** | `PortManager.cpp` L6-25 | `findAvailable` 内层线性遍历 O(n²)，`port>65535` 回绕后可能重扫已检区间 | `std::set`/位图维护已用端口；回绕后跳过已检区间 |
| **D5** | `XrayApi.cpp` L1189-1216 | 阻塞 `connect` 无超时（仅设 `SO_RCVTIMEO` 不影响 connect），不可达地址 Windows 上最长约 21s 拖死 worker | connect 前设 `SO_SNDTIMEO` 或非阻塞 connect + select 3-5s |
| **D6** | `XrayApi.cpp` L1310-1396 | `SO_RCVTIMEO` 5s 是"每次 recv"粒度，MAX_FRAMES=200 → 单调用最坏 200×5s；SETTINGS ACK send 失败被忽略（L1384）、RST_STREAM/PING 未处理空等 | 整次调用绝对 deadline；处理 RST_STREAM 立即失败 |
| **D7** | `ProxyBatchTester.cpp` L453-470 / L505-522 / L559-576 | 三个入口 + 三段逐字重复预生成代码（各 ~20 行），整体流程（start→pregen→test→summary→stopAll）三份拷贝，已现行为分叉（空结果 run() 返回 true vs runWithSubId 返回 false；L489-493 同一条日志打三遍） | 提取 `runInternal()` + `preGenerateConfigs()` 私有方法，三入口只负责数据准备与返回值语义 |
| **D8** | `CurlEasyHandle.h` L104-120 | 取消依赖 XFERINFO 回调仅传输中被调用；卡在 TCP connect/DNS 解析时无回调 → 取消延迟 = connect 超时 | `CURLOPT_CONNECTTIMEOUT` 单独调小（如 min(timeoutMs_, 3000)）或 `curl_multi` 异步取消 |

---

## 6. 错误处理空洞

| # | 位置 | 问题 | 修复 |
|---|---|---|---|
| **E1** | `TestResultQueue.h` L89-91、L99-100、L115-116 + `ProxyBatchTester.cpp` L19-23 | flush 回调（`dao.updateTestResultBatch`，DB busy/locked 时返回 false）返回值在 flush 线程、`stop()` 最终排空、`flushBatch()` 三处全部忽略 → **一批 50 条测试结果永久丢失**且无日志；队列满无背压 | callback 返回 false 时保留 batch 带退避重试；上限后落盘日志 + ERR 记录 indexid 列表 |
| **E2** | `TestResultQueue.h` L90、L100 | flush 线程回调裸调用无 try/catch，异常逃出线程 → `std::terminate` 杀进程，swap 出的批次丢失 | 回调包 try/catch(...)，catch 内保留 batch 重试 + 日志，绝不外抛 |
| **E3** | `ProxyTester.cpp` L24-25 + `CurlEasyHandle.h` L55-58 | `timeoutMs_=0` 时 `CURLOPT_TIMEOUT_MS`/`CONNECTTIMEOUT_MS`=0 表示**禁用**超时 → 挂起无上限（GUI 限 [1000,120000]，但 CLI/config.json 直接写 0/负值不校验） | `max(timeoutMs_, 1000)` 兜底 |
| **E4** | `CurlEasyHandle.h` L96-120 | `writeCallback` 内 `append` 抛 bad_alloc 穿过 C 回调栈 → terminate；NOPROGRESS/XFERINFOFUNCTION/XFERINFODATA 三个 setopt 返回值未查 | 回调内 try/catch 返回 0（CURLE_WRITE_ERROR）；setopt 走 `checkCurlCode` |
| **E5** | `XrayApi.cpp` L1319-1350、L1392-1396、L1509-1523 | recv 超时与连接关闭归为一类错误；EOF 未验消息完整性；streamSettings/mux 解析失败仅 DEBUG 日志 → 传输配置静默退化；`parseOutboundJson` L674-676 `catch(...) return false` 丢弃原因 | 区分 `WSAETIMEDOUT`；EOF 前必须收完整消息；解析失败降级 WARN + 显式回退 subprocess |
| **E6** | `XrayApi.cpp` L254-256 | `lastError_` 成功路径不清空 → 调用方（如 ProxyBatchTester L261-262）可能误判上轮失败 | 每个公开方法成功时清空 `lastError_` |
| **E7** | `ProxyBatchTester.cpp` L462-465 / L514-517 / L568-571 + `ConfigGenerator.cpp` L28-46 | 预生成失败推空 `XrayConfig{}`，worker 对空 JSON 白做 3 次 gRPC 重试（每坏代理空耗 0.3-1s 含 gRPC 超时），错误信息误导为 "XRAY_ERROR" | 预生成失败记录索引，入队时跳过（或直接入队失败结果） |
| **E8** | `UrlFetcher.cpp` L7-50 | 失败返回 `""` 与"服务器返回空 body"不可区分；10s 超时硬编码不读 config | 返回 `std::optional<std::string>`/错误码；超时从配置传入 |
| **E9** | `XrayInstance.cpp` L47-58、L124-126 | `CreateProcessA` 路径含 `"` 时命令解析错乱（低概率）；`running_` 裸 bool 跨线程读写（x86 表现为陈旧值） | 路径 `"` 转义校验 / 改 `CreateProcessW`；改 `std::atomic<bool>` |
| **E10** | `XrayApi.cpp` L151 | addOutbound 退出码非 0 仅凭输出含 "adding" 即判成功，可能掩盖真实失败 | 以退出码为准，仅在已知场景特判 |
| **E11** | `XrayApi.cpp` L41-42、L48-51、L76-80、L117-119 | CreatePipe/CreateProcessA 失败只返回 -1 不记录 `GetLastError`；`runCommand` 的 `lastError_` 无系统错误码 | 错误路径拼入 `GetLastError()` |
| **E12** | `ProxyBatchTester.cpp` L140-144、L166-169、L266-269、L301-304、L309-312 | `queueMutex_` 包裹本已是 atomic 的 `processedCount_++`，无效同步开销；三个计数器（`successCount_`/`failedCount_`/`processedCount_`）与队列互斥混用是反模式（detach 路径下 printSummary L413-414 无锁读仍竞争） | 计数器统一 `std::atomic<int>`，去多余锁包裹，`load()` 读取 |
| **E13** | `ProxyBatchTester.cpp` L209、L235、L278 | 取消路径提前 `return` 不计失败不入队 → 对账失真 | 提前返回前统一"计数失败 + 入队 + 写 lastResult_"收尾 |
| **E14** | `ProxyBatchTester.cpp` L418-429 | `waitForNetworkRecovery` 500ms 睡眠 + 取消退出条件（**非** busy-wait，已核验）；但 16 个 worker 各自轮询并发打 WARN 日志风暴；等待无上限 | 网络探测收敛单点（共享回调广播），等待上限可选 |

---

## 7. 明确未发现问题的类别

- **死锁**：`queueMutex_`/`dbMutex_`/`workerStateMutex_` 锁序一致，无嵌套交叉加锁（worker 内 db/queue 顺序释放），flush 线程仅持 dbMutex_，网络恢复不持锁。
- **网络恢复循环 busy-wait**：`waitForNetworkRecovery` 每轮 500ms sleep，有取消退出条件，无 CPU 空转（仅日志风暴与无界等待，见 E14）。
- **ConfigGenerator 拼接性能**：JSON 全部经 `boost::json::object` 组装后一次 `serialize`（ConfigGenerator.cpp L55），无 `+=` 字符串循环；出站构建器同样用 boost::json 结构体赋值，转义正确（含 `"`、`\`、控制字符与 UTF-8 中文密码/URL）。
- **curl 全局初始化**：`curl_global_init` 在 `main_cli.cpp:47`（CURL_GLOBAL_ALL）与 `main_gui.cpp:24-25`（CurlGlobalGuard）各调一次，单进程仅一个入口执行，满足"只调一次"；但 `ProxyTester`/`UrlFetcher`/`CurlEasyHandle` 自身不兜底初始化（GUI 已因此崩过一次，见 `docs/bugfix/2026-07-20-Bugfix-CurlGlobalInit-GUI-v1.0.md`）。
- **curl share handle**：各测试独立 easy handle，无共享 handle（多线程下最安全做法，无需 CURLSH）。
- **ProxyTester/UrlFetcher 资源释放**：每次测试新建局部 `CurlEasyHandle`，`curl_easy_cleanup` 由 RAII 析构保证（含异常路径）。
- **单次 run 内索引映射**：`preGenConfigs_[profileIdx]` 与 `proxies_[profileIdx]` 同循环构建、大小恒等不会错位；越界风险仅存在于重复 run（B1）与边界校验缺口（B2）。

---

## 8. 建议实施顺序

1. **第一批（崩溃/UB，A1-A6）**：detach 重构 → gRPC status 解析 → protobuf 编码修正（连同测试期望）→ XrayInstance 生命周期 → PortManager/XrayManager 加锁 → `lastResult_` 原子化
2. **第二批（正确性 + 丢数据，B 系列 + E1/E2）**：重复 run 状态重置、长度校验、flush 失败重试与异常边界
3. **第三批（性能，D 系列）**：删固定睡眠、启动就绪轮询、三入口代码去重、connect/recv 超时
4. **第四批（低危收尾，C/E 其余）**：SSL 校验、`lastError_` 语义、日志完善、端口归还

**特别提示**：A3 的 protobuf 编码错误被 `tests/test_xray_api_direct.cpp` 固化（测试通过反而掩盖 bug），修复时必须**同步更新测试期望字节**——测试断言需从"匹配当前输出"改为"匹配 Xray proto 规范"。

---

## 9. 证据与复核路径

- 审查基于源码全文通读（`XrayApi.cpp` 1608 行、`ProxyBatchTester.cpp`、`XrayInstance.cpp`、`PortManager.cpp`、`XrayManager.cpp`、`TestResultQueue.h`、`CurlEasyHandle.h`、`UrlFetcher.cpp`、`ProxyTester.cpp`、`ConfigGenerator.cpp`）
- 交叉核对：`tests/test_xray_api_direct.cpp`（编码期望）、`config/StreamSettingsBuilder.cpp`（JSON 键名来源）、`main_cli.cpp`/`main_gui.cpp`（curl 初始化点）
- 关联文档：性能实测见 `docs/reports/2026-07-31-Report-XrayApi-gRPC-vs-Subprocess-Performance-v1.0.md`；批量测试效率重构见 `docs/specs/2026-07-28-Spec-BatchTestingEfficiency-v1.0.md`（status: completed）
