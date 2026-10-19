# Bugfix RCA: `ss://` 分支误将「误挂前缀的 VMess JSON」解析为 SS 节点（garbage security）

- 文档类型：Bugfix / RCA + 修复方案（Spec）
- 模块：`SubscriptionParser`（`src/update/SubscriptionParser.cpp`）+ `SubitemUpdaterV2`
- 版本：v1.0 ｜ 日期：2026-10-19
- 现场日志：`bin/log/ui_20261019_083638.log`（426,396 B / 4,469 行）

> ⚠️ **本 v1.0 已由 `docs/bugfix/2026-10-19-Bugfix-SubscriptionParser-MislabeledSsPayload-v1.1.md` 取代**（依据独立评审 `docs/reviews/2026-10-19-Review-SubscriptionParser-MislabeledSsPayload-v1.0.md` 的 B1–B8 修订）。保留以存证。

---

## 0. 结论摘要

生产日志中被归类为「解析错误」的表象有多类，但**真正的解析缺陷只有一个**：

> **当订阅源把 VMess 节点的 base64(JSON) 载荷挂上 `ss://` 前缀时（上游诱饵/聚合源的典型污染手法），`SubscriptionParser` 的 ss 分支会把它当成合法 SS 节点解析出来，产出 `security` 为 `{"add"` / `{"v"` / `{"type"` / `{"id"` / `{"seed"` 的垃圾节点，且全程静默（不报 malformed）。**

罪魁祸首是 2026-10-16 提交 `726599d` 任务④引入的 **SIP001 过宽容错**（`SubscriptionParser.cpp:469-482`）：它在「无 `@`」时对整条 URI 做 base64 解码，只要解码结果里出现 `@` 且其前有 `:` 就认定为「SIP001 明文 userInfo」。而 VMess JSON（`{"v":"2",...,"ps":"...@..."}`）恰好满足该条件，于是被误纳。

其余「解析错误」日志均为**噪声或既有正确行为**（详见 §5）。

---

## 1. 症状与现场证据

### 1.1 日志中与解析相关的 WARN/ERROR 归类（TOP）

| 形态 | 次数 | 性质 |
|---|---|---|
| `SKIP: <ip>:<port> - invalid UUID format` | 1510 | 上游 decoy 垃圾，**跳过正确**，仅噪声 |
| `SKIP: <ip>:<port> - unsupported SS cipher: '<x>'` | ~1058 | 其中 835 `aes-256-cfb` 等旧式 cipher **跳过正确**；但含 `'{"add'`、`'ss'` ← **垃圾，是缺陷证据** |
| `Base64 decode produced invalid content ... [subid=N]` | 30 | 404 响应体副产物，噪声（H2） |
| `subid=N: parsed 0 nodes from 1 lines (...)` | 30 | 同上（H2） |
| `vmess parse failed`（syntax error / extra data / not JSON） | 22 | 上游 JSON 语法错误，**拒绝正确** |
| `[Worker-N] 注入xray outbound 错误: ... Unsupported cipher.` | 1 | **有垃圾/不受支持 cipher 绕过校验进入 Xray 注入**（H5） |

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

---

## 2. 复现（Python 忠实复刻，数字对齐生产）

工具：`temp/replica_parser.py`（逐行复刻 ss 分支 + `decodeBase64`/`urlDecode`/`parseAddressPort` + `Utils` 各校验）。

输入：经 jsDelivr 镜像下载的 `temp/argh94_ss.txt`（8,801,219 B，32,563 行；curl 直连 raw.githubusercontent.com 被 reset）。

输出（**与生产日志对齐**）：

```
total_lines=32563  ss_lines=32562
kinds={'malformed': 24399, 'node': 8163}
malformed_why={'no_colon': 16177, 'no_at': 4051, 'weakcheck': 4169, 'no_addr': 2}
produced_nodes=8163  (public_addr=8162, nonpublic=1)
--- top methods ---
    3025  '{"add"'
    1517  'aes-256-gcm'
    1261  'chacha20-ietf-poly1305'
    1232  '{"v"'
     718  'aes-256-cfb'
     ...
       2  '{"type"'
       1  '{"id"'
       1  '{"seed"'
--- top UNSUPPORTED methods ---
    3025  '{"add"'   1232  '{"v"'   718  'aes-256-cfb'   58  'null' ...
```

| 指标 | 生产日志 | Python 复刻 | 判定 |
|---|---|---|---|
| malformed | 24098 | 24399 | 量级一致（CR/行处理微差） |
| 产出节点 | otherOk=8215 | 8163 | 一致 |

**复刻成功 ⇒ 根因判定可信。**

