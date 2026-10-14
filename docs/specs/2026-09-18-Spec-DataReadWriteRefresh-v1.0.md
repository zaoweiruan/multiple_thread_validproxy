# 数据读写与界面刷新机制优化方案 v1.0

| 元信息 | 内容 |
| :--- | :--- |
| 文档类型 | Spec（技术方案） |
| 日期 | 2026-09-18 |
| 模块 | ProxyListPanel / MainFrame / AppController / Deduplicator / RegionBatchResolver / DatabaseHelper / DatabaseConnectionService |
| 版本 | v1.1（含 §11 补充调查修订：撤销「写连接无 PRAGMA」误判，收窄 Transaction 迁移范围，G3/G4 改用既有 `SubscriptionRefreshEvent`） |
| 状态 | 🔄 in-progress（P0 完成，P1/P2 未开始） |
| 关联分支 | `Optimize-data-read-write-refresh` |
| 关联文档 | `docs/bugfix/2026-08-21-Bugfix-ProxyListPanel-RegionAutoRefresh-v1.0.md`、`docs/specs/2026-08-24-Spec-ProxyListPanel-InstantSubSwitch-v1.0.md`、`docs/specs/2026-08-18-Spec-ProxyListRefresh-v2.0.md`、`docs/specs/2026-07-24-Spec-SubscriptionWritePerformance-v1.0.md`（draft） |

---

## 1. 背景与目标

### 1.1 问题陈述

用户反馈：数据发生变更时（如**地区解析**、**去重**等操作后），界面反应迟滞，需要**手工重新刷新**才能正确显示。

### 1.2 目标（用户原始要求，逐条对齐）

| # | 要求 | 可验证的验收标准 |
|---|---|---|
| R1 | 数据变更时**及时刷新** | 全部 8 类数据变更入口（§2.2 G1–G8）完成后，无需任何手工操作，界面在 ≤1s 内显示最新值 |
| R1a | **状态栏**同步反馈 | 同一时刻（0 延迟）在 Field0 显示人读完成文案，不等待列表异步重载（§4.8.2/4.8.3） |
| R1b | **日志窗口**留痕 | 每次数据变更在 `LogPanel` 追加一条 `[数据变更] ...` REPORT 行，时间与状态栏一致（§4.8.3） |
| R2 | **安全**写入 | 任何多语句写入均处于单一事务边界内，中途失败自动 ROLLBACK，不产生部分写入；写连接具备 WAL + busy_timeout，与并发读不再互相阻塞 |
| R3 | **全量**写入 | 面板缓存（`allProxies_` / `proxies_` / `exItems_`）在数据变更后整体失效并整体重载，不残留陈旧行（不采用「只补新增行」的增量假设） |

### 1.3 非目标（本期不做）

- 不重构 `ProxyListModel` 虚拟模型与 `wxDataViewListCtrl` 绑定方式（已有 `2026-05-29-proxylist-virtual-model-implementation.md`）。
- 不引入 MVVM/响应式框架。
- 不改造订阅写入的批量算法（`docs/specs/2026-07-24-Spec-SubscriptionWritePerformance-v1.0.md` 仍为独立 draft，见 §9）。
- 不改造 `-S / -sync` 数据库同步的**写入算法与目标库 schema 映射**（`SubitemUpdaterV2::syncDatabases` 既有实现保持不变）。
  > ⚠️ **初版断言已修正**：初版写「`-S / -sync` 仍为 TODO」**为假**。CLI `-S` 已实现（`src/main_cli.cpp:674-704`），GUI `doSyncDatabases`（`AppController.cpp:2506-2524`）也存在，AutoTask 亦可达（`main_cli.cpp:801` `steps = {"update","test","dedup","sync"}`）。真正 TODO 的只有菜单壳 `MainFrame::onMenuSyncDb:1061-1072`（`syncDatabasesAsync` 调用被注释）。
  > **但同步完成后无刷新属本方案范围**：见 §2.2 新增缺口 **G9**——`doSyncDatabases` 仅发状态消息落入 `else` 分支零刷新，且 `syncDatabases` 经**独立连接**写目标库（`SubitemUpdaterV2.cpp:1141-1146`），`db_` 上的面板缓存看不到这批写入。

---

## 2. 现状评审（代码实证）

### 2.1 界面刷新四层机制

`ProxyListPanel` 维护双表缓存：`proxies_` / `allProxies_` ← `ProfileItem`（Region / SubId / remarks），`exItems_` ← `ProfileExItem`（Delay / Failures / Starts / Runtime / Health / Message）。

| 方法 | 位置 | 语义 | 读取范围 |
|---|---|---|---|
| `refreshResults()` | `src/ui/ProxyListPanel.cpp:191` | 重载 `exItems_` + `rebuildMaps` + `setRunningDurations` + `Refresh` | **全表**（UI 线程同步两次全表 DB 读） |
| `refreshResultsFor(indexIds)` | `src/ui/ProxyListPanel.cpp:217` | 增量刷新指定行 | 仅指定 indexId |
| `reloadFromDatabase()` | `src/ui/ProxyListPanel.cpp:268` | `controller_->loadProxiesAsync(currentSubId_, this)` | **全表**（后台线程独立连接） |
| `applySubscriptionFilter(subId)` | `src/ui/ProxyListPanel.cpp:282` | 纯内存过滤（`cacheReady_` 未就绪回退 `reloadFromDatabase`） | 0 次 DB 读 |

**关键结构性事实**：`refreshResults()` **从不重载 `proxies_`**，因此任何只落在 `ProfileItem` 的列（典型为 `Region`）都无法通过它刷新。这正是 2026-08-21 RegionAutoRefresh bugfix 的根因，且该根因是**机制性的**——同样的陈旧会出现在其他 `ProfileItem` 写入路径上。

### 2.2 刷新缺口矩阵（本任务核心发现）

当前全量重载仅由 3 个入口触发：`DEDUP_OK`（`src/ui/MainFrame.cpp:1501`）、`REGION_RESOLVE_DONE`（`:1519`）、订阅面板手动「刷新」按钮（`src/ui/SubscriptionPanel.cpp:373` 发 `SubscriptionRefreshEvent`）。

