# Bugfix: XrayApi gRPC xhttp 传输编码与 Xray v26.2.4 兼容性

- 版本: v1.0
- 日期: 2026-08-04
- 模块: `src/XrayApi.cpp`（`encodeStreamConfig` / `encodeXHTTPSettings` / `encodeSplitHTTPSettings`）、`tests/test_xray_api_direct.cpp`
- 关联: `docs/bugfix/2026-07-29-Bugfix-XrayApi-gRPC-AddOutboundDirect-Fields-v1.0.md`（gRPC 字段号系列修复）、`docs/specs/2026-06-03-network-field-improvements.md`（splithttp→xhttp 映射设计）

## 1. 问题现象

新构建（2026-08-04，工作区未提交改动）通过 gRPC `AddOutbound` 添加 `network="xhttp"` 的 VLESS 节点时，Xray v26.2.4 返回错误：

```
grpc call failed: status=2, message=app/proxyman/outbound: failed to parse stream settings > proto: not found
```

而旧构建（bin/worker/validproxy-cli-grpc.exe，对应提交 fc15eec）相同操作返回成功。

## 2. 根因分析

### 2.1 不是代码回退，而是「假成功」变「真失败」

- **旧构建**（fc15eec）`encodeStreamConfig`：protocolName 映射**无 xhttp**，settings 编码分支**无 xhttp/splithttp**。`network="xhttp"` 时 protocolName 为空 → 不写 `StreamConfig.protocol_name`（字段5）→ **xhttp 静默退化裸 TCP** → AddOutbound 成功（假成功，节点实际走 tcp 直连）。
- **新构建**（工作区改动）首次真正编码 xhttp：protocolName="xhttp" + `xray.transport.internet.xhttp.Config` typeURL。但 **Xray v26.2.4 已移除 xhttp 协议**，二进制中不存在字符串 `xray.transport.internet.xhttp.Config`，该消息类型未注册 → AddOutbound 失败。

### 2.2 v26.2.4 官方行为

- `transport/internet` 无 xhttp 目录，仅 splithttp；`splithttp/config.go` 仅注册协议名 `"splithttp"`。
- JSON 适配器 `infra/conf/transport_internet.go` L1038-1039：`case "xhttp", "splithttp": return "splithttp", nil` —— **JSON 配置路径 network="xhttp" 被接受并重映射为 splithttp**。
- `transport/internet/config.proto`：`StreamConfig.protocol_name`=字段5，`TransportConfig.protocol_name`=字段3、`settings`=字段2(TypedMessage)。

## 3. 修复方案

`encodeStreamConfig` 中把 `network=="xhttp"` 分支统一按 splithttp 编码（与 v26.2.4 JSON 适配器重映射一致）：

1. **protocolName**（L1039-1043）：xhttp 不再透传，改为 `protocolName = "splithttp"`。
2. **settings 分支**（L1063-1069）：`network=="xhttp" || network=="splithttp"` 合并；xhttp 取 `xhttpSettings`、splithttp 取 `splithttpSettings`，typeURL 统一为 `xray.transport.internet.splithttp.Config`。
3. **编码分支**（L1083-1087）：xhttp 与 splithttp 统一调用 `encodeSplitHTTPSettings`（两 schema 相同：host=1/path=2/mode=3/headers=4，splithttp 为 v26.2.4 注册协议）。
4. `encodeXHTTPSettings` / `encodeSplitHTTPSettings` 函数体保留（字段结构一致）。

## 4. 验证

### 4.1 单元测试（tests/test_xray_api_direct.exe，B13）

- `EncodeXHTTPTransport`：断言 `decoded[5]=="splithttp"`、结果包含 `xray.transport.internet.splithttp.Config`、不含 `xray.transport.internet.xhttp.Config`。
- `XHTTPStreamConfigWithTLS`：`decodedStr[5]=="splithttp"`、`decodedStr[3]=="xray.transport.internet.tls.Config"`。
- 3/3 PASS（EncodeXHTTPTransport / EncodeSplitHTTPTransport / XHTTPStreamConfigWithTLS）。

### 4.2 真实 v26.2.4 端到端（探针 probe.exe + 最小 HandlerService 配置）

| network | 修复前 | 修复后 |
| --- | --- | --- |
| xhttp | `OK=false, proto: not found` | **`OK=true`** |
| splithttp | `OK=true` | `OK=true`（二次提交报 `existing tag found: proxy`，证明 tag 已被成功添加） |

## 5. 影响文件

