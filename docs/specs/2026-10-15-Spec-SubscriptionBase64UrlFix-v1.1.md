# Spec — 订阅 vmess 解析链路缺陷修复（base64url / 兜底裁剪 / 字段完整性 / 可观测性）

- **日期**: 2026-10-15
- **模块**: `src/update/SubscriptionParser`（+ `include/update/SubscriptionParser.h`）、`tests/test_subscription_parser.cpp`
- **版本**: v1.1（**取代 v1.0**。v1.0 经 3 份独立评审一致判定 FAIL，本版按评审结论重构）
- **触发输入**: `bin\worker\log\ui_20261016_084620.log` 中 114 条 `Failed to parse vmess`

---

## 0. 对 v1.0 的更正声明（评审强制要求，先读）

v1.0 存在 5 处**事实性错误**，本版全部撤回，不得再作为决策依据：

| v1.0 的断言 | 更正 |
|---|---|
| 「114 条解析错误**全部源于同一根因**（base64url）」 | **撤回。** 证据不足：日志 `file_level=WARN` 过滤掉全部 INFO（实测 `INFO:` 出现 **0** 次），114 条错误行形状完全一致、无 subid / 无行号 / 无载荷 ⇒ **结构上无法把任何一条归因到任何机制**。且 v1.0 自己的 §1 表格就把 9 条归缺陷 3、7 条归缺陷 4，另有明文 JSON 路径能 100% 确定性产生 `not JSON`。另：v1.0 表格次数合计 98+9+7+9=**123**，与文中「114」不符。 |
| 「`boost.json:1` ⇒ 偏移 1 ⇒ 首字节即非法（BOM）」 | **撤回。** boost 位置串格式为 `boost.json:<行号> at <文件>:<列号> in <函数>`。「1」是**行号**，列号被日志截断。`boost.json:2` + 函数 `write` ⇒ **`jsonStr` 内含换行符**，与 v1.0「一行内多个 JSON」的说法直接矛盾。BOM 从未在任何真实载荷中被发现过。 |
| 「剥离 `#备注` 会加剧 base64url 错位」 | **撤回。** `decodeBase64` 对非法字符的 `continue` 发生在 `i++` **之前**，被跳过的字符**从不占用 4 字符分组槽位** ⇒ 增删 junk 字符对输出字节流**零影响**。同理「7 条 `extra data` 归因于 `#备注` 未剥干净」也不成立。 |
| 「ss:// 路径零改动」 | **撤回。** `decodeBase64` 有**三个**调用点（`:51` body / `:77` vmess 载荷 / **`:364` ss:// userInfo**），改它必然影响 ss://。且 `:554-557` 的历史注释**明确把 `'_'` 和 `'-'` 列进了「必须跳过」清单**，v1.0 却声称「既有跳过行为完全保留、不回退历史修复」。 |
| 「现代订阅源/面板**大量使用** base64url」 | **撤回。** 无任何占比统计；主流 v2board/xboard 与 v2rayN 的订阅 body 惯例用 `base64.StdEncoding`（含 `+` `/` 与 `=` padding）。 |

**并追加一条 v1.0 完全未提的对照事实**：`src/utils/Base64Utils.cpp:25-26` 存在**同一缺陷的第二处实现**（遇 `-`/`_` 整段返回空），同文件 `looksLikeBase64()` 谓词恰好提供「解码前区分『压根不是 base64』与『是 base64 但损坏』」的能力。

---

## 1. 输入 / 输出 / 前置条件 / 边界条件

> 本节为 AGENTS.md §6.2 `writing-plans` 强制要求的四要素，v1.0 全部缺失。

### 1.1 输入

| 输入 | 类型 | 约束 |
|---|---|---|
| 订阅原文 `content` | `const std::string&` | 可能为①整段 base64 / base64url、②明文分享链接逐行、③base64 解出后不含 `://` 的非订阅内容（反爬页 / 限流 JSON / 404 空体） |
| 订阅 id `subid` | `const std::string&` | 仅用于日志归因，不参与解析决策 |
| 输出容器 | `std::vector<db::models::Profileitem>&` | 调用方仅 `src/update/SubitemUpdaterV2.cpp:567` 一处 |

### 1.2 输出

| 输出 | 类型 | 说明 |
|---|---|---|
| 解析结果 | `std::vector<db::models::Profileitem>` | 原 `parse()` 签名**保持不变**，新增 out-param 重载 |
| 解析统计 | `ParseStats&`（新增 public 结构体） | 全部失败路径与 base64url 特征计数，**可被单元测试直接断言**，不依赖读日志 |

### 1.3 前置条件

1. C++17，**全栈禁用 `auto` 类型推导**（AGENTS.md §1）。
2. `src/update/SubscriptionParser.cpp` 在 `CMakeLists.txt:143` 的 `CORE_SOURCES` 中，被 `test_autotask` / `validproxy` / `validproxy-cli` 共用 ⇒ **任何改动必须全量 `ctest -V` 回归**，不得只跑 `ctest -R SubscriptionParserTest`。
3. 追加 gtest 用例**无需改 CMake**：`CMakeLists.txt:966-974` 已把 `SubscriptionParser.cpp` 列入 `test_subscription_parser` 目标源。
4. 编译期约束不对称：MSVC 分支（`CMakeLists.txt:66`）为 `/W4 /WX`（**unused 变量直接报错**）；GCC 分支（`:69`）为 `-Wall -Wextra -pedantic` 无 `-Werror`。⇒ **不得引入任何未被消费的变量**（v1.0 的 `B64Status b64Status` 即因此被评审判死变量）。
5. `test_subscription_parser` 链接 `src/Utils.cpp` + `src/Logger.cpp` + `src/LoggerInstance.cpp` + `libboost_json` + `-lws2_32` ⇒ `Logger::pushCallback` 可用（见 §5.0），无需改链接。

### 1.4 边界条件

