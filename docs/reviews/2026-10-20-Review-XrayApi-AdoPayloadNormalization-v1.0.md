# 独立评审：XrayApi CLI 注入载荷归一化方案 v1.0

> 版本 v1.0 · 2026-10-20 · 类型：**独立技术评审（代码级可行性验证）**
> 评审对象：[`docs/specs/2026-10-20-Spec-XrayApi-AdoPayloadNormalization-v1.0.md`](../specs/2026-10-20-Spec-XrayApi-AdoPayloadNormalization-v1.0.md)
> 上游 RCA：[`docs/bugfix/2026-10-20-RCA-XrayApi-ExistingTagInjection-v1.0.md`](../bugfix/2026-10-20-RCA-XrayApi-ExistingTagInjection-v1.0.md)
> 评审方式：独立评审代理（`ce-feasibility-reviewer`，只读），将方案中的代码逐字复刻到临时目录，用 `g++ 14.2.0` + vcpkg Boost 1.91 静态库实际编译、链接、运行并做变异测试；**评审期间未修改仓库任何文件**。

---

## 0. 结论

**APPROVE WITH CHANGES（有条件通过）**

方案方向与落点正确：在 `XrayApi::addOutbound` 入口单点归一化，可一次覆盖 `ProxyBatchTester ×2`、`ProxyFinder` 与两处内部回退，且对三个「已是单 outbound」的调用点在语义上等价（已逐点核实）。RCA 因果链与现场证据自洽。

但计划中贴出的 `OutboundPayload.h` 与测试代码，按本仓库的 Boost 1.91（非 standalone）**无法编译**——`boost::json::error_code` 不是公开类型，因此「照抄即绿」不成立；此外 V3/V4 的验收数字与可执行性存在硬伤。上述两项（B1/B2）修掉后即可直接实施，无需架构返工。

| 维度 | 评价 |
| :--- | :--- |
| 根因对应性 | ✅ 准确命中 RCA R1（整体投递 vs 单 outbound 契约错配） |
| 修复落点 | ✅ 单点接入，调用方零改动，覆盖全部 8 处调用点 |
| 编译可行性 | ❌ 原样不可编译（B1） |
| 验收可执行性 | ❌ V4 判据与生产日志级别冲突（B2） |
| 测试覆盖 | ⚠️ 5 类 rejected 形状中 3 类无用例锁定（S3） |
| 回滚成本 | ✅ 仅 3 个文件，无数据/配置迁移 |

---

## 1. 评审方法

1. **静态核对**：逐条核对方案引用的 file:line、调用点清单、CMake 片段与仓库现状是否一致。
2. **复刻编译**：把方案中的 `OutboundPayload.h` 与 `tests/test_outbound_payload.cpp` 逐字复制到 `C:\Users\dsm\AppData\Local\Temp\opencode\obp`，用 `g++ -std=c++17 -Wall -Wextra -pedantic` 编译，并链接 `gtest_main/gtest` + `libboost_json-gcc14-mt-x64-1_91.a`。
3. **变异测试**：把 `normalizeForAdo` 改成 no-op（`normalized = payload; return true;`），确认单测会变红，以判断断言是否承重。
4. **覆盖性核对**：`rg "addOutbound\("` 全仓扫描调用点，逐个判断输入形态。

---

## 2. 阻断项（Must Fix）

### B1. `boost::json::error_code` 不存在 → 头文件与单测全部编译失败

| 项 | 内容 |
| :--- | :--- |
| 证据（方案） | 头文件两处、单测一处均写 `boost::json::error_code ec;` |
| 证据（实测） | `error: 'error_code' is not a member of 'boost::json'; did you mean 'make_error_code'?` |
| 原因 | Boost.JSON 仅在 `BOOST_JSON_STANDALONE` 下自带 `error_code`；本仓库使用 boost::system 版本，非抛出版 `parse` 签名为 `parse(string_view, boost::system::error_code&, ...)` |
| 后果 | Task 2 Step 2 的「10/10 PASS」不可达；Task 1 Step 3 的预期（仅报 `OutboundPayload.h: No such file`）会掩盖该错误 |
| 修复 | 三处统一改为 `boost::system::error_code ec;`；建议显式 `#include <boost/system/error_code.hpp>` |

