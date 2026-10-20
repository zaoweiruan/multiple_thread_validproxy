# XrayApi CLI 注入载荷归一化（RCA P0）实施方案

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `xray api ado` 的 CLI 回退路径只注入单个 outbound，与 gRPC 路径 `outbounds[0]` 的语义对齐，消除 `existing tag found: direct` 的必然失败。

**Architecture:** 新增纯头文件 `include/OutboundPayload.h`（namespace `outbound_payload`，inline 纯函数，无项目内依赖，可独立单测），在 `XrayApi::addOutbound` 入口归一化一次，并把归一化后的同一载荷用于该函数内两处 `runProcess`；不改变任何调用方的数据结构与签名。

**Tech Stack:** C++17 / boost::json / GoogleTest / CMake + Ninja（MinGW GCC）

**依据文档：** `docs/bugfix/2026-10-20-RCA-XrayApi-ExistingTagInjection-v1.0.md`（R1 契约错配 / P0 修复方向）

**约束：** 全栈禁止使用 `auto` 进行类型推导（AGENTS.md §1）；测试源码只允许放在 `tests/`。

---

## 0. 背景与目标

### 0.1 问题回顾（RCA R1）

`ConfigGenerator::generateConfig()` 把**整份 Standalone 配置**（`log` / `inbounds` / `outbounds: [proxy, direct, block]` / `routing`）写入 `Config.outbound_json`。

- gRPC 路径 `XrayApi::addOutboundDirect()` → `parseOutboundJson()` **只读取 `outbounds[0]`**，因此正常工作；
- 当协议没有 protobuf 编码器（如 `hysteria` / ConfigType 7）时，`addOutboundDirect()` 回退到 `XrayApi::addOutbound()`，该函数把**整份 JSON** 通过 stdin 交给 `xray api ado`，ado 会逐个注入数组内的所有 outbound → 与测试实例中永远存在的 `direct` 冲突 → `exit 1`，报 `existing tag found: direct`；
- 且 `ado` 在失败前已成功注入 `proxy`（部分成功），调用方视为整体失败，`proxy` 残留。

现场证据：`bin/worker/log/ui_20261020_092411.log` 中 13/13 失败对象均为 ConfigType 7；库内 Hysteria2 存量 3,077 条。

### 0.2 目标

| 编号 | 目标 | 验收方式 |
| :--- | :--- | :--- |
| G1 | CLI 路径注入的载荷恒为「仅含 1 个 outbound」的文档，且该 outbound 与 gRPC 路径选取的 `outbounds[0]` 完全一致 | 单测 `FullConfigIsReducedToFirstOutbound` |
| G2 | 已是单 outbound 的载荷保持幂等（不改变语义、不重复包装） | 单测 `SingleOutboundPayloadIsIdempotent` |
| G3 | 对异常/不可解析载荷保持既有行为（原样透传，由 ado 报错），不引入新的崩溃路径 | 单测 7 个 rejected 分支 + 现有回归 |

### 0.3 非目标（本次不做）

| 编号 | 非目标 | 归属 |
| :--- | :--- | :--- |
| N1 | 内部空输出重试前先 `removeOutbound(tag)` | RCA P1 |
| N2 | 三次重试耗尽后清理残留 tag | RCA P2 |
| N3 | 为 Hysteria2/TUIC/WireGuard 补 protobuf 编码器 | RCA 明确不建议 |
| N4 | 修改 `ConfigGenerator` 的 `outbound_json` 语义或新增 `single_outbound_json` 字段 | 本次选择在 XrayApi 侧单点归一化（见 D1） |

---

## 1. 设计决策

### D1：归一化位置选在 `XrayApi::addOutbound`，而不是调用方或 `ConfigGenerator`

`addOutbound` 的全部调用点共 8 处，其中 5 处传入整份配置（`ProxyBatchTester.cpp:274`、`:333`，`ProxyFinder.cpp:370`，以及 `XrayApi.cpp:2506`、`:2533` 两处内部回退），3 处传入已是单 outbound 的文档（`ProxyProbePool.cpp:294`、`StandaloneProxyPool.cpp:156`、`:281`）。在 `addOutbound` 入口统一收敛：一处修复覆盖全部路径，且调用方无需感知 ado 的语义差异。若在 `ConfigGenerator` 新增字段，则 `ProxyFinder` 与所有内部回退点仍需各自改造，属于多点修改。

