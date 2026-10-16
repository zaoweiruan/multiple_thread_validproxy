# 独立评审报告 — Spec-SubscriptionBase64UrlFix-v1.2

- **日期**: 2026-10-16
- **模块**: `docs/specs/2026-10-15-Spec-SubscriptionBase64UrlFix-v1.2.md`（评审对象，713 行）
- **版本**: v1.0（评审报告本身）
- **评审对象代码**: `src/update/SubscriptionParser.cpp` / `include/update/SubscriptionParser.h` / `src/SubitemUpdaterV2.cpp`
- **触发输入**: `bin/worker/log/ui_20261016_084620.log`

## 1. 结论

**FAIL** —— 3 名不同视角的独立评审者一致否决。

| 评审者 | 视角 | 结论 |
|---|---|---|
| A | 技术断言攻击（尝试证伪每一条技术断言） | FAIL |
| B | 可实施性（编译/链接/运行时/阶段可分解性） | FAIL |
| C | 文档一致性（AGENTS.md 合规 / 内在矛盾 / 完备性） | FAIL |

**三方共识**：v1.2 的**设计骨架是 sound 的**，失败集中在 **§5 测试夹具构造**、**计数器记账闭合**、**少量文档论断**三处。

---

## 2. 共识阻断项（≥2/3 命中，必须修）

### B1. T15 等式不变量算不平（3/3 命中）

v1.2 声明 `lineCount == droppedEmptyLine + vmessOk + vmessFailParse + vmessSkipEmpty + unrecognizedLines + droppedMalformedLine`，并把「进入 `while(getline)` 的每一行恰好落入一个桶」作为设计原则。

但 T15 自己的输入含 **2 个正常 vless**，而 vless 不计入 `vmessOk`（§0.2 文档自己写明），且 `ParseStats` **没有「其他协议成功」桶** ⇒ 桶和 = 5 ≠ `lineCount` = 6。

**这不是夹具笔误，是结构性缺陷**：任何含成功 vless / ss / trojan / hy2 节点的订阅都永久违反此等式。

**修法**：新增 `long otherOk` 桶 + 补自增点（vless/ss/trojan/hy2 主路径成功出口），并重算 T15 期望值。

### B2. `vmessOk` / `vmessFailSalvaged` 声明了但无自增点（3/3 命中）

P0.3 的 14 行记账表缺两处：

| 缺失的自增点 | 位置 | 被谁依赖 |
|---|---|---|
| `++vmessOk` | `:525/:526`（与既有 `validCount++` 同址） | T15 等式；P0.5 第二/三档触发条件 |
| `++vmessFailSalvaged` | `:169`（裁剪救回成功） | P0.4 summary 行的 `salvaged=` 字段 |

照表实现 ⇒ `vmessOk` 恒 0 ⇒ **T15 必红**，且 P0.5 第二档「解析出 0 个节点」对任何含成功节点的订阅都误报。

**连带未决问题**：一条「先失败后被裁剪救回」的行，计入 `vmessOk` 还是单列 `vmessFailSalvaged`？单列 ⇒ `vmessOk` 低报；两者同时计 ⇒ T15 重复计数。v1.2 把 T15 从 `<=` 收紧为**等式**，恰恰让这个未决问题从「无所谓」变成阻断，但文档没有回答。

### B3. T12 构造死循环（2/3 命中）

```cpp
do {
    userInfo = b64s("aes-256-gcm:pass-word");
    /* 归一化 */
    if (含 '-' 或 '_') break;
    userInfo = b64s("aes-256-gcm:pass-word-xx");   // ← 唯一能推进的语句
} while (true);
```

第 616 行的赋值在下一轮循环开头被无条件覆盖 ⇒ **循环体是常量** ⇒ 条件永不成立。

实测：`'aes-256-gcm:pass-word'` 与 `'...-xx'` 的 base64 分别是
`YWVzLTI1Ni1nY206cGFzcy13b3Jk` 与 `YWVzLTI1Ni1nY206cGFzcy13b3JkLXh4`，**均不含 `+` / `/`**。

