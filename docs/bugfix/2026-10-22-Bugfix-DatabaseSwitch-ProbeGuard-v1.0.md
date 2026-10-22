# 切库守卫：周期 silent 探活标志近乎恒真导致「操作进行中，无法切换数据库」

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:systematic-debugging to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复「切换数据库时始终提示『操作进行中，无法切换数据库』」——把「一律拒绝切库」改为「先停止周期 silent 探活、再安全切库」，同时消除切库时旧 sqlite3 句柄的泄漏。

**Tech Stack:** C++17 / wxWidgets 3.2+ / SQLite3 / std::thread / GoogleTest / CMake + Ninja（MinGW GCC）

**依据文档:**
- `docs/specs/2026-10-21-Spec-StandaloneAdoption-InstanceBoundPortFallback-v1.1.md`
- `docs/bugfix/2026-10-21-Bugfix-StandaloneAdoption-ConcurrencyAndDiagnostics-v1.1.md`
- `docs/reports/2026-10-22-Report-StandaloneAdoption-CurrentLogic-v1.0.md`

**约束:** 全栈禁止使用 `auto` 进行类型推导（AGENTS.md §1）；测试源码只允许放在 `tests/`；文档补丁脚本禁用正则（PowerShell `-replace` 在含全角括号/竖线/emoji 时极易不匹配且曾写截断 INDEX 行），一律用 `ReadAllText` + `WriteAllText` + 字符串插入。

---

## 0. 现场证据

### 0.1 用户报告

截图 `bin\log\78874b29-9eb5-4c87-a510-de9b2ab7fa8b.png`（447 KB）显示弹窗：

- 标题：**操作进行中**
- 内容：**操作进行中，无法切换数据库**
- 按钮：`wxOK | wxICON_WARNING`

窗口为 v2rayN 风格主界面 + 底部本项目 LogPanel，日志区可见：

```
[StandaloneProxy] Adopted dangling process pid=16684 indexId=5046338036629160457 on SOCKS5 :10808 source=runtime-history verdict=verified
[StandaloneProxy] Configuration of pid=16684 indexId=... does not match the current database record (protocol); ...
[REPORT] Migration Result — Total: 175, Succeeded: 0, Failed: 0
[REPORT] 数据库同步完成
```

用户描述为「**始终**提示」。

### 0.2 触发点

`src/ui/MainFrame.cpp:1194`（修复前）：

```cpp
        bool dbPathChanged = !cfg.database_path.empty() && cfg.database_path != config_.database_path;
        // Switching the live database requires FULL idle (user op OR probe):
        // switchDatabase() swaps db_ that both use — the probe's exDao_ holds
        // the old db_ pointer, so letting the probe run across a switch would
        // be use-after-free.
        if ((controller_->isRunning() || controller_->isOnlineProbeRunning()) && dbPathChanged) {
            wxMessageBox(L"操作进行中，无法切换数据库", L"操作进行中", wxOK | wxICON_WARNING);
            return;
        }
```

### 0.3 关键推理：命中的是「第二个」弹窗

`MainFrame::onMenuConfig` 里有两个守卫：

| 行号 | 条件 | 文案 |
| :--- | :--- | :--- |
| :1186 | 仅 `controller_->isRunning()` | 操作进行中，无法保存配置 |
| :1195 | `isRunning() \|\| isOnlineProbeRunning()` | 操作进行中，无法切换数据库 |

用户截图中**只出现了「无法切换数据库」**，**没有**出现「无法保存配置」⇒ 第一个守卫（:1186）未命中 ⇒ **`isRunning()` 当时为 `false`** ⇒ 只有 `isOnlineProbeRunning()` 为 `true` 才能触发第二个守卫。

⇒ **根因锁定在「周期 silent 探活标志近乎恒为 true」。**

### 0.4 探活占空比实测

运行实例是 `bin\worker\validproxy.exe`（pid 13712，11:29:06 启动），其配置 `bin\worker\config.json`：

