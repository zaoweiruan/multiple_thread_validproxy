# 去除无效代理逻辑 — 完整地图

> **目的**: 清晰展示从订阅源 → 数据库 → 可用的完整路径中，所有过滤/去除无效代理的检查点
> **适用版本**: 1.0.3+

---

## 一、总览：数据流与过滤点

```
订阅源 (URL/文件)
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│  1. parseSubscription()              src/SubitemUpdaterV2.cpp:514  │
│     解析 vmess:// vless:// trojan:// ss:// hysteria2:// ...  │
│     仅做字段提取，不做校验                                      │
└─────────────────────────────────────────────────────────────┘
    │ parsed profiles (vector<Profileitem>)
    ▼
┌─────────────────────────────────────────────────────────────┐
│  2. isValidProxy() 预过滤          src/SubitemUpdaterV2.cpp:75   │
│     Phase 0 in updateProfileItems()              :982         │
│     ┌─────────────────────────────────────────────┐          │
│     │ R1: REALITY 缺少 PublicKey → 跳过            │          │
│     │ R2: REALITY 缺少 Sni       → 跳过            │          │
│     │ R3: Network 非法（非空时）   → 跳过            │          │
│     │ R4: Address 为空           → 跳过            │          │
│     │ R5: Port 无效（非数字/越界）  → 跳过            │          │
│     └─────────────────────────────────────────────┘          │
└─────────────────────────────────────────────────────────────┘
    │ validProfiles
    ▼
┌─────────────────────────────────────────────────────────────┐
│  3. 去重检查 (dedup check)          src/SubitemUpdaterV2.cpp:1010 │
│     5字段联合键检查重复（保留原记录不更新）                       │
└─────────────────────────────────────────────────────────────┘
    │ INSERT ProfileItem + ProfileExItem
    ▼
┌─────────────────────────────────────────────────────────────┐
│  4. Dedup Phase 0         src/SubitemUpdaterV2.cpp:1373          │
│     将工作代理(Delay>0)重新标记到受保护 SubId                    │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│  5. Dedup Phase 1a/1b/1c  src/SubitemUpdaterV2.cpp:1414          │
│     ┌─────────────────────────────────────────────┐          │
│     │ Phase 1a: 删除私有地址/无效地址代理           │          │
│     │   · 10.x, 172.16-31.x, 192.168.x           │          │
│     │   · 127.x, 0.0.0.0, 地址含空格             │          │
│     │   · 地址<5字符, 不含点, http://等            │          │
│     │   · 空SubId, 孤儿子SubId                    │          │
│     ├─────────────────────────────────────────────┤          │
│     │ Phase 1b: 删除无效端口代理(<=0或>65535)      │          │
│     ├─────────────────────────────────────────────┤          │
│     │ Phase 1c: 删除无效 REALITY 代理              │          │
│     │   · StreamSecurity='reality' AND PublicKey=''│          │
│     │   · StreamSecurity='reality' AND Sni=''     │          │
│     └─────────────────────────────────────────────┘          │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│  6. Dedup Merged Phase (CTE)  src/SubitemUpdaterV2.cpp:1494      │
│     按 Address+Port+ConfigType+Id+Network 分组                │
│     保留首选 SubId + Delay 最高的记录，删除其余                 │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│  7. cleanupProfileExItem    src/SubitemUpdaterV2.cpp:1530        │
│     删除孤立的 ProfileExItem（IndexId 不在 ProfileItem 中）    │
└─────────────────────────────────────────────────────────────┘
    │ 数据库中的有效代理
    ▼
┌─────────────────────────────────────────────────────────────┐
│  8. ConfigGenerator::loadProfiles()  src/ConfigGenerator.cpp:27  │
│     ┌─────────────────────────────────────────────┐          │
│     │ Network 空 → 默认 "tcp"                      │          │
│     │ Network 非法 → 跳过该代理                    │          │
│     └─────────────────────────────────────────────┘          │
└─────────────────────────────────────────────────────────────┘
    │ 生成 outbound JSON 配置
    ▼
┌─────────────────────────────────────────────────────────────┐
│  9. Profileitem::checkRequired()     include/Profileitem.h:72   │
│     运行时最终校验（throw 异常）                               │
│     · Address 空                                        │
│     · Port 无效                                          │
│     · Id 空                                             │
│     · ConfigType 空                                      │
│     · REALITY 缺少 PublicKey 或 Sni                     │
└─────────────────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────────────────┐
│ 10. tourl 导出过滤                         src/main.cpp:455     │
│     SQL: CAST(COALESCE(pe.Delay,0) AS INTEGER) > 0          │
│     仅导出测试通过的代理                                         │
└─────────────────────────────────────────────────────────────┘
```

---

## 二、各检查点详细说明

### 1. `parseSubscription()` — 订阅解析
- **文件**: `src/SubitemUpdaterV2.cpp:514`
- **协议支持**: vmess (1)、vless (5)、trojan (6)、ss (3)、hysteria2
- **Sni 处理**: vmess 从 JSON `"sni"` 字段读取；若为空且 `streamsecurity=="tls"`，则 fallback 到 `"host"` 字段（:609-612）
- **校验逻辑**: **无**。仅做解析提取字段，不验证任何字段合法性
- **问题根源**: 脏数据在此处进入系统