### D2：新增纯头文件 `include/OutboundPayload.h`

沿用 `include/BatchWorkerPolicy.h` 的成功先例：纯 inline 函数、仅依赖 boost::json，单测可直接包含头文件，无需链接 XrayApi/网络/数据库。避免把逻辑塞进 1,000+ 行的 `XrayApi.cpp` 而无法独立验证。

### D3：只接受「object 根 + `outbounds` 数组 + 首元素为 object」的载荷

| 输入形状 | 处理 | 理由 |
| :--- | :--- | :--- |
| `{"outbounds":[{...}, {...}]}` | 归一化为仅首元素 | 本次缺陷主体 |
| `{"log":...,"outbounds":[{...}]}` | 归一化为 `{"outbounds":[{...}]}` | 整份配置的正常形态 |
| `{"outbounds":[]}` | 返回 false，原样透传 | 无 outbound 可注入，交给 ado 报错，保持既有行为 |
| `[1,2]` / `"x"` / 非法 JSON | 返回 false，原样透传 | 不做猜测性修复 |
| 裸 outbound 对象（无 `outbounds`） | 返回 false，原样透传 | 当前无任何调用点使用该形状，不扩大契约 |

### D4：归一化后丢弃除 `outbounds` 外的所有顶层键

`xray api ado` 只消费 `outbounds` 数组；`inbounds`/`routing`/`log` 对其无意义。丢弃可让调试日志中的载荷体积从数十 KB 降到单节点大小，并避免未来 ado 版本对意外顶层键的严格校验。

### D5：归一化失败不阻断流程，只保持原行为

`normalizeForAdo` 返回 false 时继续使用原始载荷，不新增错误分支、不改变返回值语义。这样修复只做「减法」，不引入新的失败面。

---

## 2. 接口契约

```cpp
namespace outbound_payload {

// 将任意「可能包含多个 outbound 的 ado 载荷」归一化为仅含首元素的文档。
// 成功：normalized = {"outbounds":[<首个元素>]}，返回 true。
// 失败：normalized 不被修改（调用方应保留原值），返回 false。
bool normalizeForAdo(const std::string& payload, std::string& normalized);

// 统计载荷中 outbounds 数组的长度；非法输入返回 0。仅用于日志。
std::size_t outboundCount(const std::string& payload);

} // namespace outbound_payload
```

不变式：

1. `normalizeForAdo` 成功时，输出必定是「根 object 且仅含 1 个键 `outbounds`，其值为长度 1 的数组」。
2. 失败时 `normalized` 保持调用前的值（不得被写空或写入半成品）。
3. 函数为纯函数：不读写全局状态、不打日志、不抛异常（boost::json 的 `parse(payload, ec)` 重载不抛）。
4. 两个函数均不得使用 `auto`。

---

## 3. 文件清单

| 文件 | 动作 | 职责 |
| :--- | :--- | :--- |
| `include/OutboundPayload.h` | 新建 | 归一化与计数纯函数 |
| `tests/test_outbound_payload.cpp` | 新建 | 10 个单测用例（3 个正常路径 + 7 个 rejected 分支） |
| `CMakeLists.txt` | 修改（追加测试目标） | 构建 `test_outbound_payload` 并注册 `OutboundPayloadTest` |
| `src/XrayApi.cpp` | 修改（include + 2 处 `runProcess` 参数） | 接入归一化 |
| `include/XrayApi.h` | 修改 | `addOutbound` 契约注释：入口会把多 outbound 载荷归一化为 `{"outbounds":[outbounds[0]]}` |
| `docs/INDEX.md` | 修改 | §7.5 追加 #54，并更新计数 |
| `docs/plans/project-plans-tracker.md` | 修改 | 追加方案行 |
| `docs/context.md` | 修改 | §三 追加条目 |

---

## 4. 实施任务

### Task 1：先写失败的单测与构建目标

**Files:**
- Create: `tests/test_outbound_payload.cpp`
- Modify: `CMakeLists.txt`（在 `# -- Batch worker lifecycle policy tests` 区块之后追加）

- [ ] **Step 1: 新建测试文件（完整内容）**

