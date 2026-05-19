---
title: "数据库方案分析报告 — Database Schema & Data Quality Analysis"
type: analysis
status: completed
date: 2026-05-14
analyst: Sisyphus
sources:
  - "Profileitem.h / ProfileExItem.h — 数据模型定义"
  - "dedup-blacklist-design.md — 去重与黑名单设计方案"
  - "error-report_20260514.md — 日志错误分析"
  - "test-report_20260512_135738.md — 功能测试报告"
  - "bin/worker/log/*.log — 454 份运行日志"
---

# 数据库方案分析报告 — 2026-05-14

## 1. 数据库架构总览

本项目使用 **SQLite3** 作为唯一持久化存储，包含两个数据库文件：

| 数据库 | 路径 | 用途 | 规模 |
|--------|------|------|------|
| 生产库 | `bin/worker/guindb.db` | 实际运行 | 约 53,000+ profiles |
| 测试库 | `test/guindb.db` | 功能验证 | 53,837 profiles, 44 SubIDs |
| 测试空库 | `test/guiNDB_empty.db` | CLI 测试 | 711 profiles, 8 SubIDs |
| 同步测试源 | `test/guiNDB.db` | 同步测试源 | 约 41MB, 703 有效代理 |

---

## 2. 核心表结构

### 2.1 ProfileItem 表（主表）

基于 `include/Profileitem.h` 定义，共 **35 个字段**：

| 序号 | 字段名 | 类型 | 说明 | 数据质量 |
|------|--------|------|------|---------|
| 0 | IndexId | TEXT PK | 唯一标识（UUID） | ✅ 稳定 |
| 1 | ConfigType | TEXT | 协议类型（1=VMess, 3=SS, 5=VLESS, 6=Trojan, 7=Socks, 8=HTTP, 9=Hysteria2, 10=TUIC, 11=WireGuard） | ✅ 稳定 |
| 2 | ConfigVersion | TEXT | 配置版本 | ✅ 稳定 |
| 3 | Address | TEXT | 服务器地址 | ⚠️ 存在空值 |
| 4 | Port | TEXT | 端口号（字符串存储） | ⚠️ ~50条为空 |
| 5 | Ports | TEXT | 多端口 | ⚠️ 稀疏填充 |
| 6 | Id | TEXT | 用户ID/密码 | ⚠️ 2.5% 大小写混合 |
| 7 | AlterId | TEXT | AlterId（VMess） | 稳定 |
| 8 | Security | TEXT | 加密方式 | 稳定 |
| 9 | Network | TEXT | 传输协议 | ❌ **15% 重复记录中不一致**，含脏数据 |
| 10 | Remarks | TEXT | 备注 | 非结构化 |
| 11 | HeaderType | TEXT | 伪装类型 | 稀疏 |
| 12 | RequestHost | TEXT | 请求主机 | 稀疏 |
| 13 | Path | TEXT | 路径 | 稀疏 |
| 14 | StreamSecurity | TEXT | 传输安全（tls/reality/空） | ⚠️ 空值较多 |
| 15 | AllowInsecure | TEXT | 跳过证书验证 | 通常为 "1" 或空 |
| 16 | Subid | TEXT | 订阅源ID | ✅ 稳定 |
| 17 | IsSub | TEXT | 是否来自订阅 | ✅ 稳定 |
| 18 | Flow | TEXT | 流控（VLESS） | 稀疏 |
| 19 | Sni | TEXT | SNI/ServerName | ❌ **46% 为空** |
| 20 | Alpn | TEXT | ALPN 协商 | 稀疏 |
| 21 | CoreType | TEXT | 内核类型 | 通常为 "0" |
| 22 | PreSocksPort | TEXT | 预设SOCKS端口 | 通常为空 |
| 23 | Fingerprint | TEXT | TLS指纹 | 稀疏 |
| 24 | DisplayLog | TEXT | 日志显示 | 通常为空 |
| 25 | PublicKey | TEXT | 公钥（REALITY必需） | ❌ REALITY记录中大量为空 |
| 26 | ShortId | TEXT | 短ID（REALITY） | 稀疏 |
| 27 | SpiderX | TEXT | SpiderX 分流 | 稀疏 |
| 28 | Mldsa65Verify | TEXT | 多路验证 | 通常为空 |
| 29 | Extra | TEXT | 扩展字段 | 通常为空 |
| 30 | MuxEnabled | TEXT | 多路复用 | 通常为 "0" |
| 31 | Cert | TEXT | 证书链 | 极少填充 |
| 32 | CertSha | TEXT | 证书SHA | 极少填充 |
| 33 | EchConfigList | TEXT | ECH配置 | 极少填充 |
| 34 | EchForceQuery | TEXT | ECH强制查询 | 极少填充 |