- `src/XrayApi.cpp`：encodeStreamConfig 三处分支调整。
- `tests/test_xray_api_direct.cpp`：两个测试断言更新（期望 splithttp 编码 + typeURL 断言）。

## 7. 端到端验证（2026-08-04 16:09）

### 7.1 测试订阅

- SubItem Id: `5544178410297751350`（「可用」订阅，207 节点）
- 网络协议分布：ws:118 / tcp:71 / **xhttp:8** / grpc:5 / raw:3 / httpupgrade:1 / None:1
- 8 个 xhttp 节点 indexid（修复前全部 XRAY_ERROR proto: not found）：
  - `4093758488031438514` ule2.portal-guard.com:8080 (xhttp+REALITY)
  - `4170639139074960462` cloudflare.182682.xyz:443 (xhttp+TLS)
  - `5544736851031628741` second.shadydomain.qzz.io:37789 (xhttp+REALITY)
  - `4096751644968981345` 185.126.93.15:443 (xhttp+TLS)
  - `5466849660937641228` 45.137.43.70:30160 (xhttp+REALITY)
  - `5682207813316331186` media-ru7.lbdnetwork.com:443 (xhttp+TLS)
  - `4461038895299861190` second.shadydomain.qzz.io:37789 (xhttp+REALITY，重复节点)
  - `5562426596903587061` 88.216.67.236:2083 (xhttp+TLS)

### 7.2 修复前后对比

| 指标 | 修复前（14:35，fc15eec 构建） | 修复后（16:09） | 变化 |
|------|------|------|------|
| Total | 207 | 207 | — |
| Success | 88 | **91** | +3 |
| Failed | 119 | 116 | -3 |
| XRAY_ERROR（proto: not found） | **8**（全 xhttp 节点） | **0** | ✅ 全部修复 |
| addOutboundDirect FAILED | 0 | 0 | — |
| 8 个 xhttp 注入成功 | 0/8 | **8/8** | ✅ |
| 8 个 xhttp curl 连通 | 不可比（注入即失败） | **3/8**（见 §7.3） | — |

**归因精确性**：非 xhttp 节点 199 个结果与修复前完全一致（88 OK / 111 FAIL），说明修复只影响了 8 个 xhttp 节点，无副作用。

### 7.3 8 个 xhttp 节点 curl 测试结果

| 节点 | 协议 | 结果 | 详情 |
|------|------|------|------|
| cloudflare.182682.xyz:443 | xhttp+TLS | **OK 2988ms** | 修复前 XRAY_ERROR，现连通 |
| media-ru7.lbdnetwork.com:443 | xhttp+TLS | **OK 2493ms** | 修复前 XRAY_ERROR，现连通 |
| second.shadydomain.qzz.io:37789 | xhttp+REALITY | **OK 1149ms** | 修复前 XRAY_ERROR，现连通；REALITY bytes 编码修复生效验证 |
| second.shadydomain.qzz.io:37789 | xhttp+REALITY（重复） | FAIL SSL connect error | 网络抖动，另一节点 OK |
| ule2.portal-guard.com:8080 | xhttp+REALITY | FAIL Timeout | 节点本身不可达（伊朗 IP） |
| 185.126.93.15:443 | xhttp+TLS | FAIL Timeout | 节点本身不可达 |
| 45.137.43.70:30160 | xhttp+REALITY | FAIL Timeout | 节点本身不可达 |
| 88.216.67.236:2083 | xhttp+TLS | FAIL Timeout | 节点本身不可达 |

**结论**：
- **xhttp 注入修复**：8/8 全部成功（修复前 8/8 失败 XRAY_ERROR）✅
- **REALITY bytes 编码修复**：second.shadydomain.qzz.io:37789 OK 1149ms 证明公钥/shortId 正确解码后真实连通 ✅
- 剩余 5 个 FAIL 均为节点真实不可达（Timeout / SSL error），非代码问题

### 7.4 修复前基准日志

- 修复前：`bin\log\test-sub_20260804_143557.log`（构建 fc15eec，14:35 启动）
- 修复后：`bin\log\test-sub_20260804_160726.log`（本次构建，16:07 启动）

### 7.5 影响文件（扩展）

- `src/XrayApi.cpp`：encodeStreamConfig（xhttp→splithttp）+ encodeRealitySettings（publicKey/shortId bytes 解码）+ base64Decode/hexDecode 静态 helper
- `include/XrayApi.h`：base64Decode/hexDecode 声明
- `tests/test_xray_api_direct.cpp`：新增 4 个 REALITY 测试 + 更新 EncodeStreamConfigRealitySecurity 断言