```cpp
#include <gtest/gtest.h>

#include <string>

#include <boost/json.hpp>

#include "OutboundPayload.h"

namespace {

const char* kFullConfig = R"({
  "log": {"loglevel": "warning"},
  "inbounds": [{"tag": "socks-in", "port": 10808, "protocol": "socks"}],
  "outbounds": [
    {"tag": "proxy", "protocol": "hysteria", "settings": {"address": "1.2.3.4", "port": 443}},
    {"tag": "direct", "protocol": "freedom"},
    {"tag": "block", "protocol": "blackhole"}
  ],
  "routing": {"rules": []}
})";

const char* kSingleOutbound = R"({"outbounds":[{"tag":"proxy","protocol":"freedom"}]})";

} // namespace

TEST(OutboundPayloadTest, FullConfigIsReducedToFirstOutbound) {
    std::string normalized;
    ASSERT_TRUE(outbound_payload::normalizeForAdo(kFullConfig, normalized));

    boost::system::error_code ec;
    boost::json::value parsed = boost::json::parse(normalized, ec);
    ASSERT_FALSE(ec);
    ASSERT_TRUE(parsed.is_object());

    boost::json::object const& root = parsed.get_object();
    EXPECT_EQ(root.size(), static_cast<std::size_t>(1));

    boost::json::value const* outbounds = root.if_contains("outbounds");
    ASSERT_NE(outbounds, nullptr);
    ASSERT_TRUE(outbounds->is_array());

    boost::json::array const& list = outbounds->get_array();
    ASSERT_EQ(list.size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(list.front().is_object());

    boost::json::object const& first = list.front().get_object();
    EXPECT_EQ(std::string(first.at("tag").as_string()), std::string("proxy"));
    EXPECT_EQ(std::string(first.at("protocol").as_string()), std::string("hysteria"));

    // 锁定现场签名：direct/block 不得出现在 ado 载荷中。
    EXPECT_EQ(normalized.find("direct"), std::string::npos);
    EXPECT_EQ(normalized.find("blackhole"), std::string::npos);
}

TEST(OutboundPayloadTest, SingleOutboundPayloadIsIdempotent) {
    std::string firstPass;
    std::string secondPass;
    ASSERT_TRUE(outbound_payload::normalizeForAdo(kSingleOutbound, firstPass));
    ASSERT_TRUE(outbound_payload::normalizeForAdo(firstPass, secondPass));
    EXPECT_EQ(firstPass, secondPass);
    EXPECT_EQ(outbound_payload::outboundCount(firstPass), static_cast<std::size_t>(1));
}

TEST(OutboundPayloadTest, EmptyOutboundsReturnsFalse) {
    std::string normalized = "sentinel";
    EXPECT_FALSE(outbound_payload::normalizeForAdo("{\"outbounds\":[]}", normalized));
    EXPECT_EQ(normalized, std::string("sentinel"));
}

TEST(OutboundPayloadTest, NonObjectRootReturnsFalse) {
    std::string normalized = "sentinel";
    EXPECT_FALSE(outbound_payload::normalizeForAdo("[{\"tag\":\"proxy\"}]", normalized));
    EXPECT_EQ(normalized, std::string("sentinel"));
}

TEST(OutboundPayloadTest, InvalidJsonReturnsFalse) {
    std::string normalized = "sentinel";
    EXPECT_FALSE(outbound_payload::normalizeForAdo("{not json", normalized));
    EXPECT_EQ(normalized, std::string("sentinel"));
}

TEST(OutboundPayloadTest, OutboundsNotArrayReturnsFalse) {
    std::string normalized = "sentinel";
    EXPECT_FALSE(outbound_payload::normalizeForAdo("{\"outbounds\":{}}", normalized));
    EXPECT_EQ(normalized, std::string("sentinel"));
}

TEST(OutboundPayloadTest, FirstElementNotObjectReturnsFalse) {
    std::string normalized = "sentinel";
    EXPECT_FALSE(outbound_payload::normalizeForAdo("{\"outbounds\":[1]}", normalized));
    EXPECT_EQ(normalized, std::string("sentinel"));
}

TEST(OutboundPayloadTest, BareOutboundObjectReturnsFalse) {
    std::string normalized = "sentinel";
    // 锁定 D3：裸 outbound 对象不被自动包裹（当前无调用方使用该形状）。
    EXPECT_FALSE(outbound_payload::normalizeForAdo("{\"tag\":\"proxy\",\"protocol\":\"freedom\"}", normalized));
    EXPECT_EQ(normalized, std::string("sentinel"));
}

TEST(OutboundPayloadTest, EmptyPayloadReturnsFalse) {
    std::string normalized = "sentinel";
    EXPECT_FALSE(outbound_payload::normalizeForAdo("", normalized));
    EXPECT_EQ(normalized, std::string("sentinel"));
}

TEST(OutboundPayloadTest, OutboundCountReportsAllOutbounds) {
    EXPECT_EQ(outbound_payload::outboundCount(kFullConfig), static_cast<std::size_t>(3));
    EXPECT_EQ(outbound_payload::outboundCount(kSingleOutbound), static_cast<std::size_t>(1));
    EXPECT_EQ(outbound_payload::outboundCount("{not json"), static_cast<std::size_t>(0));
    EXPECT_EQ(outbound_payload::outboundCount("{\"outbounds\":[]}"), static_cast<std::size_t>(0));
}
```