| 边界 | 期望行为 |
|---|---|
| body 长度 < 10 或解出内容不含 `://` | 保持现状：回退原始 content（`:53-56`），但**必须**记 `bodyFallbackToOriginal=true` 并在 WARN 中输出 |
| 整段 base64url（body 含 `-`/`_`） | 归一化后正确解出（**P2**） |
| 载荷为空 / 仅 1 个字母表字符 | 计入 `vmessSkipEmptyPayload`，**不得**静默 |
| 载荷是明文 JSON（`vmess://{...}`） | 直接采用（**P3.1**），不经过 decodeBase64 |
| 载荷是 URL 编码 base64（含 `%2B` / `%3D`） | 先 `urlDecode` 再 `decodeBase64`（**P3.2**）；现状是 `urlDecode` 死代码必然失败 |
| `#备注` 片段含 base64 字母表字符 | 片段不得进入解码流（**P3.3**）；`ps` 缺失时回落为 `remarks` |
| 尾部垃圾含 `}` | 括号配平扫描必须取到**JSON 自身的**闭合 `}`（**P1.2**） |
| 顶层是数组 `[{...}]` | 结构化裁剪会取出内层对象并接受（**行为变更，须在 §6 声明**） |
| 顶层非对象（`"str"` / `123` / `true`） | `as_object()` 抛 `std::runtime_error` 派生异常，`catch (const std::exception&)` 可捕获 ⇒ 丢弃，无风险 |

---

## 2. 现状代码事实（已逐行核对，行号可直接引用）

### 2.1 `decodeBase64`（`src/update/SubscriptionParser.cpp:533-600`）

```cpp
533: std::string SubscriptionParser::decodeBase64(const std::string& input) {
534:     static const char base64_chars[] =
535:         "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";   // sizeof == 65
...
546:         if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;  // padding/空白跳过
550:         if (static_cast<unsigned char>(c) > 127) continue;                            // 非 ASCII 丢弃
554:     // Locate c in the base64 alphabet. Non-alphabet characters (e.g. '%',
555:     // '@', ':', '_', '-') MUST be skipped — decoding them as index 0
556:     // corrupts the output stream (root cause of binary-garbage
557:     // method/password bytes for malformed ss:// links).
560:         for (size_t k = 0; k < sizeof(base64_chars); k++)      // ← 65，会命中结尾 NUL
562:             if (base64_chars[k] == c) { pos = k; found = true; break; }
567:         if (!found) continue;                                 // ← 非法字符静默丢弃（在 i++ 之前）
574-583:  /* 标准 4→3 字节解算 */
586-597:  /* 尾部：i==1 → 0 字节（ret 空）；i==2 → 1 字节（可能是单个 \0）；i==3 → 2 字节 */
```

**三个已实测确认的缺陷**：

1. **无 base64url 归一化** ⇒ `-` / `_` 落入 `:567` 静默丢弃。
2. **`sizeof(base64_chars) == 65`**（含结尾 NUL）⇒ 输入含 `\0` 时会在 `k=64` 命中并以 `pos=64` 污染输出流。实测 `old("QUJD\0REVG")` → `b'ABC\x01\x11\x15'`，正确应为 `b'ABCDEF'`。
3. **无任何失败/统计返回通道** ⇒ 调用方无法区分「解码成功但内容无 `://`」与「解码本身就坏了」。

### 2.2 vmess 解析主路径与兜底路径的字段差异

主路径 `:92-144` 赋值 25 个字段；兜底路径 `:154-169` 只赋 11 个，**实际丢失 12 项**：

| 丢失项 | 主路径行号 | 后果 |
|---|---|---|
| `port` | `:101-110`（int64 / string / double 三态） | **空端口** ⇒ `Profileitem::checkRequired()`（`include/Profileitem.h:75-79`）直接抛异常 |
| `aid` → `alterid` 三态 | `:114-125` | 兜底恒为 `"0"` |
| `path` | `:131` | 空 |
| `requesthost` | `:132` | 空 |
| `streamsecurity` | `:133` | **空 ⇒ 节点按明文 TCP 走，TLS 参数全丢** |
| `sni` | `:134` | 空 |
| **`sni` ← `requesthost` 的 tls 回退** | `:135-137`（条件：`sni` 空 + `requesthost` 非空 + `streamsecurity=="tls"`） | 条件块，最易漏 |
| `flow` | `:138` | 空 |
| `fingerprint` | `:139`（**默认值 `"chrome"` 非空**） | 静默改变 TLS 指纹 |
| `alpn` | `:140` | 空 |
| `headertype` | `:141` | 空 |
| `allowinsecure` | `:142-143`（**默认值 `"false"` 不是 `""`**） | 静默改变证书校验 |

兜底路径 `:160` 硬编码 `network = "tcp"` 且**从不读 JSON 的 `net`**。

### 2.3 结构性问题

| 位置 | 问题 |
|---|---|
| `:78-80` | `urlDecode` 是**死代码**。`:586-597` 保证任何含 ≥2 个字母表字符的输入都返回非空 ⇒ `if (jsonStr.empty())` 为假 ⇒ `urlDecode` **永不执行** ⇒ URL 编码型载荷（`%2B`/`%3D`）**必然**落到 `not JSON`，这是与 base64url **无关**的独立失败模式 |
| `:147-148` | 兜底裁剪的 errMsg 白名单只认 `"incomplete"` / `"extra data"`。日志里的 `syntax error` 不在其中 ⇒ 已含 `{`/`}` 的载荷拿不到第二次机会 |
| `:151` | `rfind('}')` 是**非括号配平扫描** ⇒ 尾随垃圾含 `}` 时取错尾括号，重试仍 `extra data`，被 `:170 catch(...) {}` 静默吞掉 |
| `:521-522` | `else { continue; }` 是**最大静默丢弃通道，零计数零日志**。整段解码错位时第 2 行起全部走这里 ⇒ 98 条 `not JSON` 只是「每个错位订阅的首行数」，**真实损坏量无上界** |
| `:81` / `:78-80` / 拟新增的 `if (encoded.empty()) continue;` | 三条早退 `continue` 若放在失败计数之前，新汇总指标**天生低报** |
| `:529` | 唯一汇总 `INFO: Parsed N valid profiles from M lines` 是 INFO 级 ⇒ 在 `file_level=WARN` 下**不落盘** |

### 2.4 `:53-56` 守卫的误判机理

```cpp
53: if (decoded.length() < 10 || decoded.find("://") == std::string::npos) {
54:     Logger::write("WARN: Base64 decode produced invalid content, using original", WARN);
55:     decoded = content;
56: }
```

