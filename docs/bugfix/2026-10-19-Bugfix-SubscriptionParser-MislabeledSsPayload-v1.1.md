# Bugfix RCA: `ss://` 分支误将「误挂前缀的 VMess JSON」解析为 SS 节点（garbage security）

- 文档类型：Bugfix / RCA + 修复方案（Spec）
- 模块：`SubscriptionParser`（`src/update/SubscriptionParser.cpp`）+ `SubitemUpdaterV2`
- 版本：**v1.1** ｜ 日期：2026-10-19（v1.0 → v1.1，依据独立评审 `docs/reviews/2026-10-19-Review-SubscriptionParser-MislabeledSsPayload-v1.0.md` 的 B1–B8 修订）
- 现场日志：`bin/log/ui_20261019_083638.log`（426,396 B / 4,469 行）

> 变更摘要（相对 v1.0）：
> 1. **新增 §1.4 影响量化（实测）**：白名单 100% 拦截、**零垃圾入库** ⇒ 严重度下调为 LOW（原 v1.0 高估为「数据污染」）。
> 2. **B1 实测归因**：4261 个垃圾节点 **100% 出自 SIP001 路径 A**，直连路径 B 垃圾 = 0 ⇒ 重新定义 **P0-A 为主修、P0-B 为防御层**（v1.0 反向）。
> 3. **B2**：P0-C 给出精确调用点 `SubitemUpdaterV2.cpp:281-283` + 复用既有 `httpStatusIsAboutOrigin` 谓词（:271-273）。
> 4. **B3**：H4「证伪」措辞改为「降优先级」，并给出真实依据。
> 5. **B4**：H5 降级为「根因待定位」，列三条互斥候选 + 回溯方法。
> 6. **B6**：P1-A 由「可选」升为推荐项（单订阅可回收 ~4257 节点，是 P0-B 不丢数据的补偿）。
> 7. **B7**：显式声明 P0-B 为弱形态门，与 `isSupportedSsCipher` 白名单构成 defense-in-depth 两层。
> 8. **B8**：验证计划由 6 条扩为 10 条。

---

## 0. 结论摘要

生产日志中被归类为「解析错误」的表象有多类，但**真正的解析缺陷只有一个**：

> **当订阅源把 VMess 节点的 base64(JSON) 载荷挂上 `ss://` 前缀时（上游诱饵/聚合源的典型污染手法），`SubscriptionParser` 的 ss 分支会把它当成合法 SS 节点解析出来，产出 `security` 为 `{"add"` / `{"v"` / `{"type"` / `{"id"` / `{"seed"` 的幻觉节点，且全程静默（不报 malformed）。**

缺陷位置：**SIP001 过宽容错**（`SubscriptionParser.cpp:469-482`）——它在「无 `@`」时对整条 URI 做 base64 解码，只要解码结果里出现 `@` 且其前有 `:` 就认定为「SIP001 明文 userInfo」。VMess JSON（`{"v":"2",...,"ps":"...@..."}`）恰好满足该条件，于是被误纳（v1.0 记为 R1）。共犯：`:618-624` 的 method 校验仅要求「可打印 ASCII」（R2）。

**严重度修正（实测）**：这些幻觉节点 **100% 被下游 `SubitemUpdaterV2::isValidProxy` 的 `isSupportedSsCipher` 白名单拦截，零垃圾入库**（详见 §1.4）⇒ 数据完整性风险 = **LOW**。实际影响是**健壮性 / 无谓计算 / 日志卫生**，而非数据污染。修复价值仍在，但定位从「堵数据污染」调整为「消除内部幻觉 + 减少无谓 WARN + 回收误挂节点（P1-A）」。

其余「解析错误」日志均为**噪声或既有正确行为**（详见 §5）。

---

## 1. 症状与现场证据

### 1.1 日志中与解析相关的 WARN/ERROR 归类（TOP）

