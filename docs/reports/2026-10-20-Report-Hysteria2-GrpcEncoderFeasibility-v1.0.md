# 调查报告：xray-core Hysteria2 是否存在"专属编码器"，以及本项目改用 gRPC 直连注入的可行性

**日期**: 2026-10-20
**类型**: 源码调查报告
**模块**: XrayApi (gRPC 直连 / `xray api ado` CLI 回退) / ConfigBuilder (Hysteria2 Outbound 生成)
**版本**: v1.0
**状态**: 已完成（源码级证据闭环，未改码）

---

## 1. 结论先行

**xray-core 有 Hysteria2 outbound 的 protobuf 定义（typeUrl = `xray.proxy.hysteria.ClientConfig`），但不存在"Hysteria2 专属编码器"这一层；它走的是通用 JSON → `infra/conf` 装配路径。** 换句话说：

- CLI `xray api ado` 与 gRPC `AddOutbound` **用的是同一套 proto/typeUrl**，区别只在于"谁来做 JSON→proto 的转换"——CLI 由 xray 自己做，gRPC 必须由调用方手写编码器。
- 本项目 `XrayApi::addOutboundDirect` 之所以对 Hysteria2 回退 CLI，**不是 xray 不支持，而是本项目没有为它写编码器**：`supportedTypeUrls` 白名单里没有 `xray.proxy.hysteria.ClientConfig`，且容器侧也缺 `xray.transport.internet.hysteria.Config` 的 transport_settings 编码分支。
- 因此这一项**不是"能不能"的问题，而是"值不值得"的问题**：要改 gRPC 直连，必须补齐**两层**编码器（容器 proxy_settings + 传输层 transport_settings）外加 TLS，只加 typeUrl 会"注入成功、运行必坏"。

---

## 2. 调查方法

| 项 | 说明 |
|---|---|
| **上游源码** | `E:\eclipse_workspace\Xray-core`，`git log -1` = `d2758a02 v26.3.27`，`git describe --tags` = `v1.260327.0` |
| **运行期二进制** | `E:\v2rayN-windows-64\bin\xray\xray.exe`，自报 `Xray 26.3.27 ... d2758a0 (go1.26.1 windows/amd64)` → **与上游源码同一版本** |
| **本项目** | `src/XrayApi.cpp`（2672 行）、`include/XrayApi.h`、`src/config/outbound/Hysteria2OutboundBuilder.cpp`、`src/config/ConfigBuilder.cpp` |
| **方法** | 逐文件精读 proto / `infra/conf` loader / CLI 命令实现 / 本项目编码路径，交叉比对 JSON schema 与 proto 字段 |

> 注：本次调查**只做源码级判定**，未执行网络级注入实验；文中"可注入/不可注入"均为代码路径推论，标注了对应的 file:line 依据。

---

## 3. 上游 xray-core 事实（源码级证据）

### 3.1 outbound 容器层 proto

`proxy/hysteria/config.proto`：

```proto
package xray.proxy.hysteria;

message ClientConfig {
  int32 version = 1;
  xray.common.protocol.ServerEndpoint server = 2;
}

message ServerConfig {
  repeated User users = 1;
}
```

- outbound 使用的 typeUrl = **`xray.proxy.hysteria.ClientConfig`**，已在 `proxy/hysteria/config.pb.go:147` 注册。
- 依赖的 `ServerEndpoint` 定义在 `common/protocol/server_spec.proto:12-16`：

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| 1 | `xray.common.net.IPOrDomain` | address |
| 2 | `uint32` | port |
| 3 | `xray.common.protocol.User` | user（Hysteria2 不用） |

### 3.2 JSON → conf 装配层

- `infra/conf/xray.go:52`：`outboundConfigLoader` 注册了 `"hysteria": func() interface{} { return new(HysteriaClientConfig) }`（同文件 :36 是 inbound 的 `HysteriaServerConfig`）。
- `infra/conf/hysteria.go:12-31`：

```go
type HysteriaClientConfig struct {
    Version int32
    Address *Address
    Port    uint16
}
// Build(): if c.Version != 2 { return errors.New("version != 2") }
```

  → **只支持 Hysteria2**（version 必须为 2），产出 `hysteria.ClientConfig{Version, Server: ServerEndpoint{Address, Port}}`。
- `proxy/hysteria/client.go`：`Client` / `NewClient` / `Process` 实现 outbound，底层经 `transport/internet/hysteria`（apernet/quic-go）拨号。

