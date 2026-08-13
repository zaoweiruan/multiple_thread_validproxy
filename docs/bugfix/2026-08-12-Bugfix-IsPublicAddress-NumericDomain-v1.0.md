# Bugfix: isPublicAddress 误判数字开头合法域名（private/invalid address 误杀）v1.0

- **日期**：2026-08-12
- **模块**：`Utils`（`isPublicAddress` / `isPublicDomain`）
- **关联问题**：`bin/worker/log/ui_20260812_084452.log`（4591 行，08:47:15→09:12:31）导入闸门 `SKIP: ... - private/invalid address` 共 162 条：其中约 79 条为真实私网/保留 IP（`127.*`×72、`0.0.0.0`×7，上游订阅数据污染，拦截正确），约 80 条为**数字开头合法域名被 `isPublicAddress` 误判**（`8103.shomaparvazroyadetonnist-bypassishere.lat`×22、`*.outline-vpn.cloud`×13、超长数字子域 `*.poki-pakipon.ir`×10、`1744156156.tencentapp.cn`×4、`*.webnama.com.tr`×7 等），另有空 host `.`×3
- **关联文档**：`docs/plans/2026-08-10-Plan-ImportProxyValidation-v1.0.md`（U1 `isPublicAddress` 数值判定的制定）、`docs/bugfix/2026-08-11-Bugfix-SsCipher-Whitelist-v1.0.md`（同类导入闸门误伤修复先例）

## 1. 背景与动机

### 1.1 日志分析（`ui_20260812_084452.log` private/invalid address 专项统计）

对 `bin/worker/log/ui_20260812_084452.log` 全文件统计 `private/invalid address` 相关 `[WARN] SKIP` 行（导入闸门 `SubitemUpdaterV2::isValidProxy` → `!utils::isPublicAddress(p.address)`，`src/SubitemUpdaterV2.cpp` L58-61）：

| 分类 | 数量 | 判定 |
| --- | --- | --- |
| 私网/保留 IP（127.*/0.0.0.0） | 79 | 正确拦截（上游把本机地址/DNS stub 当节点，数据污染） |
| 数字开头合法域名（疑似误判） | 约 80 | **代码缺陷（本次修复对象）** |
| 空 host `.` | 3 | 正确拦截 |

Top host（次数）：`127.0.0.53`×40（systemd-resolved）、`127.0.0.1`×16、`8103.shomaparvazroyadetonnist-bypassishere.lat`×11、`12812.shomaparvazroyadetonnist-bypassishere.lat`×11、`0101010101010101010101001010101010110010101010101010101010101.poki-pakipon.ir`×8、`0.0.0.0`×7、`1744156156.tencentapp.cn`×4、`. `×3、`230920393.f-sub.com`×3、`147135004002.sec20org.com`×3、`127.1.1.127`×3、`99823232.webnama.com.tr`×3 等。时间集中两个导入批次（08:46 桶 124 条 + 08:48 桶 38 条）。

### 1.2 根因分析（RCA）

`src/Utils.cpp` `isPublicAddress`（L347-410）对含点字符串采用**边解析边短路**的 IPv4 判定：

```cpp
// 修复前（节选）
if (current < 0 || current > 255) return false;   // L383：遇 '.' 时八位组 >255 → 立即拒绝
...
if (!inOctet || octetIndex != 3) return false;     // L394
if (current < 0 || current > 255) return false;    // L395：最后八位组 >255 → 立即拒绝
```

缺陷链：

1. **主缺陷**：`8103.shomaparvaz…lat` 等合法域名格式的首标签（或任一八位组位置）为 `>=256` 的数字串时，L383/L395 在**尚未遇到字母**（`isPublicDomain` 回退分支 L388-391 只在遇非数字非点字符时进入）之前就已 `return false`。即"数字段 >255 → 拒"的短路逻辑把合法 FQDN 当作非法 IP 处理，永远走不到域名回退。
2. **附带隐患**：`current = current * 10 + (ch - '0')`（L379）对 32 位整型超长数字串（如 `93343878961078676381579883502615.international-ixp.com`）存在**有符号整型溢出 UB**。

