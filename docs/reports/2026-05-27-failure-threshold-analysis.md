---
title: "分析: 批量测试代理时失败次数阈值检查覆盖"
type: report
status: completed
date: 2026-05-27
origin: "代码审计 — consecutive_failures / blacklist_threshold 覆盖范围"
---

# 技术报告: 失败次数阈值检查覆盖分析

## 概述

项目中通过 `blacklist_threshold` 配置项控制代理黑名单阈值，当代理的 `consecutive_failures`（连续失败次数）达到或超过该阈值时，该代理不再被批量测试选择。本报告分析该过滤机制在所有测试路径中的覆盖情况。

## 过滤机制原理

### SQL 层过滤（主要方式）

过滤在 SQL 查询层面实现，而非 C++ 层：

```sql
AND (pe.consecutive_failures IS NULL OR pe.consecutive_failures < {blacklist_threshold})
```

`{blacklist_threshold}` 占位符在 `ConfigReader::load()` 加载配置时被实际数值替换：
- 文件: `src/ConfigReader.cpp:237-238`
- 默认值: `5`（`bin/config.json` 第 14 行）

### 计数器更新逻辑

`ProfileExItemDAO::updateTestResult()` 维护 `consecutive_failures`：

| 条件 | 操作 | 文件:行 |
|------|------|---------|
| 测试成功 | 重置为 `0` | `include/ProfileExItem.h:140` |
| 测试失败 | 递增 `+1` | `include/ProfileExItem.h:131` |

被以下位置调用：
- `ProxyBatchTester::workerThreadFunc()` — `src/ProxyBatchTester.cpp:105,153,197,211`
- `ProxyFinder::findFirstWorkingProxy()` — `src/ProxyFinder.cpp:90`
- `ProxyFinder::findWorkingProxy()` — `src/ProxyFinder.cpp:169`

## 已检查失败阈值的路径 ✅

这些路径使用可配置的 SQL（`config_.sql_query` 或 `config_.sql_by_subid`），包含 `consecutive_failures < {blacklist_threshold}` 过滤条件。

| # | 路径 | 文件:行 | SQL 来源 |
|---|------|---------|----------|
| 1 | `ProxyBatchTester::run()` | `src/ProxyBatchTester.cpp:252` | `config_.sql_query` |
| 2 | `ProxyBatchTester::runWithSubId()` | `src/ProxyBatchTester.cpp:280` | `config_.sql_by_subid` |
| 3 | `AppController::doTestSubscription()` | `src/ui/AppController.cpp:330` | → 委托 `runWithSubId()` |
| 4 | `main.cpp` 默认模式 | `src/main.cpp:809` | → 委托 `run()` |
| 5 | `main.cpp` test-sub 命令 | `src/main.cpp:736` | → 委托 `runWithSubId()` |

**典型代码路径：**

```
GUI 测试订阅 / CLI test-sub
    → AppController::doTestSubscription() / main.cpp
    → ProxyBatchTester::runWithSubId()
    → ConfigGenerator::loadProfiles(sql_by_subid)   ← 含 blacklist_threshold 过滤
    → ProfileItemDAO::getAll(sql_query)
```

## 未检查失败阈值的路径 ❌

这些路径使用硬编码 SQL，不含 `consecutive_failures` 过滤条件。

| # | 路径 | 文件:行 | 使用的 SQL |
|---|------|---------|------------|
| 1 | `ProxyBatchTester::runWithIndexId()` | `src/ProxyBatchTester.cpp:312` | `SELECT * FROM ProfileItem` |
| 2 | `AppController::doTestSingleProxy()` | `src/ui/AppController.cpp:358` | → 委托 `runWithIndexId()` |
| 3 | `ProxyFinder::findFirstWorkingProxy()` | `src/ProxyFinder.cpp:40` | `loadFallbackProxies()` 硬编码 SQL |
| 4 | `ProxyFinder::findWorkingProxy()` | `src/ProxyFinder.cpp:115` | 同上 |
| 5 | `AppController::doFindFirstProxy()` | `src/ui/AppController.cpp:392` | → 委托 `ProxyFinder` |
| 6 | `AppController::doFindBestProxy()` | `src/ui/AppController.cpp:452` | → 委托 `ProxyFinder` |
| 7 | `main.cpp` find-proxy/findminproxy | `src/main.cpp:437` | → 委托 `ProxyFinder` |

