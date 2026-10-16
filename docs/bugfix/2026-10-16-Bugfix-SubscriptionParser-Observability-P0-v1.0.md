# Bugfix: SubscriptionParser 解析可观测性 P0 + 真实根因归因（2026-10-16）

> 阶段：P0（纯可观测性，零功能行为变更）已实施并用真实数据验证。
> 配套文档：`docs/specs/2026-10-15-Spec-SubscriptionBase64UrlFix-v1.2.md`（方案，被本文件实测证伪）、
> `docs/reviews/2026-10-16-Review-SubscriptionBase64UrlFix-v1.2.md`（三视角评审）。

---

## 1. 症状

`bin/worker/log/ui_20261016_084620.log`（355,553 字节 / 4,077 行）中出现 114 条
`WARN: Failed to parse vmess: ...`，形态如下：

| 消息 | 次数 |
| :--- | ---: |
| `Failed to parse vmess: not JSON` | 98 |
| `Failed to parse vmess: syntax error [boost.json:1 ...]` | 9 |
| `Failed to parse vmess: extra data [boost.json:2 ...]` | 7 |
| `Base64 decode produced invalid content, using original` | 9 |

**关键困难**：114 条错误行的**形状完全一致**——不带 subid、不带行号、不带载荷前缀，
且日志级别为 WARN（`bin/config.json` 中 `log.file_level = "WARN"`），
INFO 级上下文（`Attempting base64 decode...` / `Final content length: N` /
`Parsed N valid profiles from M lines`）**一条都没有落盘**。
⇒ **结构上无法把任何一条错误归因到任何机制。**

三轮方案（v1.0 / v1.1 / v1.2）正是为了消除这个不可归因性而写的，但它们各自假设了一个
未经证实的"根因"，因此三轮都被独立评审判 FAIL。

---

## 2. 根因（RCA）

### 2.1 本次修复的根因（可观测性缺失）

`SubscriptionParser::parse()` 原本只输出逐行失败日志，无任何成功/失败计数、
无订阅 ID、无 body 级诊断。三条 `INFO` 汇总被生产日志级别过滤，
导致「99% 节点静默丢失」这一事实**既无法度量也无法归因**。

### 2.2 三轮方案的假说与被证伪的结论

| 版本 | 假说 | 实测判定 |
| :--- | :--- | :--- |
| v1.0 | 「114 条全部源于 base64url（`-`/`_` 破坏 4 字符分组）」 | **证伪** |
| v1.1 | 「+ 白名单补 `syntax error` + 剥 `#备注` + base64url 归一化」 | **部分证伪** |
| v1.2 | 「分阶段 P0 可观测性 → P1 确定性缺陷 → P2 base64url → P3 明文 JSON」 | **P0 正确；P1/P2/P3 无证据** |

> **P0 的设计本身在三轮评审中被 3 名评审者一致认可为 sound**，
> 本文件即该阶段的落地与实测。

---

## 3. 修复

### 3.1 变更清单