```json
{
  "proxy_process_monitor": { "check_interval_ms": 5000 },
  "independent_probe": { "enabled": true },
  "test": { "timeout_ms": 5000 },
  "database": { "path": "bin/worker/guiNDB.db" },
  "log": { "console_level": "REPORT", "file_level": "WARN" }
}
```

连续 4 次采样 `bin\worker\guiNDB.db` 的 `ProfileExItem`：

| ProfileIndexId | 采样 1 | 采样 2 | 采样 3 | 采样 4 |
| :--- | :--- | :--- | :--- | :--- |
| `5127085608267216303` | 11:49:43 | 11:49:48 | 11:49:53 | 11:49:58 |
| `5046338036629160457` | 11:49:40 | 11:49:49 | 11:49:55 | — |

⇒ **探活实际完成间隔恰好 5 秒，与定时器周期 5 秒完全相同** ⇒ 探活几乎 100% 时间处于飞行状态 ⇒ `onlineProbeRunning_` 近乎恒 `true` ⇒ 守卫几乎必然命中 ⇒ 用户感知为「始终」。

### 0.5 日志佐证

- `bin\worker\log\ui_20261022_112906.log` 仅 4 行，其中 `[WARN] [OnlineProbe] test failed: 5127085608267216303: curl_easy_perform failed: Timeout was reached` 出现在 11:36:45 与 11:38:15（WARN 是阈值化的，每 3 次连续失败才记一次）。
- 早前 `ui_20261022_084346.log`（3491 行）中 OnlineProbe WARN 的间隔为 19~55 秒，佐证周期约 5~10 秒。

### 0.6 相关既有代码事实（实施依据，均已实测）

| 事实 | 位置 |
| :--- | :--- |
| `isRunning()` → `isRunning_.load()` | `src/ui/AppController.h:111` |
| `isOnlineProbeRunning()` → `onlineProbeRunning_.load()` | `src/ui/AppController.h:116` |
| `std::atomic<bool> isRunning_{false};` / `onlineProbeRunning_{false};` | `src/ui/AppController.h:285` / `:289` |
| `std::thread probeThread_;` / `workerThread_;` | `src/ui/AppController.h:292` / `:294` |
| `sqlite3* db_;` | `src/ui/AppController.h:279` |
| 周期探活启动：`if (isRunning_ \|\| onlineProbeRunning_) skip;` → `if (probeThread_.joinable()) probeThread_.join();` → `onlineProbeRunning_ = true;` → `cancelRequested_ = false;` → `probeThread_ = std::thread(&AppController::doTestOnlineProxies, this, wxHandler, true);` | `src/ui/AppController.cpp:553-582` |
| 探活标志复位：`std::optional<ui::ScopeGuard<std::atomic<bool>>> runningGuard; if (!silent) runningGuard.emplace(isRunning_);` + **无条件** `ui::ScopeGuard<std::atomic<bool>> _probeGuard{onlineProbeRunning_};` | `src/ui/AppController.cpp:3078-3091` |
| 探活循环每轮开头 `if (cancelRequested_.load()) break;` ⇒ 置位可中断 | `src/ui/AppController.cpp:3126` |
| `ScopeGuard` 析构 `ref_ = false` | `include/ui/ScopeGuard.h:16-31` |
| `switchDatabase()`：`oldDb = db_; db_ = nullptr;` → `sqlite3_open_v2(...)` → `sqlite3_close(oldDb);`（**忽略返回值**）→ `db_ = newDb;` → `applyPragmas` → `historyDao_.setDb/exDao_.setDb` → `migrateTable` → 锁内清 `standaloneProxies_`/`proxyWatchKeys_` → detach 线程调 `adoptDanglingStandaloneProxies()` | `src/ui/AppController.cpp:161-217` |
| `AsyncOperationGuard` 析构**不做**复位（`isRunning_` 由 worker 线程上的 ScopeGuard 复位）⇒ `isRunning_` 不会卡死 | `include/ui/AsyncOperationGuard.h` |
| 所有用户操作 worker 均有 `ScopeGuard<std::atomic<bool>> _guard{isRunning_};` | `src/ui/AppController.cpp` 多个 `do*` 方法 |