| # | 操作入口 | 代码位置 | 实际写表 | 现状缺口 |
|---|---|---|---|---|
| G1 | 测试单订阅完成 | `AppController::doTestSubscription` → `:2120` `resolveRegionsForValidProxies(subId)` | `ProfileItem.Region` | **不发任何刷新命令**；只广播 `ProxyTestProgressEvent` → MainFrame → `refreshResults()`（仅 `exItems_`）→ **Region 列永久陈旧** |
| G2 | 测试全部完成 | `:2206` `resolveRegionsForValidProxies()`（无 subId） | `ProfileItem.Region` | 同 G1 |
| G3 | 更新单订阅 | `doUpdateSubscription:2067`，仅发 `StatusUpdateEvent("Update completed: "+subId)`（`:2077`） | `ProfileItem` + `Subitem` | **代理面板**零刷新。⚠️ 初版描述夸大：订阅面板**已在刷新**——`MainFrame.cpp:183-189` 的 Bind lambda 已对 `payload.StartsWith("Update completed:")` 执行 `controller_->loadSubscriptionsAsync(this)` |
| G4 | 更新全部订阅 | `doUpdateAllSubscriptions:2088`，仅发 `"All subscriptions updated"`（`:2098`） | 同上 | 同 G3（订阅面板由 `"All subscriptions updated"` 前缀覆盖） |
| G5 | AutoTask 流水线 | `doAutoTaskImpl:2794`，progressCallback `:2801-2809` 仅发中文步骤名 | 全部（内置 update / test / dedup / resolve_region） | **每步骤完成后零刷新**（既有 bugfix 文档已列为已知限制） |
| G6 | 导入订阅 URL | `importSubscription:277-285`（初版误标 `:2100`），同步 `SubitemUpdaterV2::importSingleUrl` 返回 `bool` | `ProfileItem` + `Subitem` | 调用方无后续事件 |
| G7 | 去重成功交互 | `MainFrame::onStatusUpdate` DEDUP_OK 分支（`:1501-1508`） | — | 先异步重载、**再** `wxMessageBox("Dedup completed.")` 模态框 → 事件循环阻塞，观感「迟滞/需手动」 |
| G8 | 去重触发方式 | `MainFrame::onMenuDedup` | — | 裸 `std::thread([...]{...}.detach()`，**无 `AsyncOperationGuard` / `isRunning_` 保护**，可与其他写操作并发 → 写入期间读库可能读到中间态 |
| **G9**（v1.1 新增） | **GUI 数据库同步** | `AppController::doSyncDatabases:2506-2524`，完成后仅发 `StatusUpdateEvent(0, "数据库同步完成")` | 目标库全表（经**独立连接**） | 状态消息落入 `else` 分支 → **零刷新**；且 `syncDatabases` 走 `SubitemUpdaterV2.cpp:1141-1146` 的独立 `sqlite3_open_v2(targetDbPath, ...)` 写目标库，`db_` 上的面板缓存**看不到这批写入**，因此**必须**触发全量重载而非增量。AutoTask 可达（`main_cli.cpp:801` `steps = {"update","test","dedup","sync"}`） |

补充：`resolveRegionsForValidProxies(subId="")`（`AppController.cpp:2583`）= `RegionBatchResolver resolver(db_, config_); return resolver.run(subId);`——**无 progressCallback、不广播刷新命令**，确认 G1/G2。

### 2.3 写入安全现状

| 路径 | 事务现状 | 问题 |
|---|---|---|
| `src/update/Deduplicator.cpp::deduplicate()` | ✅ 已有 `BEGIN TRANSACTION` 包裹 6 个 phase + COMMIT/ROLLBACK | ❌ **缺陷 W1**：`deduplicatePhase0` 字符串拼接 SQL（`"UPDATE ProfileItem SET SubId = '" + protectedSubId + "' WHERE ..."`），subid 含单引号即语法错（虽来源为 config）；❌ **缺陷 W2**：无 `busy_timeout`，长事务期间并发写立即 `SQLITE_BUSY` |
| `src/RegionBatchResolver.cpp::flushRegionBuffer()`（`:362`） | ✅ `prepare_v2` + `BEGIN` + 循环 bind/step/reset + COMMIT/ROLLBACK；析构（`:120`）与退出锁外（`:228`）均 flush；缓冲区 swap 在锁内、flush 在锁外 | 无实质缺陷（设计良好，应作为其他写入路径的范本） |
| `AppController::doResolveSingleProxyRegion` | ❌ 单条 `UPDATE ... SET Region=? WHERE IndexId=?`，无显式事务 | 单行 auto-commit 安全但每次 fsync；与 `RegionBatchResolver` 逻辑重复 |
| **主写连接 `db_`** | ✅ **已配置**——`AppController::switchDatabase:154-169` 在 `sqlite3_open_v2(..., FULLMUTEX)` 成功后于 `:169` 调用 `service::DatabaseConnectionService::applyPragmas(db_)` | ⚠️ **缺陷 W3（真实问题，非性能瓶颈）**：`applyPragmas`（`src/service/DatabaseConnectionService.cpp:19-31`，含 `busy_timeout=5000`/`journal_mode=WAL`/`cache_size=-8000`/`synchronous=NORMAL`/`temp_store=MEMORY`/`mmap_size=268435456`/`journal_size_limit=67108864` + `CREATE INDEX IF NOT EXISTS idx_profile_dedup`）本身无缺，但 **3 个 async reader 各自内联重写了一遍 PRAGMA 而未调用它**（`loadSubscriptionsAsync:218-226`、`loadProxiesAsync:325-333`、`getRunningDurationsAsync:480-484`），且 `journal_size_limit` 取值不一致（service=64MB / reader=1GB）——属配置分叉漂移源。另全链路 `busy_timeout` 仅 5000ms（见 §4.5.1 修订） |
| `include/DatabaseHelper.h`（`db::Database`，70 行全文） | ❌ 无事务 API、无 PRAGMA 方法 | 各调用方各自 `sqlite3_exec("BEGIN")`，无异常安全保证 |

### 2.4 读路径性能现状

| 位置 | 问题 |
|---|---|
| `AppController::loadProxyResults():405` | `ProfileExItemDAO::getAll()` 全量 53k 后调 `scoring::computeBatch(items, ...)`，**结果被直接丢弃**（`:410-416` 注释自认「已 baked into rebuildMaps」）→ 每次 `refreshResults` 白算一遍全量评分 |
| `AppController::loadProxyResultsFor():426` | for 循环逐个 `dao.getByIndexId(*it)`，N 次独立 prepare/step，未用 `WHERE IndexId IN (...)` |
| `ProxyListPanel::refreshResults():191` | UI 线程同步 `loadProxyResults()` + `getRunningDurations()` 两次全表读；而 `getRunningDurationsAsync:467` + `RunningDurationsLoadedEvent` **已存在却未被使用** |
| `include/ProfileExItem.h:91` `ProfileExItemDAO` | 仅有 `getAll()`（`:107`）/ `getByIndexId(string)`（`:108`），**无批量按 indexIds 查询接口** |
| `src/ui/Events.h`（428 行） | `StatusUpdateEvent`(147) 的字符串命令**以裸字面量散落各文件，无常量集中定义**（`DEDUP_OK` / `DEDUP_FAIL` / `REGION_RESOLVE_DONE` 均内联） |

---

## 3. 根因分析

1. **刷新语义分裂**：`refreshResults()`（只 `exItems_`）与 `reloadFromDatabase()`（只 `proxies_`）两个名字相近的方法覆盖不同数据表，且**没有任何一处自动把「ProfileItem 被写」映射到 `reloadFromDatabase()`**。每个新写入点都需要作者手工记得补一条命令 → G1/G2/G3/G4/G5/G6 全部遗漏。
2. **协议层缺位**：刷新触发依赖 `StatusUpdateEvent` 里的裸字符串，无类型检查、无集中定义，且该事件同时承担「命令」与「用户消息」两种语义，`MainFrame::onStatusUpdate` 的 `else` 分支兜底吞掉一切未识别字符串（G3/G4/G6 静默丢失的直接原因）。
3. **PRAGMA 配置分叉（原判定已撤销）**：初版误判「写连接无 PRAGMA 是迟滞放大器」，复核 `switchDatabase:169` 确认其已调用 `applyPragmas`，WAL 已生效（WAL 模式下读写不互相阻塞）。**真实缺陷是分叉**：3 个 async reader 内联重写 PRAGMA 且 `journal_size_limit` 取值不一致，属维护风险而非性能瓶颈。
4. **交互阻塞**：G7 的模态框在数据已刷新后才弹出但阻塞事件循环，用户必须先关框才能看到界面，主观感受等同于「没刷新」。