### 3.3 传输层（stream / transport_settings）proto

`transport/internet/hysteria/config.proto`：

```proto
message Config {
  int32 version = 1;
  string auth = 2;
  int64 udp_idle_timeout = 3;
  int32 masq_type = 4;
  string masq_file = 5;
  string masq_url = 6;
  string masq_url_rewrite_host = 7;
  bool masq_url_insecure = 8;
  string masq_string = 9;
  map<string, string> masq_string_headers = 10;
  int32 masq_string_status_code = 11;
}
```

JSON 侧（`infra/conf/transport_internet.go`）：

- `:1742` `HysteriaSettings *HysteriaConfig json:"hysteriaSettings"`；
- struct 定义 `:522-533`：`version / auth / congestion / up / down / udphop / udpIdleTimeout / masquerade`；
- `Build()` `:535-566`：同样拒绝 `version != 2`；当 `congestion / up / down / udphop` 非空时打印
  `LogWarning("congestion & up & down & udphop move to finalmask/quicParams")`
  → **v26 已把这些字段移出 proto，旧字段只告警、不致命**；
- 网络名映射 `:1019-1020`：`case "hysteria": return "hysteria"`。

### 3.4 CLI `ado` 的执行链（关键证据）

`main/commands/all/api/outbounds_add.go:34-77`（`ado` = AddOutbounds）：

```
loadArg → serial.DecodeJSONConfig(r) → conf.OutboundConfigs
        → 逐项 out.Build()        // 走上面的 infra/conf loader，接受 protocol "hysteria"
        → gRPC AddOutboundRequest{Outbound: o}
```

**这就是本次调查最关键的一条证据**：CLI 与 gRPC 最终都落到同一个 `AddOutboundRequest`，proto/typeUrl 完全一致；CLI 只是替调用方完成了 JSON→proto。所以"xray 不支持 Hysteria2"的说法不成立。

---

## 4. 本项目现状：为什么 Hysteria2 一定回退 CLI

| 位置 | 现状 | 后果 |
| :--- | :--- | :--- |
| `src/XrayApi.cpp:1331-1333` | `network == "tcp" \|\| "http" \|\| "splithttp" \|\| "hysteria"` 时才把 `protocolName`（StreamConfig field 5）设为 `network` 本身 | "hysteria" 这个名字**已经被正确写入 StreamConfig**，不是缺口 |
| `encodeStreamConfig`（:1316-1402） | transport_settings 的选择链只覆盖 `ws / grpc / kcp / http / xhttp\|splithttp`（:1344-1365），**没有 hysteria 分支** | 即使容器层编码成功，传输层 `xray.transport.internet.hysteria.Config`（携带 `auth`）也不会被发出 → **能注入、跑不通** |
| `jsonConfigToProtobuf`（:486-520） | 9 个协议布尔位（freedom/blackhole/vmess/vless/trojan/shadowsocks/2022/socks/http），无 `isHysteria` | Hysteria2 的 `settings` 无法被编码，返回 `{}` |
| `parseOutboundJson` 协议 switch（:737-782） | `freedom/blackhole/vmess/vless/trojan/shadowsocks/socks/http` 有映射；**其余统一落到 `xray.proxy.outbound.Config`（:780-782）** | Hysteria2 拿到一个错误的 typeUrl |
| `addOutboundDirect` `supportedTypeUrls`（:2503-2513） | 白名单 9 项，**不含 `xray.proxy.hysteria.ClientConfig`** | `hasProtobufEncoder == false` → DEBUG 日志 `fallback to subprocess` → `return addOutbound(...)`（:2519-2523）→ **必然走 CLI** |
| 空值保护（:2534-2550） | 7 个 server-class typeUrl 在 `encodedValue.empty()` 时回退 CLI | 若只加 typeUrl 而不加入此列表，编码失败会**注入一个残缺 outbound** |
| `src/config/outbound/Hysteria2OutboundBuilder.cpp` | 产出 `settings{address, port, version=2}` + `streamSettings{network:"hysteria", [security:"tls" + tlsSettings], hysteriaSettings{version:2, auth, up, down}, [finalmask obfs]}` | 与上游 JSON schema 对得上；`up/down` 在 v26 会被忽略并告警 |
| `src/config/ConfigBuilder.cpp` | 存在 `ProxyType::SingBox` 分支（:246/:488-499/:538-539/:590-591/:713-714） | 部分协议走 sing-box；但**批量测试实例是 xray**，故 ConfigType 7 节点被注入 xray |