| 形态 | 次数 | 性质 |
|---|---|---|
| `SKIP: <ip>:<port> - invalid UUID format` | 1510 | 上游 decoy 垃圾，**跳过正确**，仅噪声 |
| `SKIP: <ip>:<port> - unsupported SS cipher: '<x>'` | ~1058 | 其中 835 `aes-256-cfb` 等旧式 cipher **跳过正确**；但含 `'{"add'`、`'ss'` ← **幻觉节点证据** |
| `Base64 decode produced invalid content ... [subid=N]` | 30 | 404 响应体副产物，噪声（H2） |
| `subid=N: parsed 0 nodes from 1 lines (...)` | 30 | 同上（H2） |
| `vmess parse failed`（syntax error / extra data / not JSON） | 22 | 上游 JSON 语法错误，**拒绝正确** |
| `[Worker-N] 注入xray outbound 错误: ... Unsupported cipher.` | 1 | **有不受支持 cipher 进入 Xray 注入**（H5，根因待定位） |

### 1.2 触发订阅（`temp/subs_dump.tsv` 定位）

| subid | Remarks | Url |
|---|---|---|
| 4932592678867606842 | Argh94-ShadowSocks | `raw.githubusercontent.com/Argh94/V2RayAutoConfig/main/configs/ShadowSocks.txt` |
| 5871334569476195995 | SoliSpirit-all_configs | `SoliSpirit/v2ray-configs/main/all_configs.txt` |
| 5184538877103642962 | Argh94-vmess | `Argh94/V2RayAutoConfig/main/configs/Vmess.txt` |
| 5469138616422048257 | 0xRadikal-configs | `0xRadikal/Free-v2ray-Configs/main/all/configs.txt` |
| …（共 10 条高 malformed 订阅，见 tracker） | | |

日志中该订阅的 KPI 行（**可复现锚点**）：

```
subid=4932592678867606842 lines=32313 ok=0 otherOk=8215 malformed=24098 sip001=6876 ssUrlChars=69133 bodyDecodedLen=8736343 byProto=v0,s24098
```

> 注 1：`byProto=vX,sY,tZ,hW` 实为**被丢弃行按协议**分布（v=vless、s=ss、t=trojan、h=hysteria），不是「成功按协议」——日志可读性缺陷，建议改名 `droppedByProto=`（H6）。
> 注 2：`lines == emptyLine + metaComment + ok + fail + skipEmpty + unrecognized + malformed + otherOk` 在全部 65 条 summary 中均成立，**计数无泄漏**。

### 1.3 复现（Python 忠实复刻，数字对齐生产）

工具：`temp/replica_parser.py`（逐行复刻 ss 分支 + `decodeBase64`/`urlDecode`/`parseAddressPort` + `Utils` 各校验）。
输入：经 jsDelivr 镜像下载的 `temp/argh94_ss.txt`（8,801,219 B，32,563 行；curl 直连 raw.githubusercontent.com 被 reset）。

```
total_lines=32563  ss_lines=32562
kinds={'malformed': 24399, 'node': 8163}
malformed_why={'no_colon': 16177, 'no_at': 4051, 'weakcheck': 4169, 'no_addr': 2}
produced_nodes=8163  (public_addr=8162, nonpublic=1)
```

| 指标 | 生产日志 | Python 复刻 | 判定 |
|---|---|---|---|
| malformed | 24098 | 24399 | 量级一致（CR/行处理微差） |
| 产出节点 | otherOk=8215 | 8163 | 一致 |

**复刻成功 ⇒ 根因判定可信。**

### 1.4 影响量化（**v1.1 新增，实测**）

**(a) 幻觉节点是否入库？——否，零入库。** 只读扫描生产库 `bin/worker/guindb.db`：

```
ProfileItem total = 376,888        ConfigType 分布: 5=304952 / 6=43280 / 1=17261 / 3=8331 / 7=3064
configtype='3' 且 Security LIKE '{%' 或 '[%'      = 0
configtype='3' 且 Security 不在 9 项白名单         = 0
Security 精确计数: aes-256-cfb=0  aes-128-cfb=0  rc4-md5=0  chacha20-ietf=0  null=3  '{"add"'=0
argh94 subid 4932592678867606842 旁路行 = 4049，全部白名单
   (chacha20-ietf-poly1305 1992 / aes-256-gcm 1396 / aes-128-gcm 231 / 2022-blake3-chacha20-poly1305 170
    / 2022-blake3-aes-256-gcm 157 / 2022-blake3-aes-128-gcm 59 / none 33 / chacha20-poly1305 11)
```

