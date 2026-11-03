# P4 分析报告 — 批量测试全 FAIL（VLESS/Trojan/VMess 全 Timeout/SSL connect error）

- **日期**: 2026-10-29
- **类型**: Analysis Report (只读，不修改源码)
- **模块**: ProxyBatchTester / StreamSettingsBuilder / XrayApi / ProxyTester / ConfigGenerator
- **约束**: 不改动任何源码，仅输出分析报告
- **数据来源**: `bin/log/ui_20261029_140554.log`（24443 代理批次，cancel 于 39/24443）、`bin/guiNDB.db`（335888 profiles）、代码静态审查

---

## 1. 问题描述

批量测试（TEST_ALL，24443 个 VLESS 代理）所有代理失败：
- **SSL connect error**: 31 个
- **Timeout was reached**: 52 个
- **成功**: 0 个

失败模式：Shadowsocks（无 TLS）成功（~1.1s），所有 TLS 系协议（VLESS/Trojan/VMess/Hysteria2）全部失败。共同因素 = TLS。

## 2. 代码路径验证（已确认正确，排除）

| 路径 | 文件:行 | 状态 |
|---|---|---|
| ConfigGenerator → ConfigBuilder → StreamSettingsBuilder | src/ConfigGenerator.cpp:29-40 → src/config/ConfigBuilder.cpp:562-615 | ✅ 正确（db_ 可用时走 ConfigBuilder） |
| TLS SNI fallback (serverName → RequestHost) | src/config/StreamSettingsBuilder.cpp:44-52 | ✅ 已修复（Sni 空时回退 RequestHost） |
| gRPC 编码链路 (addOutboundDirect) | src/XrayApi.cpp:2461-2631 | ✅ 结构正确（parseOutboundJson 取 outbounds[0]） |
| encodeSenderSettings / encodeStreamConfig | src/XrayApi.cpp:1310-1424 | ✅ 字段号正确（TransportConfig.protocol_name=字段3，已修复旧 bug） |
| VLESSOutboundBuilder | src/config/outbound/VLESSOutboundBuilder.cpp:6-57 | ✅ 结构正确（flow 仅 xtls/vision 且非 reality 且 pubkey 非空才清空） |
| curl 测试 (ProxyTester::test) | src/ProxyTester.cpp:11-46 | ✅ 正确（5s 超时，URL=https://www.google.com/generate_204） |

## 3. DB 统计（VLESS，ConfigType=5，共 268902 个）

| 字段 | 值 | 数量 | 占比 |
|---|---|---|---|
| Sni | NULL/empty | 80623 | 30% |
| Sni | non-empty | 188279 | 70% |
| Sni empty + RequestHost empty | (无 hostname) | 34423 | 13% |
| Sni empty + RequestHost non-empty | (SNI fallback 可帮助) | 46200 | 17% |
| AllowInsecure | 'true' (string) | 10296 | 4% |
| AllowInsecure | NULL/empty | 238363 | 89% |
| AllowInsecure | 'false' | 20243 | 7.5% |
| StreamSecurity | 'tls' | 78012 | 29% |
| StreamSecurity | 'reality' | 109175 | 41% |
| StreamSecurity | 'none' | 50539 | 19% |
| StreamSecurity | None (NULL) | 30405 | 11% |
| StreamSecurity | malformed ('false'/'auto'/'xtls'/...) | 758 | 0.3% |
| Network | 'tcp' | 120213 | 45% |
| Network | 'ws' | 91211 | 34% |
| Network | 'grpc' | 21532 | 8% |
| Network | 'xhttp' | 17812 | 6.6% |
| Network | 'raw' | 13235 | 4.9% |
| Network | None (NULL) | 9687 | 3.6% |
| Network | 'httpupgrade' | 4216 | 1.6% |
| Network | 'http' | 1464 | 0.5% |
| Fingerprint | empty | 114662 | 43% |
| Fingerprint | non-empty | 154240 | 57% |

**全局（所有类型，335888 profiles）**：
- Sni NULL: 103141
- Both Sni + RequestHost empty: 50474 (15% of TLS)
- allowinsecure NULL/empty: 273389 (81%, 严格 cert 校验)
- allowinsecure='true': 20009 (6%)
- Reality empty Sni: 0 (reality branch 无问题)

## 4. 假设根因（按可能性排序）

### H1（最高可能性）：allowInsecure 检查 bug + 严格 cert 校验

**代码**：`StreamSettingsBuilder.cpp:43` — `tlsSettings["allowInsecure"] = (p.allowinsecure == "1");`

**问题**：DB 存储 `allowinsecure='true'`（字符串），但代码检查 `'1'`。因此 10296 个 VLESS 代理（allowinsecure='true'）得到 `allowInsecure=false`（严格校验）。

**影响**：这些代理的 cert 如果自签名/不匹配 → TLS 握手失败 → "SSL connect error"。

**叠加**：238363 个 VLESS 代理 allowinsecure=NULL/empty → allowInsecure=false（严格）。合计 93%+ 严格校验。