---

## 1. 根因（RCA）

### RCA-1：把「可停止的后台任务」当成「不可中断的忙态」

守卫存在的真实理由是：**`switchDatabase()` 会 `sqlite3_close(oldDb)`，而探活线程的 `exDao_` 仍持旧 `db_` 指针** ⇒ 探活跨切库运行会造成 use-after-free / 陈旧写。

但探活**不是**用户操作，它是一个**可以安全停止并 join 的后台任务**（`probeThread_` + `cancelRequested_`）。旧守卫把这两类语义混为一谈：

| 忙态类型 | 能否安全中断 | 正确处置 |
| :--- | :--- | :--- |
| 用户操作（`isRunning_`） | 否（DB 写入中途不可打断） | **拒绝**切库并提示 |
| 周期 silent 探活（`onlineProbeRunning_`） | 是（置 `cancelRequested_` + join） | **先停再切** |

### RCA-2：探活周期与定时器周期相同 ⇒ 占空比趋近 100%

生产 `check_interval_ms = 5000`，而单次探活耗时（curl 超时上限 `test.timeout_ms = 5000`）与之同量级 ⇒ 定时器每次触发时上一轮往往仍在飞行 ⇒ `onlineProbeRunning_` 近乎恒 `true` ⇒ 守卫变成「几乎必然拒绝」。

> 注：`testOnlineProxiesAsync` 开头有 `if (isRunning_ || onlineProbeRunning_) skip;` ⇒ 飞行中的探活不会被叠加，但**新的一轮也不会启动**，于是「标志恒 true」持续存在。

### RCA-3：次生缺陷 —— 旧 sqlite3 句柄泄漏

`switchDatabase()` 直接 `sqlite3_close(oldDb);` 并**忽略返回值**。当仍有活跃语句时 `sqlite3_close()` 返回 `SQLITE_BUSY` 且**不释放**句柄 ⇒ 旧库（生产 `bin\worker\guiNDB.db` 约 184 MB）句柄与文件占用泄漏。

---

## 2. 修复方案

### D1：抽出纯策略判定（可单测）

新增 `include/DatabaseSwitchPolicy.h`（header-only，`namespace db_switch_policy`）：

```cpp
enum class Decision { Allow = 0, QuiesceProbe, RefuseUserOperation };

inline Decision decide(bool userOperationRunning, bool onlineProbeRunning) {
    if (userOperationRunning) { return Decision::RefuseUserOperation; }
    if (onlineProbeRunning) { return Decision::QuiesceProbe; }
    return Decision::Allow;
}

inline const char* decisionLabel(Decision decision);  // "allow" / "quiesce-probe" / "refuse-user-operation"
```

**理由**：项目既有惯例（`src/ui/UnifiedMonitorRows.h`、`include/StandaloneAdoptionPolicy.h`、`include/FloatingWidgetPolicy.h`）—— `AppController` 不在任何单测 target 内，把判定抽成纯函数才能有自动化回归保护。

### D2：新增「停止探活」能力

`AppController` 新增 `bool quiesceOnlineProbe();`：

```cpp
    if (!probeThread_.joinable()) {
        onlineProbeRunning_ = false;
        return true;
    }
    cancelRequested_ = true;
    probeThread_.join();
    cancelRequested_ = false;
    onlineProbeRunning_ = false;
    // DEBUG 日志
    return true;
```