对含 `-`/`_` 的大 body：**错位只丢弃字符，其后内容仍是可读的明文前缀**，首行 `vmess://` 几乎必然保留 ⇒ `://` 存在且 `length` 巨大 ⇒ **两个条件都不成立 ⇒ 根本不会打出这条 WARN**。反之，一个 HTML 反爬页经 base64 解出的垃圾几乎必然既不含 `://` 又远超 10 字节 ⇒ **必然命中** ⇒ 回退原始 base64 文本 ⇒ 逐行时无一行以 `vmess://` 开头 ⇒ `:521` 零诊断 ⇒ **0 节点**。⇒ v1.0 §1「整块解崩 ⇒ 0 节点」的因果链在自己的源码里走不通，该现象更可能来自**非订阅内容**。

---

## 3. 分阶段修复方案

> v1.0 的单阶段方案被评审判为「把未坐实的假设当已坐实根因去修」。本版按**证据依赖**分四阶段，每阶段独立可发布、独立可回滚。

### 3.0 阶段总览

| 阶段 | 性质 | 是否依赖外部证据 | 行为变更 |
|---|---|---|---|
| **P0** 可观测性 | 纯增量，零行为变更 | 否 | **无** |
| **P1** 确定性缺陷（缺陷 3） | 修「已含 `{`/`}` 却被英文文案白名单拒绝」+ 兜底字段完整性 | 否（确定性缺陷，与日志归因无关） | 有（且是**修复**） |
| **P2** base64url 支持（缺陷 1） | 补 base64url 归一化 + 修 `\0`/index-64 | 否（成本 2 行，对标准 base64 严格 no-op） | 有（对含 `-`/`_` 的输入） |
| **P3** 独立失败模式（缺陷 4 + 死兜底） | 明文 JSON / URL 编码载荷 / `#备注` 片段 | 否 | 有 |
| **P4** 归因结论 | 读 P0 产出的真实数据后判定 base64url 占比，再决定 P2 是否扩面 | **是** | 无 |

**实施顺序**：P0 → P1 → P2 → P3 → （跑一次 `-UA` 真实更新）→ P4 结论 → 更新本 Spec。

---

### 3.1 P0 — 可观测性（零行为变更，纯增量）

#### P0.1 新增 `ParseStats`（`include/update/SubscriptionParser.h`，public 区）

```cpp
struct ParseStats {
    // 行级计数（口径：进入 while(getline) 的每一行恰好计入一处）
    long lineCount            = 0;  // 总行数
    long vmessOk              = 0;  // vmess 解析成功
    long vmessFailParse       = 0;  // vmess 解析失败（含 salvage 重试仍失败）
    long vmessFailSalvaged    = 0;  // vmess 经裁剪重试救回
    long vmessSkipEmpty       = 0;  // 载荷为空 / 不足 2 个字母表字符
    long unrecognizedLines    = 0;  // :521-522 的 else 分支（当前最大静默通道）

    // body 级 base64 特征（用于判定 base64url 是否真实存在）
    bool bodyHadProtocol      = false;  // :43 hasProtocol
    bool bodyDecodeApplied    = false;  // 走了 decodeBase64 分支
    bool bodyDecodeFellBack   = false;  // :53-56 回退原始 content
    long bodyUrlCharCount     = 0;      // body 中 '-' 与 '_' 的总数（base64url 特征）
    long bodyNonBase64Chars   = 0;      // body 中非字母表/非 padding/非 ASCII 的字符数
    long bodyDecodedLength    = 0;      // 解码后长度（0 表示未解码）

    // 载荷级 base64url 特征
    long payloadUrlCharLines  = 0;      // 载荷中含 '-' 或 '_' 的行数
    long payloadUrlCharTotal  = 0;      // 载荷中 '-' 与 '_' 的总数
    long payloadPlainJsonLines= 0;      // 明文 JSON 载荷（P3.1 路径）
    long payloadUrlEncoded    = 0;      // 含 '%' 的载荷（P3.2 路径）
};
```

#### P0.2 `parse()` 新增 out-param 重载（原签名保持不变并委托）

```cpp
// 头文件 public 区
std::vector<db::models::Profileitem> parse(const std::string& content, const std::string& subid);
std::vector<db::models::Profileitem> parse(const std::string& content, const std::string& subid,
                                            ParseStats& stats);
```

调用方改动面：生产仅 `src/update/SubitemUpdaterV2.cpp:567` 一处（无需改，委托自动生效），测试 9 处可继续用旧签名。

#### P0.3 计数落位（必须覆盖全部出口，避免 I8「指标低报」）

**原则**：删除三条 early-`continue` 路径，或在 `continue` 之前显式记账。对应关系：

| 现状位置 | 现状行为 | P0.3 记账 |
|---|---|---|
| `:75-81` `encoded` 为空 / `vmess://` 无载荷 | `continue`，无日志 | `++stats.vmessSkipEmpty` |
| `:78-80` `jsonStr.empty()` | `continue`，无日志 | `++stats.vmessSkipEmpty` |
| `:178` `if (!parseSuccess) continue;` | 已打 WARN | `++stats.vmessFailParse` |
| `:521-522` `else { continue; }` | **零诊断** | `++stats.unrecognizedLines` |
| `:169` 裁剪重试成功 | 仅 `parseSuccess=true`，无日志 | `++stats.vmessFailSalvaged` + **限流** WARN |

#### P0.4 归因信息入日志（解决 C4「无法归因」）

所有本模块新增/修改的 WARN **必须携带 `subid=` 与行号**：

```
WARN: vmess parse failed [subid=42 line=17] not JSON
WARN: vmess body is not a usable subscription [subid=42] hadProtocol=0 decodedLen=812 urlChars=0 junk=64 -> 0 nodes
WARN: vmess salvage retry failed [subid=42 line=17] extra data
WARN: vmess summary [subid=42] lines=100 ok=0 fail=98 salvaged=0 empty=0 unrecognized=2
```

**明细限流前 3 条 + 末尾 1 条汇总**（`:178` 的失败明细、`:169` 的 salvage 失败明细均限流；汇总不限流）。

#### P0.5 零结果 / 高丢弃率告警（修复 I7「静默丢 99% 节点不触发任何告警」）

