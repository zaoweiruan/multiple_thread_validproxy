# Spec — 订阅 vmess 解析链路缺陷修复（可观测性 / 兜底裁剪 / base64url / 独立失败模式）

- **日期**: 2026-10-15
- **模块**: `src/update/SubscriptionParser.cpp`（+ 头文件）、`tests/test_subscription_parser.cpp`
- **版本**: **v1.2**（取代 v1.1。v1.0、v1.1 经独立评审均判 FAIL，本版逐条修正）
- **触发输入**: `bin\worker\log\ui_20261016_084620.log` 中 114 条 `Failed to parse vmess`

---

## 0. 对前两版的更正声明

### 0.1 撤回 v1.0 的 5 处事实性错误（已复核，全部成立）

| v1.0 的断言 | 更正 |
|---|---|
| 「114 条解析错误**全部源于同一根因**（base64url）」 | **撤回。** 日志 `file_level=WARN` 过滤掉全部 INFO（实测 `INFO:` **0** 次），114 条错误行形状完全一致、无 subid / 无行号 / 无载荷 ⇒ **结构上无法把任何一条归因到任何机制**。明文 JSON `vmess://{...}` 是 `not JSON` 的**确定性 100%** 生产者（`{`/`}`/`"`/`:`/`,` 全被丢弃 ⇒ 输出必无花括号），而 base64url 错位是**概率事件**。另：v1.0 表格合计 98+9+7+9=**123**，与「114」不符。 |
| 「`boost.json:1` ⇒ 偏移 1 ⇒ 首字节即非法（BOM）」 | **撤回。** boost 位置串格式为 `boost.json:<行号> at<文件>:<列号> in<函数>`。「1」是**行号**，列号被截断。`boost.json:2` + 函数 `write` ⇒ **串内含换行符**。BOM 从未在任何真实载荷中被发现。 |
| 「剥离 `#备注` 会加剧 base64url 错位」 | **撤回。** `continue` 发生在 `i++` **之前**，junk 字符**从不占用 4 字符分组槽位** ⇒ 对**单独的** `decodeBase64` 零影响。**限定语（v1.1 漏写）**：一旦 P3.2 把 `%XX` 变成**有效字符**（`%2F` → `/`），该结论即失效 ⇒ 顺序必须为「先剥 `#fragment`，再检测 `%`」（见 P3.2/P3.3）。 |
| 「ss:// 路径零改动」 | **撤回。** `decodeBase64` 有**三个**调用点：`:51` body、`:77` vmess 载荷、**`:364` ss:// userInfo**。且 `:554-557` 的历史注释**明确把 `'_'` 和 `'-'` 列进「必须跳过」清单**。 |
| 「现代订阅源/面板**大量使用** base64url」 | **撤回。** 无占比统计；主流 v2board/xboard 与 v2rayN 惯例用 `base64.StdEncoding`。 |

**追加**：`src/utils/Base64Utils.cpp:25-26` 是**同源缺陷第二处实现**，且比 `SubscriptionParser` 更严格（遇**任意**非字母表字符即**整段返回空**）。同文件 `looksLikeBase64()` 谓词可在解码**前**区分「压根不是 base64」与「是 base64 但损坏」。本次**不动**，须另立文档（§8.1）。

### 0.2 v1.1 的 5 处缺陷（本版修正）

| v1.1 的问题 | 实测证据 | 本版修正 |
|---|---|---|
| **T1 构造根本不走 base64** | `while` 条件 `body.find('/')` 被 `vmess://` 自带的斜杠满足 ⇒ **循环 0 次迭代**；随后 `/`→`_` 把 `://` 一起毁掉 ⇒ `bodyUrl` 不含 `://` ⇒ `:43 hasProtocol==false`。那条「硬保证」`ASSERT_TRUE(find('-')…find('_'))` 被 `vmess:`→`vmess:_` **自我满足**，零鉴别力。**修复后仍红，永久无效。** | §5.2 T1 重写：**先整体编码**，在 `b64s(plain)` 上循环，并加断言 `bodyUrl.find("://") == npos` 证明确实不是明文 |
| **P1.2 对尾随逗号是恒等变换** | 载荷 `{…,"net":"tcp",}` 自闭合 ⇒ 配平扫描返回 `size()-1` ⇒ `substr` 与原串**逐字节相同** ⇒ 重试抛同一个 `syntax error` ⇒ `parseSuccess` 仍 false | **撤回「9 条 `syntax error` 可被裁剪救回」的因果论证**。`syntax error` + 含 `{`/`}` 更可能是**括号内部**被污染，**切尾部救不回**。P1.2 的**唯一可测收益是修复 `rfind('}')` 取错尾括号**（=T5）。`syntax error` 列入 §8.1 待 P0 数据 |
| **T12 在 HEAD 上就是绿的** | `"aes-256-gcm:pass-word"` 的 base64 实测**不含** `-`/`_` ⇒ base64url 形式与标准形式逐字节相同 ⇒ `:365 find(':')` 成功 ⇒ 节点在 HEAD 上正常产出 ⇒ 断言全过 | §5.4 T12 改为**循环构造**（同 T1 思路），循环体作用在 `b64s("method:password")` 上 |
| **T11 与 `vmessSkipEmpty` 定义互斥** | `decodeBase64("AA")` 实测 → `b'\x00'`，`len=1`、`empty()==false` ⇒ `:81` 不触发 ⇒ 走 `:87` 抛 `not JSON` ⇒ 计入 `vmessFailParse` 而非 `vmessSkipEmpty` | §P0.1 统一口径为「有效字母表字符 < 2」，§P0.3 新增显式判定点，**T11 改用 `vmess://A`**（实测 `decodeBase64("A")` → `len=0`、`empty()==true`） |
| **T17 触发条件不可达** | P0.5 高丢弃率式要求 `vmessOk > 0`；而 T17 用「10 个 vless + 2 个坏 vmess」，**vless 不计入 `vmessOk`** ⇒ `vmessOk==0` ⇒ 命中的是「零结果」告警而非丢弃率告警 | §5.5 T17 改为 **10 个正常 vmess + 6 个坏 vmess + 2 行乱码** ⇒ `vmessOk=10`、`(6+2)*2=16 > 10` ✔ |

---

## 1. 输入 / 输出 / 前置条件 / 边界条件

### 1.1 输入

| 输入 | 类型 | 约束 |
|---|---|---|
| `content` | `const std::string&` | ①整段 base64 / base64url；②明文分享链接逐行；③base64 解出后不含 `://` 的非订阅内容（反爬页 / 限流 JSON / **404 空体**） |
| `subid` | `const std::string&` | 仅用于日志归因，不参与解析决策 |
| 输出容器 | `std::vector<db::models::Profileitem>&` | 生产调用方**仅 `src/SubitemUpdaterV2.cpp:567`** 一处（注意是顶层 `src/`，不是 `src/update/`） |

### 1.2 输出

| 输出 | 说明 |
|---|---|
| `std::vector<db::models::Profileitem>` | 原 `parse()` 签名**保持不变**，新增 out-param 重载 |
| `ParseStats&` | 全部失败路径与 base64url 特征计数，**可被单元测试直接断言**（不依赖读日志） |

### 1.3 前置条件

