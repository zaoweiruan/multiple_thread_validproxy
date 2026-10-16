# Spec — 订阅 vmess base64url 解码缺陷修复（缺陷 1–4）

- **日期**: 2026-10-15
- **模块**: `src/update/SubscriptionParser.cpp`、`include/update/SubscriptionParser.h`、`tests/test_subscription_parser.cpp`
- **版本**: v1.0

## 1. 背景

对 `bin/worker/log/ui_20261016_084620.log` 的分析（根因已用 Python 逐字复刻 `decodeBase64` 的算法实证，见 §7）确认：日志中 **114 条「解析错误」全部源于同一根因** —— `SubscriptionParser::decodeBase64()` **不识别 base64url 变体**，且对非法字符**静默丢弃**，导致 `vmess://` 链接解码错位、JSON 结构被截断。

决定性实验（同一份 JSON，仅替换 `+`→`-`、`/`→`_`）：

| 输入 | 解码结果含 `{}` |
|---|---|
| 标准 base64（含 `+` `/`） | **True** → `{"ps":"… node"}` |
| 同一内容的 base64url（含 `-` `_`） | **False** → `{"ps":"���\x06��FR` |

错误消息与根因一一对应：

| 日志消息 | 次数 | 机理 |
|---|---:|---|
| `Failed to parse vmess: not JSON` | 98 | 解码错位后不含 `}` ⇒ `SubscriptionParser.cpp:83` 的 `isJson==false` ⇒ 主动 `throw std::runtime_error("not JSON")` |
| `Failed to parse vmess: syntax error [boost.json:1 …]` | 9 | 解码出首字节即非法（boost 报偏移 **1**）⇒ BOM/错位字节。**本可救回但被缺陷 3 的白名单漏掉** |
| `Failed to parse vmess: extra data [boost.json:2 …]` | 7 | 一行内多个 JSON 或尾随垃圾（多半是 `#备注` 未剥干净，见缺陷 4） |
| `Base64 decode produced invalid content, using original` | 9 | **整段**订阅 body 也含 `-`/`_`，整块解崩 ⇒ `:53-56` 回退为原始 base64 文本 ⇒ 逐行时整块不以 `vmess://` 开头 ⇒ **该订阅解析出 0 个节点，只留一行 WARN** |

本 Spec 只处理上述 vmess/base64 相关缺陷。日志中的另两类错误**与本问题无关，不在范围内**：B 类 `addOutbound FAILED … output=`（空，139 次）属 XrayApi 注入失败；C 类 `SKIP: … - invalid UUID format`（数百次）属 UUID 校验命中订阅里混入的非 vmess/vless 节点，是正常跳过。

## 2. 需求

对缺陷 1–4 给出可实施的修复方案，并遵循项目规范：C++17、**禁止 `auto` 类型推导**、`tests/` 下补 Google Test、文档先行。

## 3. 现状代码（已核对行号）

### 3.1 `include/update/SubscriptionParser.h`（全文 24 行）

```cpp
namespace update {
class SubscriptionParser {
public:
    SubscriptionParser() = default;
    std::vector<db::models::Profileitem> parse(const std::string& content, const std::string& subid);
private:
    std::string decodeBase64(const std::string& input);     // 行 17
    std::string urlDecode(const std::string& input);
    std::pair<std::string, std::string> parseAddressPort(const std::string& addrPart);
};
}
```

### 3.2 `parse()` 的相关片段