⇒ **`SubitemUpdaterV2::isValidProxy`（调用 `utils::isSupportedSsCipher`）100% 拦截，垃圾零入库**。严重度 = **LOW（数据安全）**。

**(b) 幻觉节点的产出路径归因（B1）。** 为复刻加 `path` 标记（`direct`＝原始 URI 字面含 `@`，对应 `:461`；`sip001`＝走 `:469-482`）：

```
produced_nodes=8163
  direct(@ in raw URI, C++ :461) = 3842
  sip001(base64 whole, C++ :469-482) = 4321
garbage_nodes_by_path={'sip001': 4261}   (public-addr garbage=4261)   # direct 侧垃圾 = 0
```

⇒ **4261 个垃圾节点 100% 来自 SIP001 路径 A；直连路径 B 在本数据集零垃圾。**（v1.0 未做此拆分，故错误地把 P0-A 定位为「补强」；实为 **P0-A 覆盖全部实测垃圾**。）

**(c) P0-B 的拦截力度。** 对上述 8163 个产出节点施加 method 形态门：剔除 **4261（52.2%）** 垃圾，对 3014 个合法形态节点**零误伤**；`3014(合法) + 830(形态合法但白名单外) + 4261(垃圾) + 58(null) = 8163` 完全闭合。

---

## 2. 代码路径溯源（逐行）

### 2.1 ss 分支（`SubscriptionParser.cpp:453-626`）

```
:453  else if (line.find("ss://") == 0)
:454  uri = line.substr(5)
:455  hashPos = uri.find('#'); 有则 remarks=urlDecode(尾段), uri 截断
:461  atPos = uri.find('@')                        // ← 路径 B 判据
:469  if (atPos == npos) {                          // ── SIP001 容错（726599d 任务④）＝路径 A
:470      decodedWhole = decodeBase64(decodePercentEscapesOnly(uri));
:471      sipAt    = decodedWhole.rfind('@');
:472      sipColon = decodedWhole.find(':');
:473      if (sipAt != npos && sipAt > 0 && sipColon != npos && sipColon < sipAt) {
:477          uri = decodedWhole;  atPos = sipAt;
:478          sip001UserInfoIsPlaintext = true;  ++stats.ssSip001Recovered;
:479      }
:481  }
:483  if (atPos == npos) { malformed; ++droppedByProtocol[1]; continue; }
...
:608  decodedInfo = plaintext ? userInfo : decodeBase64(decodePercentEscapesOnly(userInfo));
:610  methodPos = decodedInfo.find(':');
:616  method   = decodedInfo.substr(0, methodPos);
:617  password = decodedInfo.substr(methodPos + 1);
:618  if (method.empty() || password.empty()
:619      || !utils::isPrintableAscii(method) || !utils::isPrintableAscii(password))
:620      { malformed; ++droppedByProtocol[1]; continue; }
:625  profile.security = method;  profile.id = password;
```

### 2.2 缺陷触发链（路径 A：以 `ss://eyJ2IjogIjIi...` 为例）

1. `eyJ2IjogIjIi` = base64(`{"v": "2"`)。**base64 字母表不含 `@`** ⇒ `:461` `atPos == npos`。
2. 进入 SIP001 分支：`decodedWhole = base64_decode(...)` = 完整 VMess JSON 文本。
3. JSON 中 `"ps"` 字段常含 `@`（如 `"...Telegram:@Farah_VPN..."`）⇒ `sipAt = rfind('@')` 命中。
4. JSON 首个 `:` 位于 `"v":` / `"add":` 之后 ⇒ `sipColon < sipAt` 成立 ⇒ **误判为 SIP001 明文 userInfo**。
5. `:610` `method = decodedInfo.substr(0, find(':'))` = `{"v"`（或 `{"add"`）。
6. `:619` `isPrintableAscii("{\"v\"")` = **true** ⇒ 无任何拦截。
7. 节点创建：`security = {"v"`。`address/port` 取自 JSON 尾部（`rfind('@')` 之后），得到垃圾地址或空端口。
8. 下游 `SubitemUpdaterV2::isValidProxy`：
   - 端口非法 / `checkRequired()` 失败 ⇒ `LogLevel::INFO` 的 `SKIP:`（**本日志为 WARN-only，不可见**）；
   - 少数端口恰好合法 ⇒ 走到 `:69-74` `isSupportedSsCipher("{\"v\"")==false` ⇒ `WARN: unsupported SS cipher: '{"v'`（日志中 `'{"add'`、`'ss'` 即此）；
   - 极端情况 ⇒ 进入 Xray 注入 ⇒ `Unsupported cipher`（H5，根因待定位）。
   - **实测：均被 `isSupportedSsCipher` 拦下，零入库（§1.4a）。**