1. C++17，**全栈禁用 `auto`**（AGENTS.md §1）。
2. `src/update/SubscriptionParser.cpp` 在 `CMakeLists.txt:143` 的 `CORE_SOURCES` 中，被 `test_autotask` / `validproxy` / `validproxy-cli` 共用 ⇒ **必须全量 `ctest -V`**，不得只跑 `-R SubscriptionParserTest`。
3. 追加 gtest 用例**无需改 CMake**（`CMakeLists.txt:966-974` 已把 `SubscriptionParser.cpp` 列入 `test_subscription_parser` 目标源）。
4. 编译期约束不对称：MSVC（`CMakeLists.txt:66`）`/W4 /WX`（**unused 变量直接报错**）；GCC（`:69`）`-Wall -Wextra -pedantic` 无 `-Werror`。⇒ **不得引入未被消费的变量**（v1.0 的 `B64Status` 即因此被判死变量）。
5. **头文件不得引入 Boost 依赖。** `include/update/SubscriptionParser.h` 当前只 include `<string>` / `<vector>` / `Profileitem.h`；`test_subscription_parser` 的 `target_include_directories`（`CMakeLists.txt:967`）**不含 vcpkg 的 boost 头路径**（只有 `:968` 链接了 `libboost_json-*.a`）⇒ 任何 `#include <boost/json/object.hpp>` 进头文件都会让一批 TU 编译失败。
   ⇒ **P1.1 的 `fillVmessProfile` 与 `findMatchingBrace` 全部放在 `.cpp` 的匿名 namespace，不进头文件**（推荐，零依赖）；`ParseStats` 只含 `bool`/`long`，进头文件无妨。
6. `Logger` 的**构造默认值**为 `fileEnabled_(true), consoleEnabled_(true), fileLevel_(DEBUG), consoleLevel_(INFO)`（`src/LoggerInstance.cpp:8-17`）⇒ **未调 `init` 时 WARN 仍会打到 `std::cout`**（`:65-67`，因 `WARN(4) >= INFO(2)`）。测试须显式静音（§5.0）。

### 1.4 边界条件

| 边界 | 期望行为 |
|---|---|
| body 长度 < 10 或解出不含 `://` | 回退原始 content（`:53-56`），记 `bodyDecodeFellBack=true` 并在 WARN 输出统计 |
| 整段 base64url（body 含 `-`/`_`） | 归一化后正确解出（P2） |
| 载荷**有效字母表字符 < 2**（空 / `"A"` / `"AA"`…） | 计入 `vmessSkipEmpty`，**不得**静默 |
| 载荷是明文 JSON（`vmess://{...}`，允许前导空白） | 直接采用（P3.1） |
| 载荷是 URL 编码 base64（`%2B`/`%3D`） | 先 `urlDecode` 再判明文、否则 `decodeBase64`（P3.2） |
| 完全 URL 编码的明文 JSON（`%7B...`） | `urlDecode` 后**必须重新判定** `{` 开头（P3.1+P3.2 合并决策链） |
| `#备注` 片段含 base64 字母表字符 | 片段不得进入解码流；`ps` 缺失时回落为 `remarks`（P3.3） |
| 尾部垃圾**含 `}`** | 括号配平必须取到 JSON 自身的闭合 `}`（P1.2，唯一可测收益） |
| 尾部逗号（`{…,}） | **无法**靠切尾部恢复 ⇒ 计入 `vmessFailParse` 并 WARN（§8.1 待 P0） |
| 顶层是数组 `[{...}]` | 裁剪出内层对象并接受（**行为变更**，已声明） |
| 顶层非对象（`"str"`/`123`/`true`） | `as_object()` 抛 `std::runtime_error` 派生异常（已实测继承），`catch (const std::exception&)` 可捕获 ⇒ 丢弃，无风险 |
| **空 body / 404 空体**（`lineCount == 0` 且 `content` 非空） | 必须告警（P0.5 第三行；v1.1 漏此项，会让 §8.1 列出的「404 空体」形态完全静默） |

---

## 2. 现状代码事实（已逐行核对）

### 2.1 `decodeBase64`（`:533-600`）

```cpp
534:     static const char base64_chars[] =
535:         "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";   // sizeof == 65
546:     if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
550:     if (static_cast<unsigned char>(c) > 127) continue;
554:     // Locate c in the base64 alphabet. Non-alphabet characters (e.g. '%',
555:     // '@', ':', '_', '-') MUST be skipped — decoding them as index 0
556:     // corrupts the output stream (root cause of binary-garbage
557:     // method/password bytes for malformed ss:// links).
560:     for (size_t k = 0; k < sizeof(base64_chars); k++)      // ← 65，会命中结尾 NUL
567:     if (!found) continue;                                 // ← 静默丢弃（在 i++ 之前）
586-597: /* 尾部：i==1 → 0 字节（ret 空）；i==2 → 1 字节；i==3 → 2 字节 */
```

**三个已实测确认的缺陷**：

1. **无 base64url 归一化** ⇒ `-`/`_` 落入 `:567` 静默丢弃。
2. **`sizeof(base64_chars) == 65`**（含结尾 NUL）⇒ 输入含 `\0` 时在 `k=64` 命中并以 `pos=64` 污染输出流。实测 `old("QUJD\0REVG")` → `b'ABC\x01\x11\x15'`，正确应为 `b'ABCDEF'`。（`\0` 是 0 不是 >127，不会被 `:550` 跳过；`std::string` 传递 `\0` 安全，长度语义而非 NUL 结尾。）
3. **无任何失败/统计返回通道**。

### 2.2 主路径 vs 兜底路径的字段差异

主路径 `:92-144` 赋值 25 个字段；兜底路径 `:154-169` 只赋 11 个。差异为 **11 个字段 + 1 处条件回退**：

| 丢失项 | 主路径行号 | 后果 |
|---|---|---|
| `port` | `:101-110`（int64 / string / double 三态） | **空端口** ⇒ `Profileitem::checkRequired()`（`include/Profileitem.h:75-79`）直接抛异常 |
| `aid` → `alterid` 三态 | `:114-125` | 兜底恒为 `"0"` |
| `path` | `:131` | 空 |
| `requesthost` | `:132` | 空 |
| `streamsecurity` | `:133` | **空 ⇒ 按明文 TCP 走，TLS 参数全丢** |
| `sni` | `:134` | 空 |
| **`sni` ← `requesthost` 的 tls 回退** | `:135-137`（`sni` 空 + `requesthost` 非空 + `streamsecurity=="tls"`） | 条件块，最易漏 |
| `flow` | `:138` | 空 |
| `fingerprint` | `:139`（**默认 `"chrome"` 非空**） | 静默改变 TLS 指纹 |
| `alpn` | `:140` | 空 |
| `headertype` | `:141` | 空 |
| `allowinsecure` | `:142-143`（**默认 `"false"` 不是 `""`**） | 静默改变证书校验 |

兜底路径 `:160` 硬编码 `network = "tcp"` 且**从不读 JSON 的 `net`**。

### 2.3 结构性问题

| 位置 | 问题 |
|---|---|
| `:78-80` | `urlDecode` 是**死代码**。`:586-597` 保证任何含 ≥2 个字母表字符的输入都返回非空 ⇒ `if (jsonStr.empty())` 为假 ⇒ `urlDecode` **永不执行**。实测：`decodeBase64("AA")` → `b'\x00'`、`len=1`、`empty()==false` |
| `:147-148` | 兜底裁剪的 errMsg 白名单只认 `incomplete`/`extra data`，**绑定第三方库英文文案**（脆弱耦合） |
| `:151` | `rfind('}')` 是**非括号配平扫描** ⇒ 尾部垃圾含 `}` 时取错括号，重试仍 `extra data`，被 `:170 catch (...) {}` 静默吞掉。实测：载荷 `{…}garbage}` 时 OLD `rfind`=73 取到垃圾括号，NEW 配平=65 取到真闭合 |
| `:521-522` | `else { continue; }` 是最大静默通道。整段解码错位时第 2 行起全部走这里 ⇒ **98 条 `not JSON` 只是「每个错位订阅的首行数」，真实损坏量无上界** |
| **其余 11 条 line 级静默丢弃** | `parse()`（`:40-531`）内共 **14 条** line 级 `continue`：`:68` 空行、`:81`、`:178`、`:182` vless 无 `@`、`:204` vless addr 空、`:255` ss 无 `@`、`:271` ss addr 空、`:368` ss 无 `:`、`:376` ss 不可打印、`:383` trojan 无 `@`、`:404` trojan addr 空、`:452` hy2 无 `@`、`:471` hy2 无 `:`、`:522`。**v1.1 的 P0.3 只覆盖 3 条**，其余 11 条修完 P0 后仍完全不可见 |
| `:529` | 唯一汇总 `INFO: Parsed N valid profiles from M lines` 是 INFO 级 ⇒ 在 `file_level=WARN` 下**不落盘** |
| `:613-618` | `urlDecode` 用 `istringstream >> hex`，**不要求消费完两位** ⇒ `%fr` 被解析成 `0x0F`。实测 `urlDecode("50%free")` → `"50\x0Fee"`。**既存缺陷**，P3.3 会把它扩大到 vmess 的 `remarks`（§8.1 登记） |

### 2.4 `:53-56` 守卫的误判机理

对含 `-`/`_` 的大 body：**错位只丢弃字符，其后内容仍是可读明文前缀**，首行 `vmess://` 几乎必然保留 ⇒ `://` 存在且 `length` 巨大 ⇒ **两个条件都不成立 ⇒ 根本不会打出这条 WARN**（实测：修正后的 T1 body 走 OLD 解码器得 `len=210 has'://'=True → fallback 未触发`）。反之，HTML 反爬页经 base64 解出的垃圾几乎必然既不含 `://` 又远超 10 字节 ⇒ **必然命中** ⇒ 回退原始 base64 ⇒ 逐行时无一行以 `vmess://` 开头 ⇒ `:522` 零诊断 ⇒ **0 节点**。⇒ v1.0「整块解崩 ⇒ 0 节点」的因果链走不通，该现象更可能来自**非订阅内容**。