---

## 3. 代码路径溯源（逐行）

### 3.1 ss 分支（`SubscriptionParser.cpp:453-626`）

```
:453  else if (line.find("ss://") == 0)
:454  uri = line.substr(5)
:455  hashPos = uri.find('#'); 有则 remarks=urlDecode(尾段), uri 截断
:461  atPos = uri.find('@')
:469  if (atPos == npos) {                       // ── SIP001 容错（726599d 任务④）
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

### 3.2 缺陷触发链（以 `ss://eyJ2IjogIjIi...` 为例）

1. `eyJ2IjogIjIi` = base64(`{"v": "2"`)。**base64 字母表不含 `@`** ⇒ `:461` `atPos == npos`。
2. 进入 SIP001 分支：`decodedWhole = base64_decode(...)` = 完整 VMess JSON 文本。
3. JSON 中 `"ps"` 字段常含 `@`（如 `"...Telegram:@Farah_VPN..."`）⇒ `sipAt = rfind('@')` 命中。
4. JSON 首个 `:` 位于 `"v":` / `"add":` 之后 ⇒ `sipColon < sipAt` 成立 ⇒ **误判为 SIP001 明文 userInfo**。
5. `:610` `method = decodedInfo.substr(0, find(':'))` = `{"v"`（或 `{"add"`）。
6. `:619` `isPrintableAscii("{\"v\"")` = **true** ⇒ 无任何拦截。
7. 节点创建：`security = {"v"`。`address/port` 取自 JSON 尾巴（首个 `@` 之后），得到垃圾地址或空端口。
8. 下游 `SubitemUpdaterV2::isValidProxy`：
   - 端口非法 / `checkRequired()` 失败 ⇒ `LogLevel::INFO` 的 `SKIP:`（**本日志为 WARN-only，不可见**）；
   - 少数端口恰好合法 ⇒ 走到 `:69-74` `isSupportedSsCipher("{\"v\"")==false` ⇒ `WARN: unsupported SS cipher: '{"v'`（日志中 `'{"add'`、`'ss'` 即此）；
   - 更进一步 ⇒ 进入 Xray 注入 ⇒ `Unsupported cipher`（H5）。

### 3.3 对照：误挂 VLESS（含 `@`）为何没出问题

`ss://<uuid>@host:port?...`：`atPos` 命中 ⇒ 直连路径 ⇒ `userInfo = 36 位 uuid` ⇒ `decodeBase64` 跳过非字母表字符（`-`）⇒ 解码得二进制 ⇒ 无 `:` ⇒ `malformed(no_colon)`。**恰好被拒**（16177 条）。这是「碰巧正确」而非设计正确。

---

## 4. 根因判定

| 编号 | 根因 | 判定 |
|---|---|---|
| **R1** | SIP001 容错缺少「JSON 载荷」闸门：只要含 `@` 与 `:` 即接纳，把 VMess JSON 误纳为 SS 节点 | **确证（主因）** |
| **R2** | `:618-624` method 校验仅要求「可打印 ASCII」，未要求「cipher 形态」，无法拦截 `{"v"` 类垃圾 | **确证（共犯）** |

---

## 5. 次要发现（非主因，但应一并处理）

| 编号 | 现象 | 性质 | 处置建议 |
|---|---|---|---|
| H2 | 404 订阅 30×「Base64 decode produced invalid content」+「parsed 0 nodes」 | fetch 已拿到 `httpStatus==404`，响应体 `404: Not Found`(14B) 仍被送进 parse | **P0**：已知 404 时短路跳过 parse（或抑制该两条 WARN） |
| H3 | 1510×`invalid UUID` + 1058×`unsupported cipher` SKIP WARN | 跳过行为**正确**，纯噪声 | **P2**：降为 DEBUG + 每轮聚合（复用 `LogStatistics.cpp:43` 既有归类设施） |
| H5 | 1×`注入xray outbound 错误: ... Unsupported cipher` | 垃圾 cipher 绕过 `isValidProxy` 进入 Xray 注入 | **P1**：注入前加 defense-in-depth 校验（配置生成侧再验 `isSupportedSsCipher`） |
| H6 | `byProto=` 语义误导（实为丢弃按协议） | 可读性 | **P2**：改名 `droppedByProto=` |
| H4 | `decodeBase64` 不认 url-safe `-`/`_` | 本轮**未证实**（复刻用标准字母表即对齐生产数字） | 记录待查，暂不改 |

---

## 6. 修复方案

### 6.0 原则
最小改动、不引入 `auto`、保持既有容错风格、可单测、零回归（尤其不得破坏 726599d 三项容错）。