| 条件 | 级别 | 文案 |
|---|---|---|
| `lineCount > 0 && vmessOk == 0` | WARN | `subid=N: 解析出 0 个节点`（+ 全量计数） |
| `vmessOk > 0 && (vmessFailParse + unrecognizedLines) * 2 > vmessOk` | WARN | `subid=N: 丢弃率 X%（ok/fail/unrecognized）` |
| `bodyDecodeFellBack == true` | WARN | 保留现状 `:54` 文案 + 补 `subid` 与统计 |

> v1.0 的零结果告警**只在 `profiles.empty()` 时响**，因此「静默丢 99%」不触发 —— 本版改为按**丢弃率**判定。

---

### 3.2 P1 — 确定性缺陷（缺陷 3：兜底裁剪 + 字段完整性）

#### P1.1 抽取 `fillVmessProfile`（消除兜底路径丢 12 个字段）

```cpp
// 头文件 private 区新增
static bool fillVmessProfile(boost::json::object& obj, db::models::Profileitem& profile);
```

> **形参必须是非 const 引用**：`:102` 与 `:115` 需要 `boost::json::value& portVal = obj.at("port");`。boost `object::at()` 的 ref-qualified 重载为 `&` → `value&`、`&&` → `value&&`、`const&` → **`value const&`**（`boost/json/object.hpp:1093-1108`）⇒ 用 `const boost::json::object&` 会编译失败。

函数体**逐行搬运 `:92-144`**，关键点（评审已逐条核对，全部必写，**不得用省略号**）：

```cpp
profile.configtype    = "1";
profile.configversion = "2";
profile.alterid       = "0";
profile.network       = "tcp";
profile.coretype      = "";
profile.muxenabled    = "0";
profile.address = getJsonValueString(obj, "add", "");
profile.indexid = utils::generateUniqueId();          // ← 必须在 sanitizeNetwork 之前
if (obj.contains("port")) {                           // :101-110 三态
    boost::json::value& portVal = obj.at("port");
    if      (portVal.is_int64())  profile.port = std::to_string(portVal.as_int64());
    else if (portVal.is_string()) profile.port = portVal.as_string().c_str();
    else if (portVal.is_double()) profile.port = std::to_string(static_cast<int>(portVal.as_double()));
}
profile.id = getJsonValueString(obj, "id", "");
if (obj.contains("aid")) {                            // :114-125 三态（非 int64/string 一律 "0"）
    boost::json::value& aidVal = obj.at("aid");
    if      (aidVal.is_int64())  profile.alterid = std::to_string(aidVal.as_int64());
    else if (aidVal.is_string()) profile.alterid = aidVal.as_string().c_str();
    else                          profile.alterid = "0";
} else { profile.alterid = "0"; }
profile.security     = getJsonValueString(obj, "scy", "auto");    // 默认 "auto"
profile.network      = getJsonValueString(obj, "net", "tcp");
sanitizeNetwork(profile.network, profile.indexid);
profile.remarks      = getJsonValueString(obj, "ps", "");
profile.path         = getJsonValueString(obj, "path", "");
profile.requesthost  = getJsonValueString(obj, "host", "");
profile.streamsecurity = getJsonValueString(obj, "tls", "");
profile.sni          = getJsonValueString(obj, "sni", "");
if (profile.sni.empty() && !profile.requesthost.empty() && profile.streamsecurity == "tls") {
    profile.sni = profile.requesthost;                // ← :135-137 条件回退，必写
}
profile.flow        = getJsonValueString(obj, "flow", "");
profile.fingerprint = getJsonValueString(obj, "fp", "chrome");    // ← 默认非空，必写
profile.alpn        = getJsonValueString(obj, "alpn", "");
profile.headertype  = getJsonValueString(obj, "type", "");
const std::string insecureVal = getJsonValueString(obj, "insecure", "");
profile.allowinsecure = (insecureVal == "1" || insecureVal == "true") ? "true" : "false"; // ← 默认 "false"，必写
```

> **`indexid` 是阻断级**：`include/Profileitem.h:22` 的 `std::string indexid;` 无默认初始化器 + `:73 Profileitem() = default;` ⇒ 漏掉它会让**全部** vmess 节点 indexid 为空串 ⇒ DAO 主键冲突 / 去重 / 列表键控 / XrayApi outbound tag 全失效；且 `sanitizeNetwork(network, indexid)` 因 `SubscriptionParser.cpp:31` 的 `if (!indexid.empty())` 短路而**静默降级为 tcp 且不打日志**。

主路径（`:92-144`）与兜底路径（`:154-169`）**双双改为调用它**，各自只保留自己独有的 `subid` / `issub` 赋值。

**行为变更声明**：兜底路径的 `network` 将从硬编码 `"tcp"` 改为读 JSON 的 `net` 并 `sanitizeNetwork`（与主路径同规格）——这是修复，不是副作用。

#### P1.2 兜底裁剪改为结构性判定 + 括号配平扫描

删除 `:147-148` 的 errMsg 白名单，改为：

```cpp
// 结构性判定：串内含配平的 {...} 就重试一次，不再匹配 boost 的英文错误文案。
// 理由：日志里 9 条 `syntax error` 的 jsonStr 明明含 { 与 }（否则 :83 isJson 先抛 not JSON），
// 却因不在白名单被丢弃；白名单绑定第三方库的错误文案本身就是脆弱耦合。
size_t braceStart = jsonStr.find('{');
if (braceStart != std::string::npos) {
    const size_t braceEnd = findMatchingBrace(jsonStr, braceStart);   // 深度配平扫描
    if (braceEnd != std::string::npos) { /* substr 重试，限流 WARN */ }
}
```

`findMatchingBrace` 为新增 private 静态辅助：跳过字符串字面量内的 `{`/`}`（处理 `\\` 转义），按深度计数返回**配对的** `}` 位置。

> 必须配平而非 `rfind('}')`：尾随垃圾含 `}` 时 `rfind` 会取到垃圾里的括号，重试仍 `extra data`，被 `:170 catch (...) {}` 静默吞掉 —— 这正是 v1.0 用例 6 用 `"garbage"`（不含 `}`）而掩盖掉的缺陷。

**保留 `catch (...)`**（不收窄为 `catch (const std::exception&)`）—— v1.0 无谓收窄与本方案「不回退历史行为」的主题不符。

> 放宽后的新接受面：顶层是数组 `[{...}]` 的载荷会被裁出内层对象并接受。已在 §1.4 声明。