---

## 3. 分阶段修复方案

| 阶段 | 性质 | 功能行为变更 | 可观测行为变更 |
|---|---|---|---|
| **P0** 可观测性 | 纯增量 | **无**（只加计数，不改任何控制流） | **有**（新增 WARN / 计数口径） |
| **P1** 确定性缺陷（缺陷 3 + 字段完整性） | 修复 | 有 | 有 |
| **P2** base64url 支持（缺陷 1） | 修复 | 有（对含 `-`/`_` 的输入） | 无 |
| **P3** 独立失败模式（缺陷 4 + 死兜底） | 修复 | 有 | 有 |
| **P4** 归因结论 | 读 P0 真实数据后判定 | 无 | 无 |

**实施顺序**：P0 → P1 → P2 → P3 → 跑真实 `-UA` → P4 结论。

### 3.1 P0 — 可观测性（零**功能**行为变更）

#### P0.1 `ParseStats`（头文件 public 区，仅 `bool`/`long`，无 Boost 依赖）

```cpp
struct ParseStats {
    // ---- 行级分类：口径为「进入 while(getline) 的每一行恰好落入一个桶」 ----
    long lineCount              = 0;  // 总行数（:67 ++，含随后被 :68 丢弃的空行）
    long droppedEmptyLine       = 0;  // :68 空行
    long vmessOk                = 0;
    long vmessFailParse         = 0;  // 解析失败（含裁剪重试仍失败）
    long vmessFailSalvaged      = 0;  // 经裁剪重试救回
    long vmessSkipEmpty         = 0;  // 载荷有效字母表字符 < 2
    long unrecognizedLines      = 0;  // :522 else 分支
    long droppedMalformedLine   = 0;  // 其余 10 条协议级畸形丢弃
    long droppedByProtocol[6]   = {0, 0, 0, 0, 0, 0};  // vless/ss/trojan/hy2/其它，明细用

    // ---- body 级（仅 :51 调用点写入） ----
    bool bodyHadProtocol        = false;
    bool bodyDecodeApplied      = false;
    bool bodyDecodeFellBack     = false;
    long bodyUrlCharCount       = 0;  // body 中 '-' 与 '_' 总数
    long bodyNonBase64Chars     = 0;
    long bodyDecodedLength      = 0;

    // ---- payload 级（仅 :77 调用点写入） ----
    long payloadUrlCharTotal    = 0;  // 载荷中 '-' 与 '_' 总数
    long payloadUrlCharLines    = 0;  // 含至少一个 '-' 或 '_' 的载荷行数
    long payloadNonBase64Chars  = 0;
    long payloadPlainJsonLines  = 0;  // P3.1
    long payloadUrlEncoded      = 0;  // P3.2

    // ---- ss 级（仅 :364 调用点写入） ----
    long ssUrlCharCount         = 0;
};
```

**逐调用点归属（必须写死，否则 P4 无法归因）**：

| 调用点 | 允许写入的字段 |
|---|---|
| `:51` body | `bodyHadProtocol` / `bodyDecodeApplied` / `bodyDecodeFellBack` / `bodyUrlCharCount` / `bodyNonBase64Chars` / `bodyDecodedLength` |
| `:77` vmess 载荷 | `payloadUrlCharTotal` / `payloadUrlCharLines` / `payloadNonBase64Chars` / `payloadPlainJsonLines` / `payloadUrlEncoded` |
| `:364` ss userInfo | `ssUrlCharCount` |

> 绝不让 `decodeBase64` 在内部无差别累加 `payload*` —— 三个层级混在一起会**制造新的不可归因**（这正是 v1.1 的缺陷）。

#### P0.2 `parse()` 新增 out-param 重载（原签名保留并委托）

```cpp
std::vector<db::models::Profileitem> parse(const std::string& content, const std::string& subid);
std::vector<db::models::Profileitem> parse(const std::string& content, const std::string& subid,
                                            ParseStats& stats);
```

#### P0.3 记账必须覆盖全部 14 条 line 级出口

**原则：在既有 `continue` 之前插入计数，不删除任何 `continue`**（保持零功能行为变更）。

| 现状位置 | 记账 |
|---|---|
| `:68` 空行 | `++droppedEmptyLine` |
| `:75-81` 载荷有效字母表字符 < 2 | `++vmessSkipEmpty`（需新增显式判定，见下） |
| `:178` `!parseSuccess` | `++vmessFailParse` |
| `:182` vless 无 `@` | `++droppedMalformedLine; ++droppedByProtocol[0]` |
| `:204` vless addr 空 | `++droppedMalformedLine; ++droppedByProtocol[0]` |
| `:255` ss 无 `@` | `++droppedMalformedLine; ++droppedByProtocol[1]` |
| `:271` ss addr 空 | `++droppedMalformedLine; ++droppedByProtocol[1]` |
| `:368` ss userInfo 无 `:` | `++droppedMalformedLine; ++droppedByProtocol[1]` |
| `:376` ss 不可打印 | `++droppedMalformedLine; ++droppedByProtocol[1]` |
| `:383` trojan 无 `@` | `++droppedMalformedLine; ++droppedByProtocol[2]` |
| `:404` trojan addr 空 | `++droppedMalformedLine; ++droppedByProtocol[2]` |
| `:452` hy2 无 `@` | `++droppedMalformedLine; ++droppedByProtocol[3]` |
| `:471` hy2 无 `:` | `++droppedMalformedLine; ++droppedByProtocol[3]` |
| `:522` else | `++unrecognizedLines` |