### 1.3 修复策略选择

| 方案 | 描述 | 结论 |
| --- | --- | --- |
| A（维持现状） | L383/L395 短路拒绝保留 | 约 80 条数字开头合法域名持续被误杀，且保留溢出 UB |
| B（严格 IPv4 完整匹配 + 域名回退） | 扫描遇**非数字非点字符** → 整体按域名 `isPublicDomain` 处理；字符串保持纯数字+点时才按 IPv4 严格校验（4 段全 0..255、段数恰为 4、无空段/尾点），任一不满足即拒绝 | **采纳** |

选择 **方案 B**：IPv4 判定从"边解析边短路"改为"先整体判定形式，再按形式分流"；`isPublicDomain` 的两条保守垃圾域名启发式（Rule 1 `0.*` 开头标签、Rule 2 ≥4 个单字符 hex 标签）**保持不变**（`0.0.0.einetwork.news` 等继续按设计拒收，属有意启发式而非缺陷）。

## 2. 变更范围

| 文件 | 变更 |
| --- | --- |
| `src/Utils.cpp` | `isPublicAddress` IPv4 解析段（原 L370-396）重写：遇字母/非点字符 → `return isPublicDomain(address)`；纯数字+点形式严格校验；`current*10` 溢出防护（`current > 255` 后停止累积） |
| `tests/test_utils.cpp` | 新增 `IsPublicAddressTest` 测试组（回归用例，见 §4） |
| `docs/bugfix/2026-08-12-Bugfix-IsPublicAddress-NumericDomain-v1.0.md` | 本文档 |

**不修改**：`include/Utils.h`（L22 声明，签名不变）；`SubitemUpdaterV2.cpp` / `Deduplicator.cpp` 调用点（同一纯函数自动生效）；`isPublicDomain` 两条启发式（Rule 1/Rule 2，有意设计）；IPv6 分支与无点单标签分支（无缺陷）。

## 3. 设计

### 3.1 `isPublicAddress` 新判定流程（L370 起替换）

```cpp
// Try to parse as an IPv4 dotted-quad.  As soon as a character is neither a
// digit nor a dot the whole string is a domain name and is validated via
// isPublicDomain() instead.  Strings that remain purely numeric/dotted are
// accepted only when they form a well-formed IPv4 address (exactly four
// octets, each 0..255, outside reserved ranges); empty octets, octets > 255
// or more than four octets are rejected.
int octets[4] = {0, 0, 0, 0};
int octetIndex = 0;
int current = 0;
bool inOctet = false;
bool malformed = false;    // invalid IPv4 shape seen (scan continues)
bool overflowed = false;   // current octet already > 255 (stop accumulating)

for (const char ch : address) {
    if (ch >= '0' && ch <= '9') {
        if (octetIndex >= 4) {
            malformed = true;              // more than four octets
        } else if (!overflowed) {
            current = current * 10 + (ch - '0');
            if (current > 255) {
                overflowed = true;
            }
        }
        inOctet = true;
    } else if (ch == '.') {
        if (!inOctet) malformed = true;    // empty octet (e.g. "1..2")
        if (overflowed) malformed = true;  // octet > 255
        if (octetIndex < 4) {
            octets[octetIndex] = current;
        }
        octetIndex++;
        current = 0;
        overflowed = false;
        inOctet = false;
    } else {
        // Non-digit, non-dot -> domain name
        return isPublicDomain(address);
    }
}

if (!inOctet) return false;                // trailing dot (e.g. "1." / ".")
if (overflowed) malformed = true;          // last octet > 255
if (octetIndex != 3) return false;         // must be exactly four octets
if (malformed) return false;
octets[3] = current;
// RFC1918 / special-use ranges (reject) —— 以下判定不变
```

