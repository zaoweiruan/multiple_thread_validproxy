# Bugfix: 订阅解析真实问题修复（404 失效检测 / ss:// URL 解码 / 元数据注释） (2026-10-16)

- 日期：2026-10-16
- 模块：src/SubitemUpdaterV2.cpp、src/update/SubscriptionParser.cpp
- 版本：v1.0
- 前置文档：`docs/bugfix/2026-10-16-Bugfix-SubscriptionParser-Observability-P0-v1.0.md`（提交 045fb99，P0 可观测性阶段）

---

## 1. 症状

P0 阶段给 24 个订阅装上了按 subid 归因的 summary 日志，实测暴露出**三个与 base64url 毫无关系**的真实问题。P0 只测量不修，本次把它们修掉。

| # | 现象 | 真实数据 | 影响 |
|---|---|---|---|
| A | 9 个订阅永久解析出 0 节点 | `bodyFellBack=1` 的 9 个 subid，全部指向 `V2RAYCONFIGSPOOL/V2RAY_SUB/v2ray_configs_no1..no9.txt`；GitHub API 查该仓库返回 **HTTP 404** | 9/53 订阅（17%）永久失效，但日志只写 `ERROR: Parse failed: <subid>`，与真解析失败无法区分，且会被三种 method 各重试一次（3 次注定失败的 fetch） |
| B | ss:// 节点密码被静默改错 | crackbest 订阅 74 条 `ss://` 中 **14 条** userInfo 尾部带 `%3D` | 节点照常入库、`isPrintableAscii` 通过，但密码多一个字符，连接必然失败且无任何日志 |
| C | 丢弃率告警被元数据注释污染 | MatinGhanbari 订阅 10 行 `unrecognized` 中 5 行是 `#profile-title:` / `#subscription-userinfo:` / `#support-url:` / `ssr://` | 面板下发的展示信息与流量配额被当成「无法识别的节点」，推高丢弃率分子，产生假告警 |

---

## 2. 根因（RCA）

### 2.1 问题 A：404 body 非空，绕过了「fetch 失败」判定

```cpp
// src/SubitemUpdaterV2.cpp fetchUrl()
curl.setUrl(url).setFollowLocation()...setTimeoutMs(...);
curl.perform();
return response;        // ← 无论 HTTP 状态码如何都返回 body
```

libcurl 默认不设 `CURLOPT_FAILONERROR`，所以 **404 不抛异常**（已核实 src/SubitemUpdaterV2.cpp 与 src/UrlFetcher.cpp 均未设置该选项），而 GitHub Raw 对已删除仓库返回的 body 是 `404: Not Found\n`——**非空**。

于是判定链 `if (!content.empty())` 为真，走进 parseSubscription，profiles 为空，落到失败分支：

```cpp
phaseFail++;
stillFailed.push_back({subId, subRemarks, subUrl});
Logger::write("ERROR: Parse failed: " + subId, LogLevel::ERR);
```

结果三重损失：(1) 诊断信息错误——上游仓库被删被报成「解析失败」；(2) **仍然进入 stillFailed**，外层会换 method 再试，3 次注定失败的 fetch；(3) 用户界面无从判断该订阅是否值得留着。

### 2.2 问题 B：ss:// 分支从未做过 URL 解码

```cpp
// src/update/SubscriptionParser.cpp ss 分支（修复前）
stats.ssUrlCharCount += countUrlAlphabetChars(userInfo);
std::string decodedInfo = decodeBase64(userInfo);   // ← 直接解码原始 userInfo
```

对比同文件 vmess 分支在 `decodeBase64` 之后还有一层 `urlDecode` 兜底（历史代码），**ss 分支从 `line.substr(5)` 到 `isPrintableAscii` 整条链路一次 URL 解码都没有**。

面板生成 SIP002 分享链接时会对 userInfo 做 URL 编码，base64 的 padding `='` 被写成 `%3D`。`decodeBase64` 的非字母表字符处理是「静默 continue」，所以：

