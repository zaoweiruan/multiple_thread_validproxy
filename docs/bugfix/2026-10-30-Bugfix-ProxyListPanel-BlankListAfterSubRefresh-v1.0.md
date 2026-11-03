# 刷新订阅后切换订阅，代理列表永久空白

**Goal:** 修复「刷新某个订阅，再切换订阅 → 代理列表为空且不再恢复」的回归；根因为异步事件路由错误 + 空结果覆盖缓存 + 同步读在测试期间失败三处叠加。
**Tech Stack:** C++17、wxWidgets 3.2 (wxMSW)、SQLite WAL、PowerShell。
**依据文档:** `docs/DEV-PROCESS.md`、`docs/specs/2026-10-30-Spec-IncrementalProxyCacheRefresh-v1.0.md`。
**约束:** 不使用 `auto`；生产环境复现（`bin/config.json` + `bin/worker/guindb.db`）。

---

## 0. 现场证据

- 用户复现步骤：「刷新某个订阅，再切换订阅边空」；环境为 `bin` 目录下文件（生产库）。
- 复现日志 `bin/log/ui_20261030_163804.log`：批量测试取消后出现
  `[ERROR] SQL错误: bad parameter or other API misuse`，随后 `[ConfigGenerator] SQL returned 0 profiles`；
  整个会话无任何 `[ProxyListPanel]` / `ProxyListLoaded` 日志 —— 事件从未到达面板。

## 1. 根因（三处叠加）

1. **事件路由错误** `wxEVT_PROXY_LIST_LOADED` 仅在 `MainFrame`（`MainFrame.cpp:346`）绑定。
   `ProxyListPanel::reloadFromDatabase()` 却调用 `loadProxiesAsync(currentSubId_, this)`
   把结果 post 给面板自身 → 事件无处理者，被静默丢弃，刷新永不落地。
2. **空结果覆盖缓存** 异步重载 `loadProxies` 重载版仅在 `proxies.empty() && cacheReady_` 时保留缓存；
   首次加载恰遇 SQL 失败返回空时 `cacheReady_==false`，空缓存被安装且 `cacheReady_=true` → 列表永久空白。
3. **同步读在测试期间失败** 右键「刷新」`onRefreshProxyList` 走 UI 线程同步 `loadProxies()`（共享 DB 连接），
   批量测试写入期间命中 `SQL busy/misuse` 返回 0 行 → 同样安装空缓存。
   同时 `applySubscriptionFilter` 在 `!cacheReady_` 时受 `isRunning()` 守卫静默跳过重载 → 切换订阅后永不恢复。

## 2. 修复（均在 `src/ui/ProxyListPanel.cpp`）

| # | 位置 | 修复 |
|---|------|------|
| 1 | `loadProxies`（异步重载版，~L189） | 空结果守卫放宽为 `if (proxies.empty())`：无论 `cacheReady_` 与否一律保留现有缓存并记 WARN，绝不安装空缓存 |
| 2 | `reloadFromDatabase`（~L289） | 事件目标改为顶层窗口：`wxGetTopLevelParent(this)` → `loadProxiesAsync(currentSubId_, target)`，与 `MainFrame` 唯一绑定点对齐 |
| 3 | `applySubscriptionFilter`（~L309） | 删除 `isRunning()` 守卫：`!cacheReady_` 时无条件 `reloadFromDatabase()`（异步读线程使用独立 WAL 连接 + 5s busy_timeout，测试期间安全） |
| 4 | `onRefreshProxyList`（~L634） | 同步 `loadProxies()` 改为 `reloadFromDatabase()` 异步路径，避免 UI 线程共享连接命中 SQL busy |

辅助链路（已验证正确，未改动）：订阅刷新 `SubscriptionRefreshEvent` → `MainFrame:321` → `loadProxiesAsync("", this)`；
订阅更新完成 `StatusUpdateEvent(kRowsChanged)` → `MainFrame:1580` → `proxyPanel_->reloadFromDatabase()`（此前被根因 1 破坏，随修复 2 一并恢复）。

## 3. 验证

- `cmake --build build --target validproxy --parallel 8` 编译通过。
- `.\\scripts\\build-and-test.bat` UI 门禁：见 `docs/plans/project-plans-tracker.md` 当日记录。
- 手工回归：启动 → 批量测试运行中刷新某订阅 → 切换订阅 → 列表保持非空；测试结束后右键刷新恢复显示。

## 4. 教训

- 自定义 wx 事件的 post 目标必须有绑定者；跨面板发起、顶层窗口消费的异步事件应统一经控制器/顶层路由，并在调试期用日志确认事件到达。
- 「空结果」与「读取失败」在 DAO 层不可区分时，UI 层绝不将空结果当作权威数据安装缓存。
