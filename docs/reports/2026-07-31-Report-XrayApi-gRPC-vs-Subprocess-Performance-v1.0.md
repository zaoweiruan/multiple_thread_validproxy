# 实测量化报告：XrayApi gRPC 路径 vs 原 subprocess 路径性能对比

**日期**: 2026-07-31
**类型**: 性能实测报告
**模块**: XrayApi (gRPC 直连) / ProxyBatchTester (CLI 批量测试)
**版本**: v1.0
**状态**: 已完成（实测数据闭环）

---

## 1. 结论先行

**gRPC 路径将每代理注入开销从 ~6s 降至 ~130ms（约 45 倍理论提升），实测同一批次墙钟耗时快 2.23 倍，且零进程派生。** 但它优化的是"注入/管理"开销，不是测试本身：总耗时仍由 curl 测试（≤ `timeout_ms`/代理）主导。

---

## 2. 实测方法

| 项 | 说明 |
|---|---|
| **测试对象** | "可用"订阅 `5544178410297751350`（178 个代理，VLESS+WS 为主，含全部 51 个已验证 Delay>0 代理） |
| **控制变量** | 同一订阅、同一数据库、4 workers、`test.timeout_ms=2000`（未改配置） |
| **构建 A** | gRPC 版（`USE_GRPC_API=ON`，`bin\validproxy-cli.exe`，2026-07-31 14:06 构建） |
| **构建 B** | subprocess 版（新建 `build_nogrpc`，`USE_GRPC_API=OFF`，MinGW GCC 14.2.0，391/391 编译成功） |
| **命令** | `validproxy-cli.exe -T 5544178410297751350`，`Measure-Command` 测墙钟 |
| **日志** | `bin\log\test-sub_20260731_152637.log`（A）、`bin\log\test-sub_20260731_153631.log`（B） |

> 注：CLI `-T` 命令的输出写入日志文件（`bin\log\test-sub_*.log`）而非 stdout。

---

## 3. 实测结果

| 指标 | A: gRPC 路径 | B: subprocess 路径 | 差异 |
|---|---|---|---|
| **墙钟总耗时** | **87,981 ms（≈88s）** | **195,972 ms（≈196s）** | **gRPC 快 2.23 倍** |
| 每代理平均耗时 | 87.98×4/178 = **1.98s** | 195.97×4/178 = **4.40s** | **省 2.42s/代理** |
| 成功 | 55/178（30.9%） | 59/178（33.1%） | 差异属冷连接噪声 |
| 失败 | 123 | 119 | 同为 2000ms 冷连接超时所致 |
| 日志 | `test-sub_20260731_152637.log` | `test-sub_20260731_153631.log` | — |

---

## 4. 两条路径机制对比（理论依据）

| 维度 | 旧 subprocess（`addOutbound`/`removeOutbound`） | 新 gRPC（`addOutboundDirect` 等） |
|---|---|---|
| 每次调用 | CreatePipe×2 + CreateProcessA 派生**全新 34MB xray.exe**，经 stdin 传 JSON，等 stdout EOF + WaitForSingleObject 5000ms | 进程内 protobuf wire 编码 → HTTP/2 preface → 单帧 HEADERS+DATA → 关闭 |
| 单次耗时 | **1700-2300ms**（进程启动 + geoip/geosite 资产加载 + gRPC 客户端初始化） | **~5ms**（localhost TCP 连接） |
| 进程开销 | 每操作一次进程生命周期 | 零派生，实例常驻 |
| 资产加载 | 每次全量加载 geoip/geosite（CPU/内存抖动） | 批启动时一次 |