---

### 3.3 P2 — base64url 支持（缺陷 1：根因侧）

#### P2.1 归一化放在字母表查找**之前**

```cpp
static std::string decodeBase64(const std::string& input, ParseStats* stats = nullptr) {
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static const size_t kBase64Chars = 64;            // ← 不再用 sizeof（== 65，会命中结尾 NUL）
    ...
    for (size_t idx = 0; idx < input.size(); idx++) {
        char c = input[idx];
        // base64url(RFC 4648 §5)：'-' 代替 '+'、'_' 代替 '/'。必须在字母表查找之前归一化，
        // 否则这两个字符会走 :567 的静默丢弃分支造成 4 字符分组错位。
        if (c == '-') { c = '+'; if (stats) stats->payloadUrlCharTotal++; }
        else if (c == '_') { c = '/'; if (stats) stats->payloadUrlCharTotal++; }
        ...
        for (size_t k = 0; k < kBase64Chars; k++)      // ← 修 \0 / index-64 污染
            if (base64_chars[k] == c) { pos = k; found = true; break; }
        if (!found) { if (stats) stats->nonBase64Chars++; continue; }
        ...
    }
```

**实测依据**：`'-'`/`'_'` 在标准字母表中**不存在**（index 62 = `+`、63 = `/`）⇒ 归一化对标准 base64 是**严格 no-op**。

#### P2.2 同步改写 `:554-557` 历史注释（v1.0 遗漏，评审 I1）

```cpp
// Locate c in the base64 alphabet. '%', '@' 和 ':' MUST still be skipped —
// decoding them as index 0 corrupts the output stream (root cause of
// binary-garbage method/password bytes for malformed ss:// links,
// see docs/bugfix/2026-08-04-...GarbageSS-v1.0.md).
// '-' 和 '_' 例外：它们是 base64url(RFC 4648 §5) 的合法字符，已在上游归一化为
// '+' 和 '/'；base64url 的订阅/分享链接是真实输入形态，不再按垃圾字符丢弃。
```

#### P2.3 对 `ss://`（`:364`）的影响（v1.0 声称「零改动」，是错的）

| userInfo 形态 | 改前 | 改后 |
|---|---|---|
| 标准 base64（无 `-`/`_`） | 正常 | **不变**（归一化 no-op） |
| 明文 `method:password`（`:` 被丢弃） | `find(':')` npos ⇒ **丢弃节点** | 不变（`:` 仍丢弃） |
| 含 `-` 的 password（如 `a-b-c`） | `-` 被丢弃 ⇒ 密码错误或节点被丢弃 | **`-`→`+` 后被当数据** ⇒ 结果改变（可能更好也可能更差，由 P0 的 `payloadUrlCharLines` 判定） |
| base64url 编码的 `method:password` | `-`/`_` 被丢弃 ⇒ 分组错位 ⇒ **必失败** | **被顺手修好** ⇒ 需专门用例守护 |

> `:372-375` 的 `utils::isPrintableAscii(method) && isPrintableAscii(password)` 双检（`src/Utils.cpp` 存在该函数）仍是畸形 ss 的最后一道闸，评审已确认改后仍有丢弃保护。

#### P2.4 归因待定

P2 的**成本极低且对标准 base64 无副作用**，故可与 P1 同批上线。但**「这 114 条里有多少是 base64url 造成的」在 P0 数据出来前无法回答** ⇒ 禁止在文档或对外说明中继续声称 base64url 是「已坐实根因」。

---

### 3.4 P3 — 独立失败模式（含缺陷 4 的重新论证）

#### P3.1 明文 JSON 载荷：`vmess://{...}`（评审 C7：v1.0 用例 8 修复后必红）

`{` / `}` / `"` / `:` / `,` 全被 `:567-569` 丢弃 ⇒ 输出**必无花括号** ⇒ `:83 isJson == false` ⇒ `:87` 抛 `not JSON`。这是 `not JSON` 的**确定性 100% 生产者**（不依赖概率），且 v1.0 把它列为「范围外」同时又要求写一个断言其失败行为的用例。

```cpp
// :76 之后、decodeBase64 之前
std::string payload = encoded;
if (!payload.empty() && payload[0] == '{') {          // 明文 JSON
    if (stats) stats->payloadPlainJsonLines++;
    // 直接采用，不进解码流
} else {
    ... 原有 decodeBase64 / urlDecode 路径 ...
}
```

#### P3.2 URL 编码载荷：把 `urlDecode` 从死代码变成前置步骤（评审 I9）

现状 `:78-80` 的 `if (jsonStr.empty()) jsonStr = urlDecode(encoded);` **永不执行**（`:586-597` 保证非空）⇒ 含 `%2B`/`%3D` 的载荷**必然** `not JSON`。

```cpp
// :77 之前
std::string payload = encoded;
if (payload.find('%') != std::string::npos) {
    if (stats) stats->payloadUrlEncoded++;
    payload = urlDecode(payload);                     // 前置解码
}
std::string jsonStr = decodeBase64(payload, stats);
```

#### P3.3 缺陷 4（`#备注`）——**重新论证后保留，但降级**

v1.0 的理由「与 base64url 叠加会加剧错位」**已被证伪**（`continue` 在 `i++` 之前，junk 字符不占分组槽位 ⇒ 输出零影响）。保留的真实价值只有两条：

1. 片段不得进入解码流（P3.1/P3.2 都以 `#` 为终止判据，先剥离更干净）；
2. **`ps` 缺失时回落为 `remarks`**（v1.0 未考虑：JSON 无 `ps` 时无条件剥离会导致节点名丢失）。

```cpp
std::string remarksFallback;
const size_t hashPos = encoded.find('#');
if (hashPos != std::string::npos) {
    remarksFallback = urlDecode(encoded.substr(hashPos + 1));   // 提前取出，不再喂给解码器
    encoded = encoded.substr(0, hashPos);
}
...
if (profile.remarks.empty()) profile.remarks = remarksFallback; // 仅在 ps 缺失时回落
```

> **诚实声明**：P3.3 对 `decodeBase64` 的输出**零影响**。保留它纯粹是为了 P3.1/P3.2 的判据统一与节点名兜底，不要再把它描述成「防错位」。

---

## 4. 影响面