- `cancelRequested_ = true` ⇒ 探活循环下一轮开头 `if (cancelRequested_.load()) break;` 提前退出（`src/ui/AppController.cpp:3126`）。
- `join()` 等待线程真正结束 ⇒ 其 `ScopeGuard` 已把 `onlineProbeRunning_` 复位为 `false`，且 `exDao_` 不再触碰旧 `db_`。
- 等待上限 = 一个 curl 超时（`test.timeout_ms`，生产 5000 ms）⇒ UI 线程最多阻塞数秒。
- **必须从 UI 线程调用**（与既有 `AsyncOperationGuard` 的用法一致）。

### D3：守卫改为三态决策

`MainFrame::onMenuConfig`：

```cpp
        db_switch_policy::Decision dbSwitchDecision = db_switch_policy::Decision::Allow;
        if (dbPathChanged) {
            dbSwitchDecision = db_switch_policy::decide(
                controller_->isRunning(), controller_->isOnlineProbeRunning());
            if (dbSwitchDecision == db_switch_policy::Decision::RefuseUserOperation) {
                wxMessageBox(L"操作进行中，无法切换数据库", L"操作进行中", wxOK | wxICON_WARNING);
                return;
            }
        }
```

真正切库前：

```cpp
            bool probeQuiesced = true;
            if (dbSwitchDecision == db_switch_policy::Decision::QuiesceProbe) {
                probeQuiesced = controller_->quiesceOnlineProbe();
            }
            sqlite3* newDb = nullptr;
            if (probeQuiesced) {
                newDb = controller_->switchDatabase(cfg.database_path);
            }
```

- `QuiesceProbe` ⇒ **不拒绝**，先停探活再切库。
- 停止失败 ⇒ 恢复旧路径 + `saveConfig(cfg)` + 明确报错「The periodic probe could not be stopped, so the database was not switched. The previous database path has been restored.」
- **保存守卫（:1186「操作进行中，无法保存配置」）保持不变** —— 用户操作期间仍然不允许保存配置。

**线程安全论证**：`quiesceOnlineProbe()` 与 `switchDatabase()` 都在 UI 线程顺序执行；join 期间 UI 线程不处理事件 ⇒ 定时器不会在切换过程中再起新探活 ⇒ 安全。

### D4：`switchDatabase` 句柄健壮性 + 所有权收敛

- `sqlite3_close(oldDb)` 的返回值被检查：`SQLITE_OK` ⇒ 直接关闭；否则打 WARN「Old database handle is still busy (...); deferring its close」并把句柄存入新增成员 `std::vector<sqlite3*> retiredDbHandles_;`。
- 新增私有方法 `closeRetiredDbHandles();`：遍历尝试关闭，成功者丢弃，仍 `SQLITE_BUSY` 者留在列表里下次再试；在 `switchDatabase()` 开头与 `~AppController()` 中各调用一次。
- 旧句柄**只被 close，从不被解引用** ⇒ 不引入新的 UAF 面。

### D5：sqlite3 句柄所有权收敛到 AppController

`switchDatabase()` 会替换 `db_`，因此**当前句柄与每次切库替换掉的旧句柄都归 AppController 所有**：

- `src/main_gui.cpp` 删除 `sqlite3_close(db);`（原 :159），改为注释说明句柄归属。
- `src/ui/UIApp.cpp` 的 `OnExit()` 注释同步更新（原注释声称「调用方（main.cpp）负责关闭 sqlite3 句柄」，与事实不符）。
- `~AppController()` 中关闭 `db_`（`SQLITE_BUSY` 时打 WARN 并放弃，不崩溃）。

**核查**：`DatabaseConnectionService::open()` 不接管所有权（无析构关库）、`FullConfigTemplateDAO` 的析构为 `= default`（不关库）、`src/main_cli.cpp` 不使用 AppController ⇒ **无双重关闭来源**。

---

## 3. 改动清单