---

## 4. 优化方案

### 4.1 设计原则

- **写入方声明，UI 方执行**：后台写操作完成后只广播一条「数据已变更 + 变更面」命令，由 MainFrame 统一路由到面板重载方法。写入方不感知 UI。
- **默认全量，局部才降级**：任何不确定的写入面一律触发全量重载（满足 R3）。仅对已精确知道「只改了哪些 indexId」的路径（探活完成）保留增量刷新。
- **高频路径不加成本**：不向 `refreshResults()` 附加 5 万行级全量重载开销（2026-08-21 bugfix 已确立的约束）。
- **写连接与读连接同等加固**：PRAGMA 配置收敛为单一入口，杜绝再次分叉。
- **文档先行、逐条可验证**：每个缺口 G1–G8 均有独立验收项（§7.3）。

### 4.2 刷新协议统一（P0，对应 R1）

**新增文件 `include/RefreshCommands.h`** —— 集中定义全部字符串命令常量，替换现有裸字面量：

```cpp
// include/RefreshCommands.h
#pragma once
#include <string_view>

namespace ui {
namespace refresh_cmd {
    // 既有命令（保持字符串值不变，避免破坏已实现路径）
    inline constexpr std::string_view kStatusResolved      = "RESOLVE_REGION_START";
    inline constexpr std::string_view kRegionResolveDone   = "REGION_RESOLVE_DONE";
    inline constexpr std::string_view kDedupOk             = "DEDUP_OK";
    inline constexpr std::string_view kDedupFail           = "DEDUP_FAIL";

    // 新增命令
    inline constexpr std::string_view kRowsChanged         = "DATA_ROWS_CHANGED";   // ProfileItem 变更 → 全量重载 proxies_
    inline constexpr std::string_view kResultsChanged      = "DATA_RESULTS_CHANGED"; // ProfileExItem 变更 → 全量重载 exItems_
    inline constexpr std::string_view kSubscriptionsChanged= "DATA_SUBS_CHANGED";    // Subitem 变更 → 重载订阅面板
}  // namespace refresh_cmd
}  // namespace ui
```

**`MainFrame::onStatusUpdate` 路由表**（`src/ui/MainFrame.cpp:1495`）扩展为显式分支，并**把未识别命令从「静默吞掉」改为 `Logger::WARN`**（防止再次出现 G3/G4/G6 式静默丢失）：

| 命令 | 动作 |
|---|---|
| `kDedupOk` | `loadSubscriptionsAsync` +（有选中订阅时）`loadProxiesAsync(currentSubId)` + 状态栏提示（**移除模态框**，见 §4.6） |
| `kDedupFail` | 状态栏 WARN + `LogPanel` 日志（保留模态框，失败需用户注意） |
| `kRegionResolveDone` | `proxyPanel_->reloadFromDatabase()` |
| `kRowsChanged` | `proxyPanel_->reloadFromDatabase()` |
| `kResultsChanged` | `proxyPanel_->refreshResults()` |
| `kSubscriptionsChanged` | `subPanel_->reload()`（现有订阅重载入口） |
| 未识别且以 `DATA_` 开头 | `Logger::WARN` 记录命令原文（协议漂移早警） |
| 其他 | `setStatusText(0, text)`（保持原行为） |

**并发保护**：`onStatusUpdate` 入口增加防抖——同一命令在 `refreshInFlight_` 为 true 时合并（已有 `std::atomic<bool> refreshInFlight_`，扩展为命令去重而非仅防线程堆积）。

### 4.3 缺口补齐（P0，对应 R1 + R3）

在写操作**成功完成**后追加命令广播（取消/异常路径**不**广播，与既有 `REGION_RESOLVE_DONE` 约束一致）：

| 缺口 | 修改点 | 追加内容 |
|---|---|---|
| G1 | `AppController::doTestSubscription`，`:2120` 之后 | `wxQueueEvent(topLevel, new StatusUpdateEvent(0, std::string(ui::refresh_cmd::kRowsChanged)))` |
| G2 | `AppController::doTestOnlineProxies` / 全量测试完成点 `:2206` 之后 | 同上 |
| G3 | `doUpdateSubscription:2077` | 在既有 `"Update completed"` 消息后**另发一条** `kSubscriptionsChanged` + `kRowsChanged` |
| G4 | `doUpdateAllSubscriptions:2098` | 同上 |
| G6 | `importSubscription:2100`，`importSingleUrl` 返回 `true` 后 | 同上（调用方同步执行，需在 UI 线程或经 `wxQueueEvent` 投递） |
| G5 | `doAutoTaskImpl:2801-2809` progressCallback | 见 §4.4 |

> **注意**：`StatusUpdateEvent` 构造签名为 `(int field, const std::string&)`，而命令常量为 `std::string_view`，广播点需 `std::string(cmd)` 显式转换（C++17，不使用 `auto`）。

### 4.4 AutoTask 步骤级刷新（P1，对应 G5）

`AutoTaskProgress`（`include/AutoTaskManager.h:48-55`）已含 `current_step` / `total_steps` / `step_name` / `step_status` / `percent` / `message`。在 `doAutoTaskImpl` 的 progressCallback 中增加**步骤完成边沿检测**：

```cpp
// AppController.cpp  doAutoTaskImpl progressCallback（伪代码，实际不使用 auto）
static std::string lastStepName;
static int lastStep = -1;
static bool lastRunning = false;

if (progress.step_name != lastStepName || progress.current_step != lastStep) {
    lastStepName = progress.step_name;
    lastStep = progress.current_step;
    lastRunning = (progress.step_status == "running");
}
if (lastRunning && progress.step_status != "running") {
    // 步骤从 running 跳变为 done/failed/skipped → 该步骤已收尾
    std::string cmd;
    switch (AutoTaskManager::stepNameToType(progress.step_name)) {
        case AutoTaskType::UPDATE_ALL:    cmd = std::string(ui::refresh_cmd::kSubscriptionsChanged) + " " +
                                                 std::string(ui::refresh_cmd::kRowsChanged); break;
        case AutoTaskType::TEST_ALL:      cmd = std::string(ui::refresh_cmd::kResultsChanged); break;
        case AutoTaskType::DEDUP:         cmd = std::string(ui::refresh_cmd::kDedupOk); break;
        case AutoTaskType::RESOLVE_REGION: cmd = std::string(ui::refresh_cmd::kRowsChanged); break;
        case AutoTaskType::SYNC:          cmd = std::string(ui::refresh_cmd::kRowsChanged); break;
        case AutoTaskType::EXPORT:        cmd.clear(); break;   // 仅写文件，不刷新
        default: cmd.clear(); break;
    }
    if (!cmd.empty()) {
        wxQueueEvent(topLevel, new StatusUpdateEvent(0, cmd));
    }
}
```

