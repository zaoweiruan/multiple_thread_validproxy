# Hysteria2 gRPC 编码器（CLI 回退消除）实施方案

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Hysteria2（ConfigType 7）补齐 outbound 容器层与传输层两处 protobuf 编码器，使 `XrayApi::addOutboundDirect` 不再回退到 `xray api ado` 子进程，从而消除注入路径的进程开销。

**Architecture:** 新增纯头文件 `include/HysteriaPayload.h`（JSON 字段抽取与校验，零依赖，沿用 `OutboundPayload.h` 先例，便于免链接单测）；在 `src/XrayApi.cpp` 内新增成员编码器 `encodeHysteriaSettings`，并补齐 `parseOutboundJson` 协议映射、`jsonConfigToProtobuf` hysteria 分支、`encodeStreamConfig` 传输层分支、`supportedTypeUrls` 白名单与空值保护列表共 5 处。**不改动任何调用方签名**。

**Tech Stack:** C++17 / boost::json / GoogleTest / CMake + Ninja / xray-core v26.3.27 protobuf 线格式

---

## 0. 背景与目标

### 0.1 背景（来源：调查报告）

见 `docs/reports/2026-10-20-Report-Hysteria2-GrpcEncoderFeasibility-v1.0.md`：

- xray-core v26.3.27 **有** Hysteria2 的 proto 定义（`xray.proxy.hysteria.ClientConfig`），也有传输层定义（`xray.transport.internet.hysteria.Config`）；
- CLI `ado` 与 gRPC `AddOutbound` **共用同一 proto**，差别只在 JSON→proto 由谁完成；
- 本项目缺编码器 → `supportedTypeUrls` 未命中 → 回退 CLI（`src/XrayApi.cpp:2519-2523`），每条 Hysteria2 注入都要付出一次子进程开销（实测约 45×）；
- **两层缺一不可**：只补容器层会"注入成功、缺 `auth` 运行必坏"。

### 0.2 目标

| 编号 | 目标 | 验收方式 |
| :-: | :--- | :--- |
| **G1** | Hysteria2 走 gRPC 直连，不再回退 CLI | 现场跑 ConfigType 7，日志中**无** `fallback to subprocess for xray.proxy.hysteria.ClientConfig` |
| **G2** | 两层编码器字节级正确（version=2、server address/port、transport auth） | `HysteriaEncoderTest` 10/10（抽取层）+ `XrayApiDirectTest` 新增 4 条线格式用例（S1：容器 `080212140a0f120d612e6578616d706c652e636f6d10bb03`、传输 `{"version":2,"auth":"6f1c-4a2b"}` → `08021209366631632d34613262`、version≠2 → 空、缺 `hysteriaSettings` → 回退 CLI） |
| **G3** | 不破坏既有协议 | 全量 `ctest` 无新增失败（基线 54/55，唯一失败 `XrayManagerStartupTest`） |

### 0.3 非目标（本次不做）

| 编号 | 非目标 | 理由 |
| :-: | :--- | :--- |
| **N1** | 为 TUIC / WireGuard / Anytls / Naive 补编码器 | 无证据、无需求，YAGNI |
| **N2** | 修复 `Hysteria2OutboundBuilder` 的 TLS 缺失风险（sni 空且 allowinsecure!=1 时不输出 tlsSettings） | 独立议题，需单独 Spec（调查报告 §7.4） |
| **N3** | 移除 CLI 回退通道本身 | 非 gRPC 构建下它是唯一通道（`include/XrayApi.h:31-34` 在 `#ifdef` 之外） |
| **N4** | 加密/混淆（`finalmask`、`masq_*`）字段**编码**（本次只做「检测到即回退」，不做编码） | 上游 v26 语义未稳定；B2 已改为携带 `finalmask` 的 hysteria 直接回退 CLI，等价于既有行为，不引入未声明变更 |

---

## 1. 设计决策

| 编号 | 决策 | 理由 |
| :-: | :--- | :--- |
| **D1** | 纯逻辑（JSON 抽取与校验）放新头文件 `include/HysteriaPayload.h`，proto 字节组装留在 `XrayApi` 私有成员 | 沿用 `include/OutboundPayload.h` 先例；抽取逻辑可免链接单测；字节组装复用既有 `encodeVarintField` / `encodeString` / `encodeServerEndpoint`，避免重复实现 |
| **D2** | 校验规则严格对齐上游：`version != 2` → 不编码 | 上游 `infra/conf/hysteria.go` 直接 `errors.New("version != 2")`；提前失败可走空值保护回退，而不是注入坏 outbound |
| **D3** | 忽略 `hysteriaSettings` 中的 `up` / `down` / `congestion` / `udphop` | 上游 v26 已将其移出 proto（仅告警）；编码它们会写入未知字段 |
| **D4** | 容器层 `server` 使用 **field 2**（不是 server-class 的 field 1） | `xray.proxy.hysteria.ClientConfig { int32 version = 1; ServerEndpoint server = 2; }` |
| **D5** | 容器层 server **不得**复用 `encodeServerEndpoint`，改为内联只写 field 1/2 | 该函数**无条件**写出 field 3（`ep += encodeLengthDelimited(3, userProto)`，src/XrayApi.cpp:411-418）；零长度子消息会被解码成**非 nil** 的 `User{}` → `ToMemoryUser` 报 `Account is missing`（common/protocol/user.go:8-10）→ `NewServerSpecFromPB` 失败 → `AddOutbound` 返回非 0 status。批处理路径 `ProxyBatchTester.cpp:272-274` 在 `USE_GRPC_API` 下**没有** CLI 兜底，故写空 user 会让全部 ConfigType 7 变成 `XRAY_ERROR`，后果比回退 CLI 更严重（独立评审判定为阻断项 B1，已用 v26.3.27 真实 pb 解码实测：带 `1a00` 得 `Account is missing`，去掉后 `err=nil`） |
| **D6** | 同时在 `supportedTypeUrls`（:2503-2513）与空值保护列表（:2534-2550）追加 hysteria | 前者开启 gRPC 路径，后者保证编码失败时回退 CLI 而非注入残缺 outbound |

