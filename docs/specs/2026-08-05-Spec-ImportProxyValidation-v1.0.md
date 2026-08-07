---
title: "导入订阅稽核：去除无法正确生成配置的代理"
type: spec
status: in-progress
date: 2026-08-05
---

# 导入订阅稽核：去除无法正确生成配置的代理 技术方案 (Spec)

## 1. 需求

> 用户原文（2026-08-05）：**"调整功能：导入订阅时，稽核、去除'去重阶段需要删除的代理，如：id/password 为二进制等'无法正确生成配置的代理"**

目标：把去重阶段 `deduplicateConfigErrorPhase` 的判定（`checkRequired` + Security/Id 可打印 ASCII）**前移到订阅导入路径**——节点入库前即丢弃无法生成 xray 配置的代理，避免二进制垃圾数据进入数据库后污染批量测试（2026-08-04 曾因 29811 个乱码代理导致批量测试全失败）。

## 2. 现状分析

### 2.1 去重阶段已有判定（作为稽核标准）

`Deduplicator::deduplicateConfigErrorPhase`（`src/update/Deduplicator.cpp:257-293`）：

1. `p.checkRequired()`（`include/Profileitem.h:75-96`）：address 空 / port 空或 ≤0 或 >65535 / id 空 / configtype 空 / REALITY 缺 publicKey+sni → 判无效。
2. 匿名命名空间 `isPrintableAscii`（`Deduplicator.cpp:12-20`）：Security/Id 存在 0x20-0x7E 范围外字节（二进制垃圾）→ 判无效，`deleteByIndexIdsNoTx` 批量删除。

该判定由 2026-08-04 `Bugfix-SubscriptionParser-GarbageSS-v1.0.md` 引入（Argh94 订阅 malformed ss:// 链接经 decodeBase64 非法字符解码为索引 0 → 全库 29811 个代理 Security/Id 二进制垃圾）。**此判定目前只在去重阶段（CLI -D / UI 去重触发）执行，导入阶段不执行。**

### 2.2 导入路径缺口

- `SubitemUpdaterV2::isValidProxy`（`src/SubitemUpdaterV2.cpp:40-52`，匿名命名空间）：仅 `checkRequired()` + `utils::isValidNetwork()` 校验，**无 Security/Id 可打印 ASCII 校验** → 二进制垃圾节点仍会入库。
- `isValidProxy` 全项目唯一调用点 = `updateProfileItems` Phase 0 预过滤（`SubitemUpdaterV2.cpp:558`）→ 覆盖全部订阅更新导入路径（`run()` 内 221/403/478 三处）。
- `SubscriptionParser` 各协议分支：ss://（386 行）已有 method/password `isPrintableAscii` 校验；vmess（security=scy 默认 "auto"）/vless（security="none"）/ss（security=method）解析值天然 ASCII；**trojan://（404 行）与 hysteria2://（473 行）的 `profile.security` 未赋值（恒空串）** → 新增 `isPrintableAscii(security)` 校验对 trojan/hy2 返回 true，不误杀。

### 2.3 isPrintableAscii 重复实现

`isPrintableAscii` 现存在于**两处匿名命名空间重复实现**（逻辑相同，均 0x20-0x7E）：

| 位置 | 行号 | 消费点 |
|------|------|--------|
| `src/update/Deduplicator.cpp` | 12-20 | 272 行（Security/Id 稽核） |
| `src/update/SubscriptionParser.cpp` | 41-49 | 386 行（ss method/password 稽核） |

→ 提取为 `utils::isPrintableAscii` 统一复用，消除重复。

## 3. 设计

### 3.1 改动 1：`utils::isPrintableAscii` 提取（helper 统一）

**`include/Utils.h`**（namespace utils 内，isValidNetwork 附近）追加声明：

```cpp
bool isPrintableAscii(const std::string& s);
```

**`src/Utils.cpp`**（`getProcessNameFromPath` 之后、命名空间收尾前）追加实现：

```cpp
bool isPrintableAscii(const std::string& s) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s.c_str());
    for (; *p != '\0'; ++p) {
        if (*p < 0x20 || *p > 0x7E) {
            return false;
        }
    }
    return true;  // 空串返回 true（与现有两处实现行为一致）
}
```

