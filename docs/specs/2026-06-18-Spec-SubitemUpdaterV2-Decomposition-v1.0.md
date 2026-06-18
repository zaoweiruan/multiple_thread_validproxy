---
title: "SubitemUpdaterV2 Decomposition - Extract parsing, fetching, dedup, import logic"
type: spec
status: completed
date: 2026-06-18
---

# SubitemUpdaterV2 Decomposition Spec

## 目标
将 `SubitemUpdaterV2.cpp` (2422行) 重构为多个单一职责类，降低代码复杂度，提高可维护性和可测试性。

## 提取组件

### 1. SubscriptionParser (651行)
- **功能**: 解析 vmess/vless/ss/trojan/hysteria2 协议 URL
- **方法**: `parse(content, subid)` → 返回 `std::vector<Profileitem>`
- **辅助**: `decodeBase64()`, `urlDecode()`, `parseAddressPort()`

### 2. SubscriptionUpdater (89行)
- **功能**: HTTP 请求获取订阅内容
- **方法**: `fetchUrl()`, `fetchUrlViaAccelerator()`, `fetchUrlViaProxy()`
- **配置**: 使用 `config::AppConfig` 中的超时和加速器 URL

### 3. Deduplicator (270行)
- **功能**: 事务安全的代理去重
- **阶段**:
  - Phase 0: 标记有效代理到受保护 subid
  - Phase 1: 删除无效地址(私网 IP)
  - Phase 2: 黑名单标记 (consecutive_failures ≥ threshold)
  - Phase 3: ConfigError 检查 (checkRequired)
  - Phase 4: 合并去重 (CTE ROW_NUMBER)
- **方法**: `deduplicate()`, `countAllProxies()`, 统计 getter

### 4. Importer (330行)
- **功能**: 批量导入订阅节点
- **方法**: `importFromFile()`, `importSingleUrl()`
- **辅助**: `extractRemarksFromUrl()`, `getNextSortValue()`, `isUrlExists()`, `hasValidPath()`

## 变更文件

| 类型 | 路径 | 说明 |
|------|------|------|
| 新增 | `include/update/SubscriptionParser.h` | 协议解析头 |
| 新增 | `include/update/SubscriptionUpdater.h` | HTTP获取头 |
| 新增 | `include/update/Deduplicator.h` | 去重头 |
| 新增 | `include/update/Importer.h` | 导入头 |
| 新增 | `src/update/SubscriptionParser.cpp` | 协议解析实现 |
| 新增 | `src/update/SubscriptionUpdater.cpp` | HTTP实现 |
| 新增 | `src/update/Deduplicator.cpp` | 去重实现 |
| 新增 | `src/update/Importer.cpp` | 导入实现 |
| 修改 | `include/SubitemUpdaterV2.h` | 移除旧声明，添加新类引用 |
| 修改 | `src/SubitemUpdaterV2.cpp` | 2422→1198行，委托给新类 |

## 委托关系

```
SubitemUpdaterV2::parseSubscription() → SubscriptionParser::parse()
SubitemUpdaterV2::deduplicate() → Deduplicator::deduplicate()
SubitemUpdaterV2::importSubitemsFromFile() → Importer::importFromFile()
SubitemUpdaterV2::importSingleUrl() → Importer::importSingleUrl()
```

## 保留在 SubitemUpdaterV2

Orchestration 逻辑不可再分解：
- `run()`, `runSingle()`, `runSingleWithProxy()` - 协调方法
- `updateWithMethods()` - 多阶段更新
- `getProxyPorts()`, `releaseProxyPorts()` - Xray生命周期
- `syncDatabases()`, `migrateSubscription()`, `migrateProxy()` - DB同步
- `updateProfileItems()` - 批量插入 (保留 insertSubItem/isValidProxy 供 Importer 共享)

## 结果

- 减少度: 2422 → 1198 行 (-1224行, -50%)
- 单元测试: `test_subscription_parser` 9/9 通过
- 编译状态: ✅ 通过