讽刺点：T12 正是 §9 声称「已修正为循环构造」的 5 条之一，而 §5 的纪律要求「每条用例写入文档前必须先跑一遍」—— 一个死循环的构造不可能被跑过。

### B4. T19 修复前就是绿（零鉴别力）（2/3 命中）

§5.4 声称 T19「锁定转义感知，修复前红」。

**评审 B 跑了真实 `parse()`**：`payload = {"ps":"say \"hi}\" now","net":"tcp"}junk` ⇒ HEAD 产出 `profiles=1`、`remarks = say "hi}" now`，**两条断言全过**。

原因：HEAD 用 `rfind('}')` = **35**，与配平扫描结果 35 **完全相同**（尾部垃圾 `junk` 不含 `}`）。文档引用的「15」是**假想的非转义感知实现**，不是 HEAD。

**修法**：尾部垃圾改成含 `}` 的 `x}` ⇒ HEAD `rfind` = 31（红）、配平 = 23（绿）。

### B5. T1 循环数学上不可终止（1/3 命中，但决定性）

v1.2 的 T1 用 `do { std64 = b64s(plain); if (含 '+' 或 '/') break; plain += ...; } while (true);` 构造 base64url body。

**`plain` 只含 `vmess://` + base64 字母表 + `\n`，永远不含 `>` / `?` / `~` / DEL / ≥0x80 字节。** 而 base64 产生 sextet 62/63 需要：

| sextet | 触发条件 |
|---|---|
| 62 (`+`) | `b0 >= 0xF8` 或 `b1 >= 0xE0` 或 `b2 ∈ {0x3E, 0x3F}` |
| 63 (`/`) | `b0 >= 0xF8` 或 `b1 >= 0xE0` 或 `b2 ∈ {0x7E, 0x7F}` |

穷举 `0x20..0x7E` 全部 2,000,376 个三字节组 ⇒ **0 可达**。实测 3000 轮后 `plain` 涨到 363,166 字符、含 `+`/`/` 数仍为 **0** ⇒ `break` 永不执行、后续断言不可达、ctest 撞 TIMEOUT。

**这是 v1.1-T1 缺陷的镜像**：v1.1 是「while 条件被 `vmess://` 自带的 `/` 满足 ⇒ 0 次迭代」，v1.2 是「条件永远不满足 ⇒ ∞ 次迭代」。**T1 连续三轮都是坏的。**

### B6. P0.5 第一档条件恒假（1/3 命中，但逻辑证明完备）

条件 `lineCount == 0 && !content.empty()`：任何非空 `content` 至少产出 1 行 ⇒ `lineCount >= 1` ⇒ 第一个合取式**恒假**。

**且生产不可达**：三个调用点（`src/SubitemUpdaterV2.cpp:242 / :421 / :499`）都在 `parse()` 之前就挡掉了空 content。⇒ **T16 无法构造**。

连带：§1.4 声称「v1.1 漏此项，会让 404 空体形态完全静默」—— 结论对、**归因错**。404 空体静默是因为调用方提前拦截并打的是 INFO，P0 无论加什么档都够不着。

### B7. P4 三个必答问题依赖的字段无输出通道（1/3 命中，但致命）

P0.4 的 summary 行格式只含 `lines / ok / fail / salvaged / skipEmpty / unrecognized / malformed`，**不含任何 `payload*` 字段**。

而 §8.2 的问题 2（`payloadUrlCharLines`）与问题 3（`payloadPlainJsonLines` / `payloadUrlEncoded`）全靠这些字段。`ParseStats` 是每次 `parse()` 独立的 out-param，外部无聚合钩子。

**后果：P4 按本文档实施后拿到 P0 数据，会发现 3 个必答问题无解。** 问题 1（subid 聚集度）与问题 4 可从现有字段推导。

> 注：§0.1 撤销 v1.0 的核心理由正是「结构上无法把任何一条归因到任何机制」—— v1.2 在 P4 上重新引入了同一结构性盲区，只是换了字段名。

### B8. §1.3.5 的「boost 头路径」论证是虚构的（1/3 命中，但有铁证）

文档称：`test_subscription_parser` 的 `target_include_directories`（`CMakeLists.txt:967`）不含 vcpkg boost 头路径 ⇒ `#include <boost/json/object.hpp>` 进头文件会让一批 TU 编译失败。

