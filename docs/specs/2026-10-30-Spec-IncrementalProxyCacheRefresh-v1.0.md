# Spec — 增量代理缓存与定时序列化刷新

- 日期: 2026-10-30
- 模块: ProxyListPanel / ProxyBatchTester / AppController
- 状态: revised (post-review, cache+persist)

## 1. 问题

当前批量测试期间：
- `ProxyListPanel::allProxies_` / `exItems_` 缓存在刷新时可能被异步全表读取覆盖为空（写锁竞争）。
- 订阅切换 `applySubscriptionFilter` 依赖 `allProxies_`，缓存为空时所有订阅显示为空。
- `refreshResults()` / `refreshResultsFor()` 走同步/全量读取，容易阻塞 UI 或与写线程竞争。
- `insertProxy` 等路径虽已优化为单条查询，但整体仍以全表扫描为主。

## 2. 目标

1. **缓存优先**：维护内存缓存 `allProxies_` / `exItems_` / maps，批量测试期间只更新脏项（已测试/插入的 indexId），不重读全表。
2. **增量刷新**：`ProxyRowsUpdatedEvent` 携带 indexIds，UI 只更新对应行；不触发全表 reload。
3. **定时序列化**：定时器只刷新脏数据（增量），且串行化（避免重叠读），不与写事务竞争。
4. **订阅切换**：仅从缓存过滤，缓存为空时异步重建，重建完成前不破坏当前视图。
5. **同时更新缓存与持久化**：写库成功后同步更新内存缓存（脏标记/直接更新），避免异步回填与全表扫描。

## 3. 设计

### 3.1 脏标记
- `ProxyListPanel` 维护 `std::set<std::string> dirtyIndexIds_`（UI 线程独占，无需锁）。
- `ProxyBatchTester::rowsUpdatedCallback` 在 UI 线程入队 `ProxyRowsUpdatedEvent(ids)`；事件处理中将 ids 插入 `dirtyIndexIds_`。
- 定时器（1s）检查 `dirtyIndexIds_`，非空时触发增量刷新。

### 3.2 增量刷新
- `refreshResultsFor(dirtyIndexIds_)`：用 `ProfileExItemDAO::getAllByIndexIds`（批量 IN 查询，≤500/批）只读取脏项，更新 `exItems_` 与 maps，通知对应行。
- 刷新后清空 `dirtyIndexIds_`。

### 3.3 定时序列化
- 单个 `wxTimer`；回调中若 `refreshInFlight_` 为 true 则跳过。
- 读取使用独立连接 + busy_timeout 30s；失败时保留旧缓存，不置空；连续失败 3 次后退避到 5s。

### 3.4 缓存重建
- 仅在 `allProxies_.empty()` 或用户显式“刷新”时触发全表异步读取；重建期间保留旧视图（不显示空白）。
- 重建失败时保留旧缓存并记录 WARN。

### 3.5 订阅切换
- `applySubscriptionFilter` 仅过滤 `allProxies_`；缓存为空时异步重建，重建完成前保持当前视图（不清空列表）。

### 3.6 同时更新缓存与持久化（新增）
- **写路径**：测试结果写入 DB 成功后，同步更新 `exItems_` 对应行与 `dirtyIndexIds_`，不等待事件回填。
- **插入代理**：`insertProxy` 写库成功后，同步追加到 `allProxies_` 与 `insertedProxies_`。
- **订阅更新/导入/去重**：写库成功后，同步更新缓存或标记全缓存失效（异步重建）。
- **事务边界**：缓存更新在 DB 事务提交后执行，避免不一致。

### 3.7 接口与线程模型
- `ProxyListPanel`（UI 线程）：`markDirty(const std::vector<std::string>& ids)`、`flushDirty()`、`updateCacheRow(indexId, exItem)`。
- `ProxyBatchTester`（worker 线程）：仅通过 `rowsUpdatedCallback` 通知，不直接写缓存。
- 所有缓存读写在 UI 线程，避免锁。

### 3.8 缓存失效矩阵
| 操作 | 失效范围 | 处理 |
|------|----------|------|
| 批量测试完成 | 脏项 exItems_ | 增量刷新 |
| 插入高优先级代理 | 插入项 | 标记脏 + 同步追加缓存 |
| 订阅更新/导入 | 全缓存 | 异步重建 |
| 去重 | 全缓存 | 异步重建 |
| 删除代理 | 对应行 | 从缓存移除 |

### 3.9 失败/退避策略
- 读取失败：保留旧缓存，记录 WARN，连续 3 次失败后退避 5s。
- 重建失败：保留旧视图，不显示空白。

### 3.10 与 RefreshCommands 对齐
- 批量测试完成发 `DATA_ROWS_CHANGED`；增量刷新不重复发全量命令。
- 订阅切换不触发全表 reload。

## 4. 验收

- 批量测试期间切换订阅不阻塞、不空白。
- 刷新不触发全表扫描（日志可验证无 `getAll()` 全表调用）。
- 定时器串行化，无重叠读。
- 连续失败 3 次后退避。
- 写库成功后缓存同步更新（日志可验证）。

## 5. 风险

- 增量读取需 DAO 支持批量 IN 查询（已有 `getAllByIndexIds`）。
- 脏集合需 UI 线程独占（已定义）。
- 缓存失效矩阵需覆盖所有写路径。
- 同步更新缓存需保证事务边界，避免不一致。