---

## 2. 接口契约

### 2.1 `include/HysteriaPayload.h`

```cpp
namespace hysteria_payload {

struct ClientConfigFields {
    int version;          // 恒为 2
    std::string address;  // 非空
    int port;             // 1..65535
};

struct TransportConfigFields {
    int version;          // 恒为 2
    std::string auth;     // 允许为空
};

// 从 outbound 的 settings 对象抽取容器层字段；失败返回 false 且不修改 out。
inline bool extractClientConfig(const boost::json::object& settings, ClientConfigFields& out);

// 从 streamSettings.hysteriaSettings 对象抽取传输层字段；失败返回 false 且不修改 out。
inline bool extractTransportConfig(const boost::json::object& hysteriaSettings, TransportConfigFields& out);

} // namespace hysteria_payload
```

### 2.2 行为表

| 输入形状 | `extractClientConfig` | 说明 |
| :--- | :--- | :--- |
| `{"address":"a.com","port":443,"version":2}` | true，字段原样 | builder 正常输出 |
| `{"address":"a.com","port":"443","version":2}` | true，port=443 | 字符串端口兼容 |
| `version` 缺失 / 非整数 | false | D2 |
| `version == 1` | false | D2 |
| `address` 缺失 / 非字符串 / 空串 | false | — |
| `port == 0` 或 `> 65535` | false | 防误注入 |
| 根不是对象 / 非法 JSON | 由调用方前置判断，不进入本函数 | — |

| 输入形状 | `extractTransportConfig` | 说明 |
| :--- | :--- | :--- |
| `{"version":2,"auth":"uuid","up":"100mbps","down":"100mbps"}` | true，version=2，auth="uuid"（up/down 被忽略） | D3 |
| `{"version":2}` | true，auth 为空串 | 允许无 auth 的服务端 |
| `version` 缺失 / 非整数 / == 1 | false | D2 |

---

## 3. 文件清单

| 文件 | 动作 | 说明 |
| :--- | :--- | :--- |
| `include/HysteriaPayload.h` | 新建 | 纯逻辑抽取与校验（~95 行） |
| `tests/test_hysteria_encoder.cpp` | 新建 | 10 个 GoogleTest 用例 |
| `CMakeLists.txt` | 修改 | 在 `add_test(NAME OutboundPayloadTest ...)` 之后新增 `test_hysteria_encoder` 目标 |
| `include/XrayApi.h` | 修改 | 在 `encodeSplitHTTPSettings` 声明附近新增 `static std::string encodeHysteriaSettings(const boost::json::object& hysteria);` |
| `src/XrayApi.cpp` | 修改 | include 头文件；`parseOutboundJson` 协议映射；`jsonConfigToProtobuf` 布尔位 + 分支；`encodeHysteriaSettings` 定义；`encodeStreamConfig` 两处分支；`supportedTypeUrls` + 空值保护 |
| `docs/INDEX.md` | 修改 | §7.5 新增本文件行（#54）+ §0 计数行；§9 分析报告新增调查报告行（#17） |
| `docs/plans/project-plans-tracker.md` | 修改 | 「近期文档引用」追加行 |
| `docs/context.md` | 修改 | §三 表顶部插入行 |

---

## 4. Tasks

### Task 1: 单测先行（红灯）

**Files:**
- Create: `tests/test_hysteria_encoder.cpp`
- Modify: `CMakeLists.txt`（在 `add_test(NAME OutboundPayloadTest COMMAND test_outbound_payload)` 之后插入）

- [ ] **Step 1: 写失败测试**

创建 `tests/test_hysteria_encoder.cpp`：