**事实**：`CMakeLists.txt:114` 是**目录级** `include_directories(${VCPKG_ROOT}/installed/x64-mingw-static/include)`，对该目录下每个 target 生效。`build/compile_commands.json` 里该目标的真实编译命令**含 `-ID:/vcpkg/installed/x64-mingw-static/include`**。

对照探针：无 vcpkg `-I` ⇒ `fatal error: boost/json.hpp: No such file`（exit 1）；有 ⇒ exit 0。

⇒ 把 boost 放进头文件**不会**编译失败。把 `fillVmessProfile` / `findMatchingBrace` 放 `.cpp` 匿名 namespace 仍然是对的，但理由要改成**分层卫生**，不是「编译会失败」—— 否则实施者会据此在未来设计里过度约束。

### B9. P0.5 第二档对 vless-only 订阅误报（1/3 命中）

`lineCount > 0 && vmessOk == 0` ⇒ 纯 vless 订阅（100 行全部成功）会输出「解析出 0 个节点」纯误报警。

v1.2 只改了**测试**（把 T17 的 10 个 vless 改成 10 个 vmess），**判定式一个字没改**。§0.2 的推理完全正确，但结论止步于「改测试」而未落到「改设计」。

**修法**：第二档基改为「全部成功桶之和为 0」（`vmessOk + otherOk == 0`）。

---

## 3. 单方但成立的重要问题

| # | 问题 |
|---|---|
| `b64s()` / `urlEncode()` helper 出处未声明 | `include/utils/Base64Utils.h` 只导出 `base64Decode()` 与 `looksLikeBase64()`，**无编码器**，且 `src/utils/Base64Utils.cpp` 不在 `:966` 源列表。§1.3.3「追加用例无需改 CMake」仅在 helper 写在测试 TU 内的 `static` 自由函数时成立 —— 文档必须写死这句。 |
| `:147-148` errMsg 白名单删除是未声明的行为变更 | 且是 T20 能通过的唯一机制（`value is not an object` 本被白名单挡在裁剪分支外）。未列入 §1.4 边界条件 / §4 影响面 / §7 风险。 |
| `urlDecode` 把字面 `+` 转成空格 | `:622` 首轮生效，实测 `urlDecode("abc+def") == "abc def"`。P3.2 让 urlDecode 首次在 vmess 热路径生效 ⇒ **部分 URL 编码的 base64 载荷会丢 `+`、4 字符分组错位**，重新引入 P2 要消灭的损坏类。§8.1 只登记了 `%fr`→0x0F 半截缺陷。建议用 `looksLikeBase64()` 谓词而非 `find('%')` 作为闸门。 |
| §6 交付命令不是 `ctest -V` | L56/L496 写「必须全量 `ctest -V`」，L656 却写 `ctest --test-dir build --output-on-failure -j 4`。AGENTS.md §6.2 要求 `ctest -V`。 |
| 缺 `docs/bugfix/` 修复日志 | AGENTS.md §6.2 bug 行要求。全文仅两处 `bugfix` 引用 2026-08-04 既有文档作夹具。 |
| 日志量高估 | 按限流（前 3 条 + 汇总 1 条）算，P0 新增 ≈ 134 行 / 4077 行 = **+3%**，「明显增加」与「考虑上调 `file_level` 到 ERROR」是过度反应。 |
| 重编译扇出未写 | `include/update/SubscriptionParser.h` 改动 → `SubitemUpdaterV2.h:18` → 5 个 `.cpp` × 多 target ≈ 11 次 TU，加 `SubscriptionParser.cpp` 本身 4 个 target ≈ 15 次。§4 消费者表漏了 `test_subscription_parser`。 |
| 头文件 private 声明的改法未给 | `include/update/SubscriptionParser.h:17`。探针实测唯一编译错误就是「3 参 `parse()` 重载不存在」。 |
| `T7` 不可达 | `decodeBase64` 是 private，只能经 `parse()` 观测，而 `b'ABC\x01\x11\x15'` 与 `b'ABCDEF'` 都不含 `{}` ⇒ 两者都抛 `not JSON` ⇒ 差异不可见。必须经 `stats.payloadNonBase64Chars` 断言（未指定）。 |
| `P3` 有两条硬依赖 | `P1.1` 的 `fillVmessProfile` + `P2` 的 3 参 `decodeBase64`，不可单独上线，文档未声明。 |
| `P2` 代码片段含中文占位记号 | `stats->bodyUrlCharCountOrPayloadOrSs()`，且与 P0.1「绝不让 decodeBase64 内部累加」矛盾（归一化后无法在解码后统计 `-`/`_`，需调用点预扫 = 第二套实现）。同一概念 `alphabetChars` 有三处竞争实现（P0.3 自由函数 / P2 out-param / P3.1 `&alpha`）。 |
| 零测试守护 | `P0.4` 限流计数器必须栈上局部（防 `static` 跨订阅共享额度）无对应用例；`P0.1` 逐调用点归属表（防 `:364` ss 污染 `payload*`）无对应用例；`findMatchingBrace` 三个 `npos` 边界返回无用例。 |
| 文档瑕疵 | `droppedByProtocol[6]` 声明 6 槽但只写 `[0]..[3]`；`T4` 在 §5 纪律条款被引用但 v1.2 已删除；T22 标注「11 条」与桶定义 10 条不符；`chacha20-ietf-poly1305 37` 实测类别名是 `chacha20-ietf`（poly1305 0 次）；`缺陷 N` 存在两套互不相容编号（§2.1 与 §3 阶段表）；§8.1「INDEX.md 待登记」已过时。 |