**`vmessSkipEmpty` 的判定须显式化**（口径统一为「有效字母表字符 < 2」）：

```cpp
// decodeBase64 新增 out 参数 size_t* alphabetChars
const size_t alpha = countAlphabetChars(encoded);      // 字母表 ∪ padding 之外的有效字符数
if (alpha < 2) { ++stats.vmessSkipEmpty; continue; }   // :81 之前
```

（实测：`decodeBase64("A")` → `len=0 empty()==true`；`decodeBase64("AA")` → `len=1 empty()==false`。若沿用 `jsonStr.empty()` 判定，`AA` 会被误判为 `Ok` 并计入 `vmessFailParse`。）

#### P0.4 归因信息入日志

所有新增/修改的 WARN **必须携带 `subid=` 与行号**：

```
WARN: vmess parse failed [subid=42 line=17] not JSON
WARN: vmess body is not a usable subscription [subid=42] hadProtocol=0 decodedLen=812 urlChars=0 junk=64 -> 0 nodes
WARN: vmess salvage retry failed [subid=42 line=17] extra data
WARN: vmess summary [subid=42] lines=100 ok=0 fail=98 salvaged=0 skipEmpty=0 unrecognized=2 malformed=0
```

**明细限流前 3 条 + 末尾 1 条汇总**。⚠ 限流计数器**必须是 `parse()` 的栈上局部变量**，禁止 `static` 或成员变量 —— 否则跨订阅共享「第 3 条」额度，只有第一个订阅有明细日志。（已核对 GUI 路径由 `AppController` 单一 `workerThread_` 串行驱动、CLI `-UA` 亦然，`parse()` 内局部计数器安全。）

#### P0.5 丢弃率告警（三档，v1.1 只有两档且漏空体）

| 条件 | 级别 | 文案 |
|---|---|---|
| `lineCount == 0 && !content.empty()` | WARN | `subid=N: 订阅内容为空体`（覆盖 404 空体） |
| `lineCount > 0 && vmessOk == 0` | WARN | `subid=N: 解析出 0 个节点`（+ 全量计数） |
| `vmessOk > 0 && (vmessFailParse + unrecognizedLines + droppedMalformedLine) * 2 > vmessOk` | WARN | `subid=N: 丢弃率 X%（ok/fail/unrecognized/malformed）` |

> 三档互斥不重叠：第二行的 `vmessOk == 0` 排除与第三行重复（已验证）。第三行的分子**必须含 `droppedMalformedLine`**，否则 11 条畸形丢弃仍不可见。

---

### 3.2 P1 — 确定性缺陷

#### P1.1 `fillVmessProfile`（置于 `.cpp` 匿名 namespace，**不进头文件**）

```cpp
namespace {
// 形参必须是非 const 引用：boost object::at() 的 ref-qualified 重载为
//   &  → value&        && → value&&        const& → value const&
// （boost/json/object.hpp:1093-1109，已用探针验证 decltype(cobj.at("port")) == const value&）
// 用 const boost::json::object& 会编译失败。
bool fillVmessProfile(boost::json::object& obj, db::models::Profileitem& profile) {
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
        profile.sni = profile.requesthost;                // ← 条件回退，必写
    }
    profile.flow        = getJsonValueString(obj, "flow", "");
    profile.fingerprint = getJsonValueString(obj, "fp", "chrome");    // 默认非空，必写
    profile.alpn        = getJsonValueString(obj, "alpn", "");
    profile.headertype  = getJsonValueString(obj, "type", "");
    const std::string insecureVal = getJsonValueString(obj, "insecure", "");
    profile.allowinsecure = (insecureVal == "1" || insecureVal == "true") ? "true" : "false"; // 默认 "false"，必写
    return true;
}
}  // namespace
```

> **`indexid` 是阻断级**：`include/Profileitem.h:22` 的 `std::string indexid;` 无默认初始化器 + `:73 Profileitem() = default;` ⇒ 漏掉它会让**全部** vmess 节点 indexid 为空串 ⇒ DAO 主键冲突 / 去重 / 列表键控 / XrayApi outbound tag 全失效；且 `sanitizeNetwork(network, indexid)` 因 `SubscriptionParser.cpp:31` 的 `if (!indexid.empty())` 短路而**静默降级为 tcp 且不打日志**。

主路径（`:92-144`）与兜底路径（`:154-169`）双双改为调用它，各自只保留独有的 `subid` / `issub` 赋值。**行为变更声明**：兜底路径的 `network` 将从硬编码 `"tcp"` 改为读 `net` 并 `sanitizeNetwork`（修复）。

#### P1.2 裁剪改括号配平扫描（**唯一可测收益 = 修复 `rfind` 取错尾括号**）

```cpp
// findMatchingBrace：从 openIdx 处的 '{' 起，向右做带状态机的深度配平扫描。
// 返回配对 '}' 的下标；未配平返回 std::string::npos。
size_t findMatchingBrace(const std::string& s, size_t openIdx) {
    if (openIdx >= s.size() || s[openIdx] != '{') { return std::string::npos; }
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = openIdx; i < s.size(); ++i) {
        const char c = s[i];
        if (inString) {
            if (escaped)          { escaped = false; continue; }   // 被 \ 转义的字符
            else if (c == '\\')   { escaped = true;  continue; }
            else if (c == '"')    { inString = false; continue; }
            continue;                                            // 字符串内一切忽略
        }
        if (c == '"')      { inString = true;  continue; }
        else if (c == '{') { ++depth;          continue; }
        else if (c == '}') {
            --depth;
            if (depth == 0) { return i; }                         // 仅在 depth 归零时才算配对
            if (depth < 0)  { return std::string::npos; }          // 下溢：防御性
        }
    }
    return std::string::npos;                                      // 未配平（含未终止字符串）
}
```

三个边界情形的返回值已规定：**未终止字符串** ⇒ 扫到末尾返回 `npos`；**深度下溢** ⇒ 返回 `npos`；**起点不是 `{`** ⇒ 返回 `npos`。

裁剪入口：

```cpp
} catch (const std::exception& e) {
    const std::string errMsg = e.what();
    const size_t braceStart = jsonStr.find('{');
    if (braceStart != std::string::npos) {
        const size_t braceEnd = findMatchingBrace(jsonStr, braceStart);
        if (braceEnd != std::string::npos) {
            const std::string single = jsonStr.substr(braceStart, braceEnd - braceStart + 1);
            try { /* 用 single 重试，字段映射交给 fillVmessProfile */ }
            catch (...) { ++stats.vmessFailParse; /* 限流 WARN */ }
        }
    }
    ...
}
```

**保留 `catch (...)`**（不收窄为 `catch (const std::exception&)`）—— v1.0 无谓收窄与「不回退历史行为」的主题不符。

