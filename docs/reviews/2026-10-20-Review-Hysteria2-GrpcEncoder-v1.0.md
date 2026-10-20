# 独立评审：Hysteria2 gRPC 编码器方案 v1.0

> **评审对象**：`docs/specs/2026-10-20-Spec-Hysteria2-GrpcEncoder-v1.0.md`
> **支撑报告**：`docs/reports/2026-10-20-Report-Hysteria2-GrpcEncoderFeasibility-v1.0.md`
> **上游基线**：`E:\eclipse_workspace\Xray-core` @ `d2758a02`（v26.3.27）
> **仓库基线**：`9ab870b`（`src/XrayApi.cpp` 2672 行）
> **评审方式**：独立评审代理逐字抽取方案中的头文件与 10 条用例，在临时目录用 g++ 14.2.0 + Boost 1.91 实际编译/链接/运行；并用 v26.3.27 的真实 protobuf 解码 C++ 复刻编码器产出的字节。**仓库文件零修改。**
> **结论**：**APPROVE WITH CHANGES（有条件通过）**

---

## 1. 结论

方案的分层判断成立：容器层与传输层必须一起补，只加 typeUrl 会「注入成功、运行必坏」。两层落点、proto 字段号、两个 typeUrl 均与上游一致；`HysteriaPayload.h` 在 `-std=c++17 -Wall -Wextra -pedantic` 下零 error 零 warning，10/10 用例通过且相当承重（逐项变异至少杀死 1 条用例）。

但有三处必须改，其中 **D5 的事实前提是错的**：方案断言「空 user 子消息合法」，实测会让 `AddOutbound` 直接失败，而批处理路径没有 CLI 兜底，后果比现状更严重。另有 `finalmask` 与传输层编码失败两处静默缺口，恰好会产出方案自己声称要防的「注入成功、运行必坏」。

修完 B1/B2/B3 后可进入实施，无需架构返工。

---

## 2. 阻断性问题（Must fix）

### B1 — D5 错误：复用 `encodeServerEndpoint` 会写出非 nil 的空 `User{}`，`AddOutbound` 直接失败

**方案原文**：D5「容器层 server 复用 `encodeServerEndpoint(addr, port, std::string())`；该函数总会写出空 user 子消息（合法，Go 解码为空 `User`）」。

**实测证伪**

| 项 | 证据 |
| :--- | :--- |
| 编码端 | `encodeServerEndpoint`（`src/XrayApi.cpp:411-418`）**无条件**写 field 3：`ep += encodeLengthDelimited(3, userProto);` |
| 解码端 | `common/protocol/server_spec.go:22-27`：`if spec.User != nil { ... ToMemoryUser() ... }` —— 字段**存在**即进入，零长度子消息解码为**非 nil** 的 `User{}` |
| 失败点 | `common/protocol/user.go:8-10`：`if u.GetAccount() == nil { return nil, errors.New("Account is missing") }` |
| 上抛路径 | `proxy/hysteria/client.go:34-37` → `app/proxyman/command/command.go:165-167` → `core/xray.go:126-128`（错误直接成为 gRPC status） |

**字节级实测**（C++ 复刻编码器 → v26.3.27 真实 pb 解码）

```
方案 D5（带空 user field 3）: 080212160a0f120d612e6578616d706c652e636f6d10bb031a00  (26 B)
  → userNil=false
  → NewServerSpecFromPB err = common/protocol: Account is missing

修正（完全省略 user）:       080212140a0f120d612e6578616d706c652e636f6d10bb03          (24 B)
  → userNil=true
  → NewServerSpecFromPB err = <nil>
```

**影响比现状更差**

1. `addOutboundDirect` 返回 false（`src/XrayApi.cpp:2254-2259` 校验 grpc-status）。
2. **批处理路径没有 CLI 兜底**：`src/ProxyBatchTester.cpp:272-274` 在 `USE_GRPC_API` 下只调 `addOutboundDirect`，失败后重试 3 次即记 `XRAY_ERROR`（`:359-373`）。方案 §0.1 假定的「不在白名单 → 回退 CLI」在加入白名单后已经消失。
3. `batch_policy::shouldReportEndpointUnreachable`（`include/BatchWorkerPolicy.h:60-68`）只对 stale-tag 返回 false，`Account is missing` 返回 **true** → 每个失败目标都会触发 `onXrayEndpointUnreachable(apiPort)`，把实例健康评估链路一起拖下水。
4. `ProxyFinder.cpp:368-370` 会静默回退 CLI，掩盖问题 → 单点查找看起来正常。
5. 规模：`test/guindb.db` 中 ConfigType 7 共 **1934** 条，全部受影响。

**修复**：D5 改为「不得复用 `encodeServerEndpoint`」，容器层内联只写 field 1/2（已写入方案 Task 3 Step 4）。

---

### B2 — `finalmask`（salamander obfs）在 gRPC 路径被静默丢弃，679/1934 条 hy2 受影响