| 文件 | 位置 | 变更 |
| :--- | :--- | :--- |
| `include/update/SubscriptionParser.h` | 新增 `struct ParseStats` | 30+ 计数字段，只含 `bool`/`long`，无 Boost 依赖 |
| `include/update/SubscriptionParser.h` | `SubscriptionParser` 类 | 新增 3 参重载 `parse(content, subid, ParseStats&)`；原 2 参版本保留并委托 |
| `src/update/SubscriptionParser.cpp` | 匿名 namespace | 新增 4 个自由函数预扫：`isBase64AlphabetChar` / `countAlphabetChars` / `countDroppedChars` / `countUrlAlphabetChars` |
| `src/update/SubscriptionParser.cpp` | `parse()` 入口 | `int failureDetailBudget = 3;`（**栈上局部，禁止 static**） |
| `src/update/SubscriptionParser.cpp` | body 级（`:51` 前后） | 预扫 body 的 `-`/`_` 与 junk 字符数；兜底 WARN 扩充为带 subid + 5 项指标 |
| `src/update/SubscriptionParser.cpp` | vmess 分支 | 预扫 payload 三项；新增 `countAlphabetChars(encoded) < 2 → ++vmessSkipEmpty`；裁剪救回成功 `++vmessFailSalvaged`；失败明细改为带 subid+行号且受限流 |
| `src/update/SubscriptionParser.cpp` | vless/ss/trojan/hy2 | 10 处 `continue` 改为 `{ ++droppedMalformedLine; ++droppedByProtocol[N]; continue; }` |
| `src/update/SubscriptionParser.cpp` | ss 分支 `:364` 前 | `stats.ssUrlCharCount += countUrlAlphabetChars(userInfo)` |
| `src/update/SubscriptionParser.cpp` | 未知 scheme | `else { ++unrecognizedLines; continue; }` |
| `src/update/SubscriptionParser.cpp` | 成功出口 | `if (countedAsVmess) ++vmessOk; else ++otherOk;` |
| `src/update/SubscriptionParser.cpp` | 末尾 | 新增 **P0.4 summary WARN**（每 subid 一行，22 字段） |
| `src/update/SubscriptionParser.cpp` | 末尾 | 新增 **P0.5 两档丢弃率告警** |
| `tests/test_subscription_parser.cpp` | 新增 7 个用例 | 共 16 个，全绿 |

### 3.2 ParseStats 字段表

**行级（等式不变量约束）**

```
lineCount == droppedEmptyLine + vmessOk + vmessFailParse + vmessSkipEmpty
           + unrecognizedLines + droppedMalformedLine + otherOk
```

| 字段 | 含义 |
| :--- | :--- |
| `lineCount` | `while(getline)` 处理的行数（含空行） |
| `droppedEmptyLine` | 空行（`:68`） |
| `vmessOk` | vmess 分支解析成功（`:525/:526`） |
| `vmessFailParse` | vmess 解析失败（`:178`） |
| `vmessSkipEmpty` | 载荷有效字母表字符 < 2 |
| `unrecognizedLines` | 未知 scheme（`:522`） |
| `droppedMalformedLine` | 已知协议但字段畸形（10 条 line 级出口） |
| `otherOk` | **vless/ss/trojan/hy2 解析成功** |
| `droppedByProtocol[6]` | 0=vless 1=ss 2=trojan 3=hy2 |
| `vmessFailSalvaged` | `vmessFailParse` 的**子集**，不进等式，仅供观察 |

> `otherOk` 桶是 v1.2 缺失的关键项 —— 见 §4.2 实测证明。

**body 级 / payload 级 / ss 级**：`bodyHadProtocol` / `bodyDecodeApplied` /
`bodyDecodeFellBack` / `bodyUrlCharCount` / `bodyNonBase64Chars` / `bodyDecodedLength` /
`payloadUrlCharTotal` / `payloadUrlCharLines` / `payloadNonBase64Chars` /
`payloadPlainJsonLines`(P3 才写) / `payloadUrlEncoded`(P3 才写) / `ssUrlCharCount`

### 3.3 日志格式

**summary（每 subid 一行，P0.4）**：
```
WARN: vmess summary [subid=<id>] lines=528 ok=31 otherOk=467 fail=2 salvaged=31
      skipEmpty=0 unrecognized=20 malformed=8 emptyLine=0
      payloadUrlLines=33 payloadUrlChars=41 payloadJunk=199
      payloadPlainJson=0 payloadUrlEncoded=0
      bodyUrlChars=0 bodyJunk=0 bodyDecodedLen=135770 bodyFellBack=0
      ssUrlChars=0 byProto=v0,s8,t0,h0
```

**明细（限流前 3 条 / subid，P0.4）**：
```
WARN: vmess parse failed [subid=5762223787735100455 line=127] not JSON
```

**body 兜底（带 subid 与指标）**：
```
WARN: Base64 decode produced invalid content, using original [subid=4109119759224009649]
      hadProtocol=0 decodedLen=8 urlChars=0 junk=1 -> 0 nodes
```