- `:43` `bool hasProtocol = (content.find("://") != std::string::npos);`
- `:46-57` `hasProtocol` → `decoded = content`；否则 `:51` `decoded = decodeBase64(content)`，`:53-56` 若 `decoded.length() < 10 || decoded.find("://") == npos` 则打 `WARN: Base64 decode produced invalid content, using original` 并 `decoded = content`
- `:59` `INFO: Final content length: N`
- `:61-69` `std::istringstream` + `std::getline` 逐行，剥尾部 `\r`
- `:75-81` vmess 分支：`encoded = line.substr(8)` → `jsonStr = decodeBase64(encoded)`；空则 `jsonStr = urlDecode(encoded)`；仍空则 `continue`
- `:83` `bool isJson = (jsonStr.find('{') != npos && jsonStr.find('}') != npos);`
- `:84-90` `try { if (!isJson) throw std::runtime_error("not JSON"); boost::json::parse(jsonStr); … }`
- `:92-144` 正常填 `Profileitem`：`configtype="1"`、`configversion="2"`、`add→address`、`port`（支持 int64/string/double）、`id`、`aid→alterid`、`scy→security`、`net→network`+`sanitizeNetwork`、`ps→remarks`、`path`、`host→requesthost`、`tls→streamsecurity`、`sni`、`flow`、`fp→fingerprint`（默认 `chrome`）、`alpn`、`type→headertype`、`insecure→allowinsecure`
- `:145-177` catch 兜底（**缺陷 3 现场**）：
  ```cpp
  } catch (const std::exception& e) {
      std::string errMsg = e.what();
      if (errMsg.find("incomplete") != npos || errMsg.find("extra data") != npos) {   // ← 白名单只有这两个
          size_t braceStart = jsonStr.find('{');
          if (braceStart != npos) {
              size_t braceEnd = jsonStr.rfind('}');
              if (braceEnd != npos && braceEnd > braceStart) {
                  std::string singleJson = jsonStr.substr(braceStart, braceEnd - braceStart + 1);
                  try {
                      boost::json::value jv = boost::json::parse(singleJson);
                      boost::json::object obj = jv.as_object();
                      profile.configtype = "1"; profile.configversion = "2";
                      profile.alterid = "0"; profile.network = "tcp";
                      profile.indexid = utils::generateUniqueId();
                      profile.coretype = ""; profile.muxenabled = "0";
                      profile.address = getJsonValueString(obj, "add", "");
                      profile.id      = getJsonValueString(obj, "id", "");
                      profile.security= getJsonValueString(obj, "scy", "auto");
                      profile.remarks = getJsonValueString(obj, "ps", "");
                      sanitizeNetwork(profile.network, profile.indexid);
                      parseSuccess = true;
                  } catch (...) {}          // ← 静默吞掉
              }
          }
      }
      if (!parseSuccess) Logger::write("WARN: Failed to parse vmess: " + errMsg, LogLevel::WARN);
  }
  if (!parseSuccess) continue;
  ```
- `:179+` vless 分支（`configtype="5"`）
- `:525-527` `profiles.push_back(profile); validCount++;`
- `:529` `INFO: Parsed N valid profiles from M lines`

**新发现的连带缺陷（必须在 §4 一并修掉）**：兜底重试分支只填了 9 个字段（`configtype/configversion/alterid/network/indexid/coretype/muxenabled/address/id/security/remarks`），**丢失了 `port`、`tls`/`streamsecurity`、`sni`、`path`、`requesthost`、`fingerprint`、`alpn`、`flow`、`headertype`、`allowinsecure`**。即便兜底裁剪救回成功，得到的也是**端口为空、无法使用的畸形 Profileitem**。这正是 §4.3 要抽取共享映射函数的原因。

### 3.3 `decodeBase64()` 全文（`:533-600`）

```cpp
std::string SubscriptionParser::decodeBase64(const std::string& input) {
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";   // :534-535
    std::string ret;  int i = 0;
    unsigned char char_array_3[3];  unsigned char char_array_4[4];

    for (size_t idx = 0; idx < input.size(); idx++) {
        const char c = input[idx];
        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;   // :546-548 padding/空白
        if (static_cast<unsigned char>(c) > 127) continue;                             // :550-552 非 ASCII
        size_t pos = 0; bool found = false;
        for (size_t k = 0; k < sizeof(base64_chars); k++)                              // :560 线性查找
            if (base64_chars[k] == c) { pos = k; found = true; break; }
        if (!found) continue;                                                        // :567-569 静默丢弃
        char_array_4[i] = static_cast<unsigned char>(pos);  i++;
        if (i == 4) { /* :575-577 三字节解算 */ for (int k=0;k<3;k++) ret += char_array_3[k]; i = 0; }
    }
    if (i > 0) { /* :586-597 尾部 1~3 字符补零，已核对 i=1/2/3 的字节数计算正确 */ }
    return ret;
}
```