- [x] **Task 0** — 新建 `include/DatabaseSwitchPolicy.h`：`Decision` 枚举 + `decide()` + `decisionLabel()`（无 `auto`、header-only、零外部依赖）。
- [x] **Task 1** — `src/ui/AppController.h`：public 段新增 `bool quiesceOnlineProbe();`；private 段新增 `void closeRetiredDbHandles();`；成员区新增 `std::vector<sqlite3*> retiredDbHandles_;`。
- [x] **Task 2** — `src/ui/AppController.cpp`：实现 `quiesceOnlineProbe()` 与 `closeRetiredDbHandles()`；`switchDatabase()` 检查 `sqlite3_close` 返回值并在 BUSY 时延迟关闭；`~AppController()` 关闭 `db_` 与退休句柄。
- [x] **Task 3** — `src/ui/MainFrame.cpp`：引入 `DatabaseSwitchPolicy.h`；守卫改为三态决策；切库前按需 `quiesceOnlineProbe()`；失败时恢复旧路径并提示。
- [x] **Task 4** — `src/main_gui.cpp` 删除 `sqlite3_close(db);`；`src/ui/UIApp.cpp` 更新 `OnExit()` 注释。
- [x] **Task 5** — 新建 `tests/test_database_switch_policy.cpp`（5 个 `TEST(DatabaseSwitchPolicyTest, ...)`）+ `CMakeLists.txt` 注册 `test_database_switch_policy` 与 `add_test(NAME DatabaseSwitchPolicyTest ...)`。
- [x] **Task 6** — 构建与测试（见 §4.1）。
- [x] **Task 7** — 文档登记（见 §6）。

---

## 4. 验证

### 4.1 构建与自动化测试（实测）

```powershell
cmake --build build --parallel 8
```

⇒ **exit=0，0 error**（仅既有 `-Wunused-parameter` 警告）。

```powershell
# 必须在 build/ 目录执行
ctest -R "DatabaseSwitchPolicyTest|StandaloneAdoptionPolicyTest|RuntimeHistoryDAOTest|ProcessInspectorTest|UtilsTest" -V
```

⇒ **5/5 全绿**（`DatabaseSwitchPolicyTest` / `StandaloneAdoptionPolicyTest` / `RuntimeHistoryDAOTest` / `ProcessInspectorTest` / `UtilsTest`）。

```powershell
# 全量（build/ 目录）
ctest
```

⇒ **55/57 通过**（总数由 56 增至 57，因新增 `DatabaseSwitchPolicyTest`）。失败 2 项：

| 失败项 | 性质 |
| :--- | :--- |
| `XrayManagerStartupTest` | **既有基线失败**（需要真实就绪的 xray 运行时） |
| `UI_FLOATINGWIDGET` | **已知并行 flaky**（`WindowFromPoint` z-order 竞态）；单独复跑 ⇒ **Passed** |

### 4.2 `auto` 红线检查

```powershell
git diff HEAD -U0 -- '*.cpp' '*.h' | Select-String -Pattern '^\+.*\bauto\b' -CaseSensitive
```

⇒ **0 命中**。

### 4.3 实机验收（步骤）

1. 启动 `bin\worker\validproxy.exe`（`check_interval_ms=5000`、探活开启），打开「设置」对话框修改数据库路径并保存。
2. **预期**：不再出现「操作进行中，无法切换数据库」弹窗；切换成功，日志出现 `[AppController] Database switched to: <path>`。
3. 若探活恰好在飞行，`onlineProbeRunning_` 为 true ⇒ 决策 = `QuiesceProbe` ⇒ UI 最多阻塞一个 curl 超时（≈5 秒）后完成切换。
4. 用户操作（例如批量测试）进行中时切库 ⇒ **仍然**提示「操作进行中，无法切换数据库」（该行为未变，符合预期）。
5. 切库后查库：目标库的 `proxy_runtime_history` 出现纳管会话行；旧库文件不再被占用（可用 `Get-Process` / 资源监视器确认无残留句柄）。
6. 若探活无法在超时内停止 ⇒ 弹出「The periodic probe could not be stopped…」且数据库路径恢复为旧值。