```text
正确: YWVzLTEyOC1nY206c2hhZG93c29ja3M=          (32 字符)
线上: YWVzLTEyOC1nY206c2hhZG93c29ja3M%3D
                                  ↑ '%' 被丢弃，'3D' 被当成两个有效数据字符
修复前解出: b'aes-128-gcm:shadowsocks7'          (33 -> 24 字节)
正确解出:   b'aes-128-gcm:shadowsocks'            (32 -> 24 字节)
```

**关键事实（此处纠正了前期文档的推断）**：前一轮调查记录写的是「解出多余尾字节 → `isPrintableAscii` 判假 → 节点被静默丢弃」。实测是错的——`aes-128-gcm:shadowsocks7` 全部字符都在 0x20..0x7E，**能通过 isPrintableAscii**，节点照常入库，只是密码错了。这比「丢弃」更隐蔽：数据库里看起来有节点，列表里显示正常，只有真正去连的时候才失败，且日志里什么都没有。

### 2.3 问题 C：元数据注释落进 unrecognizedLines 桶

主循环末尾有兜底 `else { continue; }`，任何不以已知 scheme 开头的行都被计入 `unrecognizedLines`。面板下发的 `#profile-title:` / `#subscription-userinfo:` / `#support-url:` / `#profile-update-interval:` 开头的行本来就不是节点，却一并计入。

P0.5 的丢弃率告警判据是 `(vmessFailParse + unrecognizedLines + droppedMalformedLine) * 2 > parsedOk`——元数据注释进分子就直接制造假告警。P0 实测数据里 9 条 `parsed 0 nodes` 告警、1 条 `drop rate 180%` 告警，部分正是这个污染造成的。

---

## 3. 修复

### 3.1 include/SubitemUpdaterV2.h / src/SubitemUpdaterV2.cpp — 把 HTTP 状态码带出 fetch 层

| 文件 | 位置 | 变更 |
| --- | --- | --- |
| `include/SubitemUpdaterV2.h` | `L64-71` | 三个 fetch 声明各加一个可选出参 `long* httpStatus = nullptr`，附注释说明 0 表示「没拿到响应」（异常/超时/DNS 失败），与真实 4xx/5xx 可区分 |
| `src/SubitemUpdaterV2.cpp` | `fetchUrl / fetchUrlViaProxy / fetchUrlViaAccelerator` | 入口 `if (httpStatus) *httpStatus = 0;`；`curl.perform()` 之后 `*httpStatus = curl.getResponseCode();`（读 `CURLINFO_RESPONSE_CODE`，无需 body）。`fetchUrlViaAccelerator` 的两处内部 `fetchUrl` 调用同步透传 |
| `src/SubitemUpdaterV2.cpp` | `processSub` lambda | switch 前声明 `long httpStatus = 0;`，三个分支传 `&httpStatus` |
| `src/SubitemUpdaterV2.cpp` | `processSub` 失败分支 | 拆成 404 与非 404 两条路径（见 3.2） |
| `src/SubitemUpdaterV2.cpp` | `updateWithMethods` | 同上，另加 404 早退 `return false` |
| `src/SubitemUpdaterV2.cpp` | `Parse failed` 日志 | 补上 `http=<状态码>, content=<字节数>`，让「拿到 body 但解不出」与「根本没拿到」可区分 |

### 3.2 404 判定与加速器边界（重要约束）

```cpp
// 404 只有在请求真的到达源站时才是订阅失效的证据。
// 走 URL 加速器时，404 可能属于加速器自己（例如它拒绝该目标），
// 而后续 method 可能仍然成功 —— 那种情况下必须保留旧的跨 method 重试。
// fetchUrlViaAccelerator() 在 accelerator_url 为空时会退化为直接 fetch，
// 所以第二个条件是必要的。
const bool httpStatusIsAboutOrigin =
    (method != UpdateMethod::Accelerator) || config_.accelerator_url.empty();
...
if (httpStatus == 404 && httpStatusIsAboutOrigin) {
    phaseFail++;
    Logger::write("ERROR: Subscription source gone (HTTP 404): " + subId
                  + " remarks=" + subRemarks + " url=" + subUrl
                  + " - upstream removed/renamed; disable or replace this subscription",
                  LogLevel::ERR);
    // 刻意 **不** 放进 stillFailed：已删除的源站不会自愈，跨 method 重试只是浪费
} else {
    phaseFail++;
    stillFailed.push_back({subId, subRemarks, subUrl});
    Logger::write("ERROR: Parse failed: " + subId + " (http=" + std::to_string(httpStatus)
                  + ", content=" + std::to_string(content.size()) + " bytes)", LogLevel::ERR);
}
```

