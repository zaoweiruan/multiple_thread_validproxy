---
title: "fix: 订阅导入数据污染治理 — 私网地址/非法 UUID/不支持 SS 加密导入拦截与存量清洗"
type: fix
status: draft
date: 2026-08-10
origin: "docs/bugfix/2026-08-10-Analysis-InjectionError-ConfigRootCause-v1.0.md §4.7/§6 P1（订阅导入私网地址代理专项调查）"
---

# 修复计划：订阅导入代理数据污染治理（P1）

- **日期**：2026-08-10
- **类型**：fix（数据污染治理 P1 覆盖类别 A1 地址侧 / A1 id 侧 / B；附加 P2-C1 REALITY+gRPC 调度层规避与 P3 ado 回退重试）
- **来源**：`docs/bugfix/2026-08-10-Analysis-InjectionError-ConfigRootCause-v1.0.md` §4.7（私网地址调查）与 §6 P1（修复建议）
- **关联修复**：
  - `docs/bugfix/2026-08-04-Bugfix-SubscriptionParser-GarbageSS-v1.0.md`（Security/Id 可打印 ASCII 稽核，同思路扩展）
  - `docs/specs/2026-08-05-Spec-ImportProxyValidation-v1.0.md`（isPrintableAscii 提取 + isValidProxy 入库前稽核）
  - `docs/bugfix/2026-08-05-Bugfix-ProxyBatchTester-PreGenParseError-v1.0.md`（pregenFailedFlags_ skip 机制复用）

---

## 1. 问题描述

批量测试注入错误分析（报告 §4.7）确认：**订阅导入的代理地址可为私网/回环地址**，且此类代理 100% 来自上游订阅数据，非本地解析器默认值。

- **示例（indexid=5323085616270219902）**：vmess（ConfigType=1），`Address=127.0.0.1:1080`，`Id=baacbac-acab-acba-dcba-bbaccacbcaab`（首段 7 位 hex，非法 UUID），Remarks=`@Hope_Net-join-us-on-Telegram`，来源订阅 Argh94-vmess（`https://raw.githubusercontent.com/Argh94/V2RayAutoConfig/refs/heads/main/configs/Vmess.txt`）。上游订阅文件 base64 解码后逐字段命中（同 id/同端口/同备注）→ **决定性证据：私网代理原样来自上游**。
- **全库统计**：292,581 条中真私网/回环地址 **42 条（0.014%）**，分布 10 个订阅源：127.0.0.1×13、127.0.0.53×10（systemd-resolved，端口 80-89，测试 UUID `88888888-...`）、IPv6 反转垃圾域名×4（`0.5.0.0.7.0.f.1...xzhi.eu.org`）、`0.ir0.ir`×3、`0.0.0.einetwork.news`×2、0.0.0.0×2 等。
- **根因链**：上游聚合源污染（用户分享本地配置的 127.0.0.1 监听地址被导出为服务器地址；自动测试/占位条目）→ 导入端仅校验 Security/Id 可打印 ASCII，**Address 无私网/回环/保留地址校验，id 无 UUID 结构校验，SS method 无加密白名单** → 垃圾节点原样入库并参与批量测试 → 注入阶段非法 UUID 失败（A1）或连通性失败。

**治理目标**：在导入闸门拦截 + 存量清洗 + 测试前预过滤三层堵住私网地址/非法 UUID/不支持 SS 加密，使新导入订阅不再引入此类垃圾，存量逐步清除，注入失败率趋近于 0；另附加 P2-C1（REALITY+gRPC 调度层降级）与 P3（ado 回退时序）两项运行时噪音治理。注入编码链路不动，仅 P3 涉及回退重试时序。

## 2. 范围边界

