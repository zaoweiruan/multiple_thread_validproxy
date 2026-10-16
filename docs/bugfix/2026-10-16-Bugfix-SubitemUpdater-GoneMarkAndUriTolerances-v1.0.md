# Bugfix: 订阅 404 失效标记与分享链接容错（误挂 vmess 前缀 / SIP001） (2026-10-16)

- 日期：2026-10-16
- 模块：SubitemUpdaterV2 / SubscriptionParser
- 版本：v1.0
- 前置文档：`2026-10-16-Bugfix-SubscriptionParser-Observability-P0-v1.0.md`（P0 可观测性与真实数据调查）、`2026-10-16-Bugfix-SubitemUpdater-RealIssues-v1.0.md`（404 检测 / ss %3D / 元数据注释）
- 分支：`Optimize-the-data-parsing-of-subscription-sources`

## 1. 症状

| # | 症状 | 真实数据（P0 实测） |
|---|---|---|
| 1 | 上游仓库 404 与真解析失败混为一谈：只有 `ERROR: Parse failed`，无 HTTP 状态、无 URL；死订阅每轮更新都把所有 method 试一遍才放弃 | 9 个 `V2RAYCONFIGSPOOL/V2RAY_SUB/v2ray_configs_no1..no9` 订阅永久 0 节点 |
| 2 | 明文 VLESS 分享链接被上游误挂 `vmess://` 前缀，解码成乱码命中 `not JSON` 整行丢弃 | crackbest line 20/399 共 2 行（`vmess://uuid@host:port?query#frag`） |
| 3 | `ss://` userInfo 的 `%3D`（URL 编码的 padding `=`）未解码导致密码被静默改错 | crackbest 74 条 ss:// 中 14 条 —— **已在 `5e1395b` 修复**，本轮任务 3 核实无回归 |
| 4 | 旧式 SIP001（整个 `method:password@host:port` 一次性 base64、链接中无 `@`）在 ss 分支 `atPos == npos` 处直接计数丢弃 | crackbest 4 条；全库单轮实测恢复 **5686** 次（6 个订阅，最大单订阅 Argh94-ShadowSocks 5588） |

## 2. 根因（RCA）

1. **fetch 层只回 content 不回状态码**。`fetchUrl`/`fetchUrlViaProxy`/`fetchUrlViaAccelerator` 返回 `std::optional<std::string>`；GitHub raw 的 404 body 是非空文本 `404: Not Found\n`（14 字节），因此走到 `parseSubscription` → 0 节点 → 被归为「Parse failed」。（`CURLINFO_RESPONSE_CODE` 可读：`FAILONERROR` 在本工程两处 curl 封装中均未设置，404 返回 CURLE_OK。）
2. **误挂前缀的载荷不是 base64**。`decodeBase64` 静默丢弃 `@` `:` `-`，把 UUID 十六进制字符当 base64 解出乱码 → `isJson == false` → `throw "not JSON"`。容错判据必须是结构性的（含 `@`、id 非空、`host:port` 且 port∈1..65535），且**必须剥 fragment 并截断 `?query`** —— 初版实现漏截 query，单测夹具（无 query）全绿、真实数据 `mislabeled` 恒 0，见 §3.5。
3. **ss 分支既无 URL 解码也无 SIP001 分支**。`:364` 调用点（现 ss 分支解码行）直接 `decodeBase64(userInfo)`；`atPos == npos` 唯一出口是 `++droppedMalformedLine; continue;`。

## 3. 修复

### 1. `include/SubitemUpdaterV2.h` — fetch 带出 HTTP 状态码 + 404 标记 API

| 项 | 内容 |
|---|---|
| 位置 | 三个 fetch 声明；`getSubscription`/`fetchUrlViaProxy` 之后新增标记段 |
| 变更 | ① 三声明各加 `long* httpStatus = nullptr` 出参（0 = 没拿到响应，与真实 4xx/5xx 可区分）；② 新增 `kSourceGonePrefix = "404-"`、`hasSourceGoneMarker()`、`markSourceGone()`、`clearSourceGoneMarker()`、`getSourceGoneSubscriptions()`、`execSqlBind()` |

### 2. `src/SubitemUpdaterV2.cpp` — 状态码透传、404 归因闸门、标记与探测循环