| 项 | 证据 |
| :--- | :--- |
| 生产者 | `src/config/outbound/Hysteria2OutboundBuilder.cpp:62-80`：`path` 非空或 `Extra` 含 `obfs=salamander` 时写出 `streamSettings.finalmask = {udp:[{type:"salamander", settings:{password}}]}` |
| 数据规模 | `test/guindb.db`：ConfigType 7 = 1934，`Extra` 全空，**`Path` 非空 = 679** |
| CLI 侧生效 | `infra/conf/transport_internet.go:1877-1890`（`FinalMask != nil → config.Udpmasks`），`transport/internet/hysteria/dialer.go` 用 `udpmaskManager` + `WrapPacketConnClient` |
| gRPC 侧 | `grep finalmask src/XrayApi.cpp` = **0 命中**；`transport/internet/config.proto` 的 `repeated TypedMessage udpmasks = 10` 从未编码 |

**性质**：方案把 finalmask 列在 N4「本次不做」，但 CLI 路径**会**携带、gRPC 路径**不会**，同一 profile 产生不同 outbound。这是未声明的行为变更，与 G3「不破坏既有协议」的字面承诺冲突。

**修复**：在 `addOutboundDirect` 注入前检测 `streamSettings.finalmask`，命中即回退 CLI（已写入方案 Task 3 Step 4b）。实现 udpmasks 需先确定 salamander 的 TypedMessage typeUrl，列为后续。

---

### B3 — 传输层编码失败被静默丢弃，产出「注入成功、无 auth」

**证据**
- `src/XrayApi.cpp:1384-1395`：只有 `!encodedSettings.empty()` 才写 `transportConfig`；`encodeHysteriaSettings` 失败返回空串时，整段 transport settings 消失，容器层照样注入成功。
- `transport/internet/config.go:71-81`：`CreateTransportConfig("hysteria")` 返回**空 `Config{}`（非 nil）**，因此不会 panic，更隐蔽。
- `transport/internet/hysteria/dialer.go:273,286-290`：`RequestHeaderAuth: []string{c.config.Auth}`（空串）→ 服务端拒绝 → `auth failed`。
- 触发条件可达：`settings.version == 2` 但 `hysteriaSettings.version` 缺失（模板/旧数据）时，容器层过、传输层被丢。

**修复**：注入前用 `hysteria_payload::extractTransportConfig` 校验，失败即回退 CLI（与 B2 合并为同一个前置检查，已写入 Step 4b）。

---

## 3. 重要问题（Should fix）

| 编号 | 问题 | 处置 |
| :--- | :--- | :--- |
| **S1** | G2 声称「含字节断言」，实际 10 条用例只测抽取；真正会错的 `jsonConfigToProtobuf` hysteria 分支与 `encodeHysteriaSettings` **零覆盖**（B1 正是这里出错） | 采纳：在 `tests/test_xray_api_direct.cpp`（friend 类，`:45-47` 包 `jsonConfigToProtobuf`、`:70-72` 包 `encodeStreamConfig`）补 4 条线格式用例，期望值已实测 |
| **S2** | V6 的 `Select-String '\bauto\b'` 0 命中**不可能达成**：PowerShell 默认忽略大小写，且 `HysteriaPayload.h:22` 注释含「禁止使用 auto」、`XrayApi.cpp:607/616/1465` 含 auto/AUTO | 采纳：限定文件 + `-CaseSensitive`，并对 `src/XrayApi.cpp` 改用 `git diff` 只查新增行 |
| **S3** | V4 无鉴别力：加入白名单后 `fallback to subprocess` 永不打印，实现已坏也照样「0 命中」通过 | 采纳：改判 `addOutboundDirect SUCCESS tag=proxy` 必须命中、`addOutboundDirect FAILED`/`注入xray outbound 错误` 各 0 命中、`addOutbound normalized payload` 对 hysteria 0 命中 |
| **S4** | V5 的「出现 OK/FAIL」不能证明连得通；TLS 缺失会让它假绿 | 采纳：补正向对照（至少 1 条 `sni` 非空节点必须 `delay > 0`）+ 失败时检索 `xray_stderr_*.log` 的 `tls config is nil`。数据：100/1934 条 `sni` 空且 `allowinsecure != '1'` |
| **S5** | 空值保护片段按原文粘贴会语法错误（现文件 `:2546` 以 `)) {` 收尾） | 采纳：方案已标注须把 `)) {` 改为 ` ||`，新行以 `...ClientConfig")) {` 收尾 |
| **S6** | Task 4 文档步骤过期：INDEX §7.5 已有 `#54`(AdoPayload)/`#55`(Hysteria2)、§9 已有 `#17`、`:30` 已是 85，照做会撞号重复 | 采纳：Task 4 改为「核对已登记行，只在缺失时补登 + 核对 tracker/context」 |
| **S7** | 风险表漏 finalmask、且把 TLS 的缓解写成「V5 若失败需另立 Spec」（V5 不会失败） | 采纳：风险表增 finalmask 行，TLS 判据改为正向对照 + stderr 检索 |