**修改**：
- `include/Utils.h` + `src/Utils.cpp` — 新增 3 个纯函数辅助校验
- `src/update/SubitemUpdaterV2.cpp` — `isValidProxy` 导入闸门扩展（地址/UUID/cipher）
- `src/update/Deduplicator.cpp` — `deduplicateConfigErrorPhase` 存量清洗判定扩展
- `src/ProxyBatchTester.cpp` — `preGenerateConfigs` 显式预过滤（复用 pregenFailedFlags_；含 U6 REALITY+gRPC 组合降级 skip）
- `src/XrayApi.cpp` — `addOutbound` 回退分支失败重试（仅 U7 时序调整，不触碰 gRPC 编码）
- `tests/test_utils.cpp`（+`tests/test_utils.h`）、`tests/test_subscription_parser.cpp`、`tests/test_dedup.cpp`、`tests/test_proxy_batch_components.cpp`、`tests/test_xray_api_direct.cpp` — 单测

**NOT 修改**：
- `src/XrayApi.cpp` 注入链路**编码**（addOutboundDirect gRPC / addOutbound 子进程命令构造）— 报告已确认编码无缺陷，不动；仅 U7 在回退失败后追加一次短等待重试（不改变命令/参数/解析）
- `ConfigGenerator` 生成逻辑 — 保持忠实编码
- `ShareLink` 相关（`ShareLink.cpp` / `ShareLinkFactory` / `ShareLinkExportService`）— 已核实全部为**导出路径**（toShareUri，AutoTaskManager/main_cli/AppController/ProxyListPanel 仅导出），无导入校验需求
- `Profileitem::checkRequired()` — 地址/端口/字段存在性检查已够用，私网/UUID/cipher 属业务规则，放 utils 层
- 不允许引入 `auto`（AGENTS.md 核心约束，C++17）

## 3. 详细变更

### U1: `include/Utils.h` + `src/Utils.cpp` — 新增 3 个纯函数（依赖基础）

在 `namespace utils` 内新增声明（`include/Utils.h`，置于 L19 `isPrintableAscii` 之后）：

```cpp
// 地址是否公网可达：拒绝回环/私网/链路本地/保留/组播/CGNAT/0 开头垃圾域名/IPv6 反转域名
bool isPublicAddress(const std::string& address);
// UUID 结构校验：标准 8-4-4-4-12 hex 分组（不强制 version nibble=4，避免误杀 v1-v5）
bool isValidUuid(const std::string& id);
// SS 加密方法白名单（Xray v26 仅 AEAD + 2022-blake3 家族）
bool isSupportedSsCipher(const std::string& method);
```

**`isPublicAddress` 判定规则（按序短路，全部匹配才返回 true）**：

| 类别 | 规则 | 命中示例（应拒绝） |
| :--- | :--- | :--- |
| 空 | 空串/纯空白 | — |
| IPv4 回环 | `127.0.0.0/8`（首段=127） | 127.0.0.1、127.0.0.53、127.1.1.127 |
| IPv4 私网 RFC1918 | `10/8`；`172.16/12`（**第二八位组数值 16–31，数值解析**）；`192.168/16` | 172.17.0.1、192.168.1.1 |
| IPv4 链路本地 | `169.254/16` | 169.254.0.1 |
| IPv4 保留/其他 | `0.0.0.0/8`（首段=0）；`100.64/10`（CGNAT）；`224/4`（组播）；`240/4`（保留）；255.255.255.255 | 0.0.0.0、0.0.17.96 |
| IPv6 回环/未指定 | `::1`、`::` | ::1 |
| IPv6 ULA/链路本地/组播 | `fc00::/7`、`fe80::/10`、`ff00::/8` | fc00::1、fe80::1 |
| 垃圾域名 0 前缀 | 首标签为全 0 数字（`0`、`0.0` 开头多标签） | 0.ir0.ir、0.0.0.einetwork.news |
| IPv6 反转域名（启发式） | 点号分隔的标签中连续 ≥4 个单 hex 字符标签 | 0.5.0.0.7.0.f.1.0.7.4.0.1.0.0.2.xzhi.eu.org |
| 其余 | 正常公网 IPv4 / 域名 / 非上述 IPv6 | 98.89.48.191、example.com |

> **RFC1918 判定陷阱（必须按此实现）**：`LIKE '172.%'` 会把 Cloudflare 公网 anycast（172.64/66/67）与 RackNerd 段（172.232+）误判为私网；字符串区间 `>= "172.16." && < "172.32."` 也错（`"172.232" < "172.32"` 按字典序成立）。**必须解析第二八位组为整数后判定 16 ≤ n ≤ 31**。