> **⚠ 收益诚实声明（v1.1 的错误在此更正）**：本阶段**不能**救回「尾随逗号」类 `syntax error`。当 JSON 自闭合而错误在括号**内部**时，`findMatchingBrace` 返回 `size()-1` ⇒ `substr` 与原串**逐字节相同** ⇒ 重试抛同一个异常（实测已验证）。因此：
> - **P1.2 的唯一可测收益**是「尾部垃圾含 `}`」时取到正确的闭合括号（v1.1 的 T5）。
> - 日志里那 9 条 `syntax error` 的**恢复率预计为 0**，它们更可能是括号内部被污染。列入 §8.1，待 P0 的 subid 归因数据再评估是否需要「括号内修复」（那是完全不同的算法，不能用「结构性判定 + 配平扫描」糊过去）。

---

### 3.3 P2 — base64url 支持（缺陷 1）

```cpp
std::string SubscriptionParser::decodeBase64(const std::string& input, ParseStats* stats,
                                             size_t* alphabetChars) {
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static const size_t kBase64Chars = 64;            // ← 不再用 sizeof（== 65，会命中结尾 NUL）
    ...
    for (size_t idx = 0; idx < input.size(); idx++) {
        char c = input[idx];
        // base64url(RFC 4648 §5)：'-' 代替 '+'、'_' 代替 '/'。必须在字母表查找之前归一化，
        // 否则这两个字符会走 :567 的静默丢弃分支造成 4 字符分组错位。
        if (c == '-')      { c = '+'; if (stats) ++stats->bodyUrlCharCountOrPayloadOrSs(); }
        else if (c == '_') { c = '/'; if (stats) ++stats->bodyUrlCharCountOrPayloadOrSs(); }
        ...
        for (size_t k = 0; k < kBase64Chars; k++)      // ← 修 \0 / index-64 污染
            if (base64_chars[k] == c) { pos = k; found = true; break; }
        if (!found) { if (stats) ++stats->对应层级的NonBase64Chars; continue; }
        if (alphabetChars) ++(*alphabetChars);
        ...
    }
```

> **实现约束**：`stats` 的写入**必须由调用点在解码前后归集**（见 P0.1 归属表），不得在 `decodeBase64` 内部无差别累加。上面的 `bodyUrlCharCountOrPayloadOrSs()` 是占位记号 —— **实现时必须替换为按 P0.1 归属表传入的 `enum class Base64Layer { kBody, kPayload, kSsUserInfo }` 分派**，否则 P4 无法归因。

**实测依据**：`'-'`/`'_'` 在标准字母表中**不存在**（62=`+`、63=`/`）⇒ 归一化对标准 base64 是**严格 no-op**。

**同步改写 `:554-557` 历史注释**（v1.0/v1.1 均遗漏）：

```cpp
// Locate c in the base64 alphabet. '%', '@' 和 ':' MUST still be skipped —
// decoding them as index 0 corrupts the output stream (root cause of
// binary-garbage method/password bytes for malformed ss:// links,
// see docs/bugfix/2026-08-04-...GarbageSS-v1.0.md).
// '-' 和 '_' 例外：它们是 base64url(RFC 4648 §5) 的合法字符，已在上游归一化为
// '+' 和 '/'；base64url 的订阅/分享链接是真实输入形态，不再按垃圾字符丢弃。
```

**对 `ss://`（`:364`）的影响**（v1.0 声称「零改动」，是错的）：

| userInfo 形态 | 改前 | 改后 |
|---|---|---|
| 标准 base64（无 `-`/`_`） | 正常 | **不变**（归一化 no-op） |
| 明文 `method:password`（`:` 被丢弃） | `find(':')` npos ⇒ **丢弃** | 不变 |
| 含 `-` 的 password（`a-b-c`） | `-` 被丢弃 ⇒ 结果错 | **`-`→`+` 后当数据** ⇒ 结果改变（可能更好也可能更差，由 P0 的 `ssUrlCharCount` 量化） |
| base64url 编码的 `method:password` | 分组错位 ⇒ **必失败** | **被顺手修好** ⇒ 需专门用例守护（§5.4 T12） |

`:372-375` 的 `utils::isPrintableAscii(method) && isPrintableAscii(password)` 双检仍是畸形 ss 的最后防线。

**上线时机**：P2 成本 2 行且对标准 base64 无副作用，**与 P1 同批上线**。但「这 114 条里有多少是 base64url 造成的」在 P0 数据出来前**无法回答** ⇒ 禁止继续声称 base64url 是「已坐实根因」。

---

### 3.4 P3 — 独立失败模式

#### P3.1 + P3.2 合并为**单一有序决策链**（v1.1 分成两段导致顺序错误）

```cpp
// :76 之后，单一决策链，顺序不可调换：
std::string payload   = encoded;
std::string remarksFallback;

const size_t hashPos = payload.find('#');                 // ① 必须最先剥 fragment
if (hashPos != std::string::npos) {
    remarksFallback = payload.substr(hashPos + 1);        //    暂存，不 urlDecode（见 §8.1 urlDecode 缺陷）
    payload = payload.substr(0, hashPos);
}
if (payload.find('%') != std::string::npos) {             // ② URL 编码 → 先解码
    ++stats.payloadUrlEncoded;
    payload = urlDecode(payload);
}
size_t firstNonSpace = payload.find_first_not_of(" \t\r\n");
const bool isPlainJson =                                // ③ 剥空白后再判明文 JSON
    (firstNonSpace != std::string::npos && payload[firstNonSpace] == '{');
std::string jsonStr;
if (isPlainJson) {                                       // ④ 明文 JSON 直接采用
    ++stats.payloadPlainJsonLines;
    jsonStr = payload.substr(firstNonSpace);
} else {
    size_t alpha = 0;
    jsonStr = decodeBase64(payload, &stats, &alpha);      // ⑤ 其余走解码
}
```

三处修正理由：

1. **`#fragment` 必须最先剥离**（v1.1 的 I1）：一旦 `%XX` 被 `urlDecode` 转成**有效字符**（`%2F` → `/`），剥离顺序就会改变解码结果。实测：`urlDecode('abc#tag%2Fx')` = `abc#tag/x` → `decodeBase64` 得 `b'i\xb7-j\x0f\xf1'`，而剥离后只得 `b'i\xb7'` ⇒ **不同**。
2. **`{` 守卫须先剥前导空白**（v1.1 漏项）：`vmess:// {"v":…}` 空格前缀会漏判。
3. **URL 编码的明文 JSON（`%7B…`）必须在 `urlDecode` 之后重新判定**（v1.1 的 I2）：实测 v1.1 的两段式片段下，`%7B%22v%22…` 先判 `{` 失败 → 进 P3.2 → `urlDecode` 得到 `{"v":"2",…` → 却仍送进 `decodeBase64` → 花括号被丢弃 → **仍然失败**。

#### P3.3 `remarks` 回落

```cpp
// 解析成功后（在 fillVmessProfile 之后）
if (profile.remarks.empty()) { profile.remarks = remarksFallback; }
```

> **诚实声明**：P3.3 对**单独的** `decodeBase64` 输出零影响（`continue` 在 `i++` 之前）。保留它纯粹是为了决策链顺序统一与节点名兜底，**不要**再把它描述成「防错位」。
> `remarksFallback` **不做 `urlDecode`** —— `urlDecode` 的部分十六进制解析缺陷（`:613-618`，`%fr` → `0x0F`）会污染节点名（§8.1 登记）。

---

## 4. 影响面

