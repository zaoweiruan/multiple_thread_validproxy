# Bugfix: SplitHTTP nil-request Panic 应用侧防御 v1.0

- **日期**: 2026-08-10
- **模块**: `XrayApi`（gRPC Direct 注入路径） / `Xray-core`（splithttp 传输，Go 二进制）
- **关联问题**: 批量测试期间 xray 进程 panic 崩溃（`XRAY_ERROR` 类），panic 堆栈指向 splithttp `FillStreamRequest` 对 nil `*http.Request` 解引用
- **关联文档**: `docs/bugfix/2026-08-10-Bugfix-XrayInstance-StderrCapture-v1.0.md`（观测设施）、`docs/bugfix/2026-08-07-Xray-instances-are-not-automatically-restarted.md`（自动恢复）、`docs/bugfix/2026-08-07-Bug-XRAY_ERROR.md`（WSA10061）

## 1. 背景与动机

### 1.1 事实链（RCA）

1. worker 通过 gRPC `AddOutbound` 注入代理配置后，部分 xray 实例进程 panic 崩溃；StderrCapture v1.0 观测设施（stderr 尾部 4KB + 退出码）证实崩溃发生在注入期间。
2. panic 堆栈定位到 **Xray-core（Go 二进制）内部** splithttp 传输：
   - `transport/internet/splithttp/client.go` L62（commit `d2758a0`，与部署二进制一致）：
     `req, _ := http.NewRequestWithContext(context.WithoutCancel(ctx), method, url, body)`
     —— **错误被 `_` 丢弃**；
   - L63 `c.transportConfig.FillStreamRequest(req, sessionId, "")`；
   - `config.go` L295 `FillStreamRequest` 首行 `request.Header = c.GetRequestHeader()` —— **nil 解引用点**。
3. `NewRequestWithContext` 在 URL 无法解析时返回 `(nil, err)`。`dialer.go` L348 构建 URL 的方式：`url.URL{Scheme, Host, Path, RawQuery}.String()` 对 **Host 与 RawQuery 原样输出**（仅 Path 做百分号转义）。因此当 splithttp 配置的 `host` 或 `path`（含 query 部分）出现空白/控制字符（空格、`\n`、`\r`、`\t` 等 `<=0x20` 或 `0x7F`）时，生成的 URL 字符串无法被 `url.Parse` 解析 → `req == nil` → panic。
4. 空 `host` **不会**崩溃：`dialer.go` 有三级兜底（tls.ServerName → reality.ServerName → `dest.Address.String()` 服务器地址），即使全空 `"https:///path/"` 也可被 parse。
5. 应用侧 `src/XrayApi.cpp` 此前对 `streamSettings.network ∈ {splithttp, xhttp}` 的 `host`/`path` **无任何校验**（grep 证实），垃圾节点（订阅解析/手改产生的畸形 host/path）可直达 Xray-core 触发 panic。

### 1.2 修复策略选择

- **应用侧（本次实施）**：注入前校验 splithttp/xhttp 的 `host`/`path` 不含控制/空白字符，非法则拒绝注入该代理（worker 标记失败跳过），**不重建部署二进制、不改编码器**。
- **上游补丁（建议后续）**：`OpenStream` 应检查 `NewRequestWithContext` 返回的 error 并回传连接错误，而非 panic。部署二进制升级/重建时一并应用（见 §5）。

## 2. 变更范围

| 文件 | 变更 |
|------|------|
| `include/XrayApi.h` | public `#ifdef USE_GRPC_API` 块新增静态公开方法 `validateSplitHTTPSettings` 声明 |
| `src/XrayApi.cpp` | 实现 `validateSplitHTTPSettings`；`addOutboundDirect` 在 `parseOutboundJson` 之后立即调用，失败置 `lastError_` + ERR 日志 + `return false` |
| `tests/test_xray_api_direct.cpp` | 追加 `ValidateSplitHTTPSettings*` TEST_F 用例（合法/非法 host、path、xhttp、非 splithttp 放行、空 JSON 放行） |
| `docs/bugfix/2026-08-10-Bugfix-SplitHTTP-nil-request-Panic-v1.0.md` | 本文档 |