**`isValidUuid` 规则**：按 `-` 拆分为 5 段，长度依次 8/4/4/4/12，每段全为 hex 字符（0-9a-fA-F）。结构合法即通过；不检查版本/变体位（`88888888-8888-8888-8888-888888888888` 结构合法，靠地址检查拦截其 127.0.0.53 载体）。

**`isSupportedSsCipher` 白名单**：`aes-128-gcm`、`aes-256-gcm`、`chacha20-poly1305`、`xchacha20-poly1305`、`none`、`2022-blake3-aes-128-gcm`、`2022-blake3-aes-256-gcm`、`2022-blake3-chacha20-poly1305`。

**单测**（tests/test_utils.cpp）：`isPublicAddress` 正反例覆盖上表每行 + 172 边界（172.15/172.16/172.31/172.32/172.64/172.232）+ 空串；`isValidUuid` 合法/非法连字符位置/非 hex/长度错误/测试 UUID；`isSupportedSsCipher` 白名单 + 旧流式（aes-256-cfb、rc4-md5、chacha20-ietf）拒绝。

### U2: `src/update/SubitemUpdaterV2.cpp` — 导入闸门扩展（isValidProxy，L40-58）

在现有 (c) `isPrintableAscii(security/id)` 检查之后追加（同一匿名命名空间函数，保持 SKIP 语义与 WARN 日志模式）：

- (d) `!utils::isPublicAddress(p.address)` → SKIP，WARN 日志注明 `private/reserved address`
- (e) ConfigType ∈ {1(vmess), 2(vless)} 且 `!utils::isValidUuid(p.id)` → SKIP，WARN 日志注明 `invalid UUID`
- (f) ConfigType ∈ {3(shadowsocks), 7(shadowsocks-2022)} 且 `!utils::isSupportedSsCipher(p.security)` → SKIP，WARN 日志注明 `unsupported SS cipher`

> ConfigType 赋值点已核实（SubscriptionParser.cpp）：`"1"` vmess（L92/L157）、`"5"` http（L184）、`"3"` ss（L257）、`"6"` trojan（L385）、`"7"`（L454）；vless `"2"` 未在 SubscriptionParser 找到赋值点（可能在其他分支），**实现时核对 vless 实际 ConfigType 与 SS method 存储字段**（ss:// 分支 method:password 解析后 method 落 Security 字段，需确认）。
>
> 空 address 已被 (a) checkRequired 拦截，isPublicAddress 对空返回 false 属双保险。

**影响面**：`updateProfileItems()` L565 的 Phase 0 预过滤（`FILTER: Removed N invalid proxies before insert`）自动涵盖新增判定，无需改动调用点；全部过滤时 return true（非失败）语义保持。

**单测**（tests/test_subscription_parser.cpp 或新增 test 文件）：构造含 127.0.0.1 / 非法 UUID / aes-256-cfb 的订阅输入，断言解析后入库剔除、FILTER 计数正确、合法代理不受影响（含合法域名、172.64.117.10 等 Cloudflare 公网不误杀）。

### U3: `src/update/Deduplicator.cpp` — 存量清洗扩展（deduplicateConfigErrorPhase，L240-276）

现有循环内 `checkRequired() + isPrintableAscii(security/id)` 判定后追加（同一 bad 收集路径）：

- `!utils::isPublicAddress(p.address)` → bad
- ConfigType ∈ {1,2} 且 `!utils::isValidUuid(p.id)` → bad
- ConfigType ∈ {3,7} 且 `!utils::isSupportedSsCipher(p.security)` → bad

日志消息扩展为 `Phase ConfigError deleted: N (checkRequired failed + non-printable Security/Id + private/reserved address + invalid UUID + unsupported SS cipher)`；沿用 `dao.deleteByIndexIdsNoTx(failedIds)` 批量删除；事务边界不变（注意既有嵌套事务修复：deleteByIndexIdsNoTx 不得再开事务）。

**单测**（tests/test_dedup.cpp）：构造含私网地址/非法 UUID/旧 cipher 的库数据，触发 deduplicateConfigErrorPhase，断言仅删除目标行、公网 172.64.x.x 保留、计数正确。