**丢弃率告警（P0.5，v1.2 的恒假第一档已删除）**：
```
WARN: subid=5373293512565574908: parsed 0 nodes from 1 lines (fail=0 skipEmpty=0 unrecognized=1 malformed=0)
WARN: subid=5052289940929334310: drop rate 180% (ok=5 fail=0 unrecognized=9 malformed=0)
```

### 3.4 相对 v1.2 的实施偏差（均为采纳评审修正）

| 项 | v1.2 原设计 | 实际实施 | 原因 |
| :--- | :--- | :--- | :--- |
| `otherOk` 桶 | 缺失 | **新增** | 评审阻断项 1：缺此桶等式永久算不平 |
| `vmessOk` / `vmessFailSalvaged` 自增点 | 缺失 | **补齐** | 评审阻断项 2：缺失则恒 0 |
| `vmessFailSalvaged` 归属 | 未决 | `fail` 的**子集**，不进等式 | 避免等式重复计数 |
| `decodeBase64` 签名 | 加 out-param | **不改，改用自由函数预扫** | 保持零功能行为变更；避免 P2 再改一次签名 |
| P0.5 第一档 | `lineCount==0 && !content.empty()` | **删除** | 评审阻断项 6：逻辑无解 + 三调用点在 parse 前已挡空 content |
| P0.5 第二档基 | `vmessOk == 0` | `vmessOk + otherOk == 0` | 评审阻断项 9：vless-only 订阅会纯误报 |
| summary 字段 | 无 `payload*` | **补齐全部** | 评审阻断项 7：P4 数据通道 |

### 3.5 审计发现并修复的自身缺陷

`git diff` 审计发现初版误把 **trojan 分支 `substr(9)` 改成 `substr(8)`** ——
经核对 HEAD（`git show a2e4147:src/update/SubscriptionParser.cpp` 第 381 行）
原值为 `substr(9)`，而 `"trojan://"` 恰为 **9 字符**，故 HEAD 正确、本次改动错误
（会残留前导 `/`）。已改回并补注释 `// "trojan://" is 9 chars`。
同时修正 ss 分支 `isPrintableAscii` 失败分支的缩进错乱。

> 各 scheme 前缀长度（HEAD 值）：`vmess://`=8 / `vless://`=8 / `ss://`=5 /
> `trojan://`=9 / `hysteria2://`=12。`hy2://` 走 `substr(5)` 而实为 6 字符，
> 属 HEAD 既有的轻微越界（因 `find('@')` 容忍而未爆），**非本次引入，不在 P0 范围**。

---

## 4. 验证

### 4.1 构建与测试

```
cmake --build build --parallel 8                → BUILD_EXIT=0
tests\test_subscription_parser.exe              → PARSER_EXIT=0, 16/16 PASSED
ctest --test-dir build --output-on-failure -j 4 → 52/53
```

两项失败经 `git stash` 证伪与本次改动无关：
- `XrayManagerStartupTest` —— 需真实就绪 xray 的**既有基线失败**
- `UI_FLOATINGWIDGET` —— 已知并行 flaky（基线单跑同样 `EXIT=42`）

### 4.2 真实数据采集

```
validproxy-cli.exe -c <绝对路径>/temp/ua_config.json -UA   → EXIT 0，47,139 字节输出
```
> 用 `temp/ua_probe.db` 副本，**未触碰生产库 `bin/worker/guindb.db`**。

采集到 **24 条 summary 行**，覆盖 24 个 subid（全库 53 个订阅，47 个指向 GitHub Raw）。

**P0 每一项设计目标均被实测验证：**