```cpp
#include <gtest/gtest.h>
#include <cstddef>
#include <string>
#include <boost/json.hpp>

#include "HysteriaPayload.h"

namespace {

boost::json::object parseObject(const std::string& text) {
    boost::system::error_code ec;
    boost::json::value parsed = boost::json::parse(text, ec);
    EXPECT_FALSE(static_cast<bool>(ec));
    EXPECT_TRUE(parsed.is_object());
    return parsed.as_object();
}

} // namespace

TEST(HysteriaEncoderTest, ClientConfigFromBuilderShape) {
    boost::json::object settings = parseObject(
        "{\"address\":\"a.example.com\",\"port\":443,\"version\":2}");

    hysteria_payload::ClientConfigFields fields{};
    ASSERT_TRUE(hysteria_payload::extractClientConfig(settings, fields));

    EXPECT_EQ(fields.version, 2);
    EXPECT_EQ(fields.address, std::string("a.example.com"));
    EXPECT_EQ(fields.port, 443);
}

TEST(HysteriaEncoderTest, ClientConfigAcceptsStringPort) {
    boost::json::object settings = parseObject(
        "{\"address\":\"a.example.com\",\"port\":\"8443\",\"version\":2}");

    hysteria_payload::ClientConfigFields fields{};
    ASSERT_TRUE(hysteria_payload::extractClientConfig(settings, fields));

    EXPECT_EQ(fields.port, 8443);
}

TEST(HysteriaEncoderTest, ClientConfigRejectsVersionOne) {
    boost::json::object settings = parseObject(
        "{\"address\":\"a.example.com\",\"port\":443,\"version\":1}");

    hysteria_payload::ClientConfigFields fields{};
    fields.version = -1;
    fields.address = "sentinel";
    fields.port = -1;

    EXPECT_FALSE(hysteria_payload::extractClientConfig(settings, fields));
    EXPECT_EQ(fields.version, -1);
    EXPECT_EQ(fields.address, std::string("sentinel"));
    EXPECT_EQ(fields.port, -1);
}

TEST(HysteriaEncoderTest, ClientConfigRejectsMissingVersion) {
    boost::json::object settings = parseObject(
        "{\"address\":\"a.example.com\",\"port\":443}");

    hysteria_payload::ClientConfigFields fields{};
    EXPECT_FALSE(hysteria_payload::extractClientConfig(settings, fields));
}

TEST(HysteriaEncoderTest, ClientConfigRejectsEmptyAddress) {
    boost::json::object settings = parseObject(
        "{\"address\":\"\",\"port\":443,\"version\":2}");

    hysteria_payload::ClientConfigFields fields{};
    EXPECT_FALSE(hysteria_payload::extractClientConfig(settings, fields));
}

TEST(HysteriaEncoderTest, ClientConfigRejectsZeroPort) {
    boost::json::object settings = parseObject(
        "{\"address\":\"a.example.com\",\"port\":0,\"version\":2}");

    hysteria_payload::ClientConfigFields fields{};
    EXPECT_FALSE(hysteria_payload::extractClientConfig(settings, fields));
}

TEST(HysteriaEncoderTest, ClientConfigRejectsPortAboveRange) {
    boost::json::object settings = parseObject(
        "{\"address\":\"a.example.com\",\"port\":70000,\"version\":2}");

    hysteria_payload::ClientConfigFields fields{};
    EXPECT_FALSE(hysteria_payload::extractClientConfig(settings, fields));
}

TEST(HysteriaEncoderTest, TransportConfigExtractsAuthAndIgnoresLegacyFields) {
    boost::json::object hysteriaSettings = parseObject(
        "{\"version\":2,\"auth\":\"6f1c-4a2b\",\"up\":\"100mbps\",\"down\":\"100mbps\"}");

    hysteria_payload::TransportConfigFields fields{};
    ASSERT_TRUE(hysteria_payload::extractTransportConfig(hysteriaSettings, fields));

    EXPECT_EQ(fields.version, 2);
    EXPECT_EQ(fields.auth, std::string("6f1c-4a2b"));
}

TEST(HysteriaEncoderTest, TransportConfigAcceptsEmptyAuth) {
    boost::json::object hysteriaSettings = parseObject("{\"version\":2}");

    hysteria_payload::TransportConfigFields fields{};
    ASSERT_TRUE(hysteria_payload::extractTransportConfig(hysteriaSettings, fields));

    EXPECT_EQ(fields.version, 2);
    EXPECT_TRUE(fields.auth.empty());
}

TEST(HysteriaEncoderTest, TransportConfigRejectsVersionOne) {
    boost::json::object hysteriaSettings = parseObject("{\"version\":1,\"auth\":\"x\"}");

    hysteria_payload::TransportConfigFields fields{};
    EXPECT_FALSE(hysteria_payload::extractTransportConfig(hysteriaSettings, fields));
}
```

- [ ] **Step 2: 注册 CMake 目标**

在 `CMakeLists.txt` 的 `add_test(NAME OutboundPayloadTest COMMAND test_outbound_payload)` 之后插入：