### 3.3 include/update/SubscriptionParser.h / src/update/SubscriptionParser.cpp

| 文件 | 位置 | 变更 |
| --- | --- | --- |
| `src/update/SubscriptionParser.cpp` | 匿名 namespace，`countUrlAlphabetChars` 之后 | 新增 `decodePercentEscapesOnly()`：只解 `%XX`，**绝不把字面 `+` 变成空格** |
| `src/update/SubscriptionParser.cpp` | ss 分支 `decodeBase64` 调用处 | 改为 `decodeBase64(decodePercentEscapesOnly(userInfo))`；`stats.ssUrlCharCount` 的预扫**刻意仍用原始 userInfo**（保持 P0 口径不变） |
| `src/update/SubscriptionParser.cpp` | 主循环空行判定之后 | 新增 `if (line[0] == '#') { ++stats.metadataCommentLines; continue; }` |
| `include/update/SubscriptionParser.h` | ParseStats | 新增 `long metadataCommentLines = 0;`；等式注释追加该桶 |
| `src/update/SubscriptionParser.cpp` | summary 日志 | 新增 `metaComment=` 字段，使汇总可复算 |

#### 为什么必须新写一个只解 %XX 的函数（而不是复用 urlDecode）

`urlDecode`（`src/SubitemUpdaterV2.cpp` 的实现，`SubscriptionParser.cpp` 内同名私有版本同构）是为 URL 查询串的 `application/x-www-form-urlencoded` 设计的，它**第二轮会把每个字面 `+` 替换成空格**：

```cpp
while (changed && maxIterations-- > 0) {
    ... if (result[i]=='%' && i+2<size) { ...; changed = true; }
    else if (result[i]=='+' && maxIterations == 1) { decoded += ' '; changed = true; }
}
```

第一轮只解 `%XX`、保留 `+`；但只要输入含 `%XX`，`changed` 就会置位从而跑满两轮，**所有 `+` 被吃掉**。而标准 base64 的 `+` 是 sextet 62，是必需数据。直接用 `urlDecode` 会把一个能救回的节点再次破坏。

只做 `%XX` 已足以覆盖真实故障：面板破坏的都是 `='` padding（编码成 `%3D`），而 `+` 本身在 base64 文本里不会被 URL 编码（encodeURIComponent 不转义 `+`）。

#### 元数据注释的判据

只匹配**行首**第一个字符是 `#`。没有任何分享链接 scheme 以 `#` 开头，所以不会误伤合法节点；共享链接的 `#` 是 fragment（位于行尾），也不受影响。

---

## 4. 验证

| 项 | 命令 | 结果 |
| --- | --- | --- |
| 构建 | `cmake --build build --parallel 8` | EXIT 0，13/13 |
| 单元测试 | `tests\\test_subscription_parser.exe` | **EXIT 0，18/18 PASSED**（16 既有 + 2 新增） |
| 全量回归 | `ctest --test-dir build --output-on-failure -j 4` | 51/53，失败两项均为**已知基线**（见下） |

### 4.1 已知基线失败（勿误判为本次回归）

| 测试 | 原因 |
| --- | --- |
| `XrayManagerStartupTest`（RealXrayStartsBounded） | 需真实就绪的 xray 进程，与订阅解析无关 |
| `UI_FLOATINGWIDGET` | 已知并行 flaky。**单跑复验 `UITests.exe "[floatingwidget]"` 仍 EXIT 42 / 5 of 6 passed**，与 P0 阶段基线完全一致；本次改动不触及 StandaloneFloatingWidget 任何代码路径 |

