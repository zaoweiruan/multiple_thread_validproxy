# Bugfix: 订阅解析乱码 Shadowsocks 节点导致批量测试全失败 (2026-08-04)

## 症状

- 批量测试 217,802 个代理：`Total: 217802 / Success: 0 / Failed: 473`，随后用户手动取消
- 日志被 `[ERROR] addOutbound FAILED: exitCode=1, output=` 刷屏（2838 条，473 代理 × 3 次重试 × 2 条），每条附完整 outbound JSON（**含明文密码**）
- `[WARN] parseOutboundJson exception: syntax error [boost.json ... parse_string]` 伴随刷屏
- xray stderr 反复出现 `unknown cipher method: <乱码>`（250 条）

## 根因（RCA）

1. **数据源 malformed**：订阅源 `Argh94-ShadowSocks`
   （`https://raw.githubusercontent.com/Argh94/V2RayAutoConfig/.../ShadowSocks.txt`）
   全部 7505 行是 `ss://` 前缀但实为 VLESS/Reality 风格参数
   （`ss://%40MAconnectt@host:port?encryption=none&type=xhttp&security=reality&...`），
   userinfo 是 URL 编码的用户名而非标准 `method:password` base64。
2. **`decodeBase64()` 缺陷**（`src/update/SubscriptionParser.cpp` L523-581）：
   遍历输入时，未命中 base64 字母表的字符（如 `%`、`@`、`:`）**不被跳过**，
   而是被当作索引 0（字母 'A'）参与输出 —— 整个解码流被二进制垃圾污染。
3. **无字段校验**：ss:// 分支（L364-369）`decodeBase64` 后直接按 `:` 拆分为
   `profile.security`（method）与 `profile.id`（password），无任何合法性检查，
   二进制垃圾写入数据库。全库 **29811 个代理**（9.5%）Security/Id 为随机二进制
   （hex 如 `E35E5A7DCF1C035DDFE80E`），其中 29486 个来自上述订阅。
4. **注入链路放大**：乱码 method → ConfigGenerator 生成无效 outbound JSON →
   boost.json 解析抛 syntax error → addOutboundDirect 回退原始 payload
   （typeUrl=`xray.proxy.outbound.Config` 不在 8 种支持协议内）→ 子进程
   `xray.exe api ado --server=... stdin:` → xray 拒绝 `unknown cipher method`
   exitCode=1 → worker XRAY_ERROR 块打印完整 JSON（含密码）→ 3 次重试。

## 修复

### 1. `src/update/SubscriptionParser.cpp` — decodeBase64 跳过非法字符

非 base64 字母表字符（`%`、`@`、`:`、`_`、`-`、空白、非 ASCII）一律跳过，
不再解码为索引 0。同时移除不再使用的 `int in` 变量。

### 2. `src/update/SubscriptionParser.cpp` — ss:// 分支严格校验

新增匿名命名空间助手 `isPrintableAscii()`（0x20-0x7E 全字节校验）。
ss:// 解码后：无 `method:password` 分隔符、method/password 为空、
或含非可打印 ASCII 字节 → **丢弃节点**（`continue`），不再入库。

### 3. 代理配置错误日志降级为 DEBUG（用户要求）

| 文件 | 位置 | 变更 |
|------|------|------|
| `src/ProxyBatchTester.cpp` | worker 注入失败块 L223-229 | `注入xray outbound 错误` / `XRAY_ERROR` / `Xray output` 4 处 ERR→DEBUG |
| `src/XrayApi.cpp` | `addOutbound()` L182-183 | 子进程失败 2 条 ERR→DEBUG（worker 已报 XRAY_ERROR） |
| `src/XrayApi.cpp` | `removeOutbound()` L225 | ERR→DEBUG |
| `src/XrayApi.cpp` | `removeOutboundDirect()` L1969 | ERR→DEBUG |
| `src/XrayApi.cpp` | `addOutboundDirect()` L2120 | ERR→DEBUG |
| `src/XrayApi.cpp` | `listOutboundsDirect()` L2156 | ERR→DEBUG |
| `src/XrayApi.cpp` | `ensureWinsock()` L1552 | **保留 ERR**（系统级故障，需可见） |

同时 **XRAY_ERROR 块不再打印完整 outbound JSON**（消除明文密码泄露），
`Xray output` 截断至 300 字符。

### 4. `src/update/Deduplicator.cpp` — 去重阶段删除非法配置代理

`deduplicateConfigErrorPhase()`（Phase 4/6）扩展：遍历全表时除 `checkRequired()`
外，新增 `isPrintableAscii()` 校验 Security（cipher）与 Id（password）字段，
含非可打印 ASCII 字节的代理并入批量删除（`deleteByIndexIdsNoTx`，事务内）。
乱码行数仅以汇总计数报告，不打逐行 WARN（可能数万行，避免刷屏）。
Phase 4/6 标题同步更新为
"config-invalid proxies (checkRequired + non-printable Security/Id)"。

## 验证

- `cmake --build build --parallel 8` 构建成功
- `ctest` 全量 **21/21 通过**（39.93s）

## 遗留与操作

- **清理生产库**：运行 `validproxy-cli.exe -D`（或 UI 去重按钮）触发
  Phase 4，将删除全部 29811 个乱码代理（建议先备份 `bin/worker/guindb.db`）。
- 下次更新该订阅时新解析器将自动丢弃 malformed 节点（防复发）。
- 乱码代理在清理前仍会进入批量测试（现仅 DEBUG 级别日志，摘要 Failed 计数仍计入）。
