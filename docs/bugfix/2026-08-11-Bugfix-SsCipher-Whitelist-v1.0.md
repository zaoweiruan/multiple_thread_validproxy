# Bugfix: SS 加密算法白名单扩充 chacha20-ietf-poly1305（含日志分析报告）v1.0

- **日期**：2026-08-11
- **模块**：`Utils`（`isSupportedSsCipher` 白名单）/ `Deduplicator`（Phase 4/6 清洗）/ `SubitemUpdaterV2`（导入闸门）
- **关联问题**：`bin/worker/log/ui_20260811_112935.log` 去重阶段删除 14146 条 CONFIG_ERROR 代理，其中 6018 条为 `unsupported SS cipher: 'chacha20-ietf-poly1305'`——该 cipher 为 Shadowsocks 旧版但常见的 AEAD 命名，不应被白名单拒收
- **关联文档**：`docs/bugfix/2026-08-10-Analysis-InjectionError-ConfigRootCause-v1.0.md`（B 类 SS Unsupported cipher 首次观测）、`docs/plans/2026-08-10-Plan-ImportProxyValidation-v1.0.md`（U1 isSupportedSsCipher 白名单的制定）

## 1. 背景与动机

### 1.1 分析报告（`ui_20260811_112935.log` CONFIG_ERROR 统计）

对 `bin/worker/log/ui_20260811_112935.log`（48,641 行，时间范围 11:29:52 起）全文件统计 `[WARN] CONFIG_ERROR` 行：

| 分类 | 数量 | 占比 | 判定依据 |
| --- | --- | --- | --- |
| invalid UUID format | 6927 | 48.97% | configtype 1/5（vless/vmess）+ `isValidUuid` 失败 |
| unsupported SS cipher | 6020 | 42.56% | configtype 3（SS）+ `isSupportedSsCipher` 白名单外 |
| private/invalid address | 1199 | 8.47% | `isPublicAddress` 失败 |
| **合计** | **14146** | 100% | 与日志 `Phase ConfigError deleted: 14146` 完全一致 |

unsupported SS cipher 明细：**6018 条 `chacha20-ietf-poly1305`**、2 条 `aes-256-cfb`（indexid 5864443574527019790、5135494773324695271）。checkRequired 异常 0、非可打印 Security/Id 0。

**判定依据**：`Deduplicator::deduplicateConfigErrorPhase`（`src/update/Deduplicator.cpp` line 240-288）对全库 `ProfileItem` 逐条执行 5 重检查，任一失败即记 `[WARN] CONFIG_ERROR` 并 `deleteByIndexIdsNoTx` 删除：

1. `p.checkRequired()`（line 250，抛异常 → bad）
2. 非可打印 ASCII 的 `security`/`id`（line 255，仅计数不逐行日志）
3. `!utils::isPublicAddress(p.address)` → `private/invalid address`（line 262）
4. configtype 1/5 + `!utils::isValidUuid(p.id)` → `invalid UUID format`（line 266）
5. configtype 3 + `!utils::isSupportedSsCipher(p.security)` → `unsupported SS cipher: '<method>'`（line 270）

同一判定逻辑在 `SubitemUpdaterV2::isValidProxy`（line 63/69）作为导入闸门同步生效（U1-U3 计划落地）。

### 1.2 修复策略选择

`chacha20-ietf-poly1305` 是 Shadowsocks **标准 AEAD 加密**（RFC 8439 的 IETF 变体，`chacha20-poly1305` 的 IETF 命名），并非废弃的 CFB 流模式；在大量订阅源中仍被广泛使用。Xray-core 实际支持该 cipher（`common/crypto` 与 ss 出站均实现）。将其列入白名单是**数据面清洗口径与运行时能力对齐**，而非放宽安全边界。

| 方案 | 描述 | 结论 |
| --- | --- | --- |
| **A（维持现状）** | 白名单保持 8 项，6018 条该 cipher 节点持续被 Phase 4/6 删除 | 误伤可用节点；`aes-256-cfb`（废弃流模式）仍正确拒收 |
| **B（扩充白名单）** | `isSupportedSsCipher` 白名单新增 `"chacha20-ietf-poly1305"`（第 9 项） | **采纳**：该 cipher 为合法 AEAD，仅命名与白名单现有 `chacha20-poly1305` 不一致 |

选择 **方案 B**：单行白名单扩充，导入闸门（U2）与存量清洗（U3）自动同步生效，无需改动判定逻辑本身。

## 2. 变更范围

| 文件 | 变更 |
| --- | --- |
| `src/Utils.cpp` | `isSupportedSsCipher` 白名单数组（line 437-446）新增第 9 项 `"chacha20-ietf-poly1305"`（位于 `chacha20-poly1305` 之后） |
| `docs/bugfix/2026-08-11-Bugfix-SsCipher-Whitelist-v1.0.md` | 本文档 |

**不修改**：`include/Utils.h` 声明（line 24，签名不变）；`Deduplicator.cpp` 与 `SubitemUpdaterV2.cpp` 调用点（白名单数组为唯一数据源，自动生效）；`tests/`（无 `isSupportedSsCipher` 专项断言，唯一相关 `tests/test_ss_uri_builder.cpp:44` 以该 cipher 作样例值，非白名单断言）。`aes-256-cfb`（废弃 CFB 流模式）**不在**本次扩充范围，继续拒收。

## 3. 设计

### 3.1 `isSupportedSsCipher(const std::string& method)` 白名单（扩充后）

```cpp
static const char* supported[] = {
    "aes-128-gcm",
    "aes-256-gcm",
    "chacha20-poly1305",
    "chacha20-ietf-poly1305",   // ← 新增（Shadowsocks IETF AEAD 命名）
    "xchacha20-poly1305",
    "none",
    "2022-blake3-aes-128-gcm",
    "2022-blake3-aes-256-gcm",
    "2022-blake3-chacha20-poly1305"
};
```

行为要点：
- 线性比较（`src/Utils.cpp` line 448-454），命中即返回 `true`，否则 `false`
- 扩充后共 9 项：AEAD 6 项（含新增）+ 2022-blake3 3 项
- 所有调用方（Deduplicator Phase 4/6、SubitemUpdaterV2 导入闸门）无感知自动生效

## 4. 验证方案

### 4.1 单元测试

无新增断言：`tests/` 无 `isSupportedSsCipher` 专项测试（grep `SsCipher|SS.*cipher|supported.*cipher` 无匹配）；`tests/test_ss_uri_builder.cpp:44` 使用该 cipher 作为 SS URI 构建样例值，与白名单无关。既有 `UtilsTest` 不受影响。

### 4.2 构建与回归

```powershell
cmake --build build --parallel 2   # 327/327 targets 成功（内存受限下降低并行度；仅既有 warning）
ctest --test-dir build             # 22/22 全绿（63.33s，含 UtilsTest/DedupTest/XrayApiDirectTest）
```

## 5. 预期收益与后续

- **收益**：6018 条 `chacha20-ietf-poly1305` 节点在下次 Phase 4/6 清洗与订阅导入时**不再被删除**；与 Xray-core 运行时实际支持的 SS AEAD 能力对齐。`aes-256-cfb`（废弃流模式）等非法 cipher 仍按原口径拒收。
- **生效时机**：白名单为运行时读取，下次执行去重（CLI `-D` / UI 去重触发）或订阅更新导入时自动生效，无需重新导入数据。
- **后续**：`aes-256-cfb` 仅 2 条存量，属废弃流模式维持拒收；如需进一步降低误删率，可评估在分析报告中增加非 AEAD cipher 的抽样审计（当前无此需求）。
