# Bugfix: XrayApi gRPC addOutboundDirect/removeOutboundDirect — wrong protobuf field and gRPC path

- **Date**: 2026-07-29
- **Severity**: Medium (gRPC API would fail silently with Xray rejection)
- **Affected file**: `src/XrayApi.cpp`

## Bugs Found

### Bug 1: TypedMessage in wrong protobuf field

`addOutboundDirect()` placed the `TypedMessage` (containing `type` and `value` for proxy settings) in protobuf **field 2** (`sender_settings`) instead of **field 3** (`proxy_settings`).

Per `core/config.proto`:
```protobuf
message OutboundHandlerConfig {
  string tag = 1;
  SendOutbound sender_settings = 2;     // <-- was incorrectly used
  TypedMessage proxy_settings = 3;      // <-- correct field (fixed)
  ...
}
```

**Fix**: Changed `encodeLengthDelimited(2, typedMsg)` → `encodeLengthDelimited(3, typedMsg)` in `addOutboundDirect()`.

### Bug 2: Wrong gRPC service path

Both `addOutboundDirect()` and `removeOutboundDirect()` used the gRPC path prefix `/xray.core.app.proxyman.command.CommandService/` — wrong package, wrong service name.

Per `command.proto`:
```protobuf
package xray.app.proxyman.command;  // <-- note: NOT xray.core
service HandlerService {            // <-- note: NOT CommandService
  rpc AddOutbound(AddOutboundRequest) returns (AddOutboundResponse);
  rpc RemoveOutbound(RemoveOutboundRequest) returns (RemoveOutboundResponse);
}
```

**Correct** gRPC paths:
- `addOutboundDirect`: `/xray.app.proxyman.command.HandlerService/AddOutbound`
- `removeOutboundDirect`: `/xray.app.proxyman.command.HandlerService/RemoveOutbound`

**Fix**: Changed both RPC paths from `/xray.core.app.proxyman.command.CommandService/` to `/xray.app.proxyman.command.HandlerService/`.

### Bug 3: JSON shape mismatch causing parse fallback

`addOutboundDirect()` assumed the ConfigGenerator JSON had shape `{"outbound": {"tag": ..., "outbound": {"type": ..., "value": ...}}}` and read `rootObj["outbound"]` (singular), but `ConfigGenerator` actually produces a full Xray config JSON: `{"outbounds": [{...}]}` (plural array). Boost.JSON `operator[]` inserts a null for missing key `"outbound"`, then `.as_object()` on null throws → caught by catch block → fallback to raw tag + default proxy config.

The old `addOutbound()` (CLI-based) never had this issue because it pipes the full JSON directly to `xray api ado stdin:` via `runProcess()` without parsing.

**Fix**: Extracted JSON parsing into `XrayApi::parseOutboundJson()` static method that:
1. Parses `{"outbounds":[{...}]}` array format
2. Extracts `tag` from the first outbound object
3. Maps protocol (`freedom`/`blackhole`/`vmess`/`vless`/`trojan`/`shadowsocks`/`socks`/`http`) to correct `typeUrl` (e.g., `xray.proxy.freedom.Config`)
4. Serializes remaining config (minus tag) as `TypedMessage.value` JSON bytes
5. Falls back to raw tag + default treatment on any parse failure

## Verification

- 10 new parseOutboundJson unit tests: freedom, blackhole, vmess, socks, http, unknown protocol, invalid JSON, missing outbounds, empty outbounds, null tag
- All 33 tests pass with `USE_GRPC_API=ON`

- 4 new protobuf encoding unit tests added in `tests/test_xray_api_direct.cpp`:
  1. `EncodeTypedMessage` — verifies TypedMessage wire format
  2. `EncodeOutboundHandlerTagOnly` — verifies tag-only encoding
  3. `EncodeOutboundWithProxySettings` — verifies proxy_settings at field 3
  4. `EncodeAddOutboundRequest` — verifies full AddOutboundRequest wrapper
- All 23 tests pass with `USE_GRPC_API=ON`
- Build: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DUSE_GRPC_API=ON` → OK

## Files Changed

| File | Change |
|------|--------|
| `src/XrayApi.cpp` | `encodeLengthDelimited(2,...)` → `encodeLengthDelimited(3,...)` in `addOutboundDirect()` |
| `src/XrayApi.cpp` | Both gRPC paths fixed: `CommandService` → `HandlerService`, `xray.core` → `xray` |
| `src/XrayApi.cpp` | JSON parsing extracted to `parseOutboundJson()` — reads `outbounds[]` array, maps protocol→typeUrl, serializes TypedMessage.value |
| `include/XrayApi.h` | Added `parseOutboundJson()` static method declaration |
| `tests/test_xray_api_direct.cpp` | Added 4 protobuf encoding + 10 parseOutboundJson unit tests |

---

# 2026-07-31 补充修复：AddOutboundDirect 编码字段缺陷 (v1.1)

- **Date**: 2026-07-31
- **Severity**: High（port 字段静默丢失影响实际转发连通性）
- **Affected file**: `src/XrayApi.cpp`

## Bug 4: protocol 原值被忽略

`AddOutboundDirect()` 编码 outbound protocol 时误用 `unsigned_value_0`（恒为 0），导致 protocol 字段总是写入错误值。

**Fix**: 改为读取配置原值 `outboundConfig.at("protocol").as_string().c_str()`。

## Bug 5: SenderConfig 字段号错误

`encodeSenderSettings()` 的 `SenderConfig` 字段号原为 1/3，正确值应为：

```protobuf
message SenderConfig {
  StreamConfig stream_settings = 2;
  MultiplexingConfig multiplex_settings = 4;
}
```

以 Xray-core 源码 `app/proxyman/command/command.proto` 为准（原 bug 文档建议的 1/3 有误）。

**Fix**: `stream_settings=2`、`multiplex_settings=4`。

## Bug 6: port 字段被静默丢弃 (int64 守卫缺失)

`encodeStreamConfig()` 的 port 处理守卫只接受 `is_uint64()`，但 boost::json 解析 JSON 整数（`"port": 443`）与测试赋值 `stream["port"] = 443` 均产生 **int64**，导致 `is_uint64()` 恒为 false、port 字段从未被序列化 —— 生产环境 AddOutboundDirect 流程中 port 实际从未写入。

**Fix**: 守卫扩展为 uint64 直接编码；int64 且 >0 时转 uint64 编码；非数值/非正数省略（Xray 侧使用默认值）。

## 复核结论

- `src/config/StreamSettingsBuilder.cpp::addOutboundDirect()`：**无缺陷**，无需修改。
- 无 name 单条配置默认 `"proxy"`：已正确实现。
- 非空 tag 校验与重复 tag 检测：已实现。

## Verification (2026-07-31)

- 新增 10 个单元测试（44 → 54）：
  - `EncodeMultiplexConfigEnabled/Concurrency`
  - `EncodeStreamConfigProtocolName/WsTransport/TlsSecurity/Port`
  - `EncodeSenderSettingsStreamOnly/MuxOnly/Both/Neither`
- `ctest -R XrayApiDirectTest -V` → **54/54 PASSED**（含此前失败的 `EncodeStreamConfigPort`）
- Build: `cmake --build build --target test_xray_api_direct --parallel 8` → OK