### 2.3 路径 B（直连 `@`）为何在本数据集零垃圾

`ss://<uuid>@host:port?...`（误挂 VLESS）：`atPos` 命中 ⇒ 直连 ⇒ `decodeBase64(uuid)` 因 `-` 非字母表被跳过 ⇒ 二进制无 `:` ⇒ `malformed(no_colon)`（16177 条）。**碰巧被拒，非设计正确。**
若上游改为 `ss://<base64(JSON)>@host:port`，则路径 B 同样可产出 `method='{"add'` 且 **address/port 是真实公网值**（危害更大）。本数据集无此形态（实测 direct 垃圾=0），但**P0-B 必须覆盖之**。

---

## 3. 根因判定

| 编号 | 根因 | 判定 | 角色 |
|---|---|---|---|
| **R1** | SIP001 容错缺少「JSON 载荷」闸门：只要含 `@` 与 `:` 即接纳，把 VMess JSON 误纳为 SS 节点（路径 A） | **确证（主因，实测 4261/4261 垃圾）** | 由 **P0-A** 修 |
| **R2** | `:618-624` method 校验仅要求「可打印 ASCII」，未要求「cipher 形态」，无法拦截 `{"v"` 类垃圾 | **确证** | 由 **P0-B** 修（同时覆盖 A+B，防御层） |

---

## 4. 次要发现（非主因，但应一并处理）

| 编号 | 现象 | 性质 | 处置 | v1.1 变化 |
|---|---|---|---|---|
| H2 | 404 订阅 30×「Base64 decode produced invalid content」+「parsed 0 nodes」 | fetch 已拿到 `httpStatus==404`，响应体 `404: Not Found`(14B) 仍被送进 parse | **P0-C**（§6.3） | 精确化调用点 |
| H3 | 1510×`invalid UUID` + 1058×`unsupported cipher` SKIP WARN | 跳过行为**正确**，纯噪声 | **P2**：降 DEBUG + 每轮聚合（复用 `LogStatistics.cpp:43`） | 不变 |
| H5 | 1×`注入xray outbound 错误: ... Unsupported cipher` | 不受支持 cipher 进入 Xray 注入 | **P1-B：根因待定位**（见下），再决定加固 | **降级为待定位** |
| H6 | `byProto=` 语义误导（实为丢弃按协议） | 可读性 | **P2**：改名 `droppedByProto=` | 已实施（见 `docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md` Task 9） |
| H4 | `decodeBase64` 不认 url-safe `-`/`_` | **降优先级**（非本轮根因） | 记录待查，暂不改 | 措辞修正（B3） |

**H4 依据修正**：v1.0 以「复刻用标准字母表即对齐生产数字」作为「证伪」论据，**该论证无效**——复刻与生产用同一套（有缺陷的）算法，两边同样误解码则数字照样对齐。真实降优先级依据是：2026-10-16 可观测性实测中，malformed 集合内 `-`/`_` 出现 **0 次**（详见 `docs/bugfix/2026-10-16-Bugfix-SubscriptionParser-Observability-P0-v1.0.md`）。