```cmake
# -- Hysteria2 payload extraction tests (pure logic, boost::json only) --
add_executable(test_hysteria_encoder tests/test_hysteria_encoder.cpp)
target_include_directories(test_hysteria_encoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_hysteria_encoder PRIVATE gtest_main gtest ${VCPKG_ROOT}/installed/x64-mingw-static/lib/libboost_json-gcc14-mt-x64-1_91.a)
set_target_properties(test_hysteria_encoder PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME HysteriaEncoderTest COMMAND test_hysteria_encoder)
```

- [ ] **Step 3: 编译确认红灯**

Run:
```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target test_hysteria_encoder --parallel 8
```
Expected: FAIL，报 `HysteriaPayload.h: No such file or directory`（`fatal error`），即红灯原因正确。

- [ ] **Step 4: 提交测试骨架**

```powershell
git add tests/test_hysteria_encoder.cpp CMakeLists.txt
git commit -m "test(xray): add hysteria payload extraction tests (red)"
```

---

### Task 2: 纯逻辑头文件（绿灯）

**Files:**
- Create: `include/HysteriaPayload.h`

- [ ] **Step 1: 写实现**

创建 `include/HysteriaPayload.h`：

```cpp
#pragma once

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <boost/json.hpp>

// Hysteria2 (ConfigType 7) 的 JSON 字段抽取与校验。
//
// 与 xray-core v26.3.27 的行为严格对齐：
//   - 容器层: xray.proxy.hysteria.ClientConfig { int32 version = 1; ServerEndpoint server = 2; }
//     JSON 形状由 src/config/outbound/Hysteria2OutboundBuilder.cpp 产出：
//     settings { address, port, version }（平铺，无 servers[]，无 account）
//   - 传输层: xray.transport.internet.hysteria.Config { int32 version = 1; string auth = 2; ... }
//     JSON 形状: streamSettings.hysteriaSettings { version, auth, up, down }
//   - infra/conf/hysteria.go 要求 version == 2，否则 errors.New("version != 2")
//   - 上游 v26 已把 up/down/congestion/udphop 移出 proto（仅告警），故这里刻意忽略
//
// 本头文件保持"零依赖纯逻辑"，便于免链接单测（沿用 include/OutboundPayload.h 的先例）。
//
// 注意：项目红线 —— 禁止使用 auto 进行类型推导。

namespace hysteria_payload {

namespace detail {

// 从 JSON 值读取十进制整数（支持 int64 / uint64 / 十进制字符串）。
// 成功返回 true 并写入 out；失败返回 false 且不修改 out。
inline bool readInt(const boost::json::value& value, int& out) {
    if (value.is_int64()) {
        out = static_cast<int>(value.as_int64());
        return true;
    }
    if (value.is_uint64()) {
        out = static_cast<int>(value.as_uint64());
        return true;
    }
    if (value.is_string()) {
        const char* text = value.as_string().c_str();
        if (text == nullptr || *text == '\0') {
            return false;
        }
        errno = 0;
        char* endPtr = nullptr;
        long long parsed = std::strtoll(text, &endPtr, 10);
        if (errno == ERANGE || endPtr == text || *endPtr != '\0') {
            return false;
        }
        out = static_cast<int>(parsed);
        return true;
    }
    return false;
}

} // namespace detail

struct ClientConfigFields {
    int version;
    std::string address;
    int port;
};

struct TransportConfigFields {
    int version;
    std::string auth;
};

// 从 outbound 的 settings 对象抽取容器层字段。
// 失败返回 false 且不修改 out。
inline bool extractClientConfig(const boost::json::object& settings, ClientConfigFields& out) {
    const boost::json::value* versionValue = settings.if_contains("version");
    int version = 0;
    if (versionValue == nullptr || !detail::readInt(*versionValue, version)) {
        return false;
    }
    if (version != 2) {
        return false; // 上游: errors.New("version != 2")
    }

    const boost::json::value* addressValue = settings.if_contains("address");
    if (addressValue == nullptr || !addressValue->is_string()) {
        return false;
    }
    std::string address = std::string(addressValue->as_string().c_str());
    if (address.empty()) {
        return false;
    }

    const boost::json::value* portValue = settings.if_contains("port");
    int port = 0;
    if (portValue == nullptr || !detail::readInt(*portValue, port)) {
        return false;
    }
    if (port <= 0 || port > 65535) {
        return false;
    }

    out.version = version;
    out.address = address;
    out.port = port;
    return true;
}

// 从 streamSettings.hysteriaSettings 对象抽取传输层字段。
// 失败返回 false 且不修改 out。
inline bool extractTransportConfig(const boost::json::object& hysteriaSettings, TransportConfigFields& out) {
    const boost::json::value* versionValue = hysteriaSettings.if_contains("version");
    int version = 0;
    if (versionValue == nullptr || !detail::readInt(*versionValue, version)) {
        return false;
    }
    if (version != 2) {
        return false; // 上游: errors.New("version != 2")
    }

    std::string auth;
    const boost::json::value* authValue = hysteriaSettings.if_contains("auth");
    if (authValue != nullptr && authValue->is_string()) {
        auth = std::string(authValue->as_string().c_str());
    }
    // up / down / congestion / udphop 在 v26 已被移出 proto，刻意忽略。

    out.version = version;
    out.auth = auth;
    return true;
}

} // namespace hysteria_payload
```

