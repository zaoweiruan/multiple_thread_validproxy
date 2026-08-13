# Bugfix: shadowsocks-2022 (SS2022) outbound gRPC 注入编码修复

- **Date**: 2026-08-12
- **Severity**: High (SS2022 代理批量测试全部失败：Xray 拒绝 `2022-blake3-*` 未知加密方法)
- **Affected file**: `src/XrayApi.cpp`（编码层，无公共 API 变更）
- **Regression**: 新增 7 项 Shadowsocks 单测；构建 347/347、ctest 24/24

## Bug

`jsonConfigToProtobuf()` 将所有 Shadowsocks 出站统一编码为 legacy `xray.proxy.shadowsocks.ClientConfig`（内部嵌套 `xray.proxy.shadowsocks.Account`，含 cipher_type 枚举）。该 protobuf 消息类型的 cipher 枚举 **不包含** shadowsocks-2022 的 `2022-blake3-aes-128-gcm` / `2022-blake3-aes-256-gcm` / `2022-blake3-chacha20-poly1305`，Xray 解析时报：

```
unsupported shadowsocks cipher method: 2022-blake3-aes-256-gcm
```

→ SS2022 节点注入失败、批量测试全部失败（此前 `docs/plans/2026-08-10-Plan-ImportProxyValidation-v1.0.md` 根因分析 B 类：SS Unsupported cipher 为最高频失败类别之一）。

## Root Cause

`xray.proxy.shadowsocks.Account` 的 cipher_type 为有限枚举（aes-128-gcm=5 / aes-256-gcm=6 / chacha20-ietf-poly1305=7 / xchacha20-ietf-poly1305=8 / none=9），不存在表示 `2022-blake3-*` 的枚举值；而 Xray 对 SS2022 使用独立的 `xray.proxy.shadowsocks_2022` 包（`ClientConfig` / `ServerConfig` / `Account{key=1}`）。JSON 适配层（`proxy/shadowsocks_2022/config.go`）将 `"method":"2022-blake3-..."` + `"password":"<raw key>"` 映射到新消息，因此编码层必须按 method 前缀 `2022-` 特征检测并切换消息类型。

## Fix（Method A — 编码层切换）

在 `src/XrayApi.cpp` 5 处修改：

1. **`jsonConfigToProtobuf` 增加特征标志**：`const bool isShadowsocks2022 = (typeUrl == "xray.proxy.shadowsocks_2022.ClientConfig");`；supported-check 条件纳入 `!isShadowsocks2022`。

2. **新增 `isShadowsocks2022` 编码分支**（位于 legacy `isShadowsocks` 分支之后、`isSocks` 之前，命中即早返回）——按 `xray.proxy.shadowsocks_2022.ClientConfig` 扁平结构编码，**无 Account/ServerEndpoint 嵌套**：
   ```cpp
   //   IPOrDomain address = 1; uint32 port = 2;
   //   string method = 3; string key = 4;
   std::string password, method;
   if (srv.contains("password") && srv.at("password").is_string())
       password = srv.at("password").as_string().c_str();
   if (srv.contains("method") && srv.at("method").is_string())
       method = srv.at("method").as_string().c_str();
   if (method.empty() || password.empty()) return {};   // 回退子进程路径

   std::string cfg;
   cfg += encodeLengthDelimited(1, encodeIPOrDomain(addr));   // address
   cfg += encodeVarintField(2, static_cast<uint64_t>(port));  // port
   cfg += encodeString(3, method);                            // method
   cfg += encodeString(4, password);                          // key（原样携带）
   return cfg;
   ```
   要点：`key` 字段携带 `password` 字段原始字符串（配置模型无独立 key 字段，profile 的 password 即 raw key）；method/key 任一为空返回空串 → 走既有子进程回退路径。

3. **`parseOutboundJson` shadowsocks 分支做 method 特征检测**：默认 `xray.proxy.shadowsocks.ClientConfig`；解析 `settings.servers[0].method`，`method.rfind("2022-", 0) == 0` 时改选 `xray.proxy.shadowsocks_2022.ClientConfig`。（注意：`outboundObj` 为非 const `bj::object&`，须用 `const boost::json::value*` + `is_object()`/`as_object()` 链式判空，不可用 `const object*`——对象无 `is_object()` 且 `if_contains` 返回非 const 指针。）

4. **`supportedTypeUrls[]` 注册表新增** `"xray.proxy.shadowsocks_2022.ClientConfig"` → `hasProtobufEncoder` 为 true，走 gRPC 直连注入（不再子进程回退）。

5. **空配置服务端类回退检查新增** `typeUrl == "xray.proxy.shadowsocks_2022.ClientConfig" ||` → 2022 编码结果为空时仍回退子进程路径。

### 依据的 protobuf 定义（`E:\eclipse_workspace\Xray-core\proxy\shadowsocks_2022\config.proto`，权威）

```protobuf
message ClientConfig {
  xray.common.net.IPOrDomain address = 1;
  uint32 port = 2;
  string method = 3;
  string key = 4;
  bool udp_over_tcp = 5;
  uint32 udp_over_tcp_version = 6;
}
```

## 回归保护

- 新增 7 项 `XrayApiDirectTest` 用例（tests/test_xray_api_direct.cpp）：
  - `ParseOutboundJsonShadowsocks2022` / `...Legacy` / `...NoServers`（method→typeUrl 映射与空 servers 回退）
  - `JsonConfigToProtobufShadowsocks2022Wire`（断言扁平线格式：`encodeLengthDelimited(1, encodeLengthDelimited(1, <4 字节 IP>))` + field2 port varint + field3 method + field4 key；断言结果**不含** `xray.proxy.shadowsocks.Account`）
  - `JsonConfigToProtobufShadowsocksLegacyWireUnchanged`（legacy 路径回归守卫：仍含 `xray.proxy.shadowsocks.Account`）
  - `JsonConfigToProtobufShadowsocks2022MissingKey` / `...MissingMethod`（空串回退）
- 单测关键事实：`jsonConfigToProtobuf` 期望 valueJson 为 `{"settings":{"servers":[...]}}` 包裹结构（servers/vnext 提取依赖 settings 包装）。

## Verification

- `cmake --build build --parallel 8` → 347/347 成功（仅存量 `-Wmissing-field-initializers` 警告）
- `ctest`（build 目录）→ **24/24 通过**（含 XrayApiDirectTest 10.27s）
- 手工 E2E：注入 SS2022 节点后 Xray 启动无 unknown-cipher 报错（见会话验证记录）