### P0-A（主修，R1）：SIP001 分支加「JSON 闸门」
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

### P0-B（主修，R2）：method 形态校验收紧
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

> 边界确认：`aes-256-gcm`、`chacha20-ietf-poly1305`、`2022-blake3-aes-256-gcm`、`none` 全部通过；`{"add"`、`{"v"`、`{"type"`、`{"id"`、`{"seed"` 全部被拒（含 `{`、`"`）。大写 cipher 名不受支持（SS 规范为小写），可接受。

### P0-C（H2）：404 短路
在订阅更新流程中，当 fetch 结果 `httpStatus == 404`（已被 `726599d` 标记逻辑识别）时，**在调用 `parse()` 之前 return**，不再对 `404: Not Found` 文本执行 base64/parse，从而消除 30× 两条 WARN。

### P1-A（可选，恢复式容错）：误挂 VMess 重新路由
与 726599d 任务②（`vmess://` 挂 vless → `looksLikePlainVlessUri` 改 `vless://`）**对称**地：当 ss 载荷 base64 解码为 `{` 起始的 JSON 时，判定为「误挂 `ss://` 的 VMess」，改写 `line = "vmess://" + <原始 base64 段>` 并 `continue` 重新入循环，新增统计 `ssMislabeledVmess`。若采用 P0-A，则这些行被丢弃；若采用 P1-A，则可**回收**约 4257 个节点/单订阅。
**建议**：先落 P0-A/P0-B（安全、可测）；P1-A 作为后续独立变更（需评估回收后是否引入新的下游噪声）。

### P1-B（H5）：Xray 注入前复验
在 outbound 配置生成/注入路径，对 `configtype=="3"` 的节点再次断言 `utils::isSupportedSsCipher(security)`，不通过则跳过并 WARN，避免「Unsupported cipher」直达 Xray。

### P2（H3/H6）：日志降噪与可读性
- `SubitemUpdaterV2.cpp:65`（invalid UUID）、`:71`（unsupported SS cipher）由 `WARN` → `DEBUG`，并在每轮结束输出**聚合** `skipped: invalidUuid=N unsupportedCipher=N`。
- `SubscriptionParser.cpp:781-811` summary 中 `byProto=` → `droppedByProto=`。

---

## 7. 验证计划（TDD，`tests/` 下 Google Test `TEST_F`）

| # | 用例 | 断言 |
|---|---|---|
| 1 | `ss://` + base64(`{"v":"2","add":"1.2.3.4","port":"443","id":"<uuid>","ps":"a@b"}`) | 产出 0 节点；`droppedByProtocol[1]` 增 1；`ssSip001Recovered` 不增 |
| 2 | `ss://` + base64(`{"add":"67.220.95.3","port":"18000",...,"ps":"x@y"}`) | 同上（回归 `{"add"` 场景） |
| 3 | 合法 SIP001（base64(`aes-256-gcm:pass@1.2.3.4:8388`)） | 恢复为节点；`ssSip001Recovered` 增 1（回归任务④） |
| 4 | 误挂 plain VLESS（`vmess://vless://...`） | 仍正确改路由（回归任务②） |
| 5 | `ss://aes-256-gcm:pass@1.2.3.4:8388` | 正常产出，`security=="aes-256-gcm"` |
| 6 | method 边界：`none`、`2022-blake3-aes-256-gcm` 通过；`{"x"`、`ABC!`、41 字符长名 被拒 | 形态校验命中 |

命令：
```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
ctest -V            # 基线：52/53（XrayManagerStartupTest 为已知基线失败）
```

---

## 8. 影响面与回滚
- 影响文件：`src/update/SubscriptionParser.cpp`（P0-A/P0-B）、订阅更新调用点（P0-C）、`src/SubitemUpdaterV2.cpp`（P2）、配置生成侧（P1-B）。
- 行为变化：误挂 VMess-JSON 的 `ss://` 行由「静默产出垃圾节点」→「计为 malformed 丢弃」（P0）；合法 SS / 合法 SIP001 / 误挂 VLESS 行为不变。
- 回滚：单文件级 revert 即可，无 schema / DB 变更。

## 9. 后续待办
- 未指派：anytls/ssr/socks 协议、裸 base64 行、`hy2://` substr 越界、`Base64Utils.cpp` 同源缺陷（承 726599d 遗留）。
- H4（url-safe 字母表）待取真实 base64url 载荷实证后再议。

## 10. 变更记录
| 日期 | 版本 | 变更 |
|---|---|---|
| 2026-10-19 | v1.0 | 首版：RCA（R1/R2 确证）+ P0/P1/P2 修复方案 + 验证计划 |