- [ ] **Step 2: 编译并运行，确认绿灯**

Run:
```powershell
cmake --build build --target test_hysteria_encoder --parallel 8
.\tests\test_hysteria_encoder.exe
```
Expected: `[==========] 10 tests from 1 test suite ran.` / `[  PASSED  ] 10 tests.`

- [ ] **Step 3: 提交**

```powershell
git add include/HysteriaPayload.h
git commit -m "feat(xray): add hysteria payload extraction helpers"
```

---

### Task 3: 接入 XrayApi（gRPC 直连）

**Files:**
- Modify: `src/XrayApi.cpp`（include 段；`parseOutboundJson` :737-782；`jsonConfigToProtobuf` :492-506 与 :559-563 之后；`encodeStreamConfig` :1365 与 :1379-1383；`supportedTypeUrls` :2503-2513；空值保护 :2534-2550；新增 `encodeHysteriaSettings` 定义）
- Modify: `include/XrayApi.h`（`encodeSplitHTTPSettings` 声明附近）

- [ ] **Step 1: 头文件包含与成员声明**

`src/XrayApi.cpp`：在 `#include "OutboundPayload.h"` 之后新增一行：

```cpp
#include "HysteriaPayload.h"
```

`include/XrayApi.h`：在 `encodeSplitHTTPSettings` 声明之后新增：

```cpp
    static std::string encodeHysteriaSettings(const boost::json::object& hysteria);
```

- [ ] **Step 2: `parseOutboundJson` 增加协议映射**

`src/XrayApi.cpp` 中，把 `protocol == "http"` 分支之后、最终 `else` 之前改为：

```cpp
        } else if (protocol == "http") {
            typeUrl = "xray.proxy.http.ClientConfig";
        } else if (protocol == "hysteria") {
            typeUrl = "xray.proxy.hysteria.ClientConfig";
        } else {
            typeUrl = "xray.proxy.outbound.Config";
        }
```

（即只插入 `hysteria` 那一支，其余保持原样。）

- [ ] **Step 3: `jsonConfigToProtobuf` 布尔位与 guard**

在 `const bool isHTTP = (typeUrl == "xray.proxy.http.ClientConfig");` 之后新增：

```cpp
    const bool isHysteria = (typeUrl == "xray.proxy.hysteria.ClientConfig");
```

并把原 guard 改为：

```cpp
    if (!isFreedom && !isBlackhole && !isVMess && !isVLESS && !isTrojan &&
        !isShadowsocks && !isShadowsocks2022 && !isSocks && !isHTTP && !isHysteria) {
        return {};
    }
```

- [ ] **Step 4: `jsonConfigToProtobuf` 增加 hysteria 分支**

在 `if (isBlackhole) { ... }` 代码块之后、`// ---- Server-based protocols` 注释之前插入：

```cpp
    // ---- Hysteria2: settings{ address, port, version } 平铺，无 servers[]，无 account ----
    // 容器层: xray.proxy.hysteria.ClientConfig { int32 version = 1; ServerEndpoint server = 2; }
    if (isHysteria) {
        hysteria_payload::ClientConfigFields fields;
        if (!hysteria_payload::extractClientConfig(settings, fields)) {
            return {}; // 交由调用方的空值保护回退 CLI，绝不注入残缺 outbound
        }
        // ServerEndpoint.user（field 3）必须【完全省略】：写入零长度子消息会被解码成
        // 非 nil 的 User{}，ToMemoryUser 随即报 "Account is missing"（common/protocol/user.go:8-10），
        // AddOutbound 直接失败；而批处理路径没有 CLI 兜底。故此处不复用 encodeServerEndpoint。
        std::string endpoint;
        endpoint += encodeLengthDelimited(1, encodeIPOrDomain(fields.address)); // address
        endpoint += encodeVarintField(2, static_cast<uint64_t>(fields.port));   // port
        std::string hysteriaConfig;
        hysteriaConfig += encodeVarintField(1, static_cast<uint64_t>(fields.version));
        hysteriaConfig += encodeLengthDelimited(2, endpoint);
        return hysteriaConfig;
    }

```

- [ ] **Step 4b: hysteria 传输层预检 + `finalmask` 回退（B2/B3）**

在 `addOutboundDirect` 的 splitHTTP 校验（`validateSplitHTTPSettings` 调用）之后、组装 `typedMsg` 之前插入：