| 面 | 影响 |
|---|---|
| `ss://`（`:364`） | **行为变更**（P2.3 已列全三种形态）。畸形 ss 仍由 `isPrintableAscii` 双检丢弃 |
| `vless` / `trojan` / `hysteria2` / `hy2` | 不变（不走 `decodeBase64`；`vless` 的 `remarks` 本就用 fragment，见 `:243`） |
| 兜底路径产出节点 | **修复**：补齐 12 个字段，含 `port`（否则 `checkRequired()` 抛异常）与 `streamsecurity` |
| 顶层为数组 `[{...}]` 的载荷 | **新接受**（P1.2），已在 §1.4 声明 |
| 日志输出量 | **上升**：新增 subid + 行号 + 汇总 + 零结果告警。P0.4 已对明细限流前 3 条 |
| `SubscriptionParser.cpp` 消费者 | `test_autotask` + `validproxy` + `validproxy-cli`（`CMakeLists.txt:143` `CORE_SOURCES`）⇒ **必须全量 `ctest -V`** |
| `src/utils/Base64Utils.cpp` | **不在本次范围**。其 `:25-26` 是同源缺陷第二处实现（遇 `-`/`_` 整段返回空）。**已知问题，须另立 Spec**；本 Spec 至少在 §6 记录，避免「以为已全局修复」的误解 |

---

## 5. 测试方案（每条标注「修复前状态」，确保可证伪）

### 5.0 日志捕获手段（解决 v1.0 评审 I6「可观测性零覆盖」）

`src/LoggerInstance.cpp:74-79` 的 callback 分支位于 `consoleEnabled_`（`:65`）与 `fileEnabled_ && fileLevel_`（`:69`）判断**之外**，`logCallback_(fullMsg, level)` 对**任何级别**无条件触发 ⇒ **无需 `Logger::init()`、无需日志文件**，`pushCallback` 即可捕获 WARN。

```cpp
// tests/test_subscription_parser.cpp 顶部新增（禁 auto：显式类型）
class LogCapture {
public:
    LogCapture() {
        Logger::pushCallback([this](const std::string& msg, LogLevel lvl) {
            if (lvl == LogLevel::WARN) { warnLines_.push_back(msg); }
        });
    }
    ~LogCapture() { Logger::popCallback(); }
    bool has(const std::string& needle) const {
        for (const std::string& line : warnLines_) {
            if (line.find(needle) != std::string::npos) { return true; }
        }
        return false;
    }
    std::vector<std::string> warnLines_;
};
```

> `Logger::init` 不调用 ⇒ `consoleEnabled_`/`fileEnabled_` 默认关闭，测试无副作用、无文件污染（符合「测试不得污染非 sandbox 目录」）。

### 5.2 base64url 与字段完整性

| # | 用例 | 覆盖 | 修复前 |
|---|---|---|---|
| T1 | `Base64UrlBodyIsDecoded` | P2.1 | **红** |
| T2 | `Base64UrlIsNoOpVersusStandard` | P2.1 no-op 性质 + P1.1 indexid | **红** |
| T3 | `StandardBase64BodyUnchanged` | P2.1 严格 no-op（回归锁） | 绿（防回退） |
| T4 | `VmessSalvageByStructuralBraceMatching` | P1.2 白名单（缺陷 3） | **红** |
| T5 | `VmessSalvageWithBraceInTrailingGarbage` | P1.2 括号配平（I10） | **红** |
| T6 | `VmessSalvagedProfileHasAllFields` | P1.1 字段完整性 + indexid | **红** |
| T7 | `Base64WithNulByteIsNotPolluted` | P2.1 `sizeof`→64 | **红** |

**T1 构造（证伪性硬保证，v1.0 缺此步导致用例在有 bug 的代码上也是绿的）**：

```cpp
// 必须循环直到 body 里真的出现 '+' 或 '/'，再转 base64url；
// 否则 ASCII JSON 的 base64 极少产生 sextet 62/63，老代码照样解码成功。
// 实测：不做此步时 300 组 / 200 次采样中含 '-' 或 '_' 的为 0 次。
std::string body = "vmess://" + b64s(jsonA) + "\n" + "vmess://" + b64s(jsonB) + "\n";
while (body.find('+') == std::string::npos && body.find('/') == std::string::npos) {
    body += "vmess://" + b64s(jsonOf("padding-hint-" + std::to_string(body.size()))) + "\n";
}
std::string bodyUrl = body;
for (char& ch : bodyUrl) { if (ch == '+') ch = '-'; else if (ch == '/') ch = '_'; }
ASSERT_TRUE(bodyUrl.find('-') != std::string::npos || bodyUrl.find('_') != std::string::npos);  // 硬保证
```

断言：`profiles.size() >= 2`，且每个节点的 `address` / `port` / `id` 非空、`indexid` 非空。

**T2 排除 `indexid`（v1.0 C6：两次 `parse()` 的 indexid 必然不同，v1.0 写「逐字段相等」必红）**：
同一 body 分别以标准 base64 与 base64url 喂入两次 `parse()`，逐字段比较 `configtype / configversion / address / port / id / alterid / security / network / remarks / path / requesthost / streamsecurity / sni / flow / fingerprint / alpn / headertype / allowinsecure / subid / issub`，**显式跳过 `indexid`**；并断言 `indexid` 两者**非空且互不相等**（顺带守住 P1.1 的 indexid 缺失）。

**T4 构造**：让载荷解出为 `{"v":"2","ps":"x","add":"1.2.3.4","port":443,"id":"...","net":"tcp",}` —— **尾随逗号**是确切的 `syntax error`（不是 `incomplete`/`extra data`），且串内同时含 `{` 与 `}` ⇒ `:83 isJson == true` ⇒ 老代码因白名单漏判而丢弃（红），新代码结构性判定命中（绿）。断言 `stats.vmessFailSalvaged >= 1`。

> **不使用 BOM 构造**：v1.0 用 `\xEF\xBB\xBF` 是为迎合「偏移 1 = 首字节非法」这个误读而设计的，且 BOM 从未在任何真实载荷中被发现。