### 2. `isValidProxy()` — 预过滤（Phase 0）
- **文件**: `src/SubitemUpdaterV2.cpp:75-112`
- **调用位置**: `updateProfileItems()` :990-1005
- **5条规则**:
  | # | 检查 | 条件 |
  |---|------|------|
  | R1 | REALITY → PublicKey | `streamsecurity=="reality" && publickey.empty()` |
  | R2 | REALITY → Sni | `streamsecurity=="reality" && sni.empty()` |
  | R3 | Network 合法性 | `!network.empty() && !isValidNetwork(network)` |
  | R4 | Address 非空 | `address.empty()` |
  | R5 | Port 合法性 | `port.empty() || stoi<=0 || stoi>65535` |
- **日志**: 每条过滤记录 `SKIP: addr:port - reason` (WARN)，汇总 `FILTER: Removed N invalid proxies` (REPORT)
- **注意**: 空 Network 不做过滤（留到 ConfigGenerator 中默认 "tcp"）

### 3. 去重检查 (dedup check in INSERT)
- **文件**: `src/SubitemUpdaterV2.cpp:1010-1016`
- **去重键**: `lower(Address) + Port + ConfigType + lower(Id) + lower(Network)`
- **行为**: 发现重复 → **跳过**（保留原 IndexId 和测试记录），不更新
- **本质**: 这不是过滤，但防止了重复无效代理的覆盖

### 4. Dedup Phase 0 — 工作代理重标记
- **文件**: `src/SubitemUpdaterV2.cpp:1373-1412`
- **逻辑**: 将 `pe.Delay > 0` 的代理 SubId 改为 `dedup_subids[0]`（受保护 SubId），非工作代理改为 `dedup_subids[1]`（fallback）
- **目的**: 区分工作代理和非工作代理，便于后续 Phase 1 按 SubId 策略删除

### 5. Dedup Phase 1a/1b/1c — 批量 SQL 删除
- **文件**: `src/SubitemUpdaterV2.cpp:1414-1492`

#### Phase 1a — 无效地址（~30 条 SQL OR 条件）
```
- 私有 IP: 10.x, 172.16-31.x, 192.168.x
- 回环: 127.x
- 零地址: 0.0.0.0
- 地址太短: LENGTH(Address) < 5
- 不含点: Address NOT LIKE '%.%'
- 含空格: Address LIKE '% %'
- IPv6: Address LIKE '%:%' 或 Address LIKE '%[%]%'
- 含 @: Address LIKE '%@%'
- HTTP URL 前缀: Address LIKE 'http://%' 或 Address LIKE 'https://%'
- 结尾点: Address LIKE '%.'
- 空 SubId: SubId = '' OR SubId IS NULL
- 孤儿 SubId: SubId NOT IN (SELECT Id FROM SubItem)
- 无 StreamSecurity 且非受保护 SubId: StreamSecurity = '' AND SubId NOT IN (subidsList)
```

#### Phase 1b — 无效端口
```
Port <= 0 OR Port > 65535 OR Port IS NULL
```

#### Phase 1c — 无效 REALITY 代理
```sql
DELETE FROM ProfileItem WHERE StreamSecurity = 'reality' AND (PublicKey IS NULL OR PublicKey = '');
DELETE FROM ProfileItem WHERE StreamSecurity = 'reality' AND (Sni IS NULL OR Sni = '');
```

### 6. Dedup Merged Phase (CTE) — 智能去重
- **文件**: `src/SubitemUpdaterV2.cpp:1494-1528`
- **SQL**: 使用 `ROW_NUMBER() OVER (PARTITION BY ... ORDER BY ...)` 窗口函数
- **分组键**: `lower(Address), Port, ConfigType, lower(Id), lower(Network)`
- **排序优先级**: (1) 首选 SubId (0) vs 其他 (1) → (2) Delay 降序
- **结果**: 每组保留 1 条最优记录，删除其余

### 7. `cleanupProfileExItem()` — 孤儿记录清理
- **文件**: `src/SubitemUpdaterV2.cpp:1530-1542`
- **SQL**: `DELETE FROM ProfileExItem WHERE IndexId NOT IN (SELECT IndexId FROM ProfileItem)`
- **目的**: 清除已被删除代理的扩展信息（测试结果）

### 8. `ConfigGenerator::loadProfiles()` — 配置生成过滤
- **文件**: `src/ConfigGenerator.cpp:27-50`
- **Network 处理**:
  - 空 network → 默认 "tcp"（允许运行）
  - 非法 network → 跳过代理并日志警告
- **isValidNetwork()**: 同名单函数在 `ConfigGenerator.cpp` 和 `SubitemUpdaterV2.cpp` 中各有一份（分别位于各命名空间内），逻辑一致