> 实现细节：`stepNameToType` 现位于 `src/AutoTaskManager.cpp:371-378`（私有/静态），需提升为 `AutoTaskManager` 的**公有静态**方法以便 AppController 复用。EXPORT 不刷新因它只写磁盘分享链接文件。

### 4.5 写入安全加固（P0，对应 R2）

#### 4.5.1 PRAGMA 分叉收敛（P1，原「写连接无 PRAGMA」判定已撤销，见 §11.1）

> ⚠️ **本节初版有误**：复核 `AppController::switchDatabase:169` 确认主写连接已调用 `service::DatabaseConnectionService::applyPragmas`，WAL 已生效。**真实问题是配置分叉**（3 个 async reader 内联重写 PRAGMA 且 `journal_size_limit` 取值不一致），属 P1 维护性修复，不是 P0 性能瓶颈。原「新增 `db::applyDefaultPragma`」方案**作废**——改为删除 3 处内联块、统一调用既有 `applyPragmas`。

新增 `DatabaseHelper.h/.cpp` 中的纯函数，**写连接与读连接共用同一实现**，彻底消除配置分叉：

```cpp
// include/DatabaseHelper.h  新增
namespace db {
// 将连接配置为「本项目标准读/写策略」。
// writable=false 仅用于只读副本（当前无此场景，保留对称性）。
void applyDefaultPragma(sqlite3* db, bool writable = true);
}
```

```cpp
// src/DatabaseHelper.cpp  新增
#include "DatabaseHelper.h"
#include <sqlite3.h>
namespace db {
void applyDefaultPragma(sqlite3* db, bool writable) {
    if (!db) return;
    static const char* const kPragmas[] = {
        "PRAGMA journal_mode=WAL;",
        "PRAGMA synchronous=NORMAL;",
        "PRAGMA busy_timeout=10000;",          // 写端 10s，覆盖 Deduplicator 6-phase 长事务
        "PRAGMA cache_size=-8000;",
        "PRAGMA temp_store=MEMORY;",
        "PRAGMA mmap_size=268435456;",
        "PRAGMA journal_size_LIMIT=1073741824;"
    };
    for (const char* sql : kPragmas) { sqlite3_exec(db, sql, nullptr, nullptr, nullptr); }
    if (writable) { sqlite3_exec(db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr); }
}
}  // namespace db
```

**调用点（4 处，全部替换为 `db::applyDefaultPragma`）**：
- `AppController::switchDatabase:169`（**主写连接，已调用，作为基准不动**）
- `AppController::loadSubscriptionsAsync:218-226`
- `AppController::loadProxiesAsync:325-333`
- `AppController::getRunningDurationsAsync:480-484`

> `busy_timeout` 由读端 5000ms 统一提升到 10000ms：Deduplicator 6-phase 事务在 5.3 万行库上可能超过 5s，5s 超时会导致读端返回 `SQLITE_BUSY` 进而显示空列表（误判为数据丢失）。

#### 4.5.2 事务 RAII（P0，消除散落 `sqlite3_exec("BEGIN")`）

```cpp
// include/DatabaseHelper.h  新增
namespace db {
// RAII 事务守卫：构造即 BEGIN，析构时若未 commit 则 ROLLBACK。
// 覆盖异常退出与提前 return 路径，保证「安全写入」。
class Transaction {
public:
    explicit Transaction(sqlite3* db);
    ~Transaction();
    bool commit();      // COMMIT 成功返回 true；失败自动 ROLLBACK
    bool rollback();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
private:
    sqlite3* db_ = nullptr;
    bool active_ = false;
    bool committed_ = false;
};
}  // namespace db
```

迁移清单（把裸 `sqlite3_exec("BEGIN TRANSACTION;")` 替换为 `db::Transaction txn(db.get())`）：
- `src/update/Deduplicator.cpp::deduplicate()` 6-phase 主事务
- `src/RegionBatchResolver.cpp::flushRegionBuffer():362`

#### 4.5.3 消除 SQL 拼接（P0，W1）

`src/update/Deduplicator.cpp::deduplicatePhase0` 的字符串拼接改为参数绑定：

```cpp
// 修复前（危险）
std::string sql = "UPDATE ProfileItem SET SubId = '" + protectedSubId + "' WHERE ...";

// 修复后
const char* kSql =
    "UPDATE ProfileItem SET SubId = ? WHERE IndexId IN (SELECT IndexId FROM ... )";
sqlite3_stmt* stmt = nullptr;
if (sqlite3_prepare_v2(db_, kSql, -1, &stmt, nullptr) != SQLITE_OK) { /* ROLLBACK */ }
sqlite3_bind_text(stmt, 1, protectedSubId.c_str(), -1, SQLITE_TRANSIENT);
sqlite3_step(stmt);
sqlite3_finalize(stmt);
```

#### 4.5.4 单行 Region 写入复用缓冲（P1）

`AppController::doResolveSingleProxyRegion`（`:2679-2777`）与 `RegionBatchResolver` 的 `fetchRegionFromIpWhoIs` / `parseRegionFromJson` / `UPDATE ... WHERE IndexId` 完全重复。改为：构造 `RegionBatchResolver`、`enqueue` 单条、析构时自动 flush（已有析构 flush 保证 `:120`），从而**免费获得事务包裹与批量能力**，并删除重复实现。

### 4.6 交互非阻塞化与并发保护（P1，对应 G7 / G8）

- **G7**：`MainFrame::onStatusUpdate` 的 `DEDUP_OK` 分支删除 `wxMessageBox("Dedup completed.")`，改为状态栏第 0 段显示「去重完成」+ `LogPanel` 追加一条 REPORT 日志。数据重载**先于**状态提示入队（保持既有顺序约束）。
- **G8**：`MainFrame::onMenuDedup` 的裸 `std::thread(...).detach()` 改为与其他操作一致的 `AsyncOperationGuard{workerThread_, isRunning_, cancelRequested_, this}` + `ScopeGuard` 模式（`doResolveRegionsBatch:2597` 已有完整范本）。收益：去重期间 `isRunning_` 置位 → 阻塞配置保存/切库等冲突操作，消除「写入中读库得到中间态」。

### 4.7 读路径优化（P2，性能，非阻塞交付）

| # | 修改点 | 变更 |
|---|---|---|
| R1 | `AppController::loadProxyResults():405-416` | 删除被丢弃的 `scoring::computeBatch(items, ...)` 调用（评分已在 `rebuildMaps` 内计算） |
| R2 | `include/ProfileExItem.h:91` `ProfileExItemDAO` | 新增 `std::vector<ProfileExItem> getAllByIndexIds(const std::vector<std::string>& indexIds, sqlite3* db)`，用 `WHERE IndexId IN (...)`（参数化占位符，无字符串拼接） |
| R3 | `AppController::loadProxyResultsFor():426` | 由 N 次 `getByIndexId` 改为单次 `getAllByIndexIds` |
| R4 | `ProxyListPanel::refreshResults():191` | `getRunningDurations()` 同步调用替换为已存在的 `getRunningDurationsAsync` + `RunningDurationsLoadedEvent`（`setRunningDurations` 增量更新，与 2026-08-18 Spec 已建通道一致） |