```cpp
    // hysteria 走 gRPC 时：① finalmask(udpmasks=10) 尚未编码，携带时退回 CLI 保持 obfs 语义
    // （Hysteria2OutboundBuilder 在 path 非空时写出 finalmask）；② hysteriaSettings 编码失败会让
    // transportConfig 整段缺失，运行期表现为 "auth failed"，故先校验再注入。
    if (typeUrl == "xray.proxy.hysteria.ClientConfig") {
        boost::json::object streamObj;
        boost::json::object hysteriaSettings;
        bool haveStream = false;
        if (!streamSettingsJson.empty()) {
            boost::system::error_code streamEc;
            boost::json::value streamValue = boost::json::parse(streamSettingsJson, streamEc);
            if (!streamEc && streamValue.is_object()) {
                streamObj = streamValue.as_object();
                haveStream = true;
            }
        }
        if (haveStream && streamObj.if_contains("finalmask") != nullptr) {
            Logger::write("[XrayApi] addOutboundDirect: hysteria finalmask not encodable "
                          "via gRPC, fallback to subprocess", LogLevel::INFO);
            return addOutbound(outboundJson, tag, resultOutput);
        }
        const boost::json::value* hysteriaValue =
            haveStream ? streamObj.if_contains("hysteriaSettings") : nullptr;
        if (hysteriaValue != nullptr && hysteriaValue->is_object()) {
            hysteriaSettings = hysteriaValue->as_object();
        }
        hysteria_payload::TransportConfigFields transportFields;
        if (!haveStream || !hysteria_payload::extractTransportConfig(hysteriaSettings, transportFields)) {
            Logger::write("[XrayApi] addOutboundDirect: missing/invalid hysteriaSettings, "
                          "fallback to subprocess", LogLevel::INFO);
            return addOutbound(outboundJson, tag, resultOutput);
        }
    }

```

- [ ] **Step 5: 新增 `encodeHysteriaSettings` 定义**

在 `XrayApi::encodeSplitHTTPSettings` 定义之后新增：

```cpp
std::string XrayApi::encodeHysteriaSettings(const boost::json::object& hysteria) {
    // 传输层: xray.transport.internet.hysteria.Config { int32 version = 1; string auth = 2; ... }
    hysteria_payload::TransportConfigFields fields;
    if (!hysteria_payload::extractTransportConfig(hysteria, fields)) {
        return std::string();
    }
    std::string result;
    result += encodeVarintField(1, static_cast<uint64_t>(fields.version));
    if (!fields.auth.empty()) {
        result += encodeString(2, fields.auth);
    }
    return result;
}
```

- [ ] **Step 6: `encodeStreamConfig` 两处分支**

第一处（transport type 选择链，在 splithttp 分支的 `}` 之后、`}` 收尾之前）：

```cpp
    } else if (network == "hysteria") {
        settingsValue = stream.if_contains("hysteriaSettings");
        transportTypeUrl = "xray.transport.internet.hysteria.Config";
    }
```

第二处（settings 编码 dispatch，在 `xhttp || splithttp` 分支之后）：

```cpp
        } else if (network == "hysteria") {
            encodedSettings = encodeHysteriaSettings(settingsObj);
        }
```

- [ ] **Step 7: 白名单与空值保护**

`src/XrayApi.cpp:2503-2513` 的 `supportedTypeUrls` 数组，在 `"xray.proxy.freedom.Config",` 之前插入：

```cpp
        "xray.proxy.hysteria.ClientConfig",
```

同文件 :2500-2502 的说明注释追加一句：

```cpp
    // jsonConfigToProtobuf additionally handles hysteria (xray.proxy.hysteria.ClientConfig) via
    // direct protobuf wire encoding of ClientConfig{version, server}.
```

空值保护列表（:2534-2550）中追加一项（放在 `typeUrl == "xray.proxy.http.ClientConfig"` 之后）：

```cpp
            || typeUrl == "xray.proxy.hysteria.ClientConfig"
```

- [ ] **Step 8: 编译**

Run:
```powershell
cmake --build build --parallel 8
```
Expected: 全部目标链接成功，无新增警告；仅允许既有的 `src/XrayApi.cpp:62` / `:69` 的 `STARTUPINFOA` 初始化警告。

- [ ] **Step 9: 提交**

```powershell
git add src/XrayApi.cpp include/XrayApi.h
git commit -m "feat(xray): inject hysteria2 via grpc instead of the api ado subprocess"
```

---

### Task 4: 全量回归与文档登记

**Files:**
- Modify: `docs/INDEX.md`
- Modify: `docs/plans/project-plans-tracker.md`
- Modify: `docs/context.md`

- [ ] **Step 1: 全量回归**

Run（工作目录必须是 `build`，否则 ctest 报 `No tests were found!!!`）：
```powershell
ctest -V
```
Expected: **55/56 通过**（基线 54/55 + 新增 `HysteriaEncoderTest` 1 个通过；唯一失败为已知基线 `36 - XrayManagerStartupTest`）。单独确认：

```powershell
.\tests\test_hysteria_encoder.exe
.\tests\test_outbound_payload.exe
```
Expected: 分别为 10/10、10/10。

- [ ] **Step 2: 核对 INDEX §7.5 与计数行（已登记，只核对不重加）**

> 独立评审 S6：本方案与调查报告**已在文档阶段登记完成**，此处只核对，**不要重复追加**（会撞号/重复）。