> 补充：`settings{address, port}` 是**平铺**在 `settings` 顶层的（不像 vmess/vless 是 `vnext[]`、不像 trojan/ss/socks/http 是 `servers[]`），这一点决定了编码器不能复用 server-class 的数组解析代码。

---

## 5. 若要改为 gRPC 直连，需要补齐什么

### 5.1 两层编码器（缺一不可）

| 层 | proto 消息 | 必需字段 | 说明 |
| :--- | :--- | :--- | :--- |
| **容器层** proxy_settings | `xray.proxy.hysteria.ClientConfig` | field 1 `version` = 2（varint）；field 2 `server` = `ServerEndpoint{field1 address(IPOrDomain), field2 port}` | **注意：server 是 field 2**，与所有 server-class 协议（field 1）不同 |
| **传输层** transport_settings | `xray.transport.internet.hysteria.Config` | field 1 `version`；field 2 `auth` | 只加容器层 → 注入成功但**没有 auth，运行期必失败** |
| **名字** StreamConfig field 5 | `protocol_name` = `"hysteria"` | — | 现状已正确（:1331-1333） |
| **TLS** | `security == "tls"` 分支 | — | Hysteria2 必须 TLS；而 builder 仅在 `sni` 非空或 `allowinsecure == "1"` 时才输出 `tlsSettings` → **存在"无 TLS 配置"的真实风险**，需在验证阶段覆盖 |

### 5.2 代码落点清单（4 处 + 1 处保护）

| # | 文件:行 | 改动 |
| :-: | :--- | :--- |
| 1 | `src/XrayApi.cpp:780-782` 之前 | 新增 `else if (protocol == "hysteria") { typeUrl = "xray.proxy.hysteria.ClientConfig"; }` |
| 2 | `src/XrayApi.cpp:501` 与 `:503-506` | 新增 `isHysteria` 布尔位并加入 guard |
| 3 | `src/XrayApi.cpp`（`jsonConfigToProtobuf` 内） | 新增 hysteria 分支：读 `settings.at("address")` / `settings.at("port")`，输出 `encodeVarintField(1, 2) + encodeLengthDelimited(2, <ServerEndpoint>)` |
| 4 | `src/XrayApi.cpp:1365` 后 / `:1379-1383` 后 | `encodeStreamConfig` 增加 `network == "hysteria"` 的 `transportTypeUrl` 与 `encodedSettings = encodeHysteriaSettings(settingsObj)` 分支；新增成员函数 `encodeHysteriaSettings`（声明于 `include/XrayApi.h:220` 附近） |
| 5 | `src/XrayApi.cpp:2503-2513` 与 `:2534-2550` | `supportedTypeUrls` 追加 `"xray.proxy.hysteria.ClientConfig"`；空值保护列表同步追加（防止"注入残缺 outbound"） |

---

## 6. 决策对比

| 维度 | A. 保持 CLI 回退（现状，P0 已修） | B. 补齐 gRPC 两层编码器 |
| :--- | :--- | :--- |
| **功能正确性** | ✅ 已验证：P0 归一化后单 outbound 文档可被 `ado` 接受 | ⚠️ 需同时正确实现 2 层 proto + TLS，任一缺失即"注入 ok、运行坏" |
| **收益性质** | — | ⚠️ **不修复任何已知缺陷**：`existing tag found` 已由 P0（`436fc52`）闭环，hysteria 走 CLI 目前是**可用**的；本项只带来性能与风险面收益 |
| **性能** | 注入走子进程（`docs/reports/2026-07-31-Report-XrayApi-gRPC-vs-Subprocess-Performance-v1.0.md`：178 代理 / 4 worker 下 gRPC 87,981 ms vs 子进程 195,972 ms，约 2.23×；单次注入 ~6s → ~130ms，约 45×） | ✅ 恢复快速路径 |
| **覆盖度（关键折扣）** | 100%（CLI 适配器支持 `hysteriaSettings`、`finalmask` 全部字段） | ⚠️ **仅 ~65%**：`udpmasks(10)` 未编码，携带 `finalmask` 的 679/1934 条仍需回退 CLI |
| **吞吐收益（全量一轮）** | 基准 | `test/guindb.db` 口径约省 **2.05 h** worker 时间 ≈ **31 min** 墙钟（4 并发）；生产库 3077 条口径约省 **3.26 h** ≈ **49 min**（算法见 §6.1） |
| **风险面收敛** | ❌ CLI 通道特有失败模式仍在：tag 冲突、stdin 时序、子进程空输出重试、`ado` 文本日志不可判定 | ✅ hysteria 不再经过 JSON→proto 适配器；失败从"exit code + stdout 文本"变为 gRPC status |
| **上游漂移风险** | ✅ 低：JSON schema 由 xray 自己维护，v26 改字段（如 up/down 迁移）本项目免维护 | ❌ 高：v26.3 刚刚把 `up/down/udphop/congestion` 移出 proto；本项目需持续跟踪 |
| **测试成本** | ✅ 低（已有 P0 单测 + 现场验证） | ❌ 高：需单测（字节级）+ 真实节点连通性集成测试 |
| **回滚性** | — | 中（4 处改动，可 revert） |
| **结论** | **推荐保持现状** | 仅在"必须消除子进程"或"同时接入 TUIC/WireGuard 等同类协议"时立项，单独为 Hysteria2 做 ROI 偏低 |