| 面 | 影响 |
|---|---|
| `ss://`（`:364`） | **行为变更**（P2.3 已列全四种形态）。畸形 ss 仍由 `isPrintableAscii` 双检丢弃 |
| `vless` / `trojan` / `hysteria2` | 不变（不走 `decodeBase64`；`vless` 的 `remarks` 本就用 fragment） |
| 兜底路径产出节点 | **修复**：补齐 11 字段 + 1 条件回退，含 `port`（否则 `checkRequired()` 抛异常）与 `streamsecurity` |
| 顶层为数组 `[{...}]` | **新接受**（§1.4 声明；实测 `as_object()` 抛 `value is not an object`，被 `catch(const std::exception&)` 捕获后仍会走到裁剪分支） |
| 日志输出量 | **上升**：`file_level=WARN` 的生产配置下会**明显增加磁盘日志**。明细限流前 3 条 + 汇总 1 条；三档告警只在异常时触发 |
| `SubscriptionParser.cpp` 消费者 | `test_autotask` + `validproxy` + `validproxy-cli`（`CMakeLists.txt:143`）⇒ **必须全量 `ctest -V`** |
| 头文件 | **零 Boost 依赖**（P1.1/P1.2 全在 `.cpp` 匿名 namespace；`ParseStats` 只含 `bool`/`long`） |
| `src/utils/Base64Utils.cpp` | **不在本次范围**（同源缺陷第二处实现，更严格）。**上线说明必须写明只修了 `SubscriptionParser` 一处**，避免「以为已全局修复」 |

---

## 5. 测试方案

> **纪律**：每条用例在**写入文档前**必须先用「未修复代码 + 可执行构造」跑一遍，确认「修复前红」。v1.1 的 T1/T4/T11/T12 四条构造层面就是坏的 —— 这类「修复后也红」比「修复前就绿」更严重。

### 5.0 日志捕获（须静音 console）

`src/LoggerInstance.cpp:74-79` 的 callback 分支位于 `consoleEnabled_`（`:65`）与 `fileEnabled_ && fileLevel_`（`:69`）判断**之外**，对**任何级别**无条件触发 ⇒ 无需 `Logger::init()`、无需日志文件。**但**构造默认 `consoleEnabled_(true), consoleLevel_(INFO)` ⇒ WARN 仍会打到 `std::cout`（已实测 `LoggerInstance.cpp:8-17`）⇒ 必须显式静音。

```cpp
class LogCapture {
public:
    LogCapture() {
        Logger::setConsoleEnabled(false);          // 默认 consoleEnabled_=true 会喷 stdout
        Logger::pushCallback([this](const std::string& msg, LogLevel lvl) {
            if (lvl == LogLevel::WARN) { warnLines_.push_back(msg); }
        });
    }
    ~LogCapture() {
        Logger::popCallback();
        Logger::setConsoleEnabled(true);
    }
    bool has(const std::string& needle) const {
        for (const std::string& line : warnLines_) {
            if (line.find(needle) != std::string::npos) { return true; }
        }
        return false;
    }
    std::vector<std::string> warnLines_;
};
```

### 5.2 base64url / 裁剪 / 字段完整性

| # | 用例 | 覆盖 | 修复前 |
|---|---|---|---|
| T1 | `Base64UrlBodyIsDecoded` | P2.1 | **红** |
| T2 | `Base64UrlIsNoOpVersusStandard` | P2.1 no-op + P1.1 indexid | **红** |
| T3 | `StandardBase64BodyUnchanged` | P2.1 no-op（回归锁） | 绿 |
| T5 | `VmessSalvageWithBraceInTrailingGarbage` | P1.2 括号配平 | **红** |
| T6 | `VmessSalvagedProfileHasAllFields` | P1.1 字段 + indexid | **红** |
| T7 | `Base64WithNulByteIsNotPolluted` | P2.1 `sizeof`→64 | **红** |
| T19 | `VmessSalvageWithEscapedQuoteAndBraceInString` | P1.2 字符串感知 + 转义 | **红** |
| T20 | `VmessTopLevelArrayIsSalvaged` | §1.4 数组新接受 | **红** |

**T1 构造（已按实测修正：必须先整体编码，且循环作用在 `b64s(plain)` 上）**

```cpp
std::string plain = "vmess://" + b64s(jsonA) + "\n" + "vmess://" + b64s(jsonB) + "\n";
std::string std64;
do {                                       // 循环体必须重算整体编码
    std64 = b64s(plain);
    if (std64.find('+') != std::string::npos || std64.find('/') != std::string::npos) { break; }
    plain += "vmess://" + b64s(jsonOf("padding-hint-" + std::to_string(plain.size()))) + "\n";
} while (true);
std::string bodyUrl = std64;
for (size_t i = 0; i < bodyUrl.size(); ++i) {
    if (bodyUrl[i] == '+')      bodyUrl[i] = '-';
    else if (bodyUrl[i] == '/') bodyUrl[i] = '_';
}
// 三条断言缺一不可（v1.1 只写了第三条，被 "vmess:" -> "vmess:_" 自我满足）
ASSERT_TRUE(bodyUrl.find('-') != std::string::npos || bodyUrl.find('_') != std::string::npos);
ASSERT_EQ(bodyUrl.find("://"), std::string::npos);      // 证明确实不是明文
ASSERT_GT(std64.size(), 0u);
```

> v1.1 的三处错误：①`b64s` 只用于单个 JSON、从未用于 `body`；②`while` 条件被 `vmess://` 自带的 `/` 满足 ⇒ **0 次迭代**；③`/`→`_` 把 `://` 一起毁掉。实测（逐字转写 v1.1 代码）：`iterations=0`、`bodyUrl starts with "vmess:__eyJ2IjoiMiIs"`、`contains "://" = False`。

**T2**：同一 `plain` 分别以 `std64` 与 `bodyUrl` 喂入两次 `parse()`，逐字段比较 `configtype / configversion / address / port / id / alterid / security / network / remarks / path / requesthost / streamsecurity / sni / flow / fingerprint / alpn / headertype / allowinsecure / subid / issub`，**显式跳过 `indexid`**；并断言 `indexid` 两者**非空且不相等**。

**T5 构造**：载荷解出为 `{"v":"2",…,"net":"tcp"}garbage}`（尾部垃圾**含 `}`**）。已实测：OLD `rfind('}')`=73 取到垃圾括号 ⇒ 重试 `extra data` ⇒ 被 `:170 catch(...) {}` 吞掉（红）；NEW 配平=65 ⇒ `{"v":"2",…,"net":"tcp"}` 解析成功（绿）。**这是 P1.2 唯一可证伪的用例。**

**T6 断言**：`port` 非空且 1–65535、`indexid` 非空、`streamsecurity == "tls"`、`sni == requesthost`（回退生效）、`fingerprint == "chrome"`、`allowinsecure == "false"`。

**T19 构造**：载荷解出为 `{"ps":"say \"hi}\" now","net":"tcp"}junk`。已实测：字符串感知匹配得 **35**，**不**处理 `\\` 转义的实现得 **15**（取到字符串内的 `}`）⇒ 对「字符串内括号 + 转义引号」零鉴别力。断言救回成功且 `remarks == "say \"hi}\" now"`。

**T20 构造**：载荷解出为 `[{"v":"2","add":"1.2.3.4","port":443,"id":"u","net":"tcp"}]`。已实测 `as_object()` 抛 `value is not an object`（继承 `std::runtime_error`）⇒ 被 `catch(const std::exception&)` 捕获 ⇒ 仍走裁剪分支 ⇒ 裁出内层对象成功。断言 `profiles.size() == 1`。

### 5.3 独立失败模式

