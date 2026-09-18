# Spec: 订阅切换零 DB 内存过滤（ProxyListPanel 白屏阻塞修复）

- 日期: 2026-08-24
- 模块: ProxyListPanel / MainFrame
- 类型: Bugfix（性能阻塞）+ Spec
- 规范: C++17，禁止 `auto`；修改最少代码、保持风格一致

## 1. 问题现象

切换订阅时 ProxyListPanel 显示阻塞严重；即使目标订阅记录数为 0 也白屏阻塞。

## 2. 根因（RCA）

触发链：`SubscriptionPanel::onSelectionChanged` → `SubscriptionSelectedEvent`
→ `MainFrame.cpp:282 controller_->loadProxiesAsync(subId, this)`。

`AppController::loadProxiesAsync` 后台线程每次执行：

1. `ProfileitemDAO::getAll()` — **全表读取所有 profiles**（test 库 53,837 行）
2. 内存 `copy_if` 按 subid 过滤
3. `ProfileExItemDAO::getAll()` — **全量读取测试结果**（53k 行）
4. `buildProxyListMaps(exItems)` — 全量重建查找 maps

因此单次切换延迟 ≈ O(全库) 常数级秒耗，**与所选订阅大小无关**：

- 0 记录订阅同样要等全库 IO + maps 构建 → 白屏；
- 快速连续切换堆叠多个 detached 线程互相争抢磁盘 IO/CPU → 阻塞加重；
- 数据到达前 UI 保持旧内容/空白 → "白屏阻塞严重"观感。

而 `ProxyListPanel` 内已有完整缓存：`allProxies_`（全集）、`exItems_`、模型
maps（经 4 参 `loadProxies` 由后台预构建）。同文件 `filterBySearch()`
（搜索框）已验证纯内存过滤 + 双 Reset 模式即时可用——订阅切换却绕过缓存直奔 DB。

## 3. 方案

新增 `ProxyListPanel::applySubscriptionFilter(subId)`，**切换 = 内存过滤**：

1. 缓存未就绪（`cacheReady_ == false`，仅启动初期）→ 回退既有
   `reloadFromDatabase()` 异步路径（行为不变）。
2. 缓存就绪 → 从 `allProxies_` 按 subid 过滤生成 `proxies_`
   （subId 空 = 全部），`setDataWithoutRebuild`（订阅过滤不改变 maps 内容，
   避免 O(N) rebuildMaps）+ 双 `Reset` + `detectIdOffset` +
   必要时 `selectFirstProxy`——完全镜像 `updateProxyList` 的视图语义
   （sortState 重置与现状异步路径一致）。
3. `MainFrame` 的 SubscriptionSelectedEvent 处理改为调用该方法。

其余显式刷新入口（启动自动加载、删除订阅后、REGION_RESOLVE_DONE 等）继续走
`loadProxiesAsync` / `reloadFromDatabase`，顺带保持缓存新鲜，无需改动。

### 不做（范围控制）

- 不加 stale-generation 乱序防护：切换不再产生线程后该竞态对订阅场景消失，
  其余异步入口频率低，留作后续加固项。
- 不动 `filterBySearch`（其 setData→rebuildMaps 为既有行为，另行评估）。

## 4. 变更清单

| 文件 | 变更 |
| :--- | :--- |
| `src/ui/ProxyListPanel.h` | 公有声明 `applySubscriptionFilter`；私有成员 `bool cacheReady_ = false;` |
| `src/ui/ProxyListPanel.cpp` | 两条加载路径置 `cacheReady_ = true`；实现 `applySubscriptionFilter`（内存过滤 + 未就绪回退异步） |
| `src/ui/MainFrame.cpp` | SubscriptionSelectedEvent 处理改调 `proxyPanel_->applySubscriptionFilter(subId)` |
| `AppController::loadProxiesAsync` | 初版未改动；**修订后**删除 copy_if 分支、事件直接携带全表（见 §7 修订记录及 `docs/bugfix/2026-08-24-Bugfix-InstantSubSwitch-CacheSubset-v1.0.md`） |

## 5. 测试

- 项目无 wxWidgets UI 层测试框架先例（历届 UI spec 同此），采用：
  构建 0 error + `ctest -V` 全量回归 + 手动 GUI 验证。
- 手动验证清单：
  1. 启动 GUI（大库 bin/worker/guindb.db），等待首次列表加载完成；
  2. 连续快速点击多个订阅（含空订阅）→ 列表应瞬时切换、无白屏无卡顿；
  3. 切换后右键「刷新」→ 仍能从 DB 重载当前订阅（同步路径回归确认）;
  4. 搜索框过滤在切换后的订阅内仍生效；
  5. 批量测试完成后 Delay/Message 列刷新正常（refreshResults 路径回归）。

## 6. 关联

- 前序性能改造：`docs/specs/2026-08-18-Spec-ProxyListRefresh-v2.0.md`（定时刷新异步化）
- 缓存/maps 预构建引入处：`AppController::loadProxiesAsync`（同文件注释）

## 7. 修订记录

### v1.0 修订（2026-08-24，回归修正）

初版实现存在回归：切换订阅后其它订阅显示为空。

- 根因：误假设 `ProxyListLoadedEvent` 携带全表；实际后台线程 `dao.getAll()`
  全表经 copy_if 过滤后事件只携子集且全表即弃，面板缓存仅含首个订阅行。
- 修正：`AppController::loadProxiesAsync` 删除 copy_if 分支、事件直接携带
  全表；面板 4 参 `loadProxies` 尾部视图逻辑委托 `applySubscriptionFilter`
  复用。§3 方案第 1 条前提（"缓存持有全集"）自此成立。
- 详情：`docs/bugfix/2026-08-24-Bugfix-InstantSubSwitch-CacheSubset-v1.0.md`
- 验证：构建 0 error + ctest 32/32。