**T5 构造**：载荷解出为 `{...}` + 尾部垃圾**含 `}`**（如 `garbage}`）。老代码 `rfind('}')` 取到垃圾里的括号 ⇒ 重试仍 `extra data` ⇒ 被 `:170 catch (...) {}` 吞掉（红）；新代码深度配平取到真闭合（绿）。v1.0 用例 6 的 `"garbage"` 不含 `}` 恰好掩盖此缺陷。

**T6 断言集**：`port` 非空且 1–65535、`indexid` 非空、`streamsecurity == "tls"`、`sni == requesthost`（回退生效）、`fingerprint == "chrome"`（默认保留）、`allowinsecure == "false"`（默认非空串）。

### 5.3 独立失败模式

| # | 用例 | 覆盖 | 修复前 |
|---|---|---|---|
| T8 | `PlainJsonVmessPayload` | P3.1（v1.0 C7） | **红** |
| T9 | `UrlEncodedVmessPayload` | P3.2（I9） | **红** |
| T10 | `RemarksFallbackToFragment` | P3.3 | **红** |
| T11 | `DecodeOfTwoCharPayloadCountedAsSkip` | I8（`i==2` 误判 `Ok`） | **红** |

- **T8**：`vmess://{"v":"2","ps":"plain",...}`；断言解析成功、`stats.payloadPlainJsonLines == 1`。**v1.0 把该用例列入测试表却又声明「本次不修」，必然永久红 —— 本版把 P3.1 纳入范围。**
- **T9**：`encoded` 用 `urlEncode(b64s(json))`（`+`→`%2B`、`=`→`%3D`）；断言 `stats.payloadUrlEncoded == 1` 且节点解析成功。
- **T10**：JSON **无 `ps` 字段** + fragment `#HK-01`；断言 `remarks == "HK-01"`。片段用含字母表字符的 ASCII（v1.0 用 UTF-8 `我的节点🚀` 实测剥离与否解码结果逐字节相同 ⇒ 零鉴别力）。
- **T11**：`vmess://AA`；断言 `stats.vmessSkipEmpty == 1`。

### 5.4 `ss://` 路径（v1.0 C3 漏掉第三个调用点）

| # | 用例 | 覆盖 | 修复前 |
|---|---|---|---|
| T12 | `SsUserInfoWithUrlSafeChars` | P2.3 的**受益点**（base64url ss 被顺手修好） | **红** |
| T13 | `SsPlaintextUserInfoStillDropped` | P2.3 的「不变」承诺 | 绿（防回退） |
| T14 | `SsMalformedNonAlphabetUserInfoStillDropped` | `:554-557` 历史修复不回退（I1） | 绿（防回退） |

- **T12**：userInfo 用 base64url 编码的 `method:password`，构造时**硬保证含 `-` 或 `_`**（如 password 用 `pass-word`）；断言 `security == method` 且 `id == password`。这是 `:364` 唯一的正向守护。
- **T13**：`ss://chacha20-ietf-poly1305:pw@host:8388`（明文 userInfo）⇒ 断言 `profiles.empty()`。**v1.0 的用例 7 声称「含 `%`/`@`/`:` 的 ss 链接能正确解出 method/password」——前提自相矛盾不可构造**（字母表不含 `%@:`；真含则 `:` 被丢弃 ⇒ `:365 find(':')` 必 npos ⇒ `:368` 直接丢弃）。本版改为断言**被丢弃**。
- **T14**：复刻 `docs/bugfix/2026-08-04-...GarbageSS-v1.0.md` 的畸形形态（userInfo 为 URL-encoded username 的 base64）；断言 `profiles.empty()`，锁住 `:372-375` 的 `isPrintableAscii` 双检。

### 5.5 可观测性（v1.0 零覆盖）

| # | 用例 | 覆盖 | 修复前 |
|---|---|---|---|
| T15 | `CountersCoverEveryLine` | P0.3 口径完整（I7/I8） | **编译红**（无 `ParseStats`） |
| T16 | `ZeroNodeSubscriptionRaisesWarning` | P0.5 零结果 + P0.4 subid | **红** |
| T17 | `PartialLossRaisesWarning` | P0.5 高丢弃率（I7） | **红** |
| T18 | `WarningCarriesSubid` | P0.4 归因信息（**C4 的核心：没有 subid 就永远无法归因**） | **红** |

- **T15**：body = 3 个正常 vless + 1 个正常 vmess + 1 个坏 vmess + 1 行乱码。断言 `lineCount == 6`、`vmessOk == 1`、`vmessFailParse == 1`、`unrecognizedLines == 1`，且 `vmessOk + vmessFailParse + vmessSkipEmpty + unrecognizedLines <= lineCount`。最后一条专门盯住「早退 `continue` 绕过计数导致低报」。
- **T16 / T17 / T18**：用 §5.0 的 `LogCapture` 断言捕获到含 `subid=<指定值>` 的 WARN。T17 用 10 个正常 vless + 2 个坏 vmess（丢弃率 > 50%）触发高丢弃率告警 —— 这条专门覆盖 v1.0 遗留的「静默丢 99% 节点不触发任何告警」漏洞。

### 5.6 实施纪律

1. **一次只上一个阶段**。T15–T18（P0）先写、先跑、先合；P0 全绿后再动 P1，依此类推。
2. 每个阶段先写用例 → 在**未修复**的代码上跑到预期失败（红）→ 再实施 → 跑到绿。**「修复前就是绿的」用例一律视为无效用例，必须重写构造。**
3. 新增用例**禁用 `auto`**（AGENTS.md §1）。`tests/test_subscription_parser.cpp` 现有 9 处 `auto profiles = parser.parse(...)` **保持不动**（不夹带修改），但新代码一律显式类型。
4. 每阶段跑 `ctest -R SubscriptionParserTest -V` 做快速反馈，**交付前必须跑全量 `ctest -V`**（P1/P2/P3 改了 `CORE_SOURCES`，`test_autotask` 与两个主程序共用）。

---

## 6. 验证

| 阶段 | 命令 | 期望 |
|---|---|---|
| 每阶段快速迭代 | `cmake --build build --parallel 8` 然后 `ctest -R SubscriptionParserTest -V` | EXIT 0 |
| **交付前（强制）** | `ctest --test-dir build --output-on-failure -j 4` | 除已知基线失败外全绿 |
| 真实数据验证 | 把 `bin/config.json` 的 `log.file_level` 降到 `INFO`，跑 `.\bin\validproxy-cli.exe -UA` | 每个 subid 一行 `WARN: vmess summary [subid=N] lines=.. ok=.. fail=.. unrecognized=..`，可直接判定 base64url 占比 |