- [ ] **Step 2: 在 `CMakeLists.txt` 追加测试目标（完整内容）**

插入位置：紧跟 `add_test(NAME BatchWorkerPolicyTest COMMAND test_batch_worker_policy)` 之后一行。

```cmake
# -- Outbound payload normalization tests (pure logic + boost::json) --
add_executable(test_outbound_payload tests/test_outbound_payload.cpp)
target_include_directories(test_outbound_payload PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_outbound_payload PRIVATE gtest_main gtest ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a)
set_target_properties(test_outbound_payload PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME OutboundPayloadTest COMMAND test_outbound_payload)
```

- [ ] **Step 3: 配置并编译，确认红灯**

Run:
```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8 --target test_outbound_payload
```

Expected: 编译失败，错误形如 `fatal error: OutboundPayload.h: No such file or directory`（说明测试先于实现存在，红灯成立）。

- [ ] **Step 4: 提交红灯（可选，建议与 Task 2 合并提交）**

不单独提交；红灯证据保留在终端输出中即可。

---

### Task 2：实现纯头文件

**Files:**
- Create: `include/OutboundPayload.h`

- [ ] **Step 1: 新建头文件（完整内容）**

```cpp
#pragma once

#include <cstddef>
#include <string>
#include <boost/system/error_code.hpp>
#include <boost/json.hpp>

namespace outbound_payload {

// Reduce an `xray api ado` payload to exactly one outbound (the first element),
// matching the semantics of the gRPC path which only consumes outbounds[0].
// Returns false (leaving `normalized` untouched) when the payload cannot be
// interpreted as {"outbounds":[{...}, ...]}.
// 与 XrayApi::parseOutboundJson 的 outbounds[0] 语义严格对齐（src/XrayApi.cpp:716-717）。
// 与 XrayApi::parseOutboundJson 的 outbounds[0] 语义严格对齐（src/XrayApi.cpp:716-717）。
inline bool normalizeForAdo(const std::string& payload, std::string& normalized) {
    boost::system::error_code ec;
    boost::json::value parsed = boost::json::parse(payload, ec);
    if (ec || !parsed.is_object()) {
        return false;
    }

    boost::json::object const& root = parsed.get_object();
    boost::json::value const* outbounds = root.if_contains("outbounds");
    if (outbounds == nullptr || !outbounds->is_array()) {
        return false;
    }

    boost::json::array const& list = outbounds->get_array();
    if (list.empty() || !list.front().is_object()) {
        return false;
    }

    boost::json::array single;
    single.push_back(list.front());

    boost::json::object out;
    out["outbounds"] = single;

    normalized = boost::json::serialize(out);
    return true;
}

// Number of entries in the top-level outbounds array; 0 for malformed input.
inline std::size_t outboundCount(const std::string& payload) {
    boost::system::error_code ec;
    boost::json::value parsed = boost::json::parse(payload, ec);
    if (ec || !parsed.is_object()) {
        return 0;
    }

    boost::json::object const& root = parsed.get_object();
    boost::json::value const* outbounds = root.if_contains("outbounds");
    if (outbounds == nullptr || !outbounds->is_array()) {
        return 0;
    }

    return outbounds->get_array().size();
}

} // namespace outbound_payload
```

- [ ] **Step 2: 编译并运行新测试，确认全绿**

Run:
```powershell
cmake --build build --parallel 8 --target test_outbound_payload
ctest -R OutboundPayloadTest -V
```

Expected: `10 tests from OutboundPayloadTest` 全部 PASS，`100% tests passed, 0 tests failed out of 1`。

- [ ] **Step 3: 提交**

```bash
git add include/OutboundPayload.h tests/test_outbound_payload.cpp CMakeLists.txt
git commit -m "feat(xrayapi): add ado payload normalizer with unit tests"
```