**消费点改用 utils:: 版本**（行为等价，零行为变化）：

| 文件 | 改动 |
|------|------|
| `src/update/Deduplicator.cpp` | 删 12-20 匿名 `isPrintableAscii`；272 行改为 `utils::isPrintableAscii` |
| `src/update/SubscriptionParser.cpp` | 删 41-49 匿名 `isPrintableAscii`；386 行改为 `utils::isPrintableAscii` |

（两者均已 `#include "Utils.h"`，无需新增 include。）

### 3.2 改动 2：`SubitemUpdaterV2::isValidProxy` 增加 Security/Id 稽核

`isValidProxy`（`src/SubitemUpdaterV2.cpp:40-52`）在 `checkRequired` 与 `isValidNetwork` 校验之后追加：

```cpp
// 与去重阶段 deduplicateConfigErrorPhase 判定一致：Security/Id 含
// 非可打印 ASCII（二进制垃圾，如 malformed ss:// 解码产物）永远无法
// 注入 xray 生成合法配置，入库前直接丢弃
if (!utils::isPrintableAscii(p.security) || !utils::isPrintableAscii(p.id)) {
    Logger::write("SKIP: " + p.address + ":" + p.port +
                  " - non-printable Security/Id (cannot build xray config)", LogLevel::WARN);
    return false;
}
```

要点：

- **与去重阶段判定完全一致**（同一 helper、同一语义）→ 导入期丢弃的集合 ⊆ 去重阶段删除的集合，行为对齐。
- 空 security（trojan/hy2）→ `isPrintableAscii("")` = true → **不误杀 trojan/hy2**。
- 一处改动覆盖全部导入路径（`run()` 221/403/478 → `updateProfileItems` Phase 0）。
- **防回归**：即使未来 parser 新增协议分支产生二进制字段，也在入库前被拦截。

### 3.3 日志

WARN 级别，格式与既有 isValidProxy SKIP 日志一致（`SKIP: address:port - 原因`），便于用户识别被稽核丢弃的节点。

## 4. 文件清单

| 文件 | 说明 |
|------|------|
| `include/Utils.h` | +`isPrintableAscii` 声明 |
| `src/Utils.cpp` | +`isPrintableAscii` 实现 |
| `src/update/Deduplicator.cpp` | 删匿名 `isPrintableAscii`（12-20），改用 `utils::isPrintableAscii`（272） |
| `src/update/SubscriptionParser.cpp` | 删匿名 `isPrintableAscii`（41-49），改用 `utils::isPrintableAscii`（386） |
| `src/SubitemUpdaterV2.cpp` | `isValidProxy` 增加 Security/Id 可打印 ASCII 稽核 |
| `tests/test_utils.cpp` | +`isPrintableAscii` 单元测试 |

## 5. 测试策略

### 5.1 新增测试（tests/test_utils.cpp，gtest）

| 用例 | 断言 |
|------|------|
| 空串 | `utils::isPrintableAscii("")` = true |
| 纯 0x20-0x7E 范围 | `utils::isPrintableAscii("aA1 _-~")` = true |
| 含 0x00 / 0x1F / 0x7F | 各 = false |
| UTF-8 多字节（中文） | `utils::isPrintableAscii("中文")` = false |
| 混合可打印 + 非可打印 | = false |

### 5.2 既有测试回归

- `test_dedup` / `test_subscription_parser`：改用 `utils::isPrintableAscii` 为行为等价替换，应全部继续通过。
- `test_utils` 既有 JoinUrl/UrlValidation/PortCheck 用例不受影响。

### 5.3 验证命令

```powershell
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
```

## 6. 实施记录

| 步骤 | 内容 | 状态 |
|------|------|------|
| 1 | 撰写本技术方案文档 | ✅ completed |
| 2 | `utils::isPrintableAscii` 加入 Utils.h/cpp | ✅ completed |
| 3 | Deduplicator/SubscriptionParser 改用 `utils::isPrintableAscii` | ✅ completed |
| 4 | `SubitemUpdaterV2::isValidProxy` 增加 Security/Id 稽核 | ✅ completed |
| 5 | test_utils.cpp 单测 + 构建 + ctest 验证 | ✅ completed |
| 6 | 登记 docs/INDEX.md（#23）+ tracker + CONTEXT.md | ✅ completed |