| # | 用例 | 覆盖 | 修复前 |
|---|---|---|---|
| T8 | `PlainJsonVmessPayload` | P3.1 | **红** |
| T8b | `PlainJsonVmessPayloadWithLeadingSpace` | P3.1 剥前导空白 | **红** |
| T9 | `UrlEncodedBase64VmessPayload` | P3.2 | **红** |
| T21 | `UrlEncodedPlainJsonVmessPayload` | P3.1+P3.2 顺序（v1.1 的 I2） | **红** |
| T10 | `RemarksFallbackToFragment` | P3.3 | **红** |
| T11 | `SingleCharPayloadCountedAsSkipEmpty` | P0.3 显式判定 | **红** |

- **T8**：`vmess://{"v":"2","ps":"plain",…}`；断言解析成功、`stats.payloadPlainJsonLines == 1`。
- **T8b**：`vmess:// {"v":"2",…}`（`{` 前有空格）⇒ 断言仍成功。
- **T9**：`payload = urlEncode(b64s(json))`（`+`→`%2B`、`=`→`%3D`）；断言 `stats.payloadUrlEncoded == 1` 且解析成功。
- **T21**：`payload = urlEncode("{\"v\":\"2\",\"ps\":\"enc\",\"add\":\"1.2.3.4\",\"port\":443,\"id\":\"u\",\"net\":\"tcp\"}")`（**完全 URL 编码的明文 JSON**，首字符 `%7B`）。实测 v1.1 的两段式决策链在此形态下**失败**（先判 `{` 失败 → `urlDecode` 得到 `{"v":…` → 仍送进 `decodeBase64` → 花括号被丢弃）。断言 v1.2 的有序决策链成功。
- **T10**：JSON **无 `ps`** + fragment `#HK-01`；断言 `remarks == "HK-01"`。片段用含字母表字符的 ASCII（v1.0 用 UTF-8 `我的节点🚀` 实测剥离与否解码结果逐字节相同 ⇒ 零鉴别力）。
- **T11**：`vmess://A`（**1 个**字母表字符）。实测 `decodeBase64("A")` → `len=0`、`empty()==true`。（**不是** `"AA"` —— 实测 `"AA"` → `b'\x00'`、`len=1`、`empty()==false`，会绕过 `:81` 落到 `vmessFailParse`。）断言 `stats.vmessSkipEmpty == 1`。

### 5.4 `ss://` 路径

| # | 用例 | 覆盖 | 修复前 |
|---|---|---|---|
| T12 | `SsUserInfoWithUrlSafeChars` | P2.3 受益点 | **红** |
| T13 | `SsPlaintextUserInfoStillDropped` | P2.3「不变」 | 绿 |
| T14 | `SsMalformedNonAlphabetUserInfoStillDropped` | `:554-557` 历史修复不回退 | 绿 |

**T12 构造（已按实测修正为循环构造）**：

```cpp
std::string userInfo;
do {
    userInfo = b64s("aes-256-gcm:pass-word");
    for (size_t i = 0; i < userInfo.size(); ++i) {
        if (userInfo[i] == '+')      userInfo[i] = '-';
        else if (userInfo[i] == '/') userInfo[i] = '_';
    }
    if (userInfo.find('-') != std::string::npos || userInfo.find('_') != std::string::npos) { break; }
    userInfo = b64s("aes-256-gcm:pass-word-xx");       // 追加字符后重算，直到命中
} while (true);
ASSERT_TRUE(userInfo.find('-') != std::string::npos || userInfo.find('_') != std::string::npos);
```

> v1.1 直接举例 `"aes-256-gcm:pass-word"`，实测其 base64 为 `YWVzLTI1Ni1nY206cGFzcy13b3Jk`，**不含 `-`/`_`** ⇒ base64url 形式与标准形式逐字节相同 ⇒ `:365 find(':')` 成功 ⇒ **节点在 HEAD 上正常产出、断言全过** ⇒ 修复前就是绿的。

- **T13**：`ss://chacha20-ietf-poly1305:pw@host:8388`（明文 userInfo）⇒ 断言 `profiles.empty()`。**v1.0 的用例 7「含 `%`/`@`/`:` 的 ss 链接能正确解出 method/password」前提不可构造**（字母表不含 `%@:`；真含则 `:` 被丢弃 ⇒ `:365 find(':')` 必 npos ⇒ `:368` 丢弃）。
- **T14**：复刻 `docs/bugfix/2026-08-04-…GarbageSS-v1.0.md` 的畸形形态；断言 `profiles.empty()`。

### 5.5 可观测性

| # | 用例 | 覆盖 | 修复前 |
|---|---|---|---|
| T15 | `CountersClassifyEveryLineExactly` | P0.3 口径完整 | **编译红** |
| T16 | `EmptyBodyRaisesWarning` | P0.5 第一档（404 空体） | **红** |
| T17 | `PartialLossRaisesWarning` | P0.5 第三档 | **红** |
| T18 | `WarningCarriesSubid` | P0.4 归因信息 | **红** |
| T22 | `MalformedProtocolLinesAreCounted` | P0.3 的 11 条（v1.1 的 I3） | **红** |

- **T15 不变量必须是等式**（v1.1 用了 `<=`，那是上界，只检 over-report，对低报恒真）：

```cpp
ASSERT_EQ(stats.lineCount,
          stats.droppedEmptyLine + stats.vmessOk + stats.vmessFailParse
        + stats.vmessSkipEmpty + stats.unrecognizedLines + stats.droppedMalformedLine);
```

body = 2 个正常 vless + 1 个正常 vmess + 1 个坏 vmess + 1 行乱码 + 1 个畸形 vless（无 `@`）⇒ 预期 `lineCount=6`、`vmessOk=1`、`vmessFailParse=1`、`unrecognizedLines=1`、`droppedMalformedLine=1`、`droppedEmptyLine=0`。

- **T17 构造（已按实测修正）**：**10 个正常 vmess + 6 个坏 vmess + 2 行乱码** ⇒ `vmessOk=10`、`(6+2)*2=16 > 10` ✔ 触发丢弃率告警。（v1.1 用「10 个正常 vless + 2 个坏 vmess」⇒ `vmessOk==0` ⇒ 命中的是「零结果」告警，丢弃率告警不可达。）
- **T22**：body 含 1 个 `vless://xxx`（无 `@`）⇒ 断言 `droppedMalformedLine >= 1` 且 `droppedByProtocol[0] >= 1`。**修复前该行零计数零日志。**

---

## 6. 验证

| 阶段 | 命令 | 期望 |
|---|---|---|
| 每阶段快速迭代 | `cmake --build build --parallel 8` 然后 `ctest -R SubscriptionParserTest -V` | EXIT 0 |
| **交付前（强制）** | `ctest --test-dir build --output-on-failure -j 4` | 除已知基线失败外全绿 |
| 真实数据 | `bin/config.json` 的 `log.file_level` 降到 `INFO`，跑 `.\bin\validproxy-cli.exe -UA` | 每 subid 一行 `WARN: vmess summary [subid=N] lines=… ok=… fail=… unrecognized=… malformed=…` |

**已知基线失败（勿误判为回归）**：`XrayManagerStartupTest`、`RegionBatchResolverTest`、`UI_PORTCLOSE`、`UI_FLOATINGWIDGET`（并行 flaky，单跑通过）、`NetworkMonitorTest`。

**clangd 报错是假错误**（缺 include 路径），一律以 `cmake --build build` 为准。

---

## 7. 风险