**必须保留的既有行为**：`:554-557` 的注释明确记录，静默跳过非字母表字符是**上一轮为修 ss:// method/password 二进制乱码而有意加入的**（把 `%` `@` `:` 当成 index 0 会污染输出流）。本方案**不得回退这个行为**，只能在它之上叠加可观测性与 base64url 支持。

## 4. 修复方案

### 4.1 缺陷 1（根因）— `decodeBase64` 支持 base64url + 增加失败信号

**头文件**（`include/update/SubscriptionParser.h`）：

```cpp
    // base64 解码结果的状态信号（用于替代「静默丢弃」的不可观测行为）。
    enum class B64Status {
        Ok,          // 完全成功，未丢弃任何字符
        OkWithJunk,  // 解出了字节，但丢弃过非法字符（URL 片段、% 编码、UTF-8 备注等）
        Failed       // 没有任何有效字符，或结果为空 → 不可用
    };

    // 默认参数保证既有的单参数调用点（:51 / :77）无需改动。
    std::string decodeBase64(const std::string& input, B64Status* status = nullptr);
```

**实现**（`src/update/SubscriptionParser.cpp:533-600`）：

```cpp
std::string SubscriptionParser::decodeBase64(const std::string& input, B64Status* status) {
    // 标准 base64 与 base64url 两种字母表：base64url 用 '-' 代替 '+'、'_' 代替 '/'。
    // 现代订阅源/面板大量使用 base64url，原实现只认标准表，把 '-' / '_' 当成
    // 非字母表字符静默丢弃，导致 4 字符分组整体错位、解出乱码（详见 Spec §1 实验）。
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    // 长度 64 而非 sizeof(base64_chars)（=65，含结尾 NUL），
    // 否则输入里的 '\0' 会被当成 index 64 而污染输出流。
    static const size_t kBase64Chars = 64;

    std::string ret;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    bool droppedJunk = false;

    for (size_t idx = 0; idx < input.size(); idx++) {
        const char c = input[idx];

        if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;   // 既有行为：padding/空白忽略
        if (static_cast<unsigned char>(c) > 127) { droppedJunk = true; continue; }  // 既有行为：非 ASCII 忽略

        // base64url 归一化：'-' → '+'，'_' → '/'。
        char norm = c;
        if (c == '-') norm = '+';
        else if (c == '_') norm = '/';

        size_t pos = 0;
        bool found = false;
        for (size_t k = 0; k < kBase64Chars; k++) {
            if (base64_chars[k] == norm) { pos = k; found = true; break; }
        }
        if (!found) { droppedJunk = true; continue; }   // 既有行为：静默跳过，但记为 junk

        char_array_4[i] = static_cast<unsigned char>(pos);
        i++;
        if (i == 4) { /* 三字节解算不变 */ for (int k = 0; k < 3; k++) ret += char_array_3[k]; i = 0; }
    }
    /* 尾部 1~3 字符补零逻辑不变 */

    if (status != nullptr) {
        if (ret.empty())                    *status = B64Status::Failed;
        else if (droppedJunk)               *status = B64Status::OkWithJunk;
        else                                *status = B64Status::Ok;
    }
    return ret;
}
```

**为什么这样改**

- 归一化是**在字母表查找之前**做，因此 `-`/`_` 参与正常的 4 字符分组，不会错位；既有的「跳过非法字符」路径完全保留 ⇒ **ss:// 的历史修复不回退**。
- `B64Status` 是**加法**而非替换：单参数调用点（`:51`、`:77`）行为与现在完全一致，靠默认参数保证零改动编译。
- 顺带修掉 `\0` 被当成 index 64 的边界缺陷（`sizeof` → 64）。

### 4.2 缺陷 4 — 先剥离 `#备注` 片段，再解码

`vmess://<base64>#节点名` 是标准写法。当前 `#` 与备注的 UTF-8 字节（>127）都被 `decodeBase64` 跳过（缺陷 1 的静默丢弃）；备注在末尾时通常不影响对齐，但**一旦与 base64url 叠加会加剧错位**，且把备注内容喂给 `urlDecode` 兜底路径纯属浪费。