### U4: `src/ProxyBatchTester.cpp` — 测试前预过滤（preGenerateConfigs，L463-485）

> **关键点**：ConfigGenerator 忠实编码，私网地址/非法 UUID 不会抛异常 → 现有 try/catch 拦不住，必须显式校验。

在逐代理循环内 `configGen.generateConfig(...)` 之前插入显式校验（复用 U1 函数）：

- `!utils::isPublicAddress(proxy.address)` → `failCfg.configFailed=true; pregenFailedFlags_[i]=true;`（WARN 日志，标注原因）并 continue
- ConfigType ∈ {1,2} 且 `!utils::isValidUuid(proxy.id)` → 同上
- ConfigType ∈ {3,7} 且 `!utils::isSupportedSsCipher(proxy.security)` → 同上

保持既有 pregenFailedFlags_ 语义：worker 入口跳过（对应 2026-08-05 PreGenParseError 修复的 skip 机制），失败代理不进入注入链路。

**单测**（tests/test_proxy_batch_components.cpp）：PreGenFailedSkipTest 扩展 — 注入私网地址/非法 UUID 代理，断言 pregenFailedFlags_ 置位、worker 不发起注入、汇总计数正确。

### U6: `src/ProxyBatchTester.cpp` — REALITY+gRPC 组合节点调度层降级（P2-C1，规避 reality.go:273 越界 panic）

> **背景**：报告 §4.4/§6 P2-C1 —— REALITY 传输 + gRPC 流组合（vmess/vless）在握手响应为空时触发 Xray-core `reality.go:273` 越界 panic → 实例崩溃 → 全实例注入失败。根因在上游，validproxy 注入层无法修复。

在 U4 同一循环内（`configGen.generateConfig(...)` 前）追加判定：

- ConfigType ∈ {1(vmess), 2(vless)} 且 `network == "grpc"` 且 `security == "reality"` → `failCfg.configFailed=true; pregenFailedFlags_[i]=true;`（WARN 日志 `REALITY+grpc known-unstable, skip (Xray reality.go:273)`）并 continue

> **边界**：仅跳过 REALITY+grpc **组合**；普通 REALITY（tcp/ws 等）与普通 gRPC（xtls/tls 等）节点不受影响。跳过属**测试调度层降级**，不删除数据库行——上游修复后可移除该判定恢复测试。
>
> **判定字段确认**：Profileitem 的 `network`/`security` 字段在 preGenerateConfigs 可见（与 U2/U4 一致），实现时确认大小写与空值语义（network 可能为小写 "grpc"）。

**单测**（tests/test_proxy_batch_components.cpp）：构造 vmess+grpc+reality 节点断言 skip；构造 REALITY+tcp、grpc+tls 节点断言**不** skip。

### U7: `src/XrayApi.cpp` — ado 回退失败空输出时重启窗口重试（P3，低优先）

> **背景**：报告 §4.6/§6 P3 —— 实例因 C1 panic 重启的窗口内，gRPC 注入失败后回退 `xray api ado` 子进程也以 exitCode=1 立即失败且 **output 为空**，无法区分「配置错误」与「实例不可用」。

在 `addOutbound` 回退子进程分支（exitCode != 0 路径，L177-183 附近）追加：

- 若 `exitCode != 0` **且 output 为空**（`stdoutText`/stderr 均无内容）→ 判定为疑似实例重启窗口，WARN 日志 `subprocess fallback failed with empty output, likely instance restart window; retrying once` → 短等待（如 300ms）后重试一次同一子进程命令；
- 重试仍失败 → 保持 FAILED 但错误归因明确为 `instance unavailable`（区别于配置错误）；
- 若失败时 output **非空**（真实配置错误，如 Xray 打印的具体报错）→ 不重试，保持现有行为。

> **边界**：仅扩展重试与归因逻辑；子进程命令构造、stdin 内容、exit code 语义均不变。gRPC 主路径 addOutboundDirect 不动。

