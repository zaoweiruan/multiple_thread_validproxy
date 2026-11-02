# 批量测试进行中点击「全部」订阅时 UI 阻塞

**Goal:** 修复批量测试运行中切换到「全部」订阅时 UI 线程被阻塞的问题；根因为 incremental refresh 的 O(B×N) 线性扫描 + UI 线程同步 DB 读 + 事件风暴。
**Tech Stack:** C++17、wxWidgets 3.2 (wxMSW)、SQLite WAL、Catch2。
**依据文档:** `docs/DEV-PROCESS.md`、`docs/specs/2026-10-30-Spec-IncrementalProxyCacheRefresh-v1.0.md`（§3.6 dirty set + 1s 串行定时器）。

---

## 0. 现场证据

- 用户复现：启动 `bin/validproxy.exe`，点击批量测试（4920 个代理），测试进行中点击「全部」订阅 → UI 阻塞。
- 用户日志 `bin/log/ui_20261102_151906.log`：15:19:38 开始测试；15:22:18 `SQL错误: bad parameter or other API misuse`，日志结束（进程挂起/被强杀）。
- 代码证据：`MainFrame` 的 `wxEVT_PROXY_ROWS_UPDATED` 转发（commit 6547fcb）调用 `proxyPanel_->refreshResultsFor(ids)` —— 在 UI 线程上同步执行：
  - `loadProxyResultsFor(ids)`：每个 id 一次 `getByIndexId`，使用**共享** `db_` 连接 → 与批量测试写入线程并发使用同一 SQLite 连接 → `SQLITE_MISUSE`（`bad parameter or other API misuse`）。
  - `refreshResultsFor` 三处 O(N×B) 线性扫描：`exItems_` 内层循环、`syncHistoryForIndexId` 再扫一次 `exItems_`、`notifyTestResultChangedFor` → `findRowByIndexId` 扫 `proxies_`（在「全部」视图 = 4920 行；全库 53k）。
  - `ItemChanged` 每行触发 MSW 数据视图行重绘。
  - 事件风暴：每次批量刷新（~4920 个代理 / ~1-2 秒）都 queues 一个 `ProxyRowsUpdatedEvent`，在 UI 线程上排队执行 → UI 被持续占满，点击「全部」的事件（含 `applySubscriptionFilter` 全表复制 + `Reset(4920)`）被阻塞在队列尾部。

## 1. 根因

1. **MainFrame 转发绕过了 Spec §3.6 的 dirty set + 1s 串行定时器设计**：直接同步调用 `refreshResultsFor(ids)`，把每次批量刷新都变成 UI 线程的 O(B×N) 扫描。
2. `refreshResultsFor` 内部 3 处全缓存线性扫描（`exItems_` ×2 + `proxies_` ×1），在「全部」视图（53k 行）下每次 flush 事件上百万次字符串比较。
3. `loadProxyResultsFor` 在 UI 线程使用共享 `db_` 连接 → 与批量测试写入线程并发使用 → `SQLITE_MISUSE`（`bad parameter or other API misuse`）。
4. `syncHistoryForIndexId` 和 `findRowByIndexId` 各自 O(N) 线性扫描，可避免。

## 2. 修复

### 2.1 `src/ui/MainFrame.cpp`
- 转发改为 `proxyPanel_->markDirty(evt.getIndexIds())`（Spec §3.6 设计意图：dirty set + 1s 串行定时器），避免每次批量刷新都同步执行 `refreshResultsFor`。

### 2.2 `src/ui/ProxyListPanel.cpp`
- `onProxyRowsUpdated`: 仅 `markDirty(ids)`，移除直接 `refreshResultsFor(ids)` 调用（避免 MainFrame 转发 + panel 绑定双重处理）。
- `markDirty`: 确保定时器运行（构造函数已启动，这里做防御）。
- `refreshResultsFor`: O(B×N) → O(N+B)：
  - 一次 `std::unordered_map` 索引 `exItems_`（O(N)），每次更新 O(1) 查找；
  - 一次索引 `proxies_` 显示行（O(V)），每次 `notifyTestResultChangedFor` O(1)；
  - `syncHistoryForIndexId` → 改为 `syncHistoryFor(const ProfileExItem&)`（直接传入已找到的元素，避免再扫 `exItems_`）。
- `applySubscriptionFilter`：加入耗时 TRACE/INFO 日志，便于定位 UI 阻塞（≥100ms 记 INFO）。

### 2.3 `src/ui/AppController.cpp`
- `loadProxyResultsFor`：改为使用独立的 WAL 连接（参考 `loadProxiesAsync` 的 reader 模式），避免 UI 线程与批量测试写入线程共用 `db_` 连接 → 消除 `SQLITE_MISUSE`/busy contention。

### 2.4 `src/ui/ProxyListModel.cpp` / `.h`
- 新增 `syncHistoryFor(const ProfileExItem& ex)`（O(1)，读取传入元素）；
- 新增 `notifyTestResultChangedAtRow(int viewRow)`（O(1)）；
- 保留旧接口 `syncHistoryForIndexId` 为薄封装（兼容）。

## 3. 验证

- `cmake --build build --target validproxy --parallel 8` 通过。
- `.\scripts\build-and-test.bat` → 12/12 全绿（含 `UI_FLOATINGWIDGET` 15.69s）。
- 用户在 `bin/` 生产环境手工复验：启动批量测试 → 切换到「全部」订阅 → UI 不应再出现持续阻塞。
- 日志中 `[ProxyListPanel] refreshResultsFor: ... elapsed=Xms`（≥100ms 才记 INFO）应出现 ≤ tens of ms；若仍超时请贴日志。

## 4. 教训

- Spec §3.6 设计的「脏集合 + 1s 串行定时器」必须在 MainFrame 转发中严格使用，避免每次 flush 事件都同步执行全量刷新。
- 增量刷新的 per-row 线性扫描（`exItems_` / `proxies_`）在 53k 行库下是不可接受的 → 全部改为 map 索引。
- UI 线程读 SQLite 必须使用独立的 WAL 连接（参考 `loadProxiesAsync`），避免与批量测试写入线程共用 `db_` 连接。