**改动**（`src/update/SubscriptionParser.cpp:75-81`）：

```cpp
        if (line.find("vmess://") == 0) {
            std::string encoded = line.substr(8);

            // 先剥离 '#' 片段（节点名备注）。base64 载荷里没有 '#'，
            // 备注的 UTF-8 字节会被 decodeBase64 当非法字符丢弃，
            // 与 base64url 叠加时还会加剧分组错位。
            const size_t hashPos = encoded.find('#');
            if (hashPos != std::string::npos) {
                encoded = encoded.substr(0, hashPos);
            }
            if (encoded.empty()) continue;

            B64Status b64Status = B64Status::Failed;
            std::string jsonStr = decodeBase64(encoded, &b64Status);
            if (jsonStr.empty()) {
                jsonStr = urlDecode(encoded);
            }
            if (jsonStr.empty()) continue;
            …
```

同时把 `:77` 的 `decodeBase64(encoded)` 与 `:51` 的 `decodeBase64(content)` 都改为接收状态，供 §4.4 的汇总日志使用。

### 4.3 缺陷 3 — 兜底裁剪改为「结构性判定」，并抽取共享映射（修掉连带缺陷）

**改动**（`src/update/SubscriptionParser.cpp:145-177`）：**删掉** `if (errMsg.find("incomplete") … || errMsg.find("extra data") …)` 这层基于 **boost 英文错误文案** 的白名单，改为只要串内含完整的 `{…}` 就裁剪重试一次。

```cpp
            } catch (const std::exception& e) {
                const std::string errMsg = e.what();

                // 兜底裁剪：只要串内含完整的 {...} 就裁出第一个 JSON 对象重试一次。
                //
                // 旧实现用 errMsg.find("incomplete")/("extra data") 做白名单，那是
                // **boost 的英文错误文案**——日志里 9 次 `syntax error` 的 jsonStr
                // 明明含有 { 与 }（否则 isJson==false 会先抛 "not JSON"），完全符合
                // 裁剪条件，却因不在白名单被直接丢弃。判定依据应是「串内含完整
                // {...}」这一结构性事实，而非第三方库的英文措辞。
                const size_t braceStart = jsonStr.find('{');
                const size_t braceEnd   = jsonStr.rfind('}');
                if (braceStart != std::string::npos && braceEnd != std::string::npos && braceEnd > braceStart) {
                    const std::string singleJson = jsonStr.substr(braceStart, braceEnd - braceStart + 1);
                    try {
                        boost::json::object obj = boost::json::parse(singleJson).as_object();
                        fillVmessProfile(obj, profile);      // ← 抽取的共享映射，见下
                        parseSuccess = true;
                    } catch (const std::exception& retryErr) {
                        Logger::write(std::string("WARN: vmess salvage retry failed: ") + retryErr.what(),
                                      LogLevel::WARN);
                    }
                }
                if (!parseSuccess) {
                    Logger::write("WARN: Failed to parse vmess: " + errMsg, LogLevel::WARN);
                }
            }
```

**抽取共享映射函数**（新 private 方法，放在 `parse()` 之后）：

```cpp
// 把 vmess JSON 对象填进 profile。主路径与兜底裁剪路径共用，
// 避免兜底救回的节点缺失 port / tls / sni / path 等字段而变成无法使用的畸形项。
void SubscriptionParser::fillVmessProfile(const boost::json::object& obj,
                                          db::models::Profileitem& profile) {
    profile.configtype   = "1";
    profile.configversion = "2";
    profile.alterid      = "0";
    profile.coretype     = "";
    profile.muxenabled   = "0";
    profile.address      = getJsonValueString(obj, "add", "");
    profile.port         = /* 沿用 :110-120 的 int64 / string / double 三态取值 */;
    profile.id           = getJsonValueString(obj, "id", "");
    profile.alterid      = getJsonValueString(obj, "aid", profile.alterid);
    profile.security     = getJsonValueString(obj, "scy", "auto");
    profile.network      = getJsonValueString(obj, "net", "tcp");
    profile.remarks      = getJsonValueString(obj, "ps", "");
    profile.path         = getJsonValueString(obj, "path", "");
    profile.requesthost  = getJsonValueString(obj, "host", "");
    profile.streamsecurity = getJsonValueString(obj, "tls", "");
    profile.sni          = getJsonValueString(obj, "sni", "");
    /* … 其余字段与 :92-144 现状逐行搬运，不改取值语义 … */
    sanitizeNetwork(profile.network, profile.indexid);
}
```