### 9. `Profileitem::checkRequired()` — 运行时最终校验
- **文件**: `include/Profileitem.h:72-93`
- **抛出异常**: `std::runtime_error`
- **检查项**: Address 空、Port 无效、Id 空、ConfigType 空、REALITY 缺少 PublicKey/Sni
- **触发场景**: `ConfigGenerator` 构建 JSON 配置时、`ProxyBatchTester` 测试代理时
- **设计**: 这是最后的防御线，异常会记录为 `CONFIG_ERROR`

### 10. tourl 导出过滤
- **文件**: `src/main.cpp:455-461`
- **SQL**: `WHERE CAST(COALESCE(pe.Delay, 0) AS INTEGER) > 0`
- **逻辑**: 只导出一轮测试（`ProxyBatchTester` 或 `ProxyFinder`）已确认有延迟>0 的代理
- **注意**: `ShareLink::toShareUri()` 本身无过滤逻辑，仅做 URI 转换

---

## 三、过滤策略对比

| # | 检查点 | 方式 | 位置 | 行为 |
|---|--------|------|------|------|
| 1 | 解析 | 无 | parseSubscription() | 允许一切通过 |
| 2 | 预过滤 | C++ 条件检 | updateProfileItems() | 跳过（不写入 DB） |
| 3 | 去重 | SQL 查询 | INSERT 循环 | 跳过重复（保留原记录） |
| 4 | Phase 0 | SQL UPDATE | deduplicatePhase0() | 重标记 SubId |
| 5a | Phase 1a | SQL DELETE | deduplicatePhase1() | 删除 |
| 5b | Phase 1b | SQL DELETE | deduplicatePhase1() | 删除 |
| 5c | Phase 1c | SQL DELETE | deduplicatePhase1() | 删除 |
| 6 | Merged CTE | SQL DELETE | deduplicateMergedPhase() | 删除冗余 |
| 7 | 孤儿清理 | SQL DELETE | cleanupProfileExItem() | 删除 |
| 8 | Config 生成 | C++ 条件跳过 | ConfigGenerator::loadProfiles() | 跳过（不生成配置） |
| 9 | 运行时 | C++ throw | Profileitem::checkRequired() | 异常（CONFIG_ERROR） |
| 10 | 导出 | SQL WHERE | main.cpp tourl | 条件查询 |

---

## 四、错误链与改进历史

### 原始问题
```
订阅源 → parseSubscription (无校验) → SQLite ProfileItem (脏数据进入)
  → ConfigGenerator::loadProfiles (发现无效) → CONFIG_ERROR + 跳过
  → 用户看到大量错误日志，但脏数据一直残留在 DB 中
```

### 改进后
```
订阅源 → parseSubscription (无校验)
  → isValidProxy() 预过滤 (R1-R5，插入前拦截)
  → INSERT ProfileItem (仅有效代理)
  → deduplicatePhase1() SQL 清理 (Phase 1a/1b/1c，后置清理)
  → dedup Merged CTE (智能去重)
  → ConfigGenerator::loadProfiles() (最终过滤)
  → checkRequired() (运行时防御)
```

### 关键改进 (2026-05-14)
1. **新增 `isValidProxy()` 预过滤**: 在 `updateProfileItems()` 中 Phase 0，5 条规则阻止无效代理写入数据库
2. **新增 Phase 1c**: `deduplicatePhase1()` 中两条 DELETE SQL 清理已存在的无效 REALITY 代理
3. **日志可见性**: 预过滤日志 WARN 级别（单条）+ REPORT 级别（汇总）

---

## 五、文件索引

| 文件 | 关键行 | 角色 |
|------|--------|------|
| `src/SubitemUpdaterV2.cpp:69-112` | `isValidProxy()` | 预过滤规则定义 |
| `src/SubitemUpdaterV2.cpp:53-67` | `isValidNetwork()` | Network 白名单（副本） |
| `src/SubitemUpdaterV2.cpp:982-1005` | `updateProfileItems()` Phase 0 | 预过滤调用点 |
| `src/SubitemUpdaterV2.cpp:1373-1412` | `deduplicatePhase0()` | 工作代理重标记 |
| `src/SubitemUpdaterV2.cpp:1414-1492` | `deduplicatePhase1()` | Phase 1a/1b/1c SQL 删除 |
| `src/SubitemUpdaterV2.cpp:1494-1528` | `deduplicateMergedPhase()` | CTE 智能去重 |
| `src/SubitemUpdaterV2.cpp:1530-1542` | `cleanupProfileExItem()` | 清理孤儿记录 |
| `src/SubitemUpdaterV2.cpp:1903-1960` | `deduplicate()` | 去重编排 |
| `src/ConfigGenerator.cpp:27-50` | `loadProfiles()` | 配置生成过滤 |
| `src/ConfigGenerator.cpp:78-97` | `isValidNetwork()` | Network 白名单 |
| `include/Profileitem.h:72-93` | `checkRequired()` | 运行时最终校验 |
| `src/main.cpp:455-461` | tourl SQL WHERE | 导出过滤 |