**H5 待定位（B4）**：`isValidProxy`（导入闸门）已在 configtype=3 上校验 `isSupportedSsCipher`，为何仍有不受支持 cipher 进入注入？三条互斥候选，须据日志该行的 `indexId` 回溯：
- (a) **存量数据**：该行在 `isSupportedSsCipher` 白名单扩容（`2026-08-11` 增补 `chacha20-ietf-poly1305`）之前入库；
- (b) **旁路插入**：经 `ShareLink` 导入 / `-S sync` / `-IS import-sub-config` 等未经 `isValidProxy` 的路径落库；
- (c) **能力漂移**：白名单与当前 Xray-core 实际支持的 cipher 集合不再一致。

在定位之前，**P1-B 不应盲目落地**（否则可能只是掩盖症状）。

---

## 5. 修复方案

### 5.0 原则
最小改动、不引入 `auto`、保持既有容错风格、可单测、零回归（尤其不得破坏 726599d 四项容错：404 标记 / 误挂 vmess / `%3D` / SIP001）。

### 5.1 P0-A（主修，R1/路径 A）：SIP001 分支加「JSON 闸门」
位置：`src/update/SubscriptionParser.cpp:469-482`

```cpp
if (atPos == std::string::npos) {
    const std::string decodedWhole =
        decodeBase64(decodePercentEscapesOnly(uri));
    // 误挂前缀的 VMess/VLESS JSON 载荷绝不是 SS 明文 userInfo：
    // 以 '{' / '[' 开头者直接排除，避免把 JSON 当成合法 SIP001。
    const bool decodedLooksJson =
        !decodedWhole.empty()
        && (decodedWhole[0] == '{' || decodedWhole[0] == '[');
    const size_t sipAt    = decodedLooksJson ? std::string::npos
                                             : decodedWhole.rfind('@');
    const size_t sipColon = decodedWhole.find(':');
    if (sipAt != std::string::npos && sipAt > 0
        && sipColon != std::string::npos && sipColon < sipAt) {
        uri = decodedWhole;
        atPos = sipAt;
        sip001UserInfoIsPlaintext = true;
        ++stats.ssSip001Recovered;
    }
}
```

> 依据：§1.4b 实测 4261/4261 垃圾全部来自本分支；闸门仅排除 `{`/`[` 起始载荷，对合法 SIP001（以 cipher 名起始，如 `aes-256-gcm:...`）零误伤（回归 726599d 任务④）。

### 5.2 P0-B（防御层，R2/路径 A+B）：method 形态校验收紧
位置：`src/update/SubscriptionParser.cpp:618-624`

```cpp
bool methodLooksCipher = !method.empty() && method.size() <= 32;
for (const char c : method) {
    const unsigned char uc = static_cast<unsigned char>(c);
    const bool ok = (uc >= 'a' && uc <= 'z') || (uc >= '0' && uc <= '9')
                    || c == '-' || c == '_' || c == '.';
    if (!ok) { methodLooksCipher = false; break; }
}
if (!methodLooksCipher || password.empty()
    || !utils::isPrintableAscii(method) || !utils::isPrintableAscii(password)) {
    ++stats.droppedMalformedLine;
    ++stats.droppedByProtocol[1];
    continue;
}
```

**定位声明（B7）**：P0-B 是**弱形态门**（`aaaa`、`null` 之类仍会通过），其职责**不是**判断 cipher 是否受支持，而是**排除非 cipher 形态的载荷**（含 `{`、`"` 等）。它与 `utils::isSupportedSsCipher` 白名单构成 **defense-in-depth 两层**：
- 第 1 层（本处，parse 期）：形态合法性 —— 拒非 cipher 形态；
- 第 2 层（`SubitemUpdaterV2::isValidProxy`，入库前）：白名单 —— 拒受支持性。

> 边界确认：`aes-256-gcm`、`chacha20-ietf-poly1305`、`2022-blake3-aes-256-gcm`(25)、`2022-blake3-chacha20-poly1305`(30)、`none` 全部通过；`{"add"`、`{"v"`、`{"type"`、`{"id"`、`{"seed"` 全部被拒（含 `{`、`"`）。大写 cipher 名不受支持（SS 规范为小写），可接受。
> 量化：对 argh94 单订阅 8163 个产出节点，P0-B 剔除 4261（52.2%）垃圾、误伤 0。