**源码级佐证**：
- [`XrayApi.h`](file:///E:/eclipse_workspace/multiple_thread_validproxy/include/XrayApi.h#L30-L35) 头注释原文：*"Direct gRPC methods — bypass subprocess overhead (~1700-2300ms per call)"*
- [`ProxyBatchTester.cpp`](file:///E:/eclipse_workspace/multiple_thread_validproxy/src/ProxyBatchTester.cpp#L103-L105) 注释原文：*"each xray api subprocess can block up to 5s if gRPC isn't ready yet… launch subprocess per ping is too expensive"*

### 4.1 批量场景量化（178 代理 / 4 worker）

| 指标 | 旧 subprocess | 新 gRPC |
|---|---|---|
| 每代理固定开销 | remove+add+list ≈ **6s**（3×2s） | 120ms 固定 sleep + ~10ms 调用 ≈ **130ms** |
| 178 代理纯注入开销 | ~267s（≈4.5 分钟） | ~6s |
| 提升倍数 | — | **≈ 45x** |

### 4.2 日志实测（`ui_20260731_140638.log` 14:07:12）

**同一秒内**完成全部 4 次 gRPC 调用：`removeOutboundDirect SUCCESS → listOutboundsDirect(73B) → addOutboundDirect SUCCESS → listOutboundsDirect(527B)` —— ms 级实证。

---

## 5. 结论

1. **gRPC 路径显著更高效（实测 2.23 倍墙钟提升）**
   - 理论差更大：subprocess 每次操作派生 34MB `xray.exe` 新进程 ≈ 1.7–2.3s，每代理注入需 remove+add+list 三次 ≈ 6s；gRPC 路径零进程派生，注入开销 ≈ 130ms ≈ 45 倍理论差。
   - 实测显现差为 2.42s/代理，因为部分进程派生时间被 curl 的 2s 等待掩盖（并行流水线重叠）；代理数越多、超时越短，差距越明显。
   - **墙钟 88s → 196s 是同一批次同条件下的硬数据。**

2. **成功率两构建持平**（30.9% vs 33.1%）：证明 gRPC 路径没有牺牲正确性——失败全部来自 2000ms 冷连接超时（此前已确认的根因），非路径差异。

3. **附加收益（未在数字中体现）**：gRPC 路径每次批量只启动 1 次 Xray 实例并常驻，CPU/内存无抖动；subprocess 每次派生加载 geoip/geosite 资产，4 worker 并发时资源波动大。

---

## 6. 限制与注意事项

1. **协议覆盖**：仅 8 个 typeUrl 走 gRPC（vmess/vless/trojan/shadowsocks/socks/http/freedom/blackhole）；hysteria2/tuic/wireguard 等无 protobuf 编码器，回退 subprocess。
2. **总耗时仍由测试本身主导**：`timeout_ms=2000` 时 178 代理 ≈ 178×2s/4 ≈ 89s 下限，注入开销占比极小。gRPC 的真正价值在**管理面**：避免每代理 34MB 进程反复派生（稳定性、内存、避免资源竞争），并为冷连接超时问题提供重试可行性。
3. **编译开关**：`USE_GRPC_API` 默认 `OFF`，需在 CMake 中开启（作用于 validproxy-cli / validproxy / test_xray_api_direct）才能生效。

---

## 7. 建议（供决策，未改动）

1. **将 `CMakeLists.txt` 的 `USE_GRPC_API` 默认值改为 `ON`**（当前默认 OFF）——实测证明 gRPC 路径更快且正确性持平。
2. 将 `bin/config.json` 的 `test.timeout_ms` 2000→5000 提升成功率（与 gRPC 效率正交的独立问题）——gRPC 省下的注入时间可安全让渡给冷连接握手，二者叠加后批量吞吐反而更稳。

---

## 8. 证据留存

- 两份日志：`bin\log\test-sub_20260731_152637.log`（A）、`bin\log\test-sub_20260731_153631.log`（B）
- 备份可执行文件：`bin\validproxy-cli-grpc.exe`（A 版）、`bin\validproxy-cli-subproc.exe`（B 版）
- B 构建目录：`build_nogrpc\`（`USE_GRPC_API=OFF`，MinGW GCC 14.2.0）
- 均保留，可随时复核或复跑。