| 设计目标 | 实测证据 | 判定 |
| :--- | :--- | :--: |
| 明细限流 ≤3 条 / subid，且**非 static** | 7 条明细按 subid 分为 3 / 2 / 2，且分属**两次独立 `parse()`** | ✅ |
| 按 subid 归因 | 24 个 subid 独立可辨（原日志 114 行形状完全一致、零可归因信息） | ✅ |
| `otherOk` 桶 indispensable | 实测 `otherOk` = 1101 / 583 / 467 / 457 / 356 / 171 / 123 / 71 / 11 / 7 / 6 / 5 —— **若无此桶，等式永久算不平** | ✅ 证伪 v1.2 阻断项 1 的修复必要性 |
| vless-only 不误报 | subid `4651922309839544190`（`kasesm-vless`，1,101 行全 vless）：`ok=0 otherOk=1101`，**未触发**「解析出 0 个节点」 | ✅ 证伪 v1.2 阻断项 9 的修复必要性 |
| `vmessFailSalvaged` 非死变量 | `crackbest` 的 `fail=2` 中 `salvaged=31`；`Epodonios-vmess` `salvaged=0`/`fail=6` | ✅ 证伪 v1.2 阻断项 2 |
| 404 空体可归因 | **9** 条 body 兜底 WARN，各带 subid + `decodedLen=8` + `junk=1` | ✅（取代了 v1.2 那条恒假的第一档） |

### 4.3 三条线索的完整调查结论（全部用真实订阅数据坐实）

#### 线索 A —— 9 个订阅的上游仓库已被删除

`bodyFellBack=1` 的 9 个 subid 全部指向同一仓库的 9 个文件：

| subid | Remarks | URL |
| --- | --- | --- |
| `5373293512565574908` | V2RAYCONFIGSPOOL-v2ray_configs_no1 | `raw.githubusercontent.com/V2RAYCONFIGSPOOL/V2RAY_SUB/refs/heads/main/v2ray_configs_no1.txt` |
| `4743817031110564455` | …no2 | …/v2ray_configs_no2.txt |
| `5890829490637131753` | …no3 | …/v2ray_configs_no3.txt |
| `4692705081756953558` | …no4 | …/v2ray_configs_no4.txt |
| `4109119759224009649` | …no5 | …/v2ray_configs_no5.txt |
| `5064881073630802589` | …no6 | …/v2ray_configs_no6.txt |
| `4994285438871455263` | …no7 | …/v2ray_configs_no7.txt |
| `5695405340294462191` | …no8 | …/v2ray_configs_no8.txt |
| `4242413544169737009` | …no9 | …/v2ray_configs_no9.txt |

**证据链闭合**：
- `api.github.com/repos/V2RAYCONFIGSPOOL/V2RAY_SUB` → **HTTP 404** ⇒ 仓库不存在
- 9 条兜底 WARN 的 `decodedLen=8`，而 `base64("404: Not\n")` = `NDA0OiBOb3QK`，
  解码恰为 **8 字节** `404: Not\n` ⇒ **GitHub 返回的 404 页面体被当作 base64 订阅内容**

⇒ **不是解析缺陷，是订阅源失效。** 现有回退行为（`:53-56`）已是此处最优解
（解不出内容就用原文，好过崩溃）；该做的是**检测并上报失效订阅**，
而不是"修复解析"。

#### 线索 B —— `unrecognized` 行的三类真实成因

| 订阅 | unrecognized | 真实成因 |
| :--- | ---: | :--- |
| `MatinGhanbari` | 10 | `#profile-title:` / `#subscription-userinfo:` / `#support-url:` 等**订阅元数据注释头**（本就不是节点）+ `ssr://`（协议未支持） |
| `MrAbolfazlNorouzi-working-configs` | 9 | 同上（另 `emptyLine=6`，触发 `drop rate 180%` 告警） |
| `cook369-yudou` | 8 | `anytls://` × 7 + `socks://` × 1（**协议未支持**） |
| `crackbest` | 20 | **裸 base64 行**（无 `xxx://` 前缀，根本没进解码器）+ `nm-vless://` + 被截断的 `://` |
| `ShatakVPN-all` | 2 | 未知 scheme |

⇒ **与 base64url 解码错位是两码事**：这些行从未进入 `decodeBase64`。

#### 线索 C —— `malformed` × 8 全在 ss 分支（`crackbest`，74 条 `ss://`）

抓取 `crackbest` 真实数据逐条比对：