**关键语义**：
- **域名回退优先级最高**：扫描中一旦遇到非数字非点字符（含 `8103.xxx` 中 `8103` 段溢出之后的字母 `x`），立即整体按域名处理——数字段 >255 不再提前拒绝。
- **纯数字+点形式仍严格**：`256.1.1.1`、`1.2.3.4.5`、`1..2`、`1.`、`.` 等一律拒绝（与修复前行为一致）。
- **溢出防护**：`current` 一旦 >255 即停止累积（防 32 位 int 有符号溢出 UB），仅保留标记。
- **RFC1918/特殊段检查**（0/10/127/169.254/172.16-31/192.168/100.64-127/224-239/240-255）只对**严格合法的 4 段纯数字 IPv4** 生效，私网拦截能力不变。

### 3.2 行为矩阵（修复前 vs 修复后）

| 输入 | 修复前 | 修复后 | 说明 |
| --- | --- | --- | --- |
| `8103.shomaparvaz…lat` | **false（误杀）** | true | 合法域名格式，本次修复对象 |
| `876.outline-vpn.cloud` | **false（误杀）** | true | 同上 |
| `1744156156.tencentapp.cn` | **false（误杀）** | true | 同上 |
| `67.7777112.xyz` | **false（误杀）** | true | 同上 |
| `09303582303.ddns.net` | **false（误杀）** | true | `0` 开头但非 `0.` 模式，Rule 1 不适用 |
| `93343878961078676381579883502615.international-ixp.com` | false（误杀 + 溢出 UB） | true | 超长数字串，修复后无 UB |
| `127.0.0.1` / `0.0.0.0` / `192.168.1.1` / `10.1.2.3` / `172.16-31.*` / `169.254.*` / `224.*` / `240.*` | false | false | 私网/保留段拦截不变 |
| `8.8.8.8` / `1.2.3.4` / `172.32.0.1` | true | true | 公网 IPv4 不变 |
| `256.1.1.1` / `1.2.3.4.5` / `1.` / `.` / `1..2` | false | false | 非法 IP 形式仍拒 |
| `0.0.0.einetwork.news` | false | false | Rule 1 有意启发式，不属本次范围 |
| `127.0.0.1.example.com` | true | true | 字母回退 isPublicDomain，两侧一致 |
| `example.com` / `a.b.c.d.example.org`（Rule 2 拒） | true / false | true / false | 域名分支不变 |

## 4. 测试

`tests/test_utils.cpp` 新增 `IsPublicAddressTest` 组（`test_utils` 目标 = `tests/test_utils.cpp + src/Utils.cpp`，注册为 `UtilsTest`）：

- **修复目标回归**：`8103.shomaparvaz…lat`、`876.outline-vpn.cloud`、`1744156156.tencentapp.cn`、`67.7777112.xyz`、`09303582303.ddns.net`、超长数字子域 `*.poki-pakipon.ir` → `EXPECT_TRUE`
- **私网/保留段保持**：`127.0.0.1`、`0.0.0.0`、`10.1.2.3`、`192.168.1.1`、`172.16.0.1`、`169.254.1.1`、`224.0.0.1`、`240.0.0.1` → `EXPECT_FALSE`；`172.32.0.1`、`8.8.8.8` → `EXPECT_TRUE`
- **非法 IP 形式保持**：`256.1.1.1`、`1.2.3.4.5`、`1.`、`.`、`1..2` → `EXPECT_FALSE`
- **域名分支保持**：`example.com` → true；`0.example.com`（Rule 1）、`a.b.c.d.example.org`（Rule 2）→ false；`0.0.0.einetwork.news` → false
- **混合形式**：`127.0.0.1.example.com` → true（字母回退，两侧一致）

验证：`cmake --build build --parallel 8 --target test_utils`；build 目录下 `ctest -R UtilsTest -V`（必须用 `-R` 或全量 `ctest -V`，根目录运行报 No tests were found）。

## 5. 范围与约束

- 本次仅修正 `isPublicAddress` 的 IPv4/域名分流判定，**不改变** message 双时间戳、排序等其它功能。
- `isPublicDomain` 两条启发式（Rule 1 `0.*`、Rule 2 ≥4 单字符 hex 标签）为有意保守设计，维持不变。
- 全栈禁止 `auto`（显式类型）；LSP/clangd 报 wx 头找不到为既有噪音，以 gcc 编译为准。