---

### Task 3：接入 `XrayApi::addOutbound`

**Files:**
- Modify: `src/XrayApi.cpp`（文件顶部 include 区；`XrayApi::addOutbound` 内 `std::string output;` 之前；两处 `runProcess` 调用）
- Modify: `include/XrayApi.h:31`（补充契约注释，S6）

- [ ] **Step 1: 添加 include**

在 `src/XrayApi.cpp` 现有项目头文件 include 区（与 `#include "XrayApi.h"` 相邻处）添加：

```cpp
#include "OutboundPayload.h"
```

- [ ] **Step 2: 在 `addOutbound` 内插入归一化逻辑**

插入位置：`Logger::write("[XrayApi] command: " + cmd, LogLevel::DEBUG);` 之后、`std::string output;` 之前。

```cpp
    std::string adoPayload = outboundJson;
    std::string normalizedPayload;
    if (outbound_payload::normalizeForAdo(outboundJson, normalizedPayload)) {
        const std::size_t totalOutbounds = outbound_payload::outboundCount(outboundJson);
        if (totalOutbounds > 1) {
            Logger::write("[XrayApi] addOutbound normalized payload: " +
                          std::to_string(totalOutbounds) + " outbounds -> 1 (tag=" + tag + ")",
                          LogLevel::DEBUG);
        }
        adoPayload = normalizedPayload;
    }
```

- [ ] **Step 3: 两处 `runProcess` 改用归一化载荷**

将 `XrayApi::addOutbound` 内的两处：

```cpp
    int exitCode = runProcess(cmd, output, &outboundJson);
```

改为：

```cpp
    int exitCode = runProcess(cmd, output, &adoPayload);
```

以及空输出重试处（同一函数内）：

```cpp
            exitCode = runProcess(cmd, output, &outboundJson);
```

改为：

```cpp
            exitCode = runProcess(cmd, output, &adoPayload);
```

注意：`addOutboundDirect` 中传给 `addOutbound(outboundJson, tag, resultOutput)` 的入参不改；归一化只在 `addOutbound` 内部发生。

- [ ] **Step 4: 编译并跑相关测试**

Run:
```powershell
cmake --build build --parallel 8
ctest -R "OutboundPayloadTest|BatchWorkerPolicyTest" -V
```

Expected: `OutboundPayloadTest` 10/10 PASS，`BatchWorkerPolicyTest` 11/11 PASS。

- [ ] **Step 5: 提交**

```bash
git add src/XrayApi.cpp
git commit -m "fix(xrayapi): normalize ado payload to a single outbound"
```

---

### Task 4：文档登记与全量验收

**Files:**
- Modify: `docs/INDEX.md`（§7.5 规范化设计表追加一行；表头计数行按实测值更新（`(Get-ChildItem docs/specs -Filter *.md).Count`，当前 84（已含本文档）））
- Modify: `docs/plans/project-plans-tracker.md`（## 近期文档引用 追加一行）
- Modify: `docs/context.md`（§三 追加一行）

- [ ] **Step 1: 登记 `docs/INDEX.md`**

在 §7.5 规范化设计表格末尾追加（编号取当前最大值 +1 = 54）：

```markdown
| 54 | [2026-10-20-Spec-XrayApi-AdoPayloadNormalization-v1.0.md](specs/2026-10-20-Spec-XrayApi-AdoPayloadNormalization-v1.0.md) | **XrayApi CLI 注入载荷归一化（RCA P0）** — 新增 `include/OutboundPayload.h` 纯函数，在 `XrayApi::addOutbound` 入口把整份 Standalone 配置归一化为仅含 `outbounds[0]` 的文档，使 CLI 回退路径与 gRPC 语义一致，消除 `existing tag found: direct` | ✅ 已完成 | — |
```

并按 `docs/specs/*.md` 的实际文件数更新 INDEX 顶部「规范化设计」计数行。

- [ ] **Step 2: 登记 `docs/plans/project-plans-tracker.md`**

在 `## 近期文档引用` 表格末尾追加：

```markdown
| 2026-10-20 | spec | docs/specs/2026-10-20-Spec-XrayApi-AdoPayloadNormalization-v1.0.md | **RCA P0 实施方案** — CLI 回退载荷归一化为单 outbound；新增 OutboundPayload.h + 6 单测 + XrayApi 单点接入 | ✅ completed |
```