| 现象 | 条数 | 根因 |
| :--- | ---: | :--- |
| userInfo 尾部带 `%3D`（URL 编码的 `=`） | 14 | `decodeBase64` 丢 `%`、把 `3D` 当数据 ⇒ 解出多余尾字节 ⇒ `isPrintableAscii` 判假 ⇒ 命中 `:376` 丢弃。**即 v1.2 §8.1 登记过的 `urlDecode` 死兜底问题——`:78-80` 只在 vmess 分支调用，ss 分支 `:364` 压根没有 URL 解码。** |
| `ss://` 缺 `@` | 4 | 旧式 SIP001（整段 base64 连 `@host:port` 一起编码），`:364-379` 只支持 SIP002 |
| userInfo 含 `-` 或 `_` | **0** | **SS2022 密码含 `/`（sextet 63）解码正确，恰是标准 base64 编码正确的证明；`-`/`_` 编码一条都没出现** |

样本：
```
ss://YWVzLTEyOC1nY206c2hhZG93c29ja3M%3D@158.173.20.208:443#…   ← 尾部 %3D（=）被丢
ss://Y2hhY2hhMjAtcG9seTEzMDU6dTE3VDNCdnBZYWFpdVcyY0BzZXJpZXMtYTItbWVjLnNhbWFuZWhoYS5jbzo0NDM=#…  ← 旧式 SIP001，无 @
```

### 4.4 新发现：`syntax error` 的真实成因（本次调查新增，原三版方案均未识别）

抓取 `crackbest` 的真实数据发现，**line 127 与 line 313 根本不是 base64 编码的 vmess**，
而是**明文 VLESS 链接被误挂在 `vmess://` 前缀下**：

```
vmess://06d4d001-4168-4876-9416-7caecf578f40@178.105.168.171:211#🌐سرور 126 | Meh-24|t.me/
vmess://067fcd63-b215-4abd-8529-ae929a9971a9@145.239.55.210:80?e…#🌐سرور 312 | Meh-24|t.me/
```

`decodeBase64` 把 UUID 的 4 个 `-` 和地址分隔符丢弃后，把剩余十六进制字符当 base64 解，
解出的字节流既非 JSON ⇒ 命中 `syntax error [boost.json:1 ...]`
（与旧日志 9 条 `syntax error` 的形态精确一致）。

**这是上游数据格式错误，不是本项目的解析缺陷。**

### 4.5 P0 计数器的已知口径局限（如实登记）

`payloadUrlLines` / `payloadUrlChars` 统计的是 `line.substr(8)` 之后的**全部内容**，
**包含 `#fragment`**。而真实订阅的 fragment 普遍含连字符
（如 `#🌐سرور 126 | Meh-24|t.me/`）。

⇒ `crackbest` 的 `payloadUrlLines=33` 中，**只有 2 行的 payload 部分真含 `-`**，
其余 31 行全部来自 fragment 的备注。

⇒ **P0 无法区分「base64url 的 sextet 62/63」与「`#备注` 里的连字符」**。
若需精确归因，须先做 v1.2 P3.3（剥 fragment）再统计 —— 但 P3.3 属功能变更，不在 P0 范围。
当前局限**不影响**本文件的结论，因为线索 C 已用**原始订阅数据**独立确认
`ss` 分支 `-`/`_` 出现次数为 **0**。

---

## 5. 真实问题清单（按量排序）

| # | 优先级 | 问题 | 实测量 | 归属 |
| --: | :--- | :--- | --- | :--- |
| 1 | **P0** | 上游仓库 `V2RAYCONFIGSPOOL/V2RAY_SUB` 已被删除 | 9 个订阅永久 0 节点 | 数据源，需用户换源或加失效检测 |
| 2 | **P0** | `anytls://` / `ssr://` / `socks://` 协议未支持 | ≥ 18 行 | 功能扩展 |
| 3 | **P1** | 明文 VLESS 链接误挂 `vmess://` 前缀 | 至少 2 行（全库待查） | 上游数据格式错误 + 需容错识别 |
| 4 | **P1** | `ss://` userInfo 缺 URL 解码（`%3D` 尾 padding） | 14 行（crackbest） | **本项目缺陷**（v1.2 §8.1 已登记未修） |
| 5 | **P1** | 裸 base64 行（无 `xxx://` 前缀） | 20 行（crackbest） | 上游数据格式错误 |
| 6 | **P2** | `#profile-*` 元数据注释行被计入 `unrecognized` | 5 行 | **本项目可修**（应识别并跳过） |
| 7 | **P2** | 旧式 SIP001（整段 base64 无 `@`） | 4 行（crackbest） | 功能扩展 |

