# ProfileItem 去重功能设计文档

## 1. 概述

删除 ProfileItem 中的重复代理记录，保持数据整洁

## 2. 配置扩展

```json
{
    "dedup": {
        "enabled": true,
        "subids": ["5544178410297751350"]
    }
}
```

## 3. 命令行参数

- `-dedup` 或 `-D` - 触发去重功能

## 4. 去重键

- **字段**: address + port + network
- **保留策略**: 保留最小 IndexId

## 5. 五阶段执行流程

### Phase 0: 标记有效/降级代理

- delay > 0 的代理 → subids[0]（受保护）
- subids[0] 中 delay <= 0 的代理 → subids[1]（降级）

### Phase 1: 删除私网地址

删除私网地址范围内的代理：
- 10.x.x.x (10.0.0.0/8)
- 172.16-31.x.x (172.16.0.0/12)
- 192.168.x.x (192.168.0.0/16)

### Phase 2: 与有效代理 (delay > 0) 去重

删除与已知可用代理 (delay > 0) 重复的记录，保留最小 IndexId

### Phase 3: 保留指定 subid，删除其他重复

保留 dedup_subids 中指定 subid 的代理，删除其他 subid 中与这些代理重复的记录

### Phase 4: 排除 subids 后全表去重

在排除 dedup_subids 后，对剩余所有代理按 address+port+network 去重，保留最小 IndexId

## 6. ProfileExItem 清理

删除已删除 ProfileItem 的关联 ProfileExItem 记录

## 7. 输出格式

```
========================================
INFO: Starting Deduplication
========================================
INFO: Total proxies before: 500

INFO: Phase 0/5 - Marking protected/degraded proxies
INFO: Phase 0 marked: 137 (protected: 100, degraded: 37)

INFO: Phase 1/5 - Removing private network addresses
INFO: Phase 1 deleted: 15

INFO: Phase 2/5 - Removing duplicates with delay>0 proxies
INFO: Phase 2 deleted: 8

INFO: Phase 3/5 - Keeping dedup_subids, removing duplicates
INFO: Phase 3 deleted: 22

INFO: Phase 4/5 - Full deduplication excluding subids
INFO: Phase 4 deleted: 30

INFO: Cleaning up ProfileExItem...
INFO: ProfileExItem cleaned: 30 orphaned records

========================================
INFO: Deduplication Summary
========================================
INFO: Total deleted: 45
INFO: Total remaining: 455
INFO: Dedup completed successfully
```

## 8. 实现位置

- **模块**: SubitemUpdaterV2
- **新方法**:
  - `bool deduplicate()` - 主入口
  - `int deduplicatePhase0()` - Phase 0: 标记受保护/降级代理
  - `int deduplicatePhase1()` - Phase 1: 删除私网地址
  - `int deduplicatePhase2()` - Phase 2: 与有效代理去重
  - `int deduplicatePhase3()` - Phase 3: 保留指定 subid 去重
  - `int deduplicatePhase4()` - Phase 4: 全表去重
  - `void cleanupProfileExItem()` - 清理孤立记录

## 9. 版本记录

- v1.0.43 (2026-04-17): 新增 Phase 0 标记受保护/降级代理 + Phase 1 私网地址过滤
- v1.0.42 (2026-04-17): 新增去重功能 (Phase 1/2/3)

## 10. 实现细节 (v1.0.43)

### 10.1 配置扩展

```json
{
    "dedup": {
        "enabled": true,
        "subids": ["5544178410297751350"]
    }
}
```

### 10.2 命令行参数

- `-D` 或 `-dedup` - 触发去重功能

### 10.3 五阶段执行流程

#### Phase 0: 标记受保护/降级代理

```sql
-- delay > 0 的代理标记到 subids[0]
UPDATE ProfileItem
SET SubId = 'subids[0]'
WHERE IndexId IN (
    SELECT p.IndexId FROM ProfileItem p
    JOIN ProfileExItem pe ON p.IndexId = pe.IndexId
    WHERE pe.Delay > 0 AND pe.Delay != '-1'
);

-- subids[0] 中 delay <= 0 的代理降级到 subids[1]
UPDATE ProfileItem
SET SubId = 'subids[1]'
WHERE SubId = 'subids[0]'
AND IndexId IN (
    SELECT p.IndexId FROM ProfileItem p
    JOIN ProfileExItem pe ON p.IndexId = pe.IndexId
    WHERE pe.Delay <= 0 OR pe.Delay = '-1'
);
```