**非数据库字段**（仅内存中使用）：
- `grpcMultiMode` (int)
- KCP 参数：`kcpMtu`, `kcpTti`, `kcpUplink`, `kcpDownlink`, `kcpCongestion`, `kcpReadBufferSize`, `kcpWriteBufferSize`, `kcpHeaderType`
- `muxEnabled` (int)

### 2.2 ProfileExItem 表（扩展表）

基于 `include/ProfileExItem.h` 定义，共 **6 个字段**：

| 字段 | 类型 | 说明 |
|------|------|------|
| IndexId | TEXT PK | 与 ProfileItem 关联 |
| Delay | TEXT | 延迟（单位: 10ms，"999999"=不可达） |
| Speed | TEXT | 速度评分 |
| Sort | TEXT | 排序权重 |
| Message | TEXT | 状态信息（"OK"/"FAILED"/错误详情） |
| consecutive_failures | INTEGER | 连续失败次数（≥5 触发黑名单） |

**关键设计**：`IndexId` 作为主键与 ProfileItem 一对一关联，延迟数据归一化为 10ms 单位。

---

## 3. 数据质量问题汇总

### 3.1 REALITY 协议字段缺失（严重）

| 问题 | 数据 | 来源 |
|------|------|------|
| `StreamSecurity='reality'` 但 `PublicKey` 为空 | 大量（具体数量待 SQL 统计） | 订阅源未提供 |
| `StreamSecurity='reality'` 但 `Sni` 为空 | 部分同上 | 订阅源未提供 |

**影响**：导致 `CONFIG_ERROR: REALITY requires publicKey/Sni` 高频报错，测试线程逐一失败。

### 3.2 Network 字段脏数据（中等）

**已识别的脏值模式**：
- `'wshttps://@freev2configs'` — URL 标签被解析为 network
- `'tcp🌐 tel : @v2ray_unit'` — emoji 和描述文本
- `'ws@freev2configs'` — @提及标签
- `'wsoliver soul'` — 订阅源作者名
- `'tcpHajali-9002'` — 非标准后缀
- `'tcp🌐'` — emoji 后缀

**根本原因**：`SubitemUpdaterV2::parseSubscription()` 解析订阅数据时未做 Network 字段白名单校验。

### 3.3 字段格式不一致性

| 字段 | 问题 | 处理建议 |
|------|------|---------|
| Id | 2.5% 记录大小写混合，同一逻辑ID有8种变体 | 去重时统一 `lower(Id)` |
| Address | 部分大小写不一致 | 去重时统一 `lower(Address)` |
| Network | 15% 重复记录中 network 值不同 | 去重时不使用 Network 作为键 |
| Sni | 46% 为空，20% 重复记录中不一致 | 去重时不使用 Sni 作为键 |
| Port | ~50条为空 | 需要 `IS NULL OR = ''` 兼容处理 |

---

## 4. 去重键设计分析

### 4.1 最终方案

```sql
GROUP BY lower(Address), Port, ConfigType, lower(Id)
```

**选择依据**（基于数据库探索数据）：

| 候选键 | 评估 | 结论 |
|--------|------|------|
| `Address` | 大小写不一致 → `lower()` | ✅ 采用 |
| `Port` | ~50条为空，需特殊处理 | ✅ 采用 |
| `ConfigType` | 协议类型固定 | ✅ 采用 |
| `Id` | 需 `lower()` 标准化 | ✅ 采用 |
| `Network` | 15% 重复记录中不一致 | ❌ 排除 |
| `Sni` | 46% 为空，20% 不一致 | ❌ 排除 |
| `Address+Port+ConfigType+Id` 组合 | 唯一性高 | ✅ 最终方案 |

### 4.2 重复规模

基于 `test/guindb.db` 探索：
- **总记录数**: 52,530
- **重复组数**: 7,208 组（涉及 4,051 个唯一代理）
- **重复记录数**: ~8,100 条（占 15%）
- **测试结果需保留**: 51,332 条（97.7%）

---

## 5. 黑名单机制设计

### 5.1 架构

```
ProxyBatchTester::workerThreadFunc()
    │
    ├─ 加载代理 (loadProxies) → SQL 排除 blacklisted=1
    │
    ├─ checkRequired() → 基础字段校验
    │
    ├─ generateConfig() → 生成Xray配置
    │
    ├─ xray api ado → 注入出站
    │
    ├─ curl 测试 → 获取延迟
    │
    └─ updateTestResult() → 更新 ProfileExItem
         ├─ 成功: consecutive_failures = 0
         └─ 失败: consecutive_failures++，≥5 则 blacklisted=1
```

### 5.2 关键设计决策