> R4 是唯一可能影响现有事件时序的改动，安排在 P2 独立提交，便于单独回滚。

---

## 4.8 三处 UI 同步点：面板 / 状态栏 / 日志窗口

> 用户明确问询：实施后数据变更是否**同时**在 panel、状态栏、日志窗口自动刷新。本节把三处作为显式交付目标，并在 `RefreshCommands.h` 内收敛。

### 4.8.1 三处目标与现状

| UI 区域 | 数据源 | 现状 | 本方案覆盖 |
|---|---|---|---|
| **代理列表面板** `ProxyListPanel` | `proxies_` / `exItems_` | 仅 3 个入口触发全量重载（§2.2） | ✅ §4.2–4.4 补齐 G1–G8 |
| **订阅列表面板** `SubscriptionPanel` | `Subitem` | 手动刷新按钮才触发 | ✅ §11.4 复用 `SubscriptionRefreshEvent` |
| **状态栏** Field0 | `StatusUpdateEvent` 文本 | 只在命令分支被**吞掉**（G3/G4/G6），或显示原始命令字符串 | ⚠️ **原方案缺口 → 本节补齐** |
| **日志窗口** `LogPanel` | `Logger` 输出 | 各写操作日志级别参差，无「数据已变更」统一标记 | ⚠️ **原方案缺口 → 本节补齐** |

### 4.8.2 集中映射：命令 → 状态栏文案（新增到 `RefreshCommands.h`）

避免「后台发一条、UI 拼一条」的分散逻辑，也便于单测：

```cpp
// include/RefreshCommands.h  新增
namespace ui {
namespace refresh_cmd {

// 未找到映射时返回空串（调用方据此跳过状态栏写入）
std::string statusTextFor(std::string_view command);

}  // namespace refresh_cmd
}  // namespace ui
```

映射表（放在 `src/ui/RefreshCommands.cpp`）：

| 命令 | 状态栏文案（Field0） |
|---|---|
| `kRowsChanged` | `"数据已更新，代理列表已刷新"` |
| `kResultsChanged` | `"测试结果已更新"` |
| `kRegionResolveDone` | `"地区解析完成，代理列表已刷新"` |
| `kDedupOk` | `"去重完成，列表已刷新"` |
| `kSubscriptionsChanged`（= `SubscriptionRefreshEvent` 旁路） | `"订阅已更新，列表已刷新"` |
| `kDedupFail` | `"去重失败（详见日志）"` |

### 4.8.3 `MainFrame::onStatusUpdate` 命令分支的三段式动作

每个**已识别命令**分支统一执行三步（顺序固定，便于手工验收）：

```cpp
// src/ui/MainFrame.cpp  onStatusUpdate 命令分支（伪代码，C++17 无 auto）
void MainFrame::onStatusUpdate(wxEvtCommand& event) {
    const std::string& text = event.GetText().ToStdString();

    // 第 1 步：人读文案 → 状态栏（先于重载入队，用户立刻看到反馈）
    std::string status = ui::refresh_cmd::statusTextFor(text);
    if (!status.empty()) {
        setStatusText(0, wxString::FromStdString(status));
    }

    // 第 2 步：日志窗口标记（REPORT 级，生产默认可见）
    if (text == std::string(ui::refresh_cmd::kRowsChanged) ||
        text == std::string(ui::refresh_cmd::kResultsChanged)) {
        Logger::write("[数据变更] " + status, LogLevel::REPORT);
    }

    // 第 3 步：面板重载（防抖合并后执行）
    if (text == std::string(ui::refresh_cmd::kRowsChanged)) {
        proxyPanel_->reloadFromDatabase();
    } else if (text == std::string(ui::refresh_cmd::kResultsChanged)) {
        proxyPanel_->refreshResults();
    }
    // ... 其余分支
}
```

**要点**：
- **状态栏先于重载**入队——`reloadFromDatabase` 是异步后台全表读，用户不必等重载完成才看到反馈。
- `Logger::REPORT` 而非 `INFO`/`DEBUG`：生产默认 `console_level=WARN` 会隐藏 REPORT，因此**日志窗口的可见性取决于 `bin/config.json` 的 `log.console_level` / `LogPanel` 过滤器设置**。若设为 `WARN` 或更高，用户在日志窗口看不到这些行——这不是缺陷，是既有日志级别策略，验收时需先确认为 `REPORT` 或更宽。
- 未在映射表中的命令（含 `kDedupFail` 的失败路径）**不写状态栏成功文案**，避免误导；失败仍走模态框 + `ERR` 日志。

### 4.8.4 「即时」的准确语义

| 区域 | 时延 | 说明 |
|---|---|---|
| 状态栏 | **同步，0 延迟** | `onStatusUpdate` 在 UI 线程内直接 `setStatusText` |
| 日志窗口 | **同步，0 延迟** | `Logger::write` 同线程，`LogPanel` 即时追加 |
| 代理列表面板 | **异步，典型 100ms–1s** | `reloadFromDatabase` → `loadProxiesAsync` 后台线程全表读 53,837 行 + `ProfileExItemDAO::getAll` + `buildProxyListMaps` → `ProxyListLoadedEvent` 回 UI |
| 订阅列表面板 | **异步，毫秒级** | `loadSubscriptions` 数据量小 |

**结论**：状态栏与日志窗口是**同步即时**的；代理列表面板是**异步近实时**（§1.2 R1 的 ≤1s 上限），不是字面意义的「同一帧」。这是刻意的权衡——在 UI 线程做 5 万行全表读会造成数秒卡死（`docs/bugfix/2026-06-11-Bugfix-ProxyListPanel-RefreshFreeze-v1.0.md` 已有教训）。

### 4.8.5 新增验收项

| 编号 | 操作 | 预期 |
|---|---|---|
| V13 | 执行任一数据变更（如 V2 测试订阅） | 状态栏 Field0 在操作完成瞬间显示人读文案，无需等待列表重载完成 |
| V14 | 同上，`LogPanel` 过滤器设为 REPORT 或更宽 | 日志窗口出现 `[数据变更] ...` 行，时间与状态栏一致 |
| V15 | 将 `log.console_level` 设为 WARN，重复 V14 | 日志窗口**不出现**该行（预期行为，验证级别策略而非缺陷） |

---

## 5. 改动清单（文件级）

### 5.1 新增

| 文件 | 内容 |
|---|---|
| `include/RefreshCommands.h` | 刷新命令常量 + `statusTextFor()` 映射声明（§4.2 / §4.8.2） |
| `src/ui/RefreshCommands.cpp` | 命令→状态栏文案映射表实现（§4.8.2） |
| `tests/test_refresh_commands.cpp` | 命令常量唯一性 + `stepNameToType` 映射 + `statusTextFor` 映射纯函数单测 |
| `tests/test_db_transaction.cpp` | `db::Transaction` RAII 单测（**含 `sqlite3_get_autocommit` 嵌套感知用例**，§11.2）。~~`applyDefaultPragma` 单测~~ 随 §11.1 作废——改为断言 3 个 async reader 不再内联 PRAGMA、统一调用 `DatabaseConnectionService::applyPragmas` |