```powershell
Select-String -Path docs/INDEX.md -Pattern 'Spec-Hysteria2-GrpcEncoder|Report-Hysteria2-GrpcEncoderFeasibility'
```
Expected: 各命中 2 次（§7.5 行 **#55** 与 §9 行 **#17** 的 label + href）；`docs/INDEX.md:30` 已是实测值 **85**（`(Get-ChildItem docs/specs -Filter *.md).Count` 实测 85）。仅当缺失时才补登：

```markdown
| 55 | [`docs/specs/2026-10-20-Spec-Hysteria2-GrpcEncoder-v1.0.md`](./specs/2026-10-20-Spec-Hysteria2-GrpcEncoder-v1.0.md) | **Hysteria2 gRPC 编码器（CLI 回退消除）实施方案** — 补齐容器层 `xray.proxy.hysteria.ClientConfig` 与传输层 `xray.transport.internet.hysteria.Config` 两处编码器；新增纯头文件 `HysteriaPayload.h` + 10 用例；消除 `xray api ado` 子进程回退 | 📋 spec 定稿（未实施） | — |
```

- [ ] **Step 3: 核对 INDEX §9 分析报告（已登记，只核对）**

§9 行 **#17** 已存在（见 Step 2 的检索命令）；若缺失才在表格最后一行之后追加：

```markdown
| 17 | [`docs/reports/2026-10-20-Report-Hysteria2-GrpcEncoderFeasibility-v1.0.md`](./reports/2026-10-20-Report-Hysteria2-GrpcEncoderFeasibility-v1.0.md) | **xray-core Hysteria2 编码器调查报告** — xray-core 有 `xray.proxy.hysteria.ClientConfig` proto 定义但无"专属编码器"层；CLI `ado` 与 gRPC 共用同一 proto（`main/commands/all/api/outbounds_add.go`）；本项目回退 CLI 的原因是 `supportedTypeUrls` 未覆盖 + 缺传输层 `hysteriaSettings` 编码；改 gRPC 须补两层编码器 + TLS，只加 typeUrl 会"注入成功、运行必坏" | — | — |
```

- [ ] **Step 4: 核对 tracker 与 context（已各登记 2 行）**

`docs/plans/project-plans-tracker.md` 的「近期文档引用」表已追加 **report + spec 两行**（`Select-String -Path docs/plans/project-plans-tracker.md -Pattern 'Hysteria2-GrpcEncoder'` 期望命中 **2 次**）；`docs/context.md` §三 表格顶部（`| 2026-10-20 | **独立代理连通性失败后仍纳入监控` 那行之前）已插入 **2 行**（同样期望命中 2 次）。缺失时才按下列内容补登：

```markdown
| 2026-10-20 | report | docs/reports/2026-10-20-Report-Hysteria2-GrpcEncoderFeasibility-v1.0.md | **Hysteria2 编码器调查报告 + Spec** — xray-core 有 Hysteria2 proto 无专属编码器层；CLI `ado` 与 gRPC 同 proto；本项目缺 `supportedTypeUrls` 覆盖与传输层编码；方案=补两层编码器（容器 `ClientConfig{version,server}` + 传输 `Config{version,auth}`）+ 纯头文件 `HysteriaPayload.h`；配套 Spec `docs/specs/2026-10-20-Spec-Hysteria2-GrpcEncoder-v1.0.md` | ✅ completed |
```

`docs/context.md` §三 表格**顶部**（在 `| 2026-10-20 | **独立代理连通性失败后仍纳入监控` 那一行之前）插入：

```markdown
| 2026-10-20 | **Hysteria2 编码器调查报告 + gRPC 直连方案（CLI 回退消除）** — 调查确认 xray-core v26.3.27 有 `xray.proxy.hysteria.ClientConfig` 与 `xray.transport.internet.hysteria.Config` proto 定义，但无"Hysteria2 专属编码器"层；`xray api ado` 与 gRPC AddOutbound 共用同一 proto（`main/commands/all/api/outbounds_add.go:34-77`），故本项目 Hysteria2 回退 CLI 属编码器缺失而非 xray 不支持；方案=补两层编码器 + 纯头文件 `HysteriaPayload.h` + 10 用例，落点 5 处（parseOutboundJson 协议映射 / jsonConfigToProtobuf 布尔位与分支 / encodeStreamConfig 两处 / supportedTypeUrls + 空值保护） | `docs/reports/2026-10-20-Report-Hysteria2-GrpcEncoderFeasibility-v1.0.md`、`docs/specs/2026-10-20-Spec-Hysteria2-GrpcEncoder-v1.0.md` |
```

- [ ] **Step 5: 提交文档**

```powershell
git add docs/INDEX.md docs/plans/project-plans-tracker.md docs/context.md
git commit -m "docs(xray): register the hysteria2 encoder report and implementation plan"
```

---

## 5. 验证准则