主路径（`:92-144`）同样改为调用它。**净效果**：兜底裁剪不再是「只能救回半残节点」，而是与主路径产出同规格的 Profileitem。

### 4.4 缺陷 2 — 消除「静默失败」：失败计数 + 限流明细 + 零结果告警

三处补充，均为**新增日志**，不改变解析行为：

1. **整段 base64 解码失败时带上原因**（`:53-56`）：
   ```cpp
       B64Status bodyStatus = B64Status::Failed;
       decoded = decodeBase64(content, &bodyStatus);
       if (decoded.length() < 10 || decoded.find("://") == std::string::npos) {
           Logger::write("WARN: Base64 decode produced invalid content (status="
                         + std::to_string(static_cast<int>(bodyStatus))
                         + "), using original. 若订阅源使用 base64url 或内容被加密，"
                           "该订阅将解析出 0 个节点",
                         LogLevel::WARN);
           decoded = content;
       }
   ```
2. **逐行失败计数 + 明细限流**：在 `parse()` 内新增 `int vmessFailCount = 0;`，在 `:178` 的 `if (!parseSuccess) continue;` 前：
   ```cpp
       if (!parseSuccess) {
           vmessFailCount++;
           if (vmessFailCount <= 3) {   // 只打印前 3 条明细，避免刷屏
               Logger::write("DEBUG: vmess line " + std::to_string(lineCount) + " undecodable (head="
                             + encoded.substr(0, std::min<size_t>(encoded.size(), 24)) + ")", LogLevel::DEBUG);
           }
           continue;
       }
   ```
3. **零结果 / 高失败率汇总**（`:529` 之后，返回前）：
   ```cpp
       if (profiles.empty()) {
           Logger::write("WARN: Subscription " + subid + " parsed 0 profiles from "
                         + std::to_string(lineCount) + " lines"
                         + (vmessFailCount > 0 ? (" (vmess decode failed on " + std::to_string(vmessFailCount) + " lines)") : ""),
                         LogLevel::WARN);
       } else if (vmessFailCount > 0) {
           Logger::write("WARN: Subscription " + subid + ": " + std::to_string(vmessFailCount)
                         + " vmess line(s) failed to decode, " + std::to_string(validCount)
                         + " profile(s) parsed OK", LogLevel::WARN);
       }
   ```

> 这三条让「9 个订阅静默产出 0 节点」变成用户可见的 WARN，正是缺陷 2 的要害。

## 5. 测试方案（TDD，落点 `tests/test_subscription_parser.cpp`）

目标 `test_subscription_parser` 已在 `CMakeLists.txt:966-974` 注册，链接 `src/update/SubscriptionParser.cpp src/Utils.cpp src/Logger.cpp src/LoggerInstance.cpp` + `gtest_main`，ctest 名 `SubscriptionParserTest`。**新增用例追加到该文件即可，无需改 CMake**。

`decodeBase64` 是 private，因此**全部用例走 `parse()` 这一个 public 入口**做黑盒验证（符合「测行为不测实现」；也顺带避免为测试放宽可见性）。