**P0-A 与 P0-B 关系**：P0-A 精确堵住**实测全部**垃圾（路径 A）；P0-B 覆盖 A+B，为防御层（防路径 B 或未来其它订阅出现 JSON userinfo）。两者均需落地（单靠 P0-B 会丢数据；单靠 P0-A 无路径 B 防护网）。

### 5.3 P0-C（H2）：404 短路（**调用点已定位**）
位置：`src/SubitemUpdaterV2.cpp:281-283`（`processSub` lambda 内），以及 `:665-675` 的探测路径同构处。

现状（`:281-317`）：
```cpp
if (!content.empty()) {
    std::vector<db::models::Profileitem> profiles = parseSubscription(content, subId);  // :283
    if (!profiles.empty()) { ... } else {
        if (httpStatus == 404 && httpStatusIsAboutOrigin) { markSourceGone(...); }      // :304
        else { ... stillFailed ... }
    }
}
```

改法：在调用 `parseSubscription` **之前**，用**既有**谓词短路：
```cpp
if (!content.empty()) {
    // 404 权威判定（复用 :271-273 的三分支谓词，与 markSourceGone 同源）：
    // 已知源已失效时，响应体是 "404: Not Found" 占位文本，送入 parse 只会
    // 产生 “Base64 decode produced invalid content / parsed 0 nodes” 噪声。
    if (httpStatus == 404 && httpStatusIsAboutOrigin) {
        phaseFail++;
        markSourceGone(subId, subRemarks);
        Logger::write("ERROR: Subscription source gone (HTTP 404): " + subId
                      + " remarks=" + subRemarks + " url=" + subUrl
                      + " - upstream removed/renamed; marked and disabled",
                      LogLevel::ERR);
        return true;
    }
    std::vector<db::models::Profileitem> profiles = parseSubscription(content, subId);
    ...
}
```

> **必须复用 `httpStatusIsAboutOrigin`（`SubitemUpdaterV2.cpp:271-273`，三分支：非 Accelerator / 未配 accelerator_url / 目标为 GitHub 托管）**。若简化为裸 `httpStatus == 404`，会对「加速器对非 GitHub 目标的非权威 404」误短路，改变重试语义（回归 726599d 任务①）。探测路径（`:665-675`）同步改造。

### 5.4 P1-A（**推荐**，恢复式容错）：误挂 VMess 重新路由
与 726599d 任务②（`vmess://` 挂 vless → `looksLikePlainVlessUri` 改 `vless://`）**对称**：当 ss 载荷 base64 解码为 `{` 起始 JSON 时，判定为「误挂 `ss://` 的 VMess」，改写 `line = "vmess://" + <原始 base64 段>` 并 `continue` 重新入循环，新增统计 `ssMislabeledVmess`。若采用 P0-A，则这些行被丢弃；若采用 P1-A，则可**回收**约 **4257 节点/单订阅**。

**严重度修正（B6）**：v1.0 将其标为「可选」，实为**最高价值项**——它把「被 P0-A 丢弃的 4261 个真实 VMess 节点」重新回收，是 P0-B「不丢数据」的补偿，且直接增加可用代理供给。**建议状态：推荐立项**（作为独立变更，需评估回收后是否引入新的下游噪声）。

### 5.5 P1-B（H5）：**待根因定位后再议**
先据日志该行的 `indexId` 回溯命中 §4 的三条候选（存量 / 旁路 / 漂移），再决定加固点（outbound 生成侧或 DB 清洗）。**在此之前不落地**。

### 5.6 P2（H3/H6）：日志降噪与可读性
- `SubitemUpdaterV2.cpp:65`（invalid UUID）、`:71`（unsupported SS cipher）由 `WARN` → `DEBUG`，并在每轮结束输出**聚合** `skipped: invalidUuid=N unsupportedCipher=N`（复用 `LogStatistics.cpp:43` 既有归类设施）。
- `SubscriptionParser.cpp:781-811` summary 中 `byProto=` → `droppedByProto=`。

---

## 6. 验证计划（TDD，`tests/` 下 Google Test `TEST_F`；**10 条**，含 B8 补充）