| 项 | 内容 |
|---|---|
| 状态码 | 三个 fetch 实现入口置 0，`curl.perform()` 后 `*httpStatus = curl.getResponseCode()`；`fetchUrlViaAccelerator` 内部两处 `fetchUrl` 调用同步透传 |
| **404 归因闸门（三分支）** | `httpStatusIsAboutOrigin = (method != Accelerator) \|\| accelerator_url.empty() \|\| isGitHubHostedUrl(url)`。**关键教训**：初版只有前两个析取 —— 而生产 `bin/config.json` 是 `update_methods:["accelerator"]` + `accelerator_url:"https://gh-proxy.com"`，条件恒 false，**404 标记功能在生产中永不触发**。修正为：加速器 + GitHub 目标时信任 404（gh-proxy 对 GitHub 透明转发、回传源站状态码）；加速器 + 非 GitHub 目标仍不信任（gh-proxy 是 GitHub 专用，404 可能属于它自己）。`isGitHubHostedUrl` 判 `github.com` / `githubusercontent.com` 子串 |
| markSourceGone | 幂等（已带前缀只置 `Enabled=0`；remarks 空则只置 Enabled+WARN）；`UPDATE SubItem SET Remarks=?1, Enabled=0 WHERE Id=?2`；REPORT 日志注明 still probed for recovery |
| clearSourceGoneMarker | **只摘 `404-` 前缀、不动 Enabled**（re-enable 是显式用户动作）；REPORT 日志注明 Enabled left as-is |
| execSqlBind | prepared statement + `sqlite3_bind_text(...,SQLITE_TRANSIENT)` ×2（Remarks 是自由文本，禁止拼接进 SQL） |
| 404 分支（processSub / updateWithMethods 两处对称） | `phaseFail++` + `markSourceGone` + ERR（文案 `marked and disabled`）+ **刻意不入 `stillFailed`**（已删除的源站不会自愈、不跨 method 重试）；非 404 的解析失败仍入 stillFailed，且 ERR 日志补 `http=`/`content=N bytes` 归因字段 |
| 成功路径 | 两处均调 `clearSourceGoneMarker`（探测恢复时自动去前缀） |
| run() 探测循环 | `goneSubs = getSourceGoneSubscriptions()`（`Enabled=0 AND Remarks LIKE '404-%'`）；每 run 在 enabledSubs 循环后逐个探测，日志 `[probe i/N]`；**刻意不过 `shouldSkipUpdate`**（Enabled=0 落在 interval 逻辑外，且探测本就是为了发现复活）；early return 改为 `enabledSubs.empty() && goneSubs.empty()` |

### 3. `src/update/SubscriptionParser.cpp` + `include/update/SubscriptionParser.h` — 两个 URI 容错 + 观察字段

| 项 | 内容 |
|---|---|
| 任务 2 改写块 | 主循环元数据注释判定之后：`vmess://` 行先 `stripFragment(substr(8))`，`looksLikePlainVlessUri` 通过则改写 `line = "vless://" + payload` + `++vmessMislabeledPlainVless`，落入既有 vless 分支。`countedAsVmess` 保持 false → 落 `otherOk` 桶，**ParseStats 等式不变量不受影响** |
| `stripFragment` | 剥第一个 `#...` |
| `looksLikePlainVlessUri` | 判据：含 `@` 且 atPos>0；hostPart 非空；**先截 `?query`**；`rfind(':')` 后 port 全数字且 1..65535。标准 base64 字母表不含 `@`，真正 vmess 载荷不可能命中 |
| 任务 4 SIP001 | ss 分支 `atPos == npos` 时先 `decodeBase64(decodePercentEscapesOnly(uri))`，结果形如 `method:password@host:port`（`rfind('@')` 有效且 `:` 在 `@` 前）则改写 uri、置 `sip001UserInfoIsPlaintext = true`、`++ssSip001Recovered`，继续走既有 SIP002 路径；解码行改三元 `sip001... ? userInfo : decodeBase64(...)`（明文不得再喂解码器）；`ssUrlCharCount` 预扫对明文路径记 0（保持「base64url 特征」口径）；仍无 `@` 才计数丢弃 |
| ParseStats | 新增两个**纯观察字段**（不进等式）：`vmessMislabeledPlainVless`、`ssSip001Recovered`；summary 日志追加 `mislabeled=` / `sip001=` |

### 4. `tests/test_subscription_parser.cpp` — 2 个新用例（18 → 20）

| 用例 | 断言要点 |
|---|---|
| `VmessMislabeledPlainVlessIsRewrittenToVless` | 2 条明文误挂行（**均带 `?query` 与 fragment**，形态取自 crackbest line 20/399）+ 1 条真 vmess base64 反向守卫；3 条全入库、`mislabeled==2`、`fail==0`、`otherOk==2`、等式、summary 含 `mislabeled=2` |
| `SsSip001WholeLinkBase64IsRecovered` | `ss://b64(method:password@host:port)` + 1 条不可恢复垃圾行；1 入库 + 1 丢弃、字段逐项断言、`sip001==1`、等式、summary 含 `sip001=1` |