#### Phase 1: 删除私网地址

```sql
DELETE FROM ProfileItem 
WHERE 
    Address LIKE '10.%'
    OR Address LIKE '172.16.%' OR Address LIKE '172.17.%' OR Address LIKE '172.18.%'
    OR Address LIKE '172.19.%' OR Address LIKE '172.20.%' OR Address LIKE '172.21.%'
    OR Address LIKE '172.22.%' OR Address LIKE '172.23.%' OR Address LIKE '172.24.%'
    OR Address LIKE '172.25.%' OR Address LIKE '172.26.%' OR Address LIKE '172.27.%'
    OR Address LIKE '172.28.%' OR Address LIKE '172.29.%' OR Address LIKE '172.30.%'
    OR Address LIKE '172.31.%'
    OR Address LIKE '192.168.%';
```

#### Phase 2: 与有效代理 (delay > 0) 去重

```sql
DELETE FROM ProfileItem 
WHERE IndexId IN (
    SELECT pi.IndexId FROM ProfileItem pi
    JOIN (
        SELECT Address, Port, Network, MIN(IndexId) as MinIndexId
        FROM ProfileItem
        WHERE Address IN (
            SELECT DISTINCT p.Address FROM ProfileItem p
            JOIN ProfileExItem pe ON p.IndexId = pe.IndexId
            WHERE pe.Delay > 0 AND pe.Delay != '-1'
        )
        GROUP BY Address, Port, Network
    ) dup ON pi.Address = dup.Address AND pi.Port = dup.Port AND pi.Network = dup.Network
    WHERE pi.IndexId > dup.MinIndexId
);
```

#### Phase 3: 保留指定 subid，删除其他重复

```sql
DELETE FROM ProfileItem 
WHERE SubId NOT IN ('subid1', 'subid2')
AND IndexId IN (
    SELECT pi.IndexId FROM ProfileItem pi
    JOIN (
        SELECT Address, Port, Network FROM ProfileItem
        WHERE SubId IN ('subid1', 'subid2')
        GROUP BY Address, Port, Network
    ) dup ON pi.Address = dup.Address AND pi.Port = dup.Port AND pi.Network = dup.Network
);
```

#### Phase 4: 排除 subids 后全表去重

```sql
DELETE FROM ProfileItem 
WHERE SubId NOT IN ('subid1', 'subid2')
AND IndexId IN (
    SELECT pi.IndexId FROM ProfileItem pi
    JOIN (
        SELECT Address, Port, Network, MIN(IndexId) as MinIndexId
        FROM ProfileItem
        WHERE SubId NOT IN ('subid1', 'subid2')
        GROUP BY Address, Port, Network
    ) dup ON pi.Address = dup.Address AND pi.Port = dup.Port AND pi.Network = dup.Network
    WHERE pi.IndexId > dup.MinIndexId
);
```

### 10.4 ProfileExItem 清理

```sql
DELETE FROM ProfileExItem 
WHERE IndexId NOT IN (SELECT IndexId FROM ProfileItem);
```

### 10.5 修改的文件

| 文件 | 变更 |
|------|------|
| include/ConfigReader.h | 添加 dedup_enabled, dedup_subids 字段 |
| src/ConfigReader.cpp | 解析 dedup 配置 |
| include/SubitemUpdaterV2.h | 添加 deduplicate() 和去重方法 Phase 0-4 |
| src/SubitemUpdaterV2.cpp | 实现五阶段去重逻辑 |
| src/main.cpp | 添加 -D 参数支持，独立的 dedup 处理分支 |
| bin/config.json | 添加 dedup 配置示例 |

### 10.6 测试结果

```
INFO: Total proxies before: 6231
INFO: Phase 0 marked: 137 (protected: 100, degraded: 37)
INFO: Phase 1 deleted: 36
INFO: Phase 2 deleted: 445
INFO: Phase 3 deleted: 2783
INFO: Phase 4 deleted: 100
INFO: ProfileExItem cleaned: 45891 orphaned records
INFO: Total deleted: 3364
INFO: Total remaining: 2867
```