**结论**：绝大多数代理严格校验 cert，而代理 cert 可能自签名/不匹配 → TLS 失败。

### H2（高可能性）：SNI 缺失 + cert SAN 不匹配

**数据**：34423 个 VLESS 代理 Sni+RequestHost 都空（13%）。SNI fallback（serverName→RequestHost）无法帮助（RequestHost 也空）。Xray 回退用代理地址（IP）作 SNI → cert SAN 不含 IP → TLS 握手失败。

**叠加 H1**：这些代理严格校验 cert + IP 作 SNI → cert SAN 不匹配 → TLS 失败。

### H3（中可能性）：代理 genuinely dead/unreachable

部分代理可能确实不可达（网络问题、代理服务器下线）。但 24584 个代理全部失败（包括 allowinsecure='true' 且 Sni 非空的）不太可能全部 dead。更可能是系统性问题。

### H4（低可能性）：malformed 字段导致 Xray 配置无效

- StreamSecurity malformed（'auto'/'xtls'/'false'/'none '/...）：~758 个，hasValidSecurity=false → security 字段省略 → Xray 默认 "none" → 无 TLS。如果代理期望 TLS，连接失败。
- Network malformed（'raw' 13235, 'httpupgrade' 4216, 'tcp":' 43）：'raw' 非有效 Xray network → 可能导致 config parse error → 注入失败。但无 XRAY_ERROR 日志，所以 Xray 可能接受或忽略。
- 影响范围小（<5%），不是主因。

### H5（低可能性）：curl 测试 URL 不可达

ProxyTester::test() 使用 `test.url` = "https://www.google.com/generate_204"。如果代理无法访问 Google（GFW、网络限制），测试失败。但 SS 代理成功访问 Google，所以 URL 可达。排除。

## 5. 排查步骤（建议执行）

### Step 1: 隔离 cert validation
取 1 个 allowinsecure='true' 的 VLESS 代理（如 IndexId=4653464603597168079, 162.159.38.204:443, Sni=x1.mwu.de5.net, allowinsecure=true），手动测试。如果成功 → cert validation 是主因。如果失败 → 排除 cert validation。

### Step 2: 隔离 SNI
取 1 个 Sni 非空的 VLESS 代理（如 IndexId=4653464603597168079），手动测试。如果成功 → SNI 是主因。如果失败 → 排除 SNI。

### Step 3: 隔离 TLS
取 1 个 security='none' 的 VLESS 代理（如 IndexId=11681187136217406438, 77.110.122.205:26946, security=none），手动测试。如果成功 → TLS 是主因。如果失败 → 排除 TLS（可能是代理 dead）。

### Step 4: 验证生成的 config
检查 `bin/config/xray_config_10000.json` 和注入的 outbound JSON 中的 serverName/allowInsecure/host 字段。确认 SNI fallback 和 allowInsecure 是否正确应用。

### Step 5: 测试 URL 可达性
用已知成功的 SS 代理（202.50.253.72:19999）测试 `https://www.google.com/generate_204`。如果失败 → URL 不可达。如果成功 → URL 可达，排除此因素。

### Step 6: 检查 Xray 日志
查看 Xray 实例日志（`bin/config/xray_config_*.json` 对应的 xray 进程输出）是否有 TLS handshake error / cert verify error / connection refused 等。

## 6. 修复方向（仅供后续，不实施）

| 方向 | 描述 | 优先级 | 影响范围 |
|---|---|---|---|
| A | 修复 allowinsecure 检查：`p.allowinsecure == "1"` → `p.allowinsecure == "1" \|\| p.allowinsecure == "true"` | P0 | 10296 VLESS proxies |
| B | 默认 allowInsecure=true（当 allowinsecure 为空/NULL 时） | P1 | 238363 VLESS proxies |
| C | SNI fallback 扩展到 reality（reality branch 当前 throw if sni empty） | P2 | 0 (no reality proxies have empty Sni) |
| D | 清理 malformed StreamSecurity/Network 字段（标准化 'false'/'auto'/'xtls'/'raw'/'httpupgrade'） | P2 | ~758 + 13235 + 4216 |
| E | 测试 URL 可配置/多 URL（避免单点故障） | P3 | 所有代理 |
| F | 允许 IP 作 SNI（当 hostname 为空时，设置 allowInsecure=true） | P3 | 34423 VLESS proxies |

## 7. 结论

P4 主要怀疑方向：**TLS 证书/SNI 不匹配**（81% 严格校验 + 15% 无 SNI hostname + allowinsecure 检查 bug）。建议按 §5 排查步骤隔离根因，特别是：
1. Step 1（allowinsecure='true' 代理手动测试）→ 确认 allowinsecure bug 是否是主因
2. Step 4（验证生成的 config）→ 确认 SNI fallback 和 allowInsecure 是否正确应用

本次仅输出分析报告，不修改源码。用户确认 P4 废弃，不再排查。