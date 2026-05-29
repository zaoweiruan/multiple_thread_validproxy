---
title: "feat: 在去重功能中去除无效代理"
type: feat
status: completed
date: 2026-05-14
confirmations: |
  - [x] 1. 去重时跳过不更新，仅保留原记录
  - [x] 2. 黑名单代理手动修改数据库（无CLI标志）
  - [x] 3. 迁移逻辑在初始化时自动执行，忽略字段已存在的错误
  - [x] 4. 不需要对 Network 为空的记录过滤
  - [x] 5. FilterReason 仅在日志中记录，不修改数据库 schema
origin: "错误分析报告 — error-report_20260514.md"
---

# 在去重功能中去除无效代理

## 问题描述

当前去重功能（`-D` / `--dedup`）仅处理以下三类问题：
1. 标记受保护子ID（protected subid）的工作代理
2. 移除私有 IP 地址的无效代理
3. 基于 `lower(Address)+Port+ConfigType+lower(Id)` 的去重

但**未过滤在根本上无效的代理配置**，导致大量有缺陷的代理进入测试流水线，引发高频 `CONFIG_ERROR` 日志。

### 已识别的无效代理类型

| 类型 | 表现 | 影响 |
|------|------|------|
| REALITY 缺少 `PublicKey` | `CONFIG_ERROR: REALITY requires publicKey` | 测试线程报错、代理计为失败 |
| REALITY 缺少 `Sni` | `CONFIG_ERROR: REALITY requires sni` | 同上 |
| `Network` 字段脏数据 | `'wshttps://@freev2configs'`、含 emoji 等 | `isValidNetwork()` 跳过，浪费 SQL 查询和迭代资源 |
| `Network` 为空 | `ConfigGenerator` fallback 为 `tcp` | 可能生成不符合预期的配置 |

## 范围边界

### 修改范围
- `src/SubitemUpdaterV2.cpp` — 去重逻辑增强
- `src/ConfigGenerator.cpp` — 新增预校验方法
- `include/Profileitem.h` — 可选：新增批量校验方法
- `docs/design/dedup-blacklist-design.md` — 更新设计文档

### 不修改范围
- 不修改数据库 schema
- 不修改黑名单逻辑（`consecutive_failures` 机制保持不变）
- 不修改订阅解析器（`parseSubscription`）的输入处理逻辑

## 详细变更

### U1: `SubitemUpdaterV2.cpp` — 去重函数中集成无效代理过滤

在 `deduplicatePhase1()` 或新增的预处理步骤中，在去重之前先过滤无效代理：

```cpp
// 新增：在去重前过滤无效代理
int SubitemUpdaterV2::filterInvalidProxies() {
    // 过滤条件：
    // 1. StreamSecurity='reality' AND (PublicKey='' OR PublicKey IS NULL)
    // 2. StreamSecurity='reality' AND (Sni='' OR Sni IS NULL)
    // 3. Network 值不在 isValidNetwork 白名单中（且非空）
    // 4. Address 为空或 Port 无效
    
    // 返回被移除的记录数
}
```

**SQL 方案**（推荐，效率高）：
```sql
-- 删除 REALITY 但缺少 PublicKey 的记录
DELETE FROM ProfileItem 
WHERE StreamSecurity = 'reality' 
  AND (PublicKey IS NULL OR PublicKey = '');

-- 删除 REALITY 但缺少 Sni 的记录  
DELETE FROM ProfileItem 
WHERE StreamSecurity = 'reality' 
  AND (Sni IS NULL OR Sni = '');
```

**代码方案**（逐条校验，可配合日志）：
在 `deduplicatePhase1()` 中遍历所有代理，对每条记录调用 `Profileitem::checkRequired()`，捕获异常并记录到 `ProfileExItem.message`。

### U2: `ConfigGenerator.cpp` — 新增批量校验方法

```cpp
// 新增方法：批量校验代理有效性，返回无效 indexId 列表
std::vector<std::string> ConfigGenerator::validateProfiles(
    const std::vector<db::models::Profileitem>& profiles);
```

该方法调用现有的 `checkRequired()`，收集所有验证失败的 `IndexId`。

### U3: `dedup-blacklist-design.md` — 更新设计文档

在「三、去重逻辑」中新增预处理步骤，描述无效代理过滤规则。

### U4: 测试验证

- 使用 `test/guiNDB_empty.db` 插入含无效 REALITY 代理的测试数据
- 运行 `-D` 去重，验证无效代理被正确移除
- 检查日志中不再出现 `CONFIG_ERROR: REALITY requires publicKey`

## 验证步骤

1. 构建项目：`cmake --build build --parallel 8`，0 编译错误
2. 运行单元测试：`ctest -R DedupTest -V`，全部通过
3. 功能测试：
   - 在 `test/guiNDB_empty.db` 中插入 10 条 REALITY 代理（5 条无 PublicKey，3 条无 Sni，2 条正常）
   - 运行 `-D` 去重
   - 验证：5+3=8 条无效记录被移除，2 条正常记录保留
4. 回归测试：运行完整测试套件 `ctest`，无回归

## 文件变更列表

| 文件 | 变更类型 | 内容 |
|------|---------|------|
| `src/SubitemUpdaterV2.cpp` | 修改 | 新增 `filterInvalidProxies()` 或在 `deduplicatePhase1()` 前集成过滤 |
| `src/ConfigGenerator.cpp` | 修改 | 新增 `validateProfiles()` 方法 |
| `include/Profileitem.h` | 可选 | 新增 `validate()` 返回验证结果而非抛异常 |
| `docs/design/dedup-blacklist-design.md` | 修改 | 更新设计文档 |
| `docs/plans/project-plans-tracker.md` | 修改 | 登记本计划 |