---

## 5. 风险与边界

| 编号 | 风险 | 缓解 |
| :--- | :--- | :--- |
| R1 | `quiesceOnlineProbe()` 在 UI 线程 `join()`，最长阻塞一个 curl 超时（生产 5000 ms） | 这是必要的代价：探活必须先停才能换库；用户可见反馈为短暂无响应，且**不再**被完全拒绝 |
| R2 | 停止探活后，定时器会在下一个周期重新起探活（周期 5 秒） | 切库在 UI 线程连续完成（停 → 换），期间不处理事件 ⇒ 不会在切换中途起新探活；切换完成后探活自然恢复 |
| R3 | `retiredDbHandles_` 中的句柄在进程存活期内始终不被关闭（若持续 BUSY） | 仅在 `SQLITE_BUSY` 时发生；下一次 switch 或析构时重试；列表极小（每次切库最多 1 个） |
| R4 | 删除 `main_gui.cpp` 的 `sqlite3_close(db)` 后，句柄关闭时机改为 AppController 析构 | AppController 随 MainFrame 销毁，早于 `wxEntry` 返回 ⇒ 关闭发生在进程退出前；已核对无其他关闭来源 |
| R5 | `isRunning_` 为 true 时仍然拒绝切库 | 保持既有行为（用户操作中途不可换库），本次未放宽 |

**非目标**

| 编号 | 非目标 | 理由 |
| :--- | :--- | :--- |
| N1 | 把探活周期与探活耗时的占空比问题从根本解决（例如让探活跳过重叠轮次） | 属探活调度策略优化，与本缺陷的修复路径正交；本次仅解决「切库被拒」 |
| N2 | 清理生产库既有孤儿行 / `start_count` 归零 | 独立数据治理缺陷 |
| N3 | 切库时旧 watcher 跨库 `finalizeStop`（UAF） | 已知残留，另案处理 |

---

## 6. 文档登记

- `docs/INDEX.md` §9.1 bugfix 表新增编号 **105**。
- `docs/plans/project-plans-tracker.md`「近期文档引用」表追加 1 行（末列为状态）。

---

## 附录：实施记录（v1.0，已落地）

### A.1 代码改动表

| 文件 | 改动 |
| :--- | :--- |
| `include/DatabaseSwitchPolicy.h`（新建） | `Decision` 枚举 + `decide()` + `decisionLabel()` |
| `src/ui/AppController.h` | 新增 `quiesceOnlineProbe()` / `closeRetiredDbHandles()` / `retiredDbHandles_` |
| `src/ui/AppController.cpp` | 实现两个新方法；`switchDatabase()` 检查 `sqlite3_close` 返回值并延迟关闭；析构关闭句柄 |
| `src/ui/MainFrame.cpp` | 引入策略头；守卫改三态决策；切库前按需停止探活；失败恢复旧路径 |
| `src/main_gui.cpp` | 删除 `sqlite3_close(db);`（句柄归 AppController） |
| `src/ui/UIApp.cpp` | `OnExit()` 注释更正 |
| `tests/test_database_switch_policy.cpp`（新建） | 5 个 `TEST(DatabaseSwitchPolicyTest, ...)` |
| `CMakeLists.txt` | 注册 `test_database_switch_policy` + `add_test(NAME DatabaseSwitchPolicyTest ...)` |

### A.2 验证结果

- 构建：`cmake --build build --parallel 8` ⇒ **exit=0**（0 error）。
- 定向 ctest（`build/` 目录）⇒ **5/5 全绿**。
- 全量 ctest ⇒ **55/57 通过**；失败项 = `XrayManagerStartupTest`（既有基线）+ `UI_FLOATINGWIDGET`（已知并行 flaky，单跑通过）。
- `auto` 红线检查（`-CaseSensitive`）⇒ **0**。

### A.3 剩余残留

见 §5「非目标」N1-N3。