### 6.1 吞吐收益量化

单次注入节省 ≈ 6 s − 0.13 s = **5.87 s**；按 B2 修正后仅约 65% 的 hy2 真正受益。

| 口径 | hy2 总数 | 走 gRPC（≈65%） | 省下 worker 时间 | 4 并发墙钟 |
| :--- | ---: | ---: | ---: | ---: |
| `test/guindb.db` | 1,934 | ≈1,255 | 1,255 × 5.87 s ≈ **2.05 h** | ≈ **31 min / 轮** |
| 生产库（RCA 口径） | 3,077 | ≈2,000 | 2,000 × 5.87 s ≈ **3.26 h** | ≈ **49 min / 轮** |

> 估算前提：每轮对该协议全量测试、4 并发；若每轮只抽样，收益按比例缩放。
> 另有不可量化收益：去掉每次注入的 `CreateProcess` 固定开销（杀软拦截/句柄抖动），以及编码器模式可复制到同样缺编码器的 TUIC / WireGuard / Anytls / Naive。

---

## 7. 结论与建议

1. **回答原问题**：xray-core **有** Hysteria2 outbound 的 proto 定义，但**没有"专属编码器"层**；CLI 与 gRPC 共用同一 proto，区别只在 JSON→proto 由谁完成。
2. **本项目的 CLI 回退属于设计取舍，不是缺陷**；真正的缺陷（整份配置被 `ado` 全量注入导致 `existing tag found`）已在 P0 修复，见 `docs/bugfix/2026-10-20-RCA-XrayApi-ExistingTagInjection-v1.0.md` 与 `docs/specs/2026-10-20-Spec-XrayApi-AdoPayloadNormalization-v1.0.md`。
3. **建议**：短期保持 CLI 回退；若未来确实要消除子进程，按 §5 立项补齐**两层**编码器 + TLS，并配套"字节级单测 + 真实节点连通性"验收，**不要只加 typeUrl**。
4. **顺带风险提示**：`Hysteria2OutboundBuilder` 在 `sni` 为空且 `allowinsecure != "1"` 时**不输出任何 `tlsSettings`**，而 Hysteria2 强制 TLS——该路径值得单独验证（不在本报告改动范围内）。

---

## 8. 关联文档

| 类型 | 路径 |
| :--- | :--- |
| RCA（触发本次调查的问题） | [`docs/bugfix/2026-10-20-RCA-XrayApi-ExistingTagInjection-v1.0.md`](../bugfix/2026-10-20-RCA-XrayApi-ExistingTagInjection-v1.0.md) |
| P0 实施方案（已实施） | [`docs/specs/2026-10-20-Spec-XrayApi-AdoPayloadNormalization-v1.0.md`](../specs/2026-10-20-Spec-XrayApi-AdoPayloadNormalization-v1.0.md) |
| 性能对照（gRPC vs 子进程） | [`docs/reports/2026-07-31-Report-XrayApi-gRPC-vs-Subprocess-Performance-v1.0.md`](./2026-07-31-Report-XrayApi-gRPC-vs-Subprocess-Performance-v1.0.md) |
| 上游源码 | `E:\eclipse_workspace\Xray-core` @ `d2758a02 v26.3.27` |