| # | 用例 | 断言 | 来源 |
|---|---|---|---|
| 1 | `ss://` + base64(`{"v":"2","add":"1.2.3.4","port":"443","id":"<uuid>","ps":"a@b"}`) | 产出 0 节点；`droppedByProtocol[1]` 增 1；`ssSip001Recovered` 不增 | 路径 A（P0-A） |
| 2 | `ss://` + base64(`{"add":"67.220.95.3","port":"18000",...,"ps":"x@y"}`) | 同上（回归 `{"add"` 场景） | 路径 A（P0-A） |
| 3 | 合法 SIP001（base64(`aes-256-gcm:pass@1.2.3.4:8388`)） | 恢复为节点；`ssSip001Recovered` 增 1（回归任务④） | 回归 |
| 4 | 误挂 plain VLESS（`vmess://vless://...`） | 仍正确改路由（回归任务②） | 回归 |
| 5 | `ss://aes-256-gcm:pass@1.2.3.4:8388` | 正常产出，`security=="aes-256-gcm"` | 正常路径 |
| 6 | method 边界：`none`、`2022-blake3-chacha20-poly1305`(30) 通过；`{"x"`、`ABC!`、41 字符长名 被拒 | 形态校验命中 | **B8** |
| 7 | **路径 B**：`ss://` + base64(`{"add":...}`) + `@1.2.3.4:443`（含字面 `@`） | 产出 0 节点（P0-B 拦截，模拟危险形态） | **B8/B1** |
| 8 | **加速器 404 非权威**：`method=Accelerator` + 已配 `accelerator_url` + 非 GitHub 目标 + http=404 | **不**短路、**不**标 404；走原 `stillFailed` 重试 | **B8/B2** |
| 9 | **真实 SIP001 样本**（取自 726599d 六订阅实测恢复的样例） | 仍恢复成功（防回归） | **B8** |
| 10 | `isSupportedSsCipher` 分层：形态合法但白名单外（`aes-256-cfb`、`null`） | parse 期通过形态门，`isValidProxy` 期被白名单拒（验证两层职责不重叠） | **B7/B8** |

命令：
```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
ctest -V            # 基线：52/53（XrayManagerStartupTest 为已知基线失败）
```

---

## 7. 影响面与回滚
- 影响文件：`src/update/SubscriptionParser.cpp`（P0-A/P0-B）、`src/SubitemUpdaterV2.cpp`（P0-C/P2）、outbound 生成侧（P1-B，待定）。
- 行为变化：误挂 VMess-JSON 的 `ss://` 行由「内部产出幻觉节点→下游白名单丢弃」变为「parse 期直接计为 malformed」（P0-A/P0-B）；404 源在 parse 前短路（P0-C）。合法 SS / 合法 SIP001 / 误挂 VLESS 行为不变。
- 若落地 P1-A：误挂 VMess-JSON 的 `ss://` 行被**回收为 feignable VMess 节点**（正向供给）。
- 回滚：单文件级 revert 即可，无 schema / DB 变更。

## 8. 后续待办
- H5 根因定位（§4）；P1-B 待定位后再立项。
- 未指派（承 726599d 遗留）：anytls/ssr/socks 协议、裸 base64 行、`hy2://` substr 越界、`Base64Utils.cpp` 同源缺陷。
- H4（url-safe 字母表）待取真实 base64url 载荷实证后再议。

## 9. 变更记录
| 日期 | 版本 | 变更 |
|---|---|---|
| 2026-10-19 | v1.0 | 首版：RCA（R1/R2 确证）+ P0/P1/P2 修复方案 + 验证计划 |
| 2026-10-19 | v1.1 | 依据独立评审 B1–B8 修订：新增 §1.4 影响量化（实测零入库，严重度降 LOW）；B1 路径归因实测（4261/4261 垃圾出自 SIP001）并重定义 P0-A 为主修、P0-B 为防御层；P0-C 定位 `SubitemUpdaterV2.cpp:281-283` + 复用 `httpStatusIsAboutOrigin`；H4 论据修正为 0 次实测；H5 降级为待定位 + 三候选；P1-A 升为推荐（回收 ~4257）；P2 明确两层职责；验证计划 6→10 条 |