**已知基线失败（勿误判为回归）**：`XrayManagerStartupTest`（需真实 xray）、`RegionBatchResolverTest`、`UI_PORTCLOSE`、`UI_FLOATINGWIDGET`（并行 flaky，单跑通过）、`NetworkMonitorTest`（需外网）。全量基线约 46–47/49。

**clangd 报错是假错误**（缺 include 路径，如 `wx/wx.h file not found` / `boost` undeclared），一律以 `cmake --build build` 结果为准。

---

## 7. 风险

| 风险 | 评估 / 缓解 |
|---|---|
| P2 归一化改变 `ss://` 行为 | T12/T13/T14 三条正反用例夹住；`isPrintableAscii` 双检保留为最后防线；P0 的 `payloadUrlCharLines` 可事后量化 |
| `sizeof`→64 影响 `\0` 输入 | 实测确认是**修复**（`old("QUJD\0REVG")` → `b'ABC\x01\x11\x15'`，应为 `b'ABCDEF'`）；T7 锁定 |
| 括号配平扫描误判字符串内的 `{`/`}` | 实现须跳过字符串字面量并处理 `\\` 转义；T4/T5 覆盖 |
| 顶层数组 `[{...}]` 被新接受 | 已在 §1.4 与 §4 声明为**行为变更**；收益是救回原本被丢的节点 |
| 抽取 `fillVmessProfile` 影响主路径 | 主/兜底两条路径共用同一实现，字段集天然一致；T6 断言 12 个字段 |
| **「已修复」的误解外溢** | `src/utils/Base64Utils.cpp` 是同源缺陷的第二处实现，本次**不动**（§4 末行 + §8）。上线说明必须写明只修了 `SubscriptionParser` 一处 |
| 日志量上升 | P0.4 明细限流前 3 条 + 汇总 1 条；P0.5 三档告警只在异常时触发 |
| MSVC `/W4 /WX` 因未消费变量而构建失败 | P0 的 `ParseStats` 通过 out-param 传递并被**测试断言真实消费**；**不引入任何枚举状态变量**（v1.0 的 `B64Status` 即因此被评审判死变量） |

---

## 8. 范围外与后续

### 8.1 本 Spec 不覆盖（须另立文档）

| 项 | 理由 |
|---|---|
| `src/utils/Base64Utils.cpp:25-26` | 同源缺陷第二处实现（遇 `-`/`_` **整段返回空**，比 `SubscriptionParser` 更严格）。`CMakeLists.txt:150` 注册，被其它模块使用，改动面需单独评估。**本次不动，但必须记录，避免「以为已全局修复」。** |
| B 类 `addOutbound FAILED ... output=`（空） | 139 次 × 3 类同族 ERROR + 143 条 fallback WARN，属 `XrayApi` 注入失败，与解析链路无路径关联（进入兜底路径至多 7 条，无法解释 139 次） |
| C 类 `SKIP: ... invalid UUID format` | 实测 **2157** 条。v1.0 断言它「命中非 vmess/vless 节点」**被源码证伪** —— `SubitemUpdaterV2.cpp:62-67` 的条件是 `configtype == "1" \|\| configtype == "5"`，而 `"1"` **就是 vmess** ⇒ UUID 校验**只针对 vmess/vless**。⇒ C 类**可能**包含「JSON 合法但字段值被污染」的 vmess 节点。**须用 P0 的 subid 归因数据重新评估，本 Spec 不下结论。** |
| `unsupported SS cipher` | 799 条（`aes-256-cfb` 单独 697 条，`SubscriptionParser.cpp:697`），属 2026-08-11 白名单变更，与本缺陷无关 |

### 8.2 P4 — 归因结论（本 Spec 交付后的下一步）

拿到 P0 数据后**必须**回答的三个问题，并据此决定 P2 是否扩面：

1. 114 条失败按 `subid` 是**聚集在少数几个**（支持格式级成因：base64url / 加密订阅）还是**散布在几十个**（否定格式级假设，指向逐节点成因）？
2. `stats.payloadUrlCharLines` 在真实订阅里是 **0**（⇒ base64url 不是根因，P2 仅为兼容性增强）还是 **非 0**（⇒ 根因坐实，P2 价值确立）？
3. `stats.payloadPlainJsonLines`（P3.1 路径）与 `stats.payloadUrlEncoded`（P3.2 路径）各占多少？v1.0 断言的「`not JSON` 主要是 base64url 造成」很可能是错的 —— 明文 JSON 是**确定性 100%** 生产者，而错位是**概率事件**（`-`/`_` 数量 ≢ 0 mod 4 且落在前段才损坏）。

### 8.3 本日志的统计纠错

v1.0 表格次数合计 98+9+7+9 = **123**，与文中「114 条」不符（第 4 行的 9 条 `Base64 decode produced invalid content` 未计入 114）。本版 §0 已按实测计数列出并说明：**114 仅指 `Failed to parse vmess` 三类**，另有 9 条 body 级回退属不同现象。

---

## 9. 变更记录

| 版本 | 日期 | 说明 |
|---|---|---|
| v1.0 | 2026-10-15 | 初稿（3 份独立评审一致判 FAIL） |
| **v1.1** | 2026-10-15 | 按评审重构：①§0 撤回 5 处事实性错误；②§1 补齐 AGENTS.md §6.2 要求的输入/输出/前置条件/边界条件四要素；③§3 拆为 P0–P4 五阶段，P0 纯可观测性先行以取得归因证据；④`fillVmessProfile` 补 `indexid`（阻断级）与 sni 回退/`fp`/`allowinsecure` 默认值，形参改非 const 引用；⑤裁剪改结构性判定 + 括号配平；⑥补 `:521-522` 计数与早退记账，消除指标低报；⑦新增 `ParseStats` out-param 让可观测性**可被单元测试断言**（不再依赖读日志）；⑧测试用例全部标注「修复前红/绿」，并给出 v1.0 缺失的**证伪性构造**；⑨更正 ss:// 用例为断言丢弃并新增 base64url 受益点用例；⑩登记 `Base64Utils.cpp` 第二处实现与 C 类 2157 条的重新评估 |