| # | 用例 | 断言 | 覆盖缺陷 |
|---|---|---|---|
| 1 | `ParseBase64UrlVmessBody` | 构造整段 base64url 编码的多行订阅 body（含 2 个 `vmess://` 链接，其中至少一个 base64 载荷本身含 `+`/`/`）⇒ `profiles.size() == 2`，且 `port`/`address`/`id` 全部非空 | **1**（核心回归） |
| 2 | `ParseStandardBase64VmessBodyUnchanged` | 同样内容改回标准 base64 ⇒ 结果与用例 1 **逐字段相等** | 1 的「不破坏旧路径」 |
| 3 | `ParseVmessWithFragmentRemark` | `vmess://<base64url>#我的节点🚀` ⇒ 解析成功，且 `remarks` 取自 JSON 的 `ps` 字段 | **4** |
| 4 | `ParseVmessSalvagesFromBraceWrapped` | 载荷解出 `"\xEF\xBB\xBF{...json...}"`（带 BOM，触发 `syntax error`）⇒ 裁剪兜底救回 1 个 profile | **3**（旧代码在此丢失节点） |
| 5 | `ParseVmessSalvageFillsAllFields` | 用例 4 救回的 profile 的 `port` 非空、`streamsecurity`/`sni` 正确 | **3** + §3.2 连带缺陷 |
| 6 | `ParseVmessWithTrailingJunk` | 载荷解出 `"{...json...}garbage"` ⇒ 救回 1 个 profile | 3（`extra data` 路径） |
| 7 | `ParseSsLinkWithJunkStillWorks` | 含 `%`/`@`/`:` 的 ss:// 链接仍能正确解出 method/password（**防止 §4.1 的归一化回退掉历史修复**） | 1 的回归防护 |
| 8 | `ParsePlainJsonVmessLink` | `vmess://{明文JSON}`（v2rayN 旧格式）能解析 | 见 §8 附注（范围外观察项） |

**实施顺序**：先写用例 1/3/4 观察其在当前代码上失败（红），再实施 §4.1–§4.3（绿），最后补用例 5–8 防回退。

**验证命令**：

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
ctest --test-dir build -R SubscriptionParserTest --output-on-failure
```

## 6. 影响面与风险

| 项 | 评估 |
|---|---|
| 既有 ss:// / vless / trojan / hysteria2 路径 | **零改动**（本方案只动 vmess 分支、整段解码的状态上报、兜底裁剪） |
| ss:// 二进制乱码历史修复 | **不回退**：`if (!found) continue;` 保留，只额外置 `droppedJunk`；用例 7 专门守护 |
| 单参数 `decodeBase64` 调用点 | 靠默认参数零改动编译（`:51`、`:77`） |
| 日志噪音 | 明细限流前 3 条 + 汇总 1 条；失败明细用 `LogLevel::DEBUG`，默认 WARN 级不落盘 |
| 需要联网/真实订阅 | **否** —— 全部用例用内联构造的 base64/base64url 字面量，CI 可离线跑 |
| 与 B 类 `addOutbound` / C 类 `invalid UUID` | **无关**，不在本 Spec 范围 |

## 7. 根因实证（已执行）

用 Python 逐字复刻 `decodeBase64` 的算法（`temp/sim_b64.py`），对同一份 JSON 分别做标准 base64 与 base64url 编码后解码：

```
含+/ 的标准          含'{}'=True   -> '{"ps":"���� node"}'
同一内容的 base64url  含'{}'=False -> '{"ps":"���\x06��FR'
```

结论：仅把 `+`→`-`、`/`→`_`，解码结果即从合法 JSON 变为缺尾括号的乱码 ⇒ 坐实缺陷 1 为唯一根因。

> 附注：第一次尝试用 PowerShell 复刻该算法失败，因为 PowerShell 的 `[byte]` 移位算术不可靠。**位运算复刻实验一律用 Python，不要用 PowerShell。**

## 8. 范围外的观察项（本次不修，仅记录）

`vmess://{明文JSON}`（v2rayN 旧格式，不做 base64 编码）在当前代码下会失败：`encoded` 已是 JSON，但 `decodeBase64` 会把 `{ " : , }` 等非字母表字符全部丢弃（`found==false`），得到一段非空乱码 ⇒ `isJson==false` ⇒ 抛 `not JSON`。修复代价极低（在 `:77` 前加一个「`encoded` 以 `{` 开头则直接采用」的判断），但用户只要求缺陷 1–4，故列为附注，留待评审决定是否纳入。