**单测**（tests/test_xray_api_direct.cpp）：mock 子进程（首次 exitCode=1 空输出、二次成功 / 两次均空输出失败 / 一次非空输出不重试）断言重试次数与归因日志。

### U5: 文档同步

- `docs/plans/project-plans-tracker.md` — 新增本计划引用行（status: draft）
- `docs/INDEX.md` §8.2 待执行/草稿计划表 — 新增本计划行
- 实施完成后按 DEV-PROCESS 流程更新本文件 status → completed 并记录验证结果

## 4. 测试计划与验证步骤

1. **编译**：`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug` + `cmake --build build --parallel 8`（0 errors）
2. **单测**：`ctest -V`（现有 21/21 + 新增用例全绿）
3. **回归验证（对生产库 bin/worker/guindb.db 只读预演）**：
   - 用 sqlite3 快照查询当前 42 条私网代理，实施 U3 后再次查询应大幅减少（预期仅剩非校验范围条目，若有）
   - 执行一次订阅更新（`-UA` 或 UI），确认新导入无 127.0.0.1 等条目、FILTER 计数出现
4. **端到端**：对含私网条目的订阅执行批量测试，确认不再出现该类别 XRAY_ERROR（类别 A1 私网侧/B）
5. **U6 验证**：批量测试含 REALITY+gRPC 节点的订阅，确认该类节点被 skip、实例不再因 reality.go:273 panic 崩溃（对比修复前 L66 C1 场景）
6. **U7 验证**：模拟实例重启窗口（kill 实例后立即触发注入），确认 ado 回退出现一次重试且日志归因清晰

## 5. 验收标准

- [ ] U1 三个纯函数全部落地且单测覆盖每类规则（含 172 边界不误伤公网）
- [ ] 新导入订阅不再引入私网/回环地址代理、非法 UUID（vmess/vless）、旧流式 SS 加密
- [ ] `-D` 去重/UI 去重触发后存量私网条目被清除，公网 172.64/172.232 等不被误删
- [ ] 批量测试预过滤生效：私网/非法 UUID/旧 cipher 代理被 skip，不进入注入链路
- [ ] 全部 ctest 通过；构建无告警；未引入 `auto`
- [ ] U6：REALITY+gRPC 组合被 skip；普通 REALITY（tcp 等）与普通 gRPC（tls 等）不受影响
- [ ] U7：ado 回退空输出失败重试一次且归因「实例不可用」；非空输出失败不重试
- [ ] 文档登记完成（tracker + INDEX.md）

## 6. 风险与回退

- **误杀风险**：域名判定仅含「0 前缀首标签」与「IPv6 反转启发式」两种保守规则，其余域名放行；IPv4/IPv6 按数值解析，边界严格。若发现误杀，调整规则为白名单式并补充单测。
- **vless ConfigType 未知**（U2/U3/U4 均引用）：实现前先 grep 确认 vless 赋值分支；若 vless 与 vmess 共用 id 语义则归入 {1,2}，否则单独确认。
- **回退**：每个 U 单元独立可回退（函数级改动，无跨模块耦合）；U3 清洗仅删目标行，可通过库快照恢复。
- **范围控制**：不触碰注入编码与 ConfigGenerator，杜绝引入注入侧回归；U7 仅限回退重试时序。
- **U6 误跳过风险**：REALITY+gRPC 组合中理论可用节点会被保守跳过（属调度层降级、非数据删除），上游修复 reality.go:273 后可一键移除该判定。
- **U7 放大风险**：重试仅限一次 + 短等待（≤500ms），避免实例长期不可用期间放大失败延迟；重试逻辑只在 output 为空时触发。

## 7. 里程碑顺序

1. **M1**：U1 utils 函数 + 单测（无依赖）
2. **M2**：U2 导入闸门 + 单测（依赖 M1）
3. **M3**：U3 存量清洗 + 单测（依赖 M1）
4. **M4**：U4 预过滤 + 单测（依赖 M1）
5. **M5**：U6 REALITY+gRPC 调度层降级 + 单测（独立，无 U1 依赖）
6. **M6**：U7 ado 回退重试 + 单测（独立）
7. **M7**：U5 文档同步 + 全量验证（编译/ctest/回归/端到端）