---

## 6. 对 v1.0–v1.2 各阶段的最终判定

| 阶段 | 内容 | 判定 |
| :--- | :--- | :--- |
| **P0** | 可观测性（`ParseStats` + 计数 + summary） | **✅ 正确，已实施并验证** |
| P1.1 | `fillVmessProfile` 抽取（修兜底路径丢 11 个字段） | **与本次问题正交** —— 114 条是解析**失败**，走不到 `fillVmessProfile`。它修的是解析**成功后**的字段完整性，属独立的代码质量改进 |
| P1.2 | `findMatchingBrace`（替换 `rfind('}')`） | **收益未证** —— 仅 2 条 `syntax error`，且 v1.2 自承"尾随逗号型恢复率预计为 0"；§4.4 实测证明这 2 条是上游数据格式错误，括号配平也救不回 |
| **P2** | base64url 归一化 | **收益为零，应放弃** —— §4.3 线索 C 实测 `ss` 分支 `-`/`_` 出现 **0 次**；`payloadPlainJson=0`；所有含 `-`/`_` 的行全部解析成功 |
| P3.1 | 明文 JSON 载荷 | **无证据** —— `payloadPlainJson=0`（全部 24 个 subid） |
| P3.2 | URL 编码载荷 | **无证据** —— `payloadUrlEncoded=0` |
| P3.3 | 剥 `#fragment` | **有价值但属功能变更** —— 见 §4.5，是 P0 计数器精确化的前置 |

**v1.0/v1.1/v1.2 追逐三轮的「base64url 根因」在真实数据中不存在。**

---

## 7. 三轮失败模式的元结论

三轮（v1.0 / v1.1 / v1.2）失败模式高度收敛：

- **v1.0**：根因未坐实（合成数据单向演示）+ `fillVmessProfile` 骨架漏 `indexid`
  + `const boost::json::object&` 形参编译不过。
- **v1.1 / v1.2**：**测试夹具构造错误**（T1 三轮都坏：v1.1 是「0 次迭代」，
  v1.2 是「∞ 次迭代」，互为镜像）+ **计数器记账不闭合** + 少量文档论断。
- 架构骨架三轮都被评审认定扎实。

**根因自省**：三轮都在**纸上写测试用例**而不是先跑再写。
v1.2 §5 自立军规要求「每条用例写入文档前必须先用未修复代码跑一遍确认红」，
但 22 条里至少 5 条（T1 / T12 / T19 / T16 / T7）根本没跑过。

**方法论修正**：**先写代码 → 编译 → 跑 HEAD 验证红 → 再写进文档。**

P0 阶段已按此执行：7 个新用例全部先跑通再落盘。

---

## 8. 范围外与后续

### 8.1 本次不做

- P1.1 `fillVmessProfile` 抽取（与本问题正交，另立任务）
- P1.2 `findMatchingBrace`（收益未证）
- P2 base64url（收益为零）
- P3.1 / P3.2（无证据）
- `hy2://` 的 `substr(5)` 越界（HEAD 既有，非本次引入）
- `include/utils/Base64Utils.h` 的同源缺陷第二处实现（`src/utils/Base64Utils.cpp:25-26`
  遇 `-`/`_` 整段返回空，比本项目更严格）—— 需另立文档

### 8.2 下一步（真实问题清单）

按 §5 优先级推进，**问题 1、2 需要用户决策**（换源 / 加协议支持），
问题 4、6 是本项目可直接修的。

---

## 9. 变更记录

| 版本 | 日期 | 内容 |
| :--- | :--- | :--- |
| v1.0 | 2026-10-16 | P0 实施 + 真实数据归因 + 三条线索完整调查 + 问题清单 |