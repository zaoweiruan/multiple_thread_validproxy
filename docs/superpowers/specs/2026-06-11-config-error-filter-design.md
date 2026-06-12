# Config 错误代理过滤 — 设计方案

## 概述

针对代理配置错误（`checkRequired()` 无效的字段），在三个层级建立过滤机制，确保无效代理不在数据库中留存。

## 三个防线

### 防线①：更新订阅阶段（Primary Filter）

**位置**: `src/SubitemUpdaterV2.cpp` — `isValidProxy()`

将现有手写的 REALITY/Address/Port 检查替换为统一的 `checkRequired()` 调用，保留网络类型（R3）验证。

```
旧: R1(REALITY PublicKey) + R2(REALITY Sni) + R3(network) + R4(address) + R5(port)
新: checkRequired()  + isValidNetwork()
```

效果：新解析的无效代理在 insert 前被拦截，零 DB 污染。

### 防线②：去重阶段（Periodic Cleanup）

**位置**: `src/SubitemUpdaterV2.cpp` — `deduplicate()`

新增 `deduplicateConfigErrorPhase()` 阶段，放在 Phase 1 之后、Merged 之前：
- 查询所有 `ProfileItem`
- 对每条运行 `checkRequired()`
- 失败的调用 `deleteByIndexId()` 删除
- 删除的同时清除关联的 `ProfileExItem`

效果：清理已存在的坏代理（包括升级前入库的旧数据）。

### 防线③：测试阶段（Safety Net）

**位置**: `src/ProxyBatchTester.cpp` — `workerThreadFunc()`

在 `checkRequired()` 的 catch 块中，除了记录错误，同时删除该代理：
- 调用 `ProfileitemDAO::deleteByIndexId()` 删除
- 不需要手动删 ProfileExItem（已包含在 deleteByIndexId 中）

## 新增/修改的文件

| 文件 | 变更 |
|------|------|
| `include/Profileitem.h` | 新增 `deleteByIndexId()` 声明 |
| `src/ProfileitemDAO.cpp` | 实现 `deleteByIndexId()` |
| `src/SubitemUpdaterV2.cpp` | 重构 `isValidProxy()`；新增 `deduplicateConfigErrorPhase()`；更新 `deduplicate()` 流程 |
| `include/SubitemUpdaterV2.h` | 新增 `deduplicateConfigErrorPhase()` 声明 |
| `src/ProxyBatchTester.cpp` | `checkRequired()` catch 块增加删除逻辑 |

## 不变的部分

- `ConfigGenerator` — 保持原样，抛异常是其验证机制
- `ProxyFinder` — 已在 catch 中跳过坏代理
- `ShareLink` — 已有 delay > 0 过滤
- `Profileitem::checkRequired()` — 保持现有定义