---

## 4. 三方一致认定 v1.2 正确的点

架构骨架可信，以下均经独立复核通过：

- **P2 对标准 base64 是严格 no-op** —— `-` / `_` 确不在 `A-Za-z0-9+/`（实测 `+`=62、`/`=63）。
- **`sizeof`→64 修复正确** —— 实测 `old("QUJD\0REVG")` → `b'ABC\x01\x11\x15'`，应为 `b'ABCDEF'`。
- **`:554-557` 注释改写不自相矛盾** —— 归一化在字母表查找之前，skip 与 normalize 互斥。
- **boost `object.hpp:1093-1109` 引用精确正确** —— `&`→`value&`、`const&`→`value const&` ⇒ `fillVmessProfile(boost::json::object&, ...)` 必要且充分，**逐字转写后编译 exit=0 零警告**，T6 的 6 条断言全中。
- **`findMatchingBrace` 状态机逻辑正确** —— naive=15 vs string-aware=35 逐字命中。
- **`ParseStats`（只含 `bool` / `long`）进头文件安全**。
- **`LogCapture` 编译 + 运行均正确** —— `captured=2 has(subid)=1`，`setConsoleEnabled(false)` 确实静音。
- **P2 对 ss:// 的收益真实可复现** —— 明文 `aes-256-gcm:kM?_R5Kjp?1vRt-` → base64url 后 HEAD 解出二进制被 `:373` 杀，修复后精确还原明文。
- **14 条 line 级 `continue` 行号精确** —— `parse()` 作用域内共 19 条 `continue;`，另 5 条在内层 `paramStream` 循环（`:220/:292/:303/:420/:490`），正确排除。
- **`parse()` 生产调用方唯一** —— `src/SubitemUpdaterV2.cpp:567`；文件 670 行 / 头文件 24 行且无 boost。
- **`as_object()` 抛 `std::runtime_error` 派生异常**（`dynamic_cast` 实测）。
- **boost 位置串「1 是行号」** —— 真实串 `boost.json:1 at …:701 in function 'parse_value'`。
- **日志计数 114 / 98 / 9 / 7 / 2157 / 801 / 139 / 143 与 `INFO:` 0 次全部精确命中**。
- **§0.1 + §0.2 的 10 条撤回逐条在 v1.0 / v1.1 中真实存在**；**§9 的 17 项修正全部在正文有对应落地**。
- **「P0 零功能行为变更」成立** —— 新增 WARN 属可观测变更，口径界定清楚。
- **「追加用例无需改 CMake」成立** —— `SubscriptionParserTest` 是真实注册名 #21/53。
- **`T5` 是三方一致认定的唯一完全可证用例** —— HEAD `rfind`=37 取垃圾括号 → 重试失败（红）；配平=29 → 绿。