### 5.2 修改

| 文件 | 改动 | 阶段 |
|---|---|---|
| `include/DatabaseHelper.h` / `src/DatabaseHelper.cpp` | +`applyDefaultPragma` +`Transaction` RAII | P0 |
| `src/ui/AppController.cpp` | PRAGMA 4 处收敛；G1/G2/G3/G4/G6 补命令；AutoTask 边沿检测（§4.4）；单行 Region 复用 resolver；`loadProxyResults` 删无效评分；`loadProxyResultsFor` 批量化 | P0/P1/P2 |
| `src/ui/MainFrame.cpp` | `onStatusUpdate` 路由表扩展 + 未识别 `DATA_*` WARN + 防抖；G7 去模态框；G8 `AsyncOperationGuard` | P0/P1 |
| `src/update/Deduplicator.cpp` | `Transaction` 替换裸 BEGIN；Phase0 参数绑定 | P0 |
| `src/RegionBatchResolver.cpp` | `Transaction` 替换裸 BEGIN | P0 |
| `include/ProfileExItem.h` | `ProfileExItemDAO::getAllByIndexIds` | P2 |
| `src/ui/Events.h` | 无结构改动；命令常量移至 `RefreshCommands.h`（避免事件头膨胀） | — |
| `include/AutoTaskManager.h` / `src/AutoTaskManager.cpp` | `stepNameToType` 提升为公有静态 | P1 |
| `docs/INDEX.md` | 登记本文档（§7.5 表 #46 + §8.2 表） | — |
| `docs/plans/project-plans-tracker.md` | 登记执行状态 | — |

---

## 6. 分阶段实施

| 阶段 | 范围 | 交付物 | 可独立回滚 |
|---|---|---|---|
| **P0** | §4.2 协议统一 + §4.3 缺口 G1/G2/G3/G4/G6 + §4.5.1 PRAGMA 收敛 + §4.5.2 Transaction RAII + §4.5.3 Phase0 参数绑定 | 满足 R1（除 AutoTask）+ R2 + R3 | 是 |
| **P1** | §4.4 AutoTask 步骤级刷新（G5）+ §4.5.4 单行 Region 复用 + §4.6 G7/G8 | 满足 R1 全量 | 是 |
| **P2** | §4.7 读路径 R1–R4 | 性能收益，UI 观感进一步改善 | 是（R4 单独提交） |

每阶段一个 commit，遵循「文档先行 → 实现 → 验证 → 登记 INDEX」流程。

---

## 7. 测试与验证计划

### 7.1 单元测试（TDD，先写后实现）

| 测试文件 | 用例 |
|---|---|
| `tests/test_refresh_commands.cpp` | ① 全部 `ui::refresh_cmd::*` 非空且互不相同（防重复字面量）；② `AutoTaskManager::stepNameToType` 对 7 个步骤名（`update`/`update_all`/`test`/`test_all`/`dedup`/`sync`/`export`/`resolve_region`）的映射 |
| `tests/test_db_transaction.cpp` | ① 构造 → `commit()` 返回 true → 数据可见；② 构造 → 写入 → 析构（未 commit）→ 数据**不可见**（ROLLBACK 生效）；③ 嵌套/重复构造不崩溃；④ `applyDefaultPragma` 后 `PRAGMA journal_mode` 返回 `wal`、`PRAGMA busy_timeout` = 10000 |
| `tests/test_profile_ex_item_dao.cpp` | 新增 `getAllByIndexIds`：空列表返回空、部分命中、无命中、indexId 含特殊字符（单引号）不报错 |
| `tests/test_deduplicator.cpp` | 新增：`protectedSubId` 含单引号时 Phase0 不抛 `SQLITE_MISUSE`/语法错（W1 回归） |