| 风险 | 缓解 |
|---|---|
| P2 归一化改变 `ss://` 行为 | T12/T13/T14 正反夹住；`isPrintableAscii` 双检保留；P0 的 `ssUrlCharCount` 可事后量化 |
| `sizeof`→64 影响 `\0` 输入 | 实测确认是**修复**；T7 锁定 |
| 括号配平误判字符串内括号 | `findMatchingBrace` 给出完整状态机 + 三个边界返回值；T19 锁定（转义感知 35 vs 不感知 15） |
| 顶层数组被新接受 | 已在 §1.4 与 §4 声明为**行为变更**；T20 覆盖 |
| **P0 日志量上升** | `file_level=WARN` 生产配置下会明显增加磁盘写入。限流前 3 条 + 汇总 1 条；三档告警仅异常时触发。**上线前须评估 `log.file_level` 是否需上调到 ERROR** |
| 「已修复」误解外溢 | `Base64Utils.cpp` 未动；上线说明必须写明只修 `SubscriptionParser` 一处 |
| MSVC `/W4 /WX` unused 变量 | `ParseStats` 经 out-param 传递并被测试真实消费；**不引入枚举状态变量** |
| v1.1 的 4 条坏用例若照抄 | 本版逐条给出修正后的可执行构造 + 实测证据；实施时以 §5 为准，**不得回退到 v1.1 的 §5** |

---

## 8. 范围外与后续

### 8.1 本 Spec 不覆盖

| 项 | 理由（含实测计数更正） |
|---|---|
| `src/utils/Base64Utils.cpp:25-26` | 同源缺陷第二处实现，且**更严格**（遇任意非字母表字符即**整段返回空**）。`CMakeLists.txt:150` 注册，被其它模块使用。**须另立文档。** |
| B 类 `addOutbound FAILED ... output=`（空） | 实测 **139** 条空 output + 143 条 fallback WARN + 312 条 `addOutbound FAILED` 总计。属 `XrayApi` 注入失败；进入解析兜底路径至多 7 条，无法解释 139 次 |
| C 类 `SKIP: ... invalid UUID format` | 实测 **2157** 条。v1.0 断言「命中非 vmess/vless 节点」**被源码证伪** —— `src/SubitemUpdaterV2.cpp:62-67` 条件为 `configtype == "1" \|\| configtype == "5"`，而 `"1"` **就是 vmess** ⇒ UUID 校验**只针对 vmess/vless**。⇒ C 类**可能**含「JSON 合法但字段被污染」的 vmess 节点。**须用 P0 的 subid 归因数据重新评估** |
| `unsupported SS cipher` | 实测 **801** 条（`aes-256-cfb` **614** / `aes-128-cfb` 83 / `rc4-md5` 65 / `chacha20-ietf` 37 / `chacha20` 2）。**消息源在 `src/SubitemUpdaterV2.cpp:70` 与 `src/update/Deduplicator.cpp:306`，不在 `SubscriptionParser.cpp`（该文件共 670 行）** —— v1.1 误引 `:697` |
| 尾随逗号 / 括号内污染型 `syntax error` | 已实测**无法**通过切尾部恢复（P1.2 是恒等变换）。若要救需「括号内修复」，是完全不同的算法。待 P0 数据 |
| `urlDecode` 部分十六进制解析缺陷（`:613-618`） | 实测 `urlDecode("50%free")` → `"50\x0Fee"`（`%fr` → `0x0F`）。既存缺陷，影响 vless/ss/trojan/hy2。**P3.3 刻意不对 `remarksFallback` 调用 `urlDecode`**，以免扩大污染面 |
| `docs/INDEX.md` 登记 | v1.2 定稿后统一登记（v1.0 / v1.1 为被否定的中间稿，不登记） |

### 8.2 P4 — 归因结论（交付后的下一步）

拿到 P0 数据后**必须**回答：

1. 114 条失败按 `subid` 是**聚集在少数几个**（格式级成因）还是**散布在几十个**（逐节点成因）？
2. `stats.payloadUrlCharLines` 是 **0**（⇒ base64url 不是根因，P2 仅为兼容性增强）还是**非 0**（⇒ 根因坐实）？
3. `stats.payloadPlainJsonLines`（P3.1）与 `stats.payloadUrlEncoded`（P3.2）各占多少？v1.0 断言「`not JSON` 主要由 base64url 造成」很可能是错的 —— 明文 JSON 是**确定性 100%** 生产者，而错位是**概率事件**（`-`/`_` 数量 ≢ 0 mod 4 且落在前段才损坏）。
4. `stats.droppedMalformedLine` / `unrecognizedLines` 的相对量级，验证「98 条 `not JSON` 只是每个错位订阅的首行数、真实损坏量无上界」这一判断。

**决策映射**：若 (2) 为 0 且 (3) 以明文 JSON 为主 ⇒ base64url 与本日志无关，P2 应降级为纯兼容性增强并在文档中如实说明；若 (2) 非 0 ⇒ P2 是根因修复，须在变更说明中给出订阅源占比。

---

## 9. 变更记录

| 版本 | 说明 |
|---|---|
| v1.0 | 初稿（3 份评审一致判 FAIL） |
| v1.1 | 撤回 v1.0 的 5 处事实性错误；补齐四要素；拆 P0–P4；`ParseStats` out-param；测试全部标注「修复前红/绿」（**评审判 FAIL**：T1 构造不走 base64 且永久红、T12 修复前即绿、T11 口径互斥、T17 触发条件不可达、`ParseStats` 字段名与代码片段对不上、漏 `payloadUrlCharLines` 落点、引用了不存在的源码行号 `SubscriptionParser.cpp:697`） |
| **v1.2** | 逐条修正 v1.1 的 9 个阻断 + 9 个重要问题：①T1 重写（先整体编码 + 循环作用在 `b64s(plain)` + 断言 `://` 不存在）；②撤回 P1.2「`syntax error` 可救回」的因果论证，收益收窄为「修 `rfind` 取错尾括号」，尾随逗号列入范围外；③T12 改循环构造；④`vmessSkipEmpty` 口径统一为「字母表字符 < 2」并新增显式判定点，T11 改用 `vmess://A`；⑤T17 改 10 好 vmess + 6 坏 vmess + 2 乱码；⑥`ParseStats` 补 `payloadNonBase64Chars` / `ssUrlCharCount` / `droppedEmptyLine` / `droppedByProtocol[]`，并逐调用点写死归属；⑦P0.3 覆盖全部 **14** 条 line 级出口（原只 3 条）；⑧`findMatchingBrace` 给出完整状态机与三个边界返回值，新增 T19/T20；⑨P1.1/P1.2 移入 `.cpp` 匿名 namespace（规避头文件 Boost 依赖，`CMakeLists.txt:967` 无 vcpkg 头路径）；⑩P3.1+P3.2 合并为单一有序决策链（剥 fragment → urlDecode → 判 `{` → decodeBase64），新增 T21；⑪§5.0 补 console 静音（`LoggerInstance` 默认 `consoleEnabled_=true`）；⑫T15 不变量由 `<=` 改为等式 + 全桶分类；⑬P0.5 补「空体」档；⑭更正调用方路径为 `src/SubitemUpdaterV2.cpp:567`（顶层 `src/`，非 `src/update/`）；⑮更正 SS cipher 计数（801 / aes-256-cfb 614）与消息源（`SubitemUpdaterV2.cpp:70`、`Deduplicator.cpp:306`）；⑯`§2.2` 修正为「11 字段 + 1 条件回退」；⑰登记 `urlDecode` 部分十六进制缺陷 |