---

## 5. 元结论：三轮失败模式高度收敛

| 版本 | 失败模式 |
|---|---|
| v1.0 | 根因未坐实（被自身表格证伪）+ 骨架漏 `indexid` + `const` 形参编译不过 |
| v1.1 | **测试夹具构造错误**（T1 0 次迭代）+ 计数器记账不闭合 + 伪造行号引用 `:697` |
| v1.2 | **测试夹具构造错误**（T1 ∞ 次迭代，与 v1.1 互为镜像）+ 计数器记账不闭合 + 少量文档论断 |

**架构层面**（分阶段、诚实声明、源码行号、归因方法论）三轮都被认定扎实。**失败集中在收束处。**

### 根因（自省）

v1.2 的 §5 自立军规是「每条用例写入文档前必须先用未修复代码跑一遍，确认修复前红」。但 22 条里**至少 5 条（T1 / T12 / T19 / T16 / T7）根本没跑过就写进了文档** —— T12 死循环、T1 不可终止这类问题，只有真正跑起来才会暴露。

**方法论修正**：后续 P1 / P2 / P3 的用例改为「**先写代码 → 编译 → 跑 HEAD 验证红 → 再写进文档**」，不再纸上构造。

---

## 6. 建议

### 6.1 优先只做 P0（可行性评审建议）

**只做 P0**（`ParseStats` + 3 参重载 + 14 处计数 + 3 档 WARN）即完整达成归因目标，且**零功能行为变更、零回归风险**。P0 的所有验收标准都可以靠「读 WARN 日志」验证，不依赖任何测试夹具 —— 这恰好绕开了本次三轮反复失败的那一类问题。

同时补齐 P0 的 3 个记账缺口（`otherOk` / `vmessOk` / `vmessFailSalvaged`）与 P4 的数据通道（summary 行追加 `payload*` 字段）。

### 6.2 重新排序阶段优先级

现行排序 `P0 → P1 → P2 → P3` 中，**价值最高的 P3.1（明文 JSON）被排在最后**，而 §0.1 自己判定它是 98 条 `not JSON` 的**确定性 100% 生产者**（`{` / `}` / `"` / `:` / `,` 全被 `:567-569` 丢弃 ⇒ 输出必无花括号 ⇒ `:83` isJson false）。

而 P1.1 `fillVmessProfile` 重构（49 行搬迁）与 114 条错误**正交** —— 那 114 条是解析失败，根本走不到 `fillVmessProfile`（那是解析成功后的路径）。

### 6.3 未决事项

- 是否出 v1.3 只保留 P0 的收敛版 Spec，还是直接实施 P0。
- P4 的 4 个归因问题中，问题 1（subid 聚集度）目前**没有任何决策分支**，而它恰恰是最能决定 P1/P2/P3 谁是真修复的那个问题。

---

## 7. 本次评审的验证手段（供复现）

| 手段 | 用途 |
|---|---|
| Python 逐字复刻 `decodeBase64` / `urlDecode` / `findMatchingBrace` | 证明 T1 不可终止、T12 死循环、T19 零鉴别力、T15 等式不平 |
| 真实 Boost.JSON 1.91 编译探针 | 验证 `at()` ref-qualification、`as_object()` 异常类型、尾随逗号抛 `syntax error` |
| 链接真实 `Logger` 的运行探针 | 验证 `LogCapture` 可行性与 `setConsoleEnabled(false)` 静音效果 |
| **真实 `SubscriptionParser::parse()` 端到端探针** | 证明 T19 在 HEAD 上就是绿、T5 才是真红→绿 |
| `build/compile_commands.json` 核对 | 证伪 §1.3.5 的 boost include path 论证 |
| 日志重计数 | 114 / 98 / 9 / 7 / 2157 / 801 / 139 / 143 / `INFO:` 0 次全部复核 |

**未修改任何生产代码、测试代码或 Spec 文件。** 探针源码保留在 `temp/feas_probe/`。