修复后实测：`-Wall -Wextra -pedantic` 下 0 error 0 warning，链接不需要 `-lws2_32`，6/6 PASS。其余 Boost.JSON 用法全部正确（`if_contains` const 重载 `object.hpp:1227`、`array::push_back`、`object::operator[] = array` `value.hpp:859`、`serialize(object const&)`、`parse(string_view, error_code&)`）。

### B2. V4 要求在 WARN 级日志里找 DEBUG 行 → 验收按原文执行必然「失败」

| 项 | 内容 |
| :--- | :--- |
| 证据 | 方案要求日志出现 `addOutbound normalized payload: 3 outbounds -> 1`；该行写死 `LogLevel::DEBUG`；而 RCA §1.1 记录现场 `log.file_level = WARN` |
| 后果 | 实施者用现场配置跑 V4，`existing tag found` 已为 0，但 DEBUG 行永不落盘，会被判为「修复未生效」，进而怀疑代码 |
| 修复 | 拆为 V4a（WARN 级即可判定：`existing tag found` 计数 0 + 原 13 样本至少 5 个产出延迟/OK-FAIL + `xray api lso` 含 `proxy`）与 V4b（仅本轮临时把 `log.file_level` 调为 `DEBUG`，确认归一化行后恢复 `WARN`） |

---

## 3. 重要问题（Should Fix）

| 编号 | 问题 | 证据 | 处置 |
| :--- | :--- | :--- | :--- |
| S1 | ctest 基线数字漏加新增用例 | `ctest -N` 实测 54 个测试；新增 `OutboundPayloadTest` 后为 55；方案两处仍写 `52/54` | 改为 `53/55（= 基线 52/54 + 新增 1 个通过）` |
| S2 | D1 调用点计数与实际不符 | 实际 8 处：整份配置 5 处（`ProxyBatchTester.cpp:274/:333`、`ProxyFinder.cpp:370`、`XrayApi.cpp:2506/:2533`），单 outbound 3 处（`ProxyProbePool.cpp:294`、`StandaloneProxyPool.cpp:156/:281`）；方案写「7 处 / 4 处」 | 改为「共 8 处，其中 5 处传入整份配置」 |
| S3 | 单测覆盖与 D3 声明不匹配 | D3 列出 5 类 false 形状，实际只有 3 个 rejected 用例；`outbounds` 非数组、首元素非 object、裸 outbound 对象均无锚定 | 追加 4 个用例（`OutboundsNotArrayReturnsFalse` / `FirstElementNotObjectReturnsFalse` / `BareOutboundObjectReturnsFalse` / `EmptyPayloadReturnsFalse`），并在 FullConfig 用例追加 `direct`/`blackhole` 不存在断言 |
| S4 | INDEX 计数行指令自相矛盾且已过期 | `docs/INDEX.md:30` 写 80，实际 `docs/specs/*.md` 为 84；方案同时写「+1」与「按实际文件数更新」 | 删除「+1」，改为「按 `(Get-ChildItem docs/specs -Filter *.md).Count` 实测值更新」 |
| S5 | 「已用真实 xray 验证」措辞超出实验内容 | RCA §4 用例 D 的载荷是 `protocol:"freedom"`，并非 hysteria；单条 hysteria 能否被 ado 接受仍属间接证据 | 风险表改为「RCA 用例 D/E 仅证明单 outbound 文档可被 ado 接受；hysteria 单条由 V4 现场验证」，并在 V4 增加 `lso` 断言 |
| S6 | `include/XrayApi.h:31` 契约注释未同步 | 行为将变为「入口重写载荷」，但头文件仍只描述「CLI fallback」；方案文件清单也缺该头文件 | 补契约注释（归一化为 `{"outbounds":[outbounds[0]]}`，明确多 outbound 不支持），并纳入 §3 文件清单与 Task 3 Files |

**测试有效性实测**：把 `normalizeForAdo` 改成 no-op 后，原有 6 个用例中 4 个失败（FullConfig / EmptyOutbounds / NonObjectRoot / InvalidJson），说明既有断言是承重的，不是摆设。

---

## 4. 建议（Nice to Have，不阻断）