### 7.2 构建与回归

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
ctest -V
.\scripts\build-and-test.bat    # UI 变更交付前必须全绿
```

验证一律使用 `bin/` 下最新构建产物；**禁止**使用 `bin/worker/validproxy.exe`（陈旧副本）。

### 7.3 手工验收清单（逐条对应 §2.2 缺口）

测试库：`test/guindb.db`（53,837 profiles / 44 SubIDs），配置 `bin/test_config.json`。

| 编号 | 操作 | 预期 |
|---|---|---|
| V1 | 选中订阅 → 右键「解析地区」（单代理） | Region 列自动更新，**无需任何手工刷新** |
| V2 | 选中订阅 → 「测试订阅」 | 测试完成 1s 内 Region 列 + Delay 列同步为最新 |
| V3 | 「测试全部」 | 同上 |
| V4 | 订阅面板 → 右键「更新」（单订阅） | 订阅列表 + 代理列表均自动重载 |
| V5 | 订阅面板 → 「更新全部」 | 同上 |
| V6 | 「导入订阅」URL | 新代理出现在列表中（无需点刷新） |
| V7 | 设置向导运行 AutoTask（含 update_all + test_all + dedup + resolve_region） | 每个写库步骤完成后界面自动刷新，步骤间无需手工干预 |
| V8 | 「去重」 | 完成后**无模态框**，状态栏显示「去重完成」，订阅列表与代理列表已重载 |
| V9 | 去重进行中点击「保存配置」 | 被拒绝（`isRunning_` 生效，G8 修复生效） |
| V10 | 去重进行中在代理列表输入搜索词 | 搜索不被 `SQLITE_BUSY` 阻塞（PRAGMA busy_timeout 生效） |
| V11 | 探活完成后 | 相关行增量刷新（既有能力不回退） |
| V12 | 订阅面板手动「刷新」 | 既有能力不回退 |

---

## 8. 风险与回滚

| 风险 | 影响 | 缓解 |
|---|---|---|
| `busy_timeout` 10s 使「库被占用」时的等待变长 | 极端情况下 UI 线程读库等待至多 10s | 读端读库全部已异步化（`loadProxiesAsync` 等），仅搜索/排序等纯内存路径不受影响 |
| ~~`refreshResults()` 由 G3/G4 触发后变为高频路径~~ | **该行已失效，删除** | **修订（v1.1）**：G3/G4/G6 经 §11.4 后只发 `kRowsChanged`（异步 `reloadFromDatabase`）+ 复用既有 `SubscriptionRefreshEvent`，**从不**发 `kResultsChanged` → 该行与 §11.4 自相矛盾 |
| `DATA_RESULTS_CHANGED` → `refreshResults()` 是**同步 UI 线程**双全表读（第二轮评审） | 若 P1 把 AutoTask `TEST_ALL` 映射到 `kResultsChanged`，会在每步收尾追加一次同步全表读，与 `docs/bugfix/2026-06-11-Bugfix-ProxyListPanel-RefreshFreeze-v1.0.md` 的触发条件重合 | P1 的 `TEST_ALL` 改走 `refreshResultsFor(indexIds)` 增量路径或既有 `RunningDurationsLoadedEvent` 异步通道（`ProxyListPanel.cpp:350`）；**P0 不新增任何 `kResultsChanged` 发射点**，故 P0 无此风险 |
| AutoTask 边沿检测的静态局部变量在多次运行间残留 | 首次步骤误判为「完成」 | 在 `doAutoTaskImpl` **函数开头**重置 `lastStepName` / `lastStep` / `lastRunning`（每次 AutoTask 独立初始化） |
| `Transaction` RAII 替换裸 BEGIN 时遗漏 `commit()` | 数据静默丢失 | 单测用例 ②③ 覆盖；迁移时逐点 grep `BEGIN TRANSACTION` 确认无残留 |
| Phase0 参数绑定后 SQL 语义偏移 | 去重结果错误 | 单测以含单引号的 subid 做回归 + 手工 V8 |
| `getAllByIndexIds` 的 `IN (...)` 占位符超 SQL 参数上限（SQLite 默认 999） | 大批量探活结果查询失败 | 分批（每批 ≤500）并在实现中加断言；当前探活批次远小于 999 |

回滚策略：三阶段独立 commit，任一段回滚不破坏前序功能（P1/P2 不依赖新数据结构，仅依赖 §4.2 命令常量，回滚后新命令退化为 `else` 分支的 WARN 日志，无副作用）。

---

## 9. 不在本期范围

1. **订阅写入批量算法**（`docs/specs/2026-07-24-Spec-SubscriptionWritePerformance-v1.0.md`，draft，预估 2–100× 提速）——独立方案，本期仅在其上叠加 PRAGMA 与 `Transaction` 基础设施，不改变其写入流程。
2. **`-S / -sync` 数据库同步的写入算法与目标库 schema 映射**——`SubitemUpdaterV2::syncDatabases` 既有实现保持不变（含其 `SubitemUpdaterV2.cpp:1165` 的裸 `BEGIN`，见 §11.3 范围收窄说明）。**但同步完成后的刷新已纳入本期**（§2.2 G9 / 计划 Task 3 Step 6b）。
3. **`ProxyListModel` 增量失效通知**——当前全量 `rebuildMaps` 可接受；若 V4/V5 在 5.3 万行库上实测 >2s 再评估。
4. **`ProfileExItem.message` 列排序键**等既有功能——本期不改。

---

## 10. 关键设计决策记录

| 决策 | 选择 | 理由 |
|---|---|---|
| D1 协议载体 | 沿用 `StatusUpdateEvent` + 新增 `RefreshCommands.h` 常量，不新建事件类 | 与既有 `DEDUP_OK` / `REGION_RESOLVE_DONE` 先例一致，改动最小；避免 6+ 处 Bind 迁移 |
| D2 刷新粒度 | 默认全量重载，仅探活保留增量 | 用户要求 R3「全量」；增量仅在有精确 indexId 集合时安全 |
| D3 写连接 PRAGMA | 收敛为 `db::applyDefaultPragma` 单一入口 | 根因是配置分叉（读端有、写端无），单点实现杜绝复发 |
| D4 事务机制 | RAII `Transaction`，非宏/非手写 | 覆盖异常与提前 return；R2「安全」的底线保证 |
| D5 去重模态框 | 成功去框、失败保留 | 成功无需阻塞用户查看结果；失败需要用户知情 |
| D6 `refreshResults()` | P0 不修改其语义 | 高频路径附加全量 `proxies_` 重载不可接受（2026-08-21 bugfix 已确立约束） |

---

## 11. 补充调查：docs/ 既有 refresh 与事务资产（v1.0 修订）

> 对 `docs/` 全量检索「刷新/refresh」与「事务/Transaction/PRAGMA」后的补充结论。**本节优先级高于前文冲突表述**，并已就地修正 §2.3、§3、§4.5.1。

### 11.1 撤销：「写连接无 PRAGMA」判定

初版 §2.3 称 `AppController::switchDatabase:155` 无任何 PRAGMA。**复核后确认错误**——`:154-156` 仅为错误分支，`:169` 实际执行 `service::DatabaseConnectionService::applyPragmas(db_)`。

`src/service/DatabaseConnectionService.cpp:19-31` 已提供唯一 PRAGMA 入口，内容为：

| 项 | 值 |
|---|---|
| `sqlite3_busy_timeout` | 5000 ms |
| `PRAGMA journal_mode` | WAL |
| `PRAGMA cache_size` | -8000（8MB） |
| `PRAGMA synchronous` | NORMAL |
| `PRAGMA temp_store` | MEMORY |
| `PRAGMA mmap_size` | 268435456 |
| `PRAGMA journal_size_limit` | 67108864（64MB） |
| DDL | `CREATE INDEX IF NOT EXISTS idx_profile_dedup ON ProfileItem(LOWER(Address), Port, ConfigType, LOWER(Id), LOWER(Network))` |

**推论修正**：WAL 已生效 → 写事务期间读连接**不**被阻塞，§3 原根因 3 撤销。「迟滞」的主因回到 §3 根因 1/2/4。

**真实缺陷 W3（降级为 P1 维护性）**：3 个 async reader 内联重写 PRAGMA 而未调用 `applyPragmas`，且 `journal_size_limit` 取值不一致（service 64MB / reader 1GB）。修复=删除 3 处内联块，统一调用 `service::DatabaseConnectionService::applyPragmas`。**§4.5.1 原「新增 `db::applyDefaultPragma`」方案作废**（会制造第二套 PRAGMA 入口，加剧分叉）。

`busy_timeout` 5000→10000ms 的建议**保留**，但理由修正为「单写者串行场景下降低写-写争用失败率」，而非读写阻塞。

### 11.2 必须新增：`Transaction` 须做嵌套事务感知

`docs/bugfix/2026-08-13-Bugfix-ProfileExItemDAO-NestedTransaction-v1.0.md`（✅ completed）确立项目级约束：**同一 `sqlite3*` 连接不支持嵌套事务**，`ProfileExItemDAO::updateTestResultBatch` 曾因无条件 `BEGIN` 触发 `cannot start a transaction within a transaction`；修复方式=`BEGIN` 前检查 `sqlite3_get_autocommit(execDb)`，仅 autocommit 态才开启，并用 `beganTransaction` 标记确保只提交/回滚自己开启的事务。

**§4.5.2 的 `db::Transaction` 必须采纳该模式**，否则会与 `updateTestResultBatch`（Deduplicator / ProxyBatchTester 批量写路径会调用它）冲突。修订后语义：

```cpp
explicit Transaction(sqlite3* db);   // BEGIN 前检查 sqlite3_get_autocommit()
                                     // 已在外层事务中 → 进入「参与外层」模式，不 BEGIN、析构不 COMMIT/ROLLBACK