**不修改**：`encodeSplitHTTPSettings` / `encodeStreamConfig` 编码器（保持 proto 编码行为不变）、Xray-core 部署二进制（上游补丁另行跟踪）、子进程回退路径 `addOutbound`（非 Direct 注入路径，协议不在 8 个 supportedTypeUrls 时才会走，风险面极低；防御以 Direct 主路径为准）。

## 3. 设计

### 3.1 `XrayApi::validateSplitHTTPSettings`（public static）

```
bool validateSplitHTTPSettings(const std::string& streamSettingsJson,
                               std::string& errorOut)
```

- 空 JSON / 解析失败（boost::json 异常或非对象）→ `true`（不拦截；`addOutboundDirect` 后续对 streamSettings 的解析自有权衡）。
- 读取顶层 `network`：非字符串或 `network ∉ {splithttp, xhttp}` → `true`（只拦截 splithttp 系）。
- 按 network 取 `splithttpSettings` / `xhttpSettings` 对象（与 `encodeSplitHTTPSettings`/`encodeXHTTPSettings` 读取相同键：`host`→字段 1、`path`→字段 2）。
- 校验规则（对 `host`、`path` 两个字符串键，缺省或非字符串跳过）：
  - 任一字符 `<= 0x20` 或 `== 0x7F`（空白/控制字符）→ 填 `errorOut` 描述违规值，返回 `false`。
  - 空 `host` 放行（Xray-core 有 `dest.Address` 兜底，非崩溃源）。
- 全部通过 → `true`。

### 3.2 `addOutboundDirect` 接入

`parseOutboundJson` 成功拿到 `streamSettingsJson` 后（协议 encoder 检查之前）调用校验：

- 校验失败 → `lastError_ = "addOutboundDirect: <errorOut> (tag=...)"`，写 ERR 日志，`return false`。
- 效果：该代理被 worker 判定为注入失败（`XRAY_ERROR`），**不注入坏配置、不触发 Xray-core panic**。

## 4. 验证方案

### 4.1 单元测试（`tests/test_xray_api_direct.cpp`，`XrayApiDirectTest` TEST_F）

| 用例 | 断言 |
|------|------|
| `ValidateSplitHTTPSettingsOk` | 合法 splithttp（host=`example.com`、path=`/a/b/`）→ true |
| `ValidateSplitHTTPSettingsBadHostSpace` | host=`"my host"` → false，errorOut 非空 |
| `ValidateSplitHTTPSettingsBadHostNewline` | host 含 `\n` → false |
| `ValidateSplitHTTPSettingsBadPathTab` | path 含 `\t`（如 `?x=1 y=2` 带空格 query）→ false |
| `ValidateSplitHTTPSettingsXhttp` | xhttp 合法 → true；xhttp 坏 host → false |
| `ValidateSplitHTTPSettingsNonSplitHTTP` | network=`ws`/缺省 → true（不拦截） |
| `ValidateSplitHTTPSettingsEmpty` | 空串 / 非对象 → true |

### 4.2 构建与回归

```powershell
cmake --build build --parallel 8
ctest -V
```

## 5. 预期收益与后续

- **收益**：畸形 splithttp/xhttp 节点不再导致 xray 进程 panic；坏代理以可读 ERR 日志被跳过，观测设施（StderrCapture）保留用于回归验证。
- **后续（上游补丁，待 Xray-core 升级时应用）**：
  `transport/internet/splithttp/client.go` `OpenStream` 中：

  ```go
  req, err := http.NewRequestWithContext(context.WithoutCancel(ctx), method, url, body)
  if err != nil {
      return nil, err
  }
  ```

  使畸形 URL 以连接错误返回而非 panic。
- **长期**：可考虑在订阅导入稽核阶段（`SubitemUpdaterV2::isValidProxy` 同层）对 splithttp host/path 做可打印字符校验，从源头丢弃坏节点。