| 编号 | 判据 | 命令 / 观察点 | 期望 |
| :-: | :--- | :--- | :--- |
| **V1** | 新单测通过 | `.\tests\test_hysteria_encoder.exe` | 10/10 PASS |
| **V2** | 既有单测无回归 | `.\tests\test_outbound_payload.exe`；`ctest -R BatchWorkerPolicyTest -V` | 10/10；11/11 |
| **V3** | 全量无新增失败 | `ctest -V`（workdir=`build`） | 55/56，唯一失败 `XrayManagerStartupTest` |
| **V4** | Hysteria2 确实走 gRPC（判据须有鉴别力，S3） | 用 `bin/` 最新产物跑一批 ConfigType 7，临时把 `bin/config.json` 的 `log.file_level` 改为 `DEBUG`，然后三项一起看：`addOutboundDirect SUCCESS tag=proxy` **必须命中**（src/XrayApi.cpp:2626）；`addOutboundDirect FAILED` 与 `注入xray outbound 错误` **各 0 命中**；`addOutbound normalized payload` 对 hysteria 目标 **0 命中** | 三项全部满足 |
| **V5** | 注入确实生效（含 TLS 正向对照，S4） | ①`xray api lso --server <apiPort>` 输出含 `proxy`；②该批出现 OK/FAIL 延迟结果而非 `XRAY_ERROR`；③**正向对照**：至少 1 条 `sni` 非空的 hy2 节点必须 `delay > 0`（库内 100/1934 条 `sni` 空且 `allowinsecure != '1'`，这类必然拨号失败，属既有缺口） | ①②③ 全部满足；若 ③ 失败，检索 `xray_stderr_*.log` 是否含 `tls config is nil`（dialer.go:440-443），确认是既有 TLS 缺口而非本次回归 |
| **V6** | 无 `auto` 引入（S2：PowerShell 默认忽略大小写，须限定文件与 `-CaseSensitive`） | `Select-String -Path include/HysteriaPayload.h,tests/test_hysteria_encoder.cpp -Pattern '\bauto\b' -CaseSensitive`；`git diff -U0 src/XrayApi.cpp \| Select-String '^\+.*\bauto\b'` | 前者仅允许 `HysteriaPayload.h:22` 的注释行 1 条命中；后者 0 命中 |

> V4 结束后**必须把 `log.file_level` 恢复为 `WARN`**。

---

## 6. 风险与回滚

| 风险 | 影响 | 缓解 |
| :--- | :--- | :--- |
| 传输层编码缺失 → 注入成功但无 `auth` | `encodeStreamConfig` 在 `encodedSettings` 为空时整段跳过 transportConfig，运行期表现为 `auth failed` | 已前置消除：Step 4b 在注入前用 `extractTransportConfig` 校验，失败即回退 CLI（B3） |
| `finalmask`（salamander obfs）在 gRPC 路径未编码 | 库内 679/1934 条 hy2 的 `path` 非空 → 会写出 finalmask；gRPC 侧 `udpmasks(10)` 未编码则静默丢失 obfs 语义（与 CLI 路径行为不一致） | 已前置消除：Step 4b 检测到 `streamSettings.finalmask` 即回退 CLI（B2）；实现 udpmasks 需先确定 salamander TypedMessage typeUrl，列为后续 |
| `encodeServerEndpoint` 写出空 user 子消息 | **实测会失败**（`Account is missing`），且批处理无 CLI 兜底 | D5 已改为内联只写 field 1/2；S1 的字节用例锁定线格式 |
| 上游 proto 漂移 | 未来版本字段变化 | 本次只编码 version/auth/address/port；文档 §0 已记录漂移风险 |
| 误把 hysteria 判定为 server-class | 编码读不到 `servers[]` → 返回 `{}` → 回退 CLI（不会坏，但白改） | Task 1 用例直接锁定"平铺 settings"形状 |
| TLS 缺失（builder 不输出 tlsSettings） | **拨号期**失败（`tls config is nil`，dialer.go:440-443），不拦配置加载；库内 100/1934 条命中 | 既有缺口（CLI 路径同样不带 TLS），本次非目标（N2）；V5 用 `sni` 非空节点做正向对照，并以 `xray_stderr_*.log` 区分既有缺口与本次回归 |

**回滚**：`git revert` 对应 commit 即可。改动面 = 1 个新头文件 + 1 个新测试 + `CMakeLists.txt` + `src/XrayApi.cpp`（5 处小改）+ `include/XrayApi.h`（1 行声明）；无数据迁移、无配置迁移。

---

## 7. 后续（本次不做，登记备查）

| 编号 | 事项 | 触发条件 |
| :-: | :--- | :--- |
| F1 | `Hysteria2OutboundBuilder` 在 `sni` 空且 `allowinsecure != "1"` 时不输出 `tlsSettings`，而 Hysteria2 强制 TLS | V5 失败或现场出现 Hysteria2 连接异常 |
| F2 | 其余无编码器协议（TUIC / WireGuard / Anytls / Naive）是否同样值得补编码器 | 这些协议在库中出现且量大时 |
| F3 | 加密/混淆字段（`finalmask`、`masq_*`）编码 | 上游 v26 语义稳定后 |
| F4 | 独立评审本方案 | 交付前（同 AdoPayload 方案的流程） |
