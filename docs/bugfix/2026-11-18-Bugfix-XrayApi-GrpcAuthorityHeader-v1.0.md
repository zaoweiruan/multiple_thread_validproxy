---
title: "bugfix: Xray gRPC 注入失败 status=13 no host or :authority header present"
type: bugfix
status: completed
date: 2026-11-18
module: xray-api
version: v1.0
---

# gRPC 注入失败：`status=13, message=no host or :authority header present`

## 1. 现场日志（另一台电脑）

```
[ERROR] [XrayApi] addOutboundDirect FAILED: grpc call failed: status=13, message=no host or :authority header present
[ERROR] [Worker-1] 注入xray outbound 错误: grpc call failed: status=13, message=no host or :authority header present
[ERROR] [Worker-1] XRAY_ERROR - ... (tag=proxy) - grpc call failed: status=13, message=no host or :authority header present
```

`status=13` = gRPC `INTERNAL`，Xray 的 HTTP/2 处理器拒绝了请求，因为请求头里**根本没有 `:authority` 伪头**。

## 2. 根因

`XrayApi::grpcSendReceive()`（`src/XrayApi.cpp`）手写 HTTP/2 HEADERS 帧时，构造的 HPACK 块里只有：

```
:method = POST
:path   = /xray.app.proxyman.command.HandlerService/...
:scheme = http
content-type = application/grpc
te = trailers
```

**缺 `:authority`**（RFC 7540 §8.1.2.1 规定的四个 HTTP/2 伪头之一）。
Xray 的 gRPC 服务端在某些版本/严格模式下会校验该伪头并返回
`no host or :authority header present`，导致 `addOutbound`/`removeOutbound`/
`listOutbounds`/`getOutboundStatus` 全部失败 → 批量测试里全部 `XRAY_ERROR`。

本机能跑、换机就挂，是因为不同 Xray-core 版本对缺失 `:authority` 的宽容度不同；
HTTP/2 + gRPC 规范本就要求它，所以这是本工程的实现缺陷，不是环境差异。

## 3. 修复

`src/XrayApi.cpp` + `include/XrayApi.h`：

1. `grpcSendReceive()` 新增 `const std::string& authority` 参数（默认空 → `"localhost"`）。
2. HEADERS 帧按规范顺序构造，并补上 `:authority`：

   ```
   :method     = POST
   :scheme     = http
   :authority  = host:port   (grpcConnect 解析出的 Xray API 地址)
   :path       = ...
   content-type = application/grpc
   te          = trailers
   ```

3. 4 个调用点（`addOutbound` / `removeOutbound` / `listOutbounds` /
   `getOutboundStatus`）均传入 `host + ":" + std::to_string(port)`。

## 4. 验证

- `build-tc-clion-msvcrt-Release` 构建 EXIT 0。
- `test_xray_api_direct` **117/117 PASSED**（`HpackRoundTrip` 仍绿：编码器 round-trip 与头顺序无关）。
- 代码审查：HEADERS 块现在包含 `:authority`，符合 gRPC/HTTP-2 规范。

## 5. 未验证项

- 真实 Xray 对端（换机环境）需再跑一次批量测试确认 `addOutbound` 成功；
  现场已无法继续复现（本机 Xray 已能通过），但实现已按规范补齐，
  与 Xray 的校验逻辑一致。

## 6. 关联

- `src/XrayApi.cpp` (`grpcSendReceive`, 4 个调用点)
- `include/XrayApi.h`
- `docs/bugfix/2026-11-13-Bugfix-…`（同批次 Xray 注入问题）