bool commit();                        // 仅 beganTransaction_ 为 true 时执行
```

**同时删除** `throw std::runtime_error` 构造失败路径（见 §11.3 缺陷），改为返回可判定的失败态或 `Logger::ERR` + 不持有事务。

### 11.3 既有同类计划：`P4-extract-sqlite-helper-and-transaction`（status: cancelled）

`docs/plans/2026-05-13-P4-extract-sqlite-helper-and-transaction-plan.md` 已设计过 `include/SqliteHelper.h`（`sqlite::exec` / `execSilent` / `class Transaction { sqlite3* db_; bool committed_ = false; }`），但 **status=cancelled**。两项关键信息：

1. **该计划的设计存在缺陷**：其 `Transaction` 构造函数**未检查 `sqlite3_get_autocommit`** 且构造失败直接 `throw`——正是 §11.2 所禁止的模式，也解释了它被取消的部分原因。本方案的 `Transaction` 已规避。
2. **该计划已记录一项治理决策**（应被沿用）：`NOT 修改: 已有的手动 char* errMsg 调用（deduplicate/Phase0/1/cleanup）—— 已正确处理错误，替换收益不足以抵消风险`。

**因此 §4.5.2 的迁移清单需收窄**：
- ✅ 保留：`RegionBatchResolver::flushRegionBuffer` 迁移（其裸 `BEGIN` 与上层无嵌套关系，风险低）
- ⚠️ **降级为 P2 或取消**：`Deduplicator::deduplicate()` 6-phase 主事务迁移——该文件同时存在 `updateTestResultBatch` 嵌套调用可能，改造收益不足以抵消风险（与既有治理决策一致）。Deduplicator 的裸 `BEGIN` 保持原状。
- ✅ 独立保留：§4.5.3 `deduplicatePhase0` 参数绑定（与事务结构无关，纯正确性修复）

### 11.4 既有未实施修复：`2026-06-11-fix-batch-update-subscription-refresh`（status: draft）

`docs/plans/2026-06-11-fix-batch-update-subscription-refresh-v1.0.md` **已在 2026-06-11 定位到本方案的 G4**（「点击更新全部订阅后 SubscriptionPanel 含『全部』子项的有效代理计数不自动刷新，用户必须手动右键刷新」），根因分析完全一致（`doUpdateAllSubscriptions` 完成仅发 `StatusUpdateEvent` → `onStatusUpdate` 仅 `setStatusText`），修复方案=新增 `SubscriptionRefreshEvent`。**该计划状态 draft，从未实施**。

关键事实：`SubscriptionRefreshEvent` 事件类型**现已存在于代码中**（`src/ui/SubscriptionPanel.cpp:373` 手动「刷新」按钮正在使用，经 `docs/specs/2026-09-01-Spec-SubRefreshLoadAllProxy-v1.0.md` 贯通），**但从未接线到 `doUpdateSubscription` / `doUpdateAllSubscriptions`**。

**§4.2 / §4.3 修订**：G3/G4/G6 的订阅面刷新**不再新增 `DATA_SUBS_CHANGED` 字符串命令**，改为复用既有 `SubscriptionRefreshEvent`（专门事件优于字符串复用，且与手动刷新按钮同源）。字符串命令常量仅保留代理数据面两条：`DATA_ROWS_CHANGED`（→ `reloadFromDatabase`）与 `DATA_RESULTS_CHANGED`（→ `refreshResults`）。

### 11.5 其余 refresh 相关既有文档（已核对，无冲突）

| 文档 | 与本方案关系 |
|---|---|
| `docs/superpowers/specs/2026-06-02-async-db-read-for-ui-freeze.md` | **本方案异步读路径的历史源头**——`loadProxiesAsync` / `RunningDurationsLoadedEvent` 即出自此，§4.7 R4 复用它符合既有演进 |
| `docs/plans/2026-05-18-004-impl-find-proxy-async.md` | 异步 Find Proxy + Delay 列刷新先例，支持「后台写→事件回 UI」模式 |
| `docs/bugfix/2026-08-18-Bugfix-ProxyListRefresh-IdleRedraw-v1.0.md` | `runningDurations_` 整体替换语义 + 仅 changed 时刷新——§4.7 R4 须遵守，避免回归「空 map 无条件重绘」 |
| `docs/plans/2026-05-27-disable-conflict-ui-during-ops.md`、`2026-05-28-non-reentrant-update-cancel.md` | G8 的非重入/冲突拦截先例，`AsyncOperationGuard` 改造方向一致 |
| `docs/plans/2026-05-13-P4-...`（cancelled） | 见 §11.3 |
| `docs/plans/2026-07-24-SubscriptionWritePerformance-Optimization-v1.0.md`、`docs/specs/2026-07-24-Spec-SubscriptionWritePerformance-v1.0.md` | 订阅写入性能 draft，属 §9 不在本期范围 |
| `docs/specs/2026-07-28-Spec-ProfileDedupIndexOnStartup-v1.0.md`、`docs/bugfix/2026-07-28-Bugfix-DatabaseIndex-applyPragmas-v1.0.md` | 后者即 §11.1 所述 `applyPragmas` 集中化修复（✅ completed，18/18 测试通过）；三者共同证明 PRAGMA 集中化已落地，本期不应重造 |
| `docs/plans/2026-08-11-...`、`docs/bugfix/2026-06-16-Bugfix-Dedup-NestedTransaction-v1.0.md` | Dedup 嵌套事务早期版本，与 §11.2 同源约束 |
| `docs/bugfix/2026-06-11-Bugfix-ProxyListPanel-RefreshFreeze-v1.0.md` | 刷新卡死既有修复，支持「不改 `refreshResults` 语义」决策 D6 |

### 11.6 修订后根因排序

| 排名 | 根因 | 证据强度 | 处置 |
|---|---|---|---|
| 1 | 刷新语义分裂（`refreshResults` 仅 `exItems_` / `reloadFromDatabase` 仅 `proxies_`，无映射） | 强（G1/G2 实证 Region 永久陈旧） | P0，方案不变 |
| 2 | 协议层缺位（命令裸字面量、`else` 静默吞掉未识别字符串） | 强（G3/G4/G6 静默丢失实证） | P0，方案微调（§11.4 复用 `SubscriptionRefreshEvent`） |
| 3 | 交互阻塞（DEDUP_OK 模态框 / `onMenuDedup` 裸线程） | 强 | P1，方案不变 |
| 4 | 写入事务边界不一致 + 事务机制无统一守卫 | 中 | P0（`Transaction`，须含 §11.2 autocommit 检查；迁移范围按 §11.3 收窄） |
| ~~5~~ | ~~写连接无 PRAGMA 致读写阻塞~~ | **已撤销**（§11.1） | 改为 P1 消除 PRAGMA 分叉 |

### 11.7 修订后阶段划分

| 阶段 | 范围 |
|---|---|
| **P0** | §4.2 协议统一（含 §11.4 复用 `SubscriptionRefreshEvent`）+ §4.3 缺口 G1/G2/G3/G4/G6 + §4.5.2 `Transaction`（**含 autocommit 检查**，仅迁移 `RegionBatchResolver`）+ §4.5.3 Phase0 参数绑定 |
| **P1** | §4.4 AutoTask 边沿检测 + §4.5.4 单行 Region 复用 + §4.6 G7/G8 + **§4.5.1 消除 3 处内联 PRAGMA + busy_timeout 10000ms** |
| **P2** | §4.7 读路径 R1–R4 |
| **取消** | `Deduplicator` 主事务迁移至 `Transaction`（依 §11.3 治理决策） |

---

*最后更新: 2026-09-18 (v1.2 draft，含 §11 补充调查修订 + 第二轮独立评审修正：新增缺口 G9「GUI 数据库同步零刷新」、修正 §1.3/§9「sync 仍为 TODO」错误断言、删除 §8 失效风险行并补同步读风险行、G3/G4 描述收敛为「仅缺代理面板」、G6 行号修正为 `:277-285`) | 维护者: validproxy-dev*