---

## 4. 建议（Nice to have）

| 编号 | 建议 | 处置 |
| :--- | :--- | :--- |
| **N1** | `detail::readInt` 窄化不安全（`static_cast<int>` / `strtoll`），`version:"4294967298"` 会回绕成 2 | 采纳：加 `INT_MIN/INT_MAX` 范围校验 |
| **N2** | 纯头文件拆分若不补 S1 的字节用例，只覆盖「低风险的一半」，YAGNI 质疑成立 | 已由 S1 解决 |
| **N3** | `ClientConfigAcceptsStringPort` 无真实生产者（builder 用 `std::stoi`） | 保留：为 `-IS` 导入/手工 JSON 预留，注释说明 |
| **N4** | `EXPECT_FALSE(ec)` 后仍 `as_object()`，解析失败会抛异常 | 采纳：改 `ASSERT_FALSE(ec)` |
| **N5** | `extractTransportConfig` 对非字符串 `auth` 静默退化为空串 | 采纳：`auth` 存在但非字符串 → 返回 false（回退 CLI） |

---

## 5. 确认项（已核实，可放心依赖）

| # | 结论 |
| :-: | :--- |
| C1 | `HysteriaPayload.h` 原样编译通过（严格档零 error 零 warning），无 `auto` 类型推导 |
| C2 | 10 条用例 10/10 通过；5 组变异各至少杀死 1 条，断言承重 |
| C3 | proto 字段号全对：`ClientConfig.version=1/server=2`；`ServerEndpoint.address=1/port=2/user=3`；transport `Config.version=1/auth=2` |
| C4 | 两个 typeUrl 逐字节正确；hysteria 的 `server` 确为 field 2（与 server-class 的 field 1 不同） |
| C5 | `auth` **只**来自传输层（`dialer.go:447` → `:273`），从不读 `ServerEndpoint.user` |
| C6 | 忽略 `up/down/congestion/udphop` 安全（上游仅告警）；`udp_idle_timeout` 仅服务端使用 |
| C7 | `protocol_name` 对 hysteria 已正确（`src/XrayApi.cpp:1331-1333`），无需改动 |
| C8 | 5 处插入点全部正确且位于 `#ifdef USE_GRPC_API` 内；仓库内 typeUrl 判定点仅 4 处，清单完整 |
| C9 | `extractClientConfig` 收 `settings` 子对象、`extractTransportConfig` 收 `hysteriaSettings` 子对象，契约无错位 |
| C10 | 不破坏既有测试（`ParseOutboundJsonUnknownProtocol` 用 `"myproto"`）；`ctest -N` = 55 → 56；CMake 块与本仓库惯例一致，不需要 `-lws2_32` |
| C11 | 回滚面仅 1 个头文件 + 2 个测试 + `CMakeLists.txt` + `src/XrayApi.cpp` + `include/XrayApi.h`，无数据/配置迁移 |

---

## 6. 处置汇总

| 项 | 处置 |
| :--- | :--- |
| B1 | **已采纳**：D5 重写为「不得复用 `encodeServerEndpoint`」，Task 3 Step 4 改为内联 field 1/2 |
| B2 | **已采纳**：新增 Step 4b，`finalmask` 命中即回退 CLI；N4 措辞同步 |
| B3 | **已采纳**：Step 4b 增加 `extractTransportConfig` 前置校验 |
| S1 | **已采纳**：G2 改为「10 条抽取用例 + 4 条线格式用例」，附实测十六进制 |
| S2–S5 | **已采纳**：V4/V5/V6 判据重写；S5 已在方案中标注 |
| S6 | **已采纳**：Task 4 改为核对已登记行 |
| S7 | **已采纳**：风险表新增 finalmask 行，TLS 判据改写 |
| N1/N4/N5 | **已采纳**（并入 Task 1/2 的用例与实现要求） |
| N2 | 已由 S1 覆盖 |
| N3 | 保留（注释说明用途） |

---

## 7. 关联文档

| 文档 | 说明 |
| :--- | :--- |
| `docs/specs/2026-10-20-Spec-Hysteria2-GrpcEncoder-v1.0.md` | 被评审方案（已按本评审修订） |
| `docs/reports/2026-10-20-Report-Hysteria2-GrpcEncoderFeasibility-v1.0.md` | 上游源码调查报告 |
| `docs/bugfix/2026-10-20-RCA-XrayApi-ExistingTagInjection-v1.0.md` | CLI 回退缺陷 RCA（本方案的上游动因） |
| `docs/specs/2026-10-20-Spec-XrayApi-AdoPayloadNormalization-v1.0.md` | P0 载荷归一化方案（已实施，提交 `436fc52`） |