- [ ] **Step 3: 登记 `docs/context.md`**

在 §三 表格顶部追加：

```markdown
| 2026-10-20 | **XrayApi CLI 注入载荷归一化（RCA P0）** — `xray api ado` 只应收到单个 outbound；新增 `include/OutboundPayload.h`（`normalizeForAdo`/`outboundCount`），`XrayApi::addOutbound` 入口归一化并用于两处 runProcess，修复 Hysteria2 等无编码器协议必然 `existing tag found: direct` | `docs/specs/2026-10-20-Spec-XrayApi-AdoPayloadNormalization-v1.0.md` |
```

- [ ] **Step 4: 全量回归**

Run:
```powershell
ctest -V
```

Expected: 与基线一致（53/55（= 基线 53/55（= 基线 52/54 + OutboundPayloadTest 1 个通过；`ctest -N` 已核实分母 54） + OutboundPayloadTest 1 个通过；`ctest -N` 已核实分母 54）；已知 `XrayManagerStartupTest` 基线失败、`UI_FLOATINGWIDGET` 并行 flake），且 `OutboundPayloadTest` 在通过集合内。

- [ ] **Step 5: 提交文档**

```bash
git add docs/INDEX.md docs/plans/project-plans-tracker.md docs/context.md docs/specs/2026-10-20-Spec-XrayApi-AdoPayloadNormalization-v1.0.md
git commit -m "docs: register ado payload normalization spec"
```

---

## 5. 验证与验收准则

| 编号 | 准则 | 判定方式 |
| :--- | :--- | :--- |
| V1 | 归一化函数行为正确 | `ctest -R OutboundPayloadTest -V` → 10/10 PASS |
| V2 | 既有注入策略未被破坏 | `ctest -R BatchWorkerPolicyTest -V` → 11/11 PASS |
| V3 | 无全局回归 | `ctest -V` 结果与基线 53/55（= 基线 53/55（= 基线 52/54 + OutboundPayloadTest 1 个通过；`ctest -N` 已核实分母 54） + OutboundPayloadTest 1 个通过；`ctest -N` 已核实分母 54） 一致，无新增失败 |
| V4a | 现场修复有效（WARN 级即可判定） | 用 `bin/` 最新产物对 ConfigType 7 节点跑一轮：① `Select-String -SimpleMatch "existing tag found" <本次日志>` 计数 = 0；② 原 13 个样本中至少 5 个出现延迟/OK-FAIL 结果而非 `XRAY_ERROR`；③ 注入后 `xray api lso --server <api>` 输出含 `proxy` |
| V4b | 归一化确实发生 | 仅本轮把 `bin/config.json` 的 `log.file_level` 临时改为 `DEBUG`，跑完恢复 `WARN`；日志出现 `addOutbound normalized payload: 3 outbounds -> 1` |
| V5 | 无 `auto` | `git diff` 全量扫描，新增代码 0 处 `auto` |

> V4 说明：`bin/worker/validproxy.exe` 是陈旧副本，禁止作为验证对象（AGENTS.md §4.1）。

---

## 6. 风险与回滚

| 风险 | 评估 | 缓解 |
| :--- | :--- | :--- |
| 归一化误伤已正确的单 outbound 调用点 | 低——单元素数组归一化后等价 | 单测 `SingleOutboundPayloadIsIdempotent` |
| 丢弃顶层键导致 ado 报错 | 极低——ado 仅消费 `outbounds` | RCA 用例 D/E 证明**单 outbound 文档**可被 ado 接受；hysteria 单条由 V4 现场验证 |
| 非法载荷行为变化 | 无——返回 false 时原样透传 | 4 个 rejected 单测 |
| 回滚 | 只需 revert 3 个文件（头文件、XrayApi.cpp、CMakeLists.txt）；无数据/配置迁移 | `git revert <commit>` |

---

## 7. 后续（不在本次范围）

- P1：`addOutbound` 内部空输出重试前先 `removeOutbound(tag)`，或直接删除内部重试（外层已有「删除 + 重试」）。
- P2：三次重试耗尽后清理残留 tag，避免污染下一个代理。
- P3：为 `ProxyBatchTester` 增加「注入载荷恰好 1 个 outbound」的集成断言。
- 明确不补 Hysteria2 的 protobuf 编码器：CLI 本身可注入 `hysteria`，补编码器只会掩盖本次契约缺陷。