| 决策 | 方案 | 理由 |
|------|------|------|
| 黑名单存储位置 | `ProfileExItem.blacklisted` 字段 | 与现有扩展表集成，零 schema 变更 |
| 阈值 | 连续失败 5 次 | 平衡误杀与真实失败 |
| 测试时过滤 | SQL WHERE 子句排除 | 最高效，数据库层面过滤 |
| 恢复机制 | 无自动恢复，需手动 SQL | 简化实现，避免误恢复 |
| 失败计数重置 | 成功测试时归零 | 确保活跃代理不被误杀 |

---

## 6. SQL 性能考量

### 6.1 关键查询分析

**去重检查查询**（每条新记录执行一次）：
```sql
SELECT IndexId FROM ProfileItem 
WHERE lower(Address) = lower(?) 
  AND (Port IS NULL OR Port = '' OR Port = ?) 
  AND ConfigType = ? 
  AND lower(Id) = lower(?)
  AND (Network IS NULL OR Network = '' OR lower(Network) = lower(?)) 
LIMIT 1
```

**潜在性能问题**：
- `lower(Address)` 和 `lower(Id)` 导致无法使用普通索引
- `Port IS NULL OR Port = ''` 使索引选择性降低
- 建议：创建函数索引或预处理时标准化存储

### 6.2 黑名单过滤查询

```sql
AND IndexId NOT IN (SELECT IndexId FROM ProfileExItem WHERE blacklisted = 1)
```

**评估**：`blacklisted` 字段值域仅为 {0, 1}，选择性低，但 `NOT IN` 子查询在 SQLite 中优化良好，可接受。

---

## 7. 无效代理预过滤方案

### 7.1 规则定义

| 规则 | 条件 | 触发场景 |
|------|------|---------|
| R1 | REALITY + PublicKey 为空 | 订阅源不提供公钥 |
| R2 | REALITY + Sni 为空 | 订阅源不提供SNI |
| R3 | Network 不在白名单 | 订阅源解析错误，含标签/备注 |
| R4 | Address 为空 | 数据不完整 |
| R5 | Port 无效（非数字/超范围） | 数据不完整 |

### 7.2 执行位置

**推荐**：在 `SubitemUpdaterV2::updateProfileItems()` 中，**去重检查之前**执行。

```
订阅数据 → [预过滤] → [去重] → [插入]
              ↑
         过滤无效记录（不写入数据库）
```

### 7.3 影响评估

根据日志分析（2026-05-14 测试运行）：
- 单次测试约 39,000 profiles 被加载
- 其中约 40% 因 `isValidNetwork()` 被跳过（网络字段脏数据）
- REALITY 配置错误产生 20+ 条 `CONFIG_ERROR` 日志
- **预过滤可将无效代理拦截在数据库写入之前**，显著减少测试浪费

---

## 8. 方案对比

| 方案 | 优点 | 缺点 |
|------|------|------|
| **预过滤（推荐）** | 从源头阻止无效数据，减少存储和测试开销 | 需要修改订阅更新代码 |
| **SQL 后过滤** | 不改导入逻辑，查询时过滤 | 无效数据仍存入数据库，占用空间 |
| **混合方案** | 预过滤 + SQL 后过滤双重保障 | 实现复杂度最高 |

**推荐方案**：预过滤（方案1），在 `SubitemUpdaterV2` 中实现 `isValidProxy()` 方法。

---

## 9. 实施路线图

| 阶段 | 任务 | 状态 |
|------|------|------|
| 1 | 设计文档更新（已完成） | ✅ 完成 |
| 2 | 计划文档创建（已完成） | ✅ `in_progress` |
| 3 | 实现 `isValidProxy()` | 🔜 待开发 |
| 4 | 集成到 `updateProfileItems()` | 🔜 待开发 |
| 5 | 构建验证 + 单元测试 | 🔜 待验证 |
| 6 | 功能测试（插入无效数据验证过滤） | 🔜 待测试 |
| 7 | 回归测试（完整 `ctest`） | 🔜 待测试 |

---

## 10. 参考资料

| 来源 | 路径 |
|------|------|
| 设计文档 | `docs/design/dedup-blacklist-design.md` |
| 错误分析报告 | `docs/reports/error-report_20260514.md` |
| 功能测试报告 | `docs/test/test-report_20260512_135738.md` |
| 计划文档 | `docs/plans/2026-05-14-001-dedup-filter-invalid-proxies-plan.md` |
| Profileitem 模型 | `include/Profileitem.h` |
| ProfileExItem 模型 | `include/ProfileExItem.h` |
| 订阅更新逻辑 | `src/SubitemUpdaterV2.cpp` |
| 配置生成逻辑 | `src/ConfigGenerator.cpp` |
| 批量测试逻辑 | `src/ProxyBatchTester.cpp` |
| 数据模型定义 | `create_test_db.sql` |