### 5. 过程中发现并修复的自身缺陷（元教训）

1. **`?query` 漏截**：初版 `looksLikePlainVlessUri` 对 `hostPart` 直接取 port，真实行 `...:80?ed=2560&...` 的 port 切片是 `80?ed=2560...` 非纯数字 → 判据恒 false → 单测（夹具无 query）绿、真实数据 `mislabeled=0` 红。**这是三轮评审元结论（「夹具与现实脱节」）在本轮的重现**；修复后单测夹具强制带 `?query`。
2. **404 闸门与生产配置脱节**（见 §3.2）：只想着 proxy/direct 验证路径，没对齐生产 accelerator-only —— 由用户「是否使用加速器？」一问暴露，用户选择「修正闸门后用加速器重跑」。
3. **edit 工具参数丢失与垃圾 token**：本轮多次 edit 混入垃圾字符（`aca`/`iferse`/中文碎片）或 SchemaError 丢参数，全部靠「每次 edit 后立刻回读」兜底；含特殊字符的多行替换最终改用 PowerShell `ReadAllText/Replace` 脚本文件执行。

## 4. 验证

| 项 | 结果 |
|---|---|
| `cmake --build build --parallel 8` | EXIT 0 |
| `tests\test_subscription_parser.exe` | **EXIT 0，20/20 PASSED** |
| 全量 `ctest -j 4` | **52/53（98%）**，唯一失败 `34 - XrayManagerStartupTest` = 已知基线（需真实就绪 xray）；`UI_FLOATINGWIDGET` 本 run 通过 |
| 真实数据 run2（proxy 直连期） | 9/9 `Marked subscription source gone ... (Enabled=0; still probed for recovery)`，remarks 变为 `404-V2RAYCONFIGSPOOL-...` |
| 真实数据 run3/run4 | `[probe 1/9]`…`[probe 9/9]` 全部执行；重标记**幂等**（无 `404-404-` 叠加） |
| 真实数据 run6（**加速器**，闸门修正后） | `marked=9`、`probe=9`；6 个订阅 `sip001` 非零，合计 **5686**（Argh94-ShadowSocks 单订阅 5588）；`malformed` 中 SIP001 份额消失（byProto=s 归零对应恢复） |
| 真实数据 run8→run9 对照（crackbest 单更，**`?query` 修复前→后**） | `mislabeled` **0→2**；`fail` **2→0**；`otherOk` **476→478**；`sip001=4` 恒定（与勘察 4 条精确吻合）；bodyDecodedLen=134726 两 run 相同 ⇒ 纯代码差异 |
| 任务 3 核实 | `SsUserInfoPercentEncodedPaddingIsDecodedBeforeBase64` 用例绿（`5e1395b` 已交付，本轮零改动） |

已知基线失败（勿误判回归）：`XrayManagerStartupTest`、`RegionBatchResolverTest`、`UI_PORTCLOSE`、`UI_FLOATINGWIDGET`（并行 flaky，本 run 通过）、`NetworkMonitorTest`。

## 5. 范围外与后续

| 项 | 说明 |
|---|---|
| `anytls://` / `ssr://` / `socks://` 协议支持 | 功能扩展，用户未指派 |
| 裸 base64 行（无 scheme 前缀） | 模糊判定，20 行/6 订阅，另立文档 |
| `hy2://` 走 `substr(5)` 而实为 6 字符 | HEAD 既有轻微越界，`find('@')` 容忍而未爆，非本轮引入 |
| vmess 分支 `urlDecode` 死兜底吃 `+` | 尚无真实订阅命中，换 `decodePercentEscapesOnly` 留待后续 |
| `src/utils/Base64Utils.cpp` 同源缺陷第二处实现 | 未修，见 P0 文档 §8 |
| 探测失败入 stillFailed | probe 抓取失败（网络断）会入 stillFailed 被下一 phase 重试 —— 行为可接受（网络恢复后重试有益），暂不改 |

## 6. 变更记录

| 日期 | 版本 | 变更 |
|---|---|---|
| 2026-10-16 | v1.0 | 初版。四任务：404 标记机制 / 误挂 vmess 容错 / ss %3D（5e1395b 已交付，核实）/ SIP001 容错；含加速器三分支闸门修正与 `?query` 截断缺陷的发现记录 |