### 1. `runWithIndexId()` — 单代理测试

```cpp
// src/ProxyBatchTester.cpp:312-344
db::models::ProfileitemDAO dao(db_);
auto profiles = dao.getAll();  // 硬编码 SELECT * FROM ProfileItem
// 然后按 IndexId 过滤，不关联 ProfileExItem 表
```

**有意设计**：当用户明确指定要测试某个代理（通过 IndexId）时，无论该代理的黑名单状态如何，都应执行测试。这是"手动强制测试"的预期行为。

### 2. `ProxyFinder` — 查找功能

```cpp
// src/ProxyFinder.cpp:266-272
std::string sql = "SELECT pi.IndexId, pi.Address, pi.Port, pi.PreSocksPort, "
                  "COALESCE(pe.Delay, 999999) AS Delay, pi.ConfigType "
                  "FROM ProfileItem pi "
                  "LEFT JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId "
                  "WHERE pi.Address IS NOT NULL AND pi.Address != '' "
                  "AND pi.ConfigType IN ('1','3','4','5','6','7','8','9','10') "
                  "AND COALESCE(pe.Delay, 0) > 0 "
                  "ORDER BY CAST(pe.Delay AS INTEGER) ASC";
```

唯一过滤条件是 `pe.Delay > 0`（仅选择此前测试成功的代理），**不含** `consecutive_failures` 条件。

## 不适用路径 ⚠️

`SubitemUpdaterV2::run()` / `runSingle()` (`src/SubitemUpdaterV2.cpp:163-367`) 负责抓取订阅、解析、插入/更新 `ProfileItem`，**不执行代理连通性测试**。因此不涉及跳过黑名单代理的逻辑。

## 完整覆盖矩阵

```
                    tests
                   ┌────────────────────────────────────────────────────────┐
                   │  batch test    sub test    single test   find proxy    │
                   │  (run())       (subId)     (indexId)     (Finder)      │
┌──────────────────┼────────────────────────────────────────────────────────┤
│ CLI main.cpp     │  ✅ 已检查     ✅ 已检查    ❌ 无路径     ❌ 未检查     │
│                  │  (L809)        (L736)                    (L437)        │
├──────────────────┼────────────────────────────────────────────────────────┤
│ GUI              │  ❌ 无路径     ✅ 已检查    ❌ 未检查     ❌ 未检查     │
│ AppController    │                (L330)      (L358)        (L392/L452)   │
├──────────────────┼────────────────────────────────────────────────────────┤
│ SQL 方式         │  config.sql    config.sql  SELECT *      loadFallback  │
│                  │  _query        _by_subid   FROM PItem    Proxies()     │
├──────────────────┼────────────────────────────────────────────────────────┤
│ 过滤            │  ✅ 含         ✅ 含        ❌ 不含        ❌ 不含        │
│ consecutive_     │  blacklist_    blacklist_                (仅 Delay>0)  │
│ failures         │  threshold     threshold                                │
└──────────────────┴────────────────────────────────────────────────────────┘
```

## 结论

1. **批量测试路径（`run()` / `runWithSubId()`）已正确过滤** ✅ — 使用可配置 SQL，包含 `blacklist_threshold` 条件
2. **单代理测试（`runWithIndexId()`）跳过过滤** ❌ — 使用硬编码 SQL。此为有意设计（强制测试指定代理，无论黑名单状态）
3. **查找功能（`ProxyFinder`）跳过过滤** ❌ — 硬编码 SQL 仅检查 `Delay > 0`，未检查 `consecutive_failures`。已标记为黑名单的代理仍可能被 `Find Proxy`/`Find Best` 选中
4. **计数器更新正确** ✅ — `updateTestResult()` 在成功/失败后正确维护 `consecutive_failures`

### 建议

若需为 `ProxyFinder` 增加失败阈值过滤，可在 `loadFallbackProxies()` 的 SQL 中添加：

```sql
AND (pe.consecutive_failures IS NULL OR pe.consecutive_failures < {blacklist_threshold})
```

或者将 `blacklist_threshold` 作为参数传入 `ProxyFinder`，在 C++ 层过滤返回结果。