### 4.2 新增用例

| 用例 | 守护什么 | 修复前 |
| --- | --- | --- |
| `SsUserInfoPercentEncodedPaddingIsDecodedBeforeBase64` | `ss://YWVzLTEyOC1nY206c2hhZG93c29ja3M%3D@158.173.20.208:443` 必须解析出 `id == "shadowsocks"`（不是 `shadowsocks7`） | **红**：得到 `shadowsocks7` |
| `MetadataCommentLinesAreCountedSeparately` | 3 行元数据注释 + 4 个正常 vless + 1 行未知协议 ⇒ `metadataCommentLines == 3`、`unrecognizedLines == 1`、等式成立、summary 含 `metaComment=3`、**不触发 drop rate 告警** | **红**：注释进 unrecognized 桶 |

第 8 条用例的 `EXPECT_FALSE(cap.contains("s-meta: drop rate"))` 是**有鉴别力**的：丢弃率判据 `(unrecognized + malformed) * 2 > parsedOk`，正确口径下 `1 * 2 = 2 > 4` 为假（不告警）；若元数据被误计入则 `4 * 2 = 8 > 4` 为真（误告警）。构造时把成功行数定在 4 就是为了让两个口径落在阈值两侧。

两个既有的等式不变量用例（`StatsLineBucketsAreExhaustive` / `StatsVmessFailureAndSkipEmptyAreCounted`）同步追加了 `+ stats.metadataCommentLines`，否则新增桶会让它们直接红。

---

## 5. 范围外与后续

以下四项来自同一份 P0 实测数据，但属**功能扩展或判定标准模糊**，不在本次范围，另立文档：

| # | 问题 | 量 | 为何不做 |
| --- | --- | --- | --- |
| 2 | `anytls://`（7）、`ssr://`、`socks://`（1）协议未支持 | >=18 行 | 新协议需要完整的 configtype 映射、字段抽取与 outbound 模板，属功能开发 |
| 3 | 明文 VLESS 链接被误挂 `vmess://` 前缀 | >=2 行 | 上游数据错误，本项目只能识别并告警；「自动纠正协议」有误判风险（明文 UUID 也可能真是 vmess） |
| 5 | 裸 base64 行（无 `xxx://` 前缀） | 20 行 | 与 base64url 解码错位是两码事；需要判断这些行是嵌套编码的整段订阅还是垃圾 |
| 7 | 旧式 SIP001（整段 base64 无 `@`） | 4 行 | 需先确认当前 xray-core 版本是否仍支持该格式 |

### 5.1 本次修复暴露出的、尚未处理的问题

1. **问题 B 的近亲尚未修**：vmess 分支虽有 `urlDecode` 兜底，但它在第二轮会吃掉 `+`（见 3.3），所以**含 `%XX` 的 vmess 载荷同样会被 urlDecode 破坏**。本次只修了 ss 分支（改用专用函数），vmess 分支的兜底仍是原样。是否一并换成 `decodePercentEscapesOnly` 待定——需要先确认是否有真实订阅命中。
2. **`urlDecode` 自身的缺陷仍登记在案**：`urlDecode("50%free") == "50\x0Fee"`（部分十六进制被接受），会往 remarks 里塞控制字符。属 P0 阶段 bugfix 文档 §5 已登记项，本次未动。
3. **`src/utils/Base64Utils.cpp` 的同源缺陷第二处实现仍未修**：它遇任意非字母表字符**整段返回空**（比本文件的「静默丢弃」更严格），本次未触及。

---

## 6. 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v1.0 | 2026-10-16 | 首版。修 P0 实测暴露的三个「本项目缺陷」：HTTP 404 失效检测（含加速器边界约束）、ss:// userInfo 的 %3D 解码（新增专用 `decodePercentEscapesOnly`）、行首 `#` 元数据注释单列计数。新增 2 条单元测试，总数 16 -> 18。 |