| 编号 | 建议 | 结论 |
| :--- | :--- | :--- |
| N1 | 性能：一次解析即可（`normalizeForAdo(..., std::size_t* outCount = nullptr)`） | 实测 Boost 解析约 0.1 ms，而单次 ado 子进程 0.2–0.5 s，占比 < 0.1%；**不构成问题**，现有双函数形态可接受 |
| N2 | 归一化后校验 `outbounds[0].tag == tag`，不一致时 WARN | 当前 8 处调用点全部一致（`proxy` / `px-<id>`）；**列为后续增强**，本次不扩大范围 |
| N3 | 显式 `#include <boost/system/error_code.hpp>` | **采纳**（避免依赖传递包含） |
| N4 | `single.push_back(boost::json::value(list.front().get_object()))` → `single.push_back(list.front())` | **采纳**（语义等价，少一次临时对象） |
| N5 | 归一化块补注释，说明与 `parseOutboundJson` 的 `outbounds[0]` 对齐 | **采纳**（防止后人当作冗余代码删除） |

---

## 5. 已核实项（覆盖性确认）

1. **行号引用全部准确**：`addOutbound` 153-218、`parseOutboundJson` 701-799、`addOutboundDirect` 2445-2615、`removeOutbound` 220-259。
2. **调用点覆盖完整**：整份配置 5 处 + 单 outbound 3 处 = 8 处，无遗漏；**裸 outbound 调用点在全仓（src + tests）不存在**，因此 D3「不自动包裹」不会回归。
3. **3 个「已正确」调用点不被破坏**：`ProxyProbePool.cpp:271-273`、`StandaloneProxyPool.cpp:146-150`、`:273-277` 均显式构造 `{"outbounds":[ob]}`，归一化后重新序列化语义等价，tag 一致。
4. **与 gRPC 语义一致**：`parseOutboundJson` 读 `outRoot.at("outbounds").at(0)`（`XrayApi.cpp:716-717`），归一化取 `outbounds[0]`，G1 成立。
5. **`outbounds[0]` 确实是被测代理**：`buildStandaloneConfig` 先 push proxy（`ConfigBuilder.cpp:586-594`），再 direct（`:597-600`）、block（`:603-606`）。
6. **两处内部回退确实被修好**：`XrayApi.cpp:2506`（无编码器）与 `:2533`（服务端类协议编码为空）都传调用方原始 JSON，归一化在 `addOutbound` 内部生效。
7. **既有测试不受影响**：`tests/test_xray_api_direct.cpp:1543/1554` 传 `{"outbounds":[]}` → 归一化返回 false → 原样透传 → 仍 `EXPECT_FALSE`，`hookCount` 断言不变。
8. **CMake 片段符合仓库约定**：`${VCPKG_ROOT}` 在 `CMakeLists.txt:61` / `102-104` 已定义，静态库路径与 `:240`、`:690`、`:968` 同款；`add_test(NAME OutboundPayloadTest)` 与现有 54 个测试无重名。
9. **合规性**：方案代码无 `auto`；测试位于 `tests/`；文档命名符合 `AGENTS.md §6.1`；`docs/INDEX.md` §7.5 编号 54 空闲。
10. **回滚面**：仅 `include/OutboundPayload.h` + `src/XrayApi.cpp` + `CMakeLists.txt`（+ 文档），`git revert` 可完全回退。

---

## 6. 处置结论

| 类别 | 条目 | 处置 |
| :--- | :--- | :--- |
| 阻断 | B1、B2 | **全部采纳**，已修订进方案 v1.0 正文 |
| 重要 | S1–S6 | **全部采纳** |
| 建议 | N3、N4、N5 | 采纳 |
| 建议 | N1、N2 | 记录为后续增强（N2 涉及行为面扩大，不在 P0 范围） |

修订后方案可直接进入实施；实施完成后建议做一次**实施后复评**，重点核对：单测实际通过的用例数、全量 `ctest` 分母、以及现场 ConfigType 7 批量是否 0 条 `existing tag found`。

---

## 7. 关联文档

| 文档 | 关系 |
| :--- | :--- |
| [`docs/specs/2026-10-20-Spec-XrayApi-AdoPayloadNormalization-v1.0.md`](../specs/2026-10-20-Spec-XrayApi-AdoPayloadNormalization-v1.0.md) | 评审对象 |
| [`docs/bugfix/2026-10-20-RCA-XrayApi-ExistingTagInjection-v1.0.md`](../bugfix/2026-10-20-RCA-XrayApi-ExistingTagInjection-v1.0.md) | 上游根因分析 |
| [`docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md`](../bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md) | 遗留问题 #1 来源 |
