# 数据读写与刷新机制优化 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: 使用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans` 逐任务实施。步骤使用复选框（`- [ ]`）跟踪。

**Goal:** 数据变更（地区解析 / 去重 / 订阅更新 / 导入 / AutoTask / 批量测试）完成后，代理列表、订阅列表、状态栏、日志窗口**无需任何手工操作**自动刷新，且所有多语句写入具备事务原子性。

**Architecture:** 引入「写入方声明、UI 方执行」的刷新命令协议——后台写操作完成后只广播一条集中定义的命令常量，由 `MainFrame::onStatusUpdate` 统一路由到面板重载方法，并同时驱动状态栏与日志窗口。写入侧引入 `db::Transaction` RAII 守卫（含嵌套事务感知），并把散落的手写 `BEGIN` 收敛到它。分 P0 / P1 / P2 三阶段独立提交，可单独回滚。

**Tech Stack:** C++17（**全栈禁用 `auto`**）、wxWidgets 3.3.3、SQLite3、CMake + Ninja、Google Test、MinGW GCC / Windows、PowerShell。

**规格依据:** `docs/specs/2026-09-18-Spec-DataReadWriteRefresh-v1.0.md`（v1.1）。本计划仅覆盖 P0；P1/P2 见末尾 Task 10–13，可在 P0 验证通过后独立排期。

---

## 文件结构

| 文件 | 动作 | 职责 |
|---|---|---|
| `include/RefreshCommands.h` | 新建 | 刷新命令常量 + `statusTextFor()` 声明（协议单一来源） |
| `src/ui/RefreshCommands.cpp` | 新建 | 命令 → 状态栏文案映射表 |
| `tests/test_refresh_commands.cpp` | 新建 | 常量唯一性 + 映射纯函数单测 |
| `include/DatabaseHelper.h` | 修改 | 新增 `db::Transaction` 类声明 |
| `src/DatabaseHelper.cpp` | 修改 | `db::Transaction` 实现 |
| `tests/test_db_transaction.cpp` | 新建 | 事务 RAII + 嵌套感知单测 |
| `src/ui/MainFrame.cpp` | 修改 | `onStatusUpdate` 路由表 + 三段式动作 |
| `src/ui/AppController.cpp` | 修改 | G1–G4/G6 补命令发射 |
| `src/RegionBatchResolver.cpp` | 修改 | 裸 `BEGIN` 迁移至 `Transaction` |
| `src/update/Deduplicator.cpp` | 修改 | Phase0 SQL 参数绑定 |
| `tests/test_deduplicator.cpp` | 修改 | Phase0 特殊字符回归用例 |
| `CMakeLists.txt` | 修改 | 注册 2 个新测试目标 + 新源文件 |
| `docs/INDEX.md`、`docs/plans/project-plans-tracker.md` | 修改 | 完成后登记 |

---

## Task 1: 刷新命令协议（TDD）

**Files:**
- Create: `include/RefreshCommands.h`
- Create: `src/ui/RefreshCommands.cpp`
- Create: `tests/test_refresh_commands.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 先写失败的测试**

```cpp
// tests/test_refresh_commands.cpp
#include <gtest/gtest.h>

#include "RefreshCommands.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::string_view> AllCommands() {
    return {
        ui::refresh_cmd::kStatusResolved,
        ui::refresh_cmd::kRegionResolveDone,
        ui::refresh_cmd::kDedupOk,
        ui::refresh_cmd::kDedupFail,
        ui::refresh_cmd::kRowsChanged,
        ui::refresh_cmd::kResultsChanged,
    };
}

}  // namespace

TEST(RefreshCommandsTest, AllCommandsAreNonEmpty) {
    for (std::string_view cmd : AllCommands()) {
        EXPECT_FALSE(cmd.empty()) << "空命令常量";
    }
}

TEST(RefreshCommandsTest, AllCommandsAreUnique) {
    std::vector<std::string_view> cmds = AllCommands();
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        for (std::size_t j = i + 1; j < cmds.size(); ++j) {
            EXPECT_NE(cmds[i], cmds[j])
                << "命令字面量重复: " << cmds[i];
        }
    }
}

TEST(RefreshCommandsTest, RowsChangedMapsToStatusText) {
    EXPECT_EQ("数据已更新，代理列表已刷新",
              ui::refresh_cmd::statusTextFor(ui::refresh_cmd::kRowsChanged));
}

TEST(RefreshCommandsTest, ResultsChangedMapsToStatusText) {
    EXPECT_EQ("测试结果已更新",
              ui::refresh_cmd::statusTextFor(ui::refresh_cmd::kResultsChanged));
}

TEST(RefreshCommandsTest, RegionResolveDoneMapsToStatusText) {
    EXPECT_EQ("地区解析完成，代理列表已刷新",
              ui::refresh_cmd::statusTextFor(ui::refresh_cmd::kRegionResolveDone));
}

TEST(RefreshCommandsTest, DedupOkMapsToStatusText) {
    EXPECT_EQ("去重完成，列表已刷新",
              ui::refresh_cmd::statusTextFor(ui::refresh_cmd::kDedupOk));
}

TEST(RefreshCommandsTest, DedupFailHasNoStatusText) {
    // 失败路径走模态框 + ERR 日志，不写状态栏成功文案，避免误导
    EXPECT_TRUE(ui::refresh_cmd::statusTextFor(ui::refresh_cmd::kDedupFail).empty());
}

TEST(RefreshCommandsTest, UnknownCommandReturnsEmpty) {
    EXPECT_TRUE(ui::refresh_cmd::statusTextFor("SOMETHING_ELSE").empty());
}

TEST(RefreshCommandsTest, ExistingCommandsUnchanged) {
    // 既有路径依赖这些字符串值，改动会破坏已实现行为
    EXPECT_EQ(std::string_view("DEDUP_OK"), ui::refresh_cmd::kDedupOk);
    EXPECT_EQ(std::string_view("DEDUP_FAIL"), ui::refresh_cmd::kDedupFail);
    EXPECT_EQ(std::string_view("REGION_RESOLVE_DONE"),
              ui::refresh_cmd::kRegionResolveDone);
}
```

- [ ] **Step 2: 运行测试确认失败**

Run:
```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8 --target test_refresh_commands
ctest -R RefreshCommands -V
```
Expected: **构建失败**（`RefreshCommands.h` 不存在）。

- [ ] **Step 3: 实现头文件**

```cpp
// include/RefreshCommands.h
#pragma once

#include <string>
#include <string_view>

// 数据变更后的刷新命令协议。
// 既有命令（DEDUP_OK / DEDUP_FAIL / REGION_RESOLVE_DONE / RESOLVE_REGION_START）
// 的字符串值保持不变，避免破坏已实现路径。
namespace ui {
namespace refresh_cmd {

// ── 既有命令 ─────────────────────────────────────────────
inline constexpr std::string_view kStatusResolved    = "RESOLVE_REGION_START";
inline constexpr std::string_view kRegionResolveDone = "REGION_RESOLVE_DONE";
inline constexpr std::string_view kDedupOk           = "DEDUP_OK";
inline constexpr std::string_view kDedupFail         = "DEDUP_FAIL";

// ── 新增命令（P0）───────────────────────────────────────
// ProfileItem 变更（Region / SubId / remarks）→ 全量重载 proxies_
inline constexpr std::string_view kRowsChanged    = "DATA_ROWS_CHANGED";
// ProfileExItem 变更（Delay / Failures / Runtime / Health）→ 全量重载 exItems_
inline constexpr std::string_view kResultsChanged = "DATA_RESULTS_CHANGED";

// 订阅面刷新复用既有 SubscriptionRefreshEvent（专门事件），
// 不再新增字符串命令 —— 见规格 §11.4。

// 命令 → 状态栏人读文案。未命中返回空串（调用方据此跳过状态栏写入）。
std::string statusTextFor(std::string_view command);

}  // namespace refresh_cmd
}  // namespace ui
```

- [ ] **Step 4: 实现映射表**

```cpp
// src/ui/RefreshCommands.cpp
#include "RefreshCommands.h"

#include <string>
#include <string_view>

namespace ui {
namespace refresh_cmd {

std::string statusTextFor(std::string_view command) {
    if (command == kRowsChanged) {
        return "数据已更新，代理列表已刷新";
    }
    if (command == kResultsChanged) {
        return "测试结果已更新";
    }
    if (command == kRegionResolveDone) {
        return "地区解析完成，代理列表已刷新";
    }
    if (command == kDedupOk) {
        return "去重完成，列表已刷新";
    }
    // kDedupFail / kStatusResolved / 未知命令 → 空串
    return "";
}

}  // namespace refresh_cmd
}  // namespace ui
```

- [ ] **Step 5: 注册 CMake 目标**

在 `CMakeLists.txt` 中把 `src/ui/RefreshCommands.cpp` 加入主程序与 CLI 两个 target 的源列表；新增测试 target，与既有 `test_config_reader` 同构：

```cmake
add_executable(test_refresh_commands tests/test_refresh_commands.cpp src/ui/RefreshCommands.cpp)
target_link_libraries(test_refresh_commands PRIVATE gtest gtest_main)
target_include_directories(test_refresh_commands PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME RefreshCommandsTest COMMAND test_refresh_commands)
```

- [ ] **Step 6: 运行测试确认通过**

Run:
```powershell
ctest -R RefreshCommands -V
```
Expected: **9/9 PASSED**

- [ ] **Step 7: 提交**

```powershell
git add include/RefreshCommands.h src/ui/RefreshCommands.cpp tests/test_refresh_commands.cpp CMakeLists.txt
git commit -m "feat(refresh): add centralized refresh command protocol with status text mapping"
```

---

## Task 2: `MainFrame::onStatusUpdate` 路由表 + 三段式

**Files:**
- Modify: `src/ui/MainFrame.cpp`（`onStatusUpdate`，约 `:1495-1525`）
- Modify: `src/ui/MainFrame.cpp`（`#include` 区）

- [ ] **Step 1: 引入头文件**

在 `MainFrame.cpp` 顶部 include 区加入：

```cpp
#include "RefreshCommands.h"
```

- [ ] **Step 2: 改造 `onStatusUpdate`**

将 `onStatusUpdate` 重构为「命令匹配 → 三段式动作」。保留既有 `DEDUP_FAIL` 模态框行为，仅成功路径去模态框（P0 范围内先保留模态框，去模态框属 P1 Task 11）：

```cpp
void MainFrame::onStatusUpdate(StatusUpdateEvent& event) {
    const std::string text = event.getText();

    // ── 第 1 步：状态栏人读文案（同步，先于重载入队）──────────
    const std::string status = ui::refresh_cmd::statusTextFor(text);
    if (!status.empty()) {
        setStatusText(0, wxString::FromStdString(status));
    }

    // ── 第 2 步：日志窗口留痕 ──────────────────────────────
    // 覆盖全部 ProfileItem/ProfileExItem 写入面（含地区解析），
    // 否则 R1b/V14 在地区解析场景会失败（评审 LOW-2）
    if (text == std::string(ui::refresh_cmd::kRowsChanged) ||
        text == std::string(ui::refresh_cmd::kResultsChanged) ||
        text == std::string(ui::refresh_cmd::kRegionResolveDone)) {
        Logger::write("[数据变更] " + status, LogLevel::REPORT);
    }

    // ── 第 3 步：面板重载（防抖合并）───────────────────────
    if (text == std::string(ui::refresh_cmd::kDedupOk)) {
        if (subPanel_ && controller_) {
            controller_->loadSubscriptionsAsync(this);
        }
        if (proxyPanel_ && subPanel_) {
            const std::string currentSubId = subPanel_->getSelectedSubId();
            if (!currentSubId.empty()) {
                controller_->loadProxiesAsync(currentSubId, this);
            }
        }
        wxMessageBox("Dedup completed.", "Dedup", wxOK | wxICON_INFORMATION);
        return;
    }
    if (text == std::string(ui::refresh_cmd::kDedupFail)) {
        wxMessageBox("Dedup failed.", "Dedup", wxOK | wxICON_WARNING);
        setStatusText(0, "Dedup failed.");
        return;
    }
    if (text == std::string(ui::refresh_cmd::kRegionResolveDone) ||
        text == std::string(ui::refresh_cmd::kRowsChanged)) {
        if (proxyPanel_) {
            proxyPanel_->reloadFromDatabase();
        }
        return;
    }
    if (text == std::string(ui::refresh_cmd::kResultsChanged)) {
        if (proxyPanel_) {
            proxyPanel_->refreshResults();
        }
        return;
    }

    // ── 未识别：区分「协议漂移早警」与「普通状态消息」──────────
    if (text.rfind("DATA_", 0) == 0) {
        // DATA_* 前缀必然是刷新命令，未识别即协议漂移
        Logger::write("Unknown refresh command: " + text, LogLevel::WARN);
        return;
    }
    // 中文状态消息等普通文本：保持原行为
    setStatusText(0, text);
}
```

**已核实的签名事实（勿凭猜测改写）**：
- 处理器签名是 `void MainFrame::onStatusUpdate(StatusUpdateEvent& event)`，**不是** `wxEvtCommand&`。
- 取值方法是 `event.getText()`，**返回 `std::string`**（`Events.h:155`），**不是** `GetText().ToStdString()`。
- 当前订阅 id 来源是 `subPanel_->getSelectedSubId()`（`MainFrame.cpp:1501`）；`ProxyListPanel` **没有** `GetCurrentSubId()` 公有访问器（仅私有成员 `currentSubId_`），**不要新增访问器**。
- 保留 `subPanel_` / `proxyPanel_` 空指针守卫（既有代码风格）。

- [ ] **Step 2b: 保护状态栏文案不被后续加载事件覆盖（评审 BLOCKING-2，必修）**

> **问题**：`onStatusUpdate` 第 1 步写完中文文案后，`reloadFromDatabase()` → `loadProxiesAsync` 后台线程投递 `ProxyListLoadedEvent`，其 handler 在 `MainFrame.cpp:335` **无条件**执行 `setStatusText(0, "Loaded subscription: " + evt.getSubId())`；订阅刷新路径还有 `MainFrame.cpp:326` 的 `setStatusText(0, "Refresh: loaded all proxies")`。两者同处 UI 事件队列、紧随第 1 步之后 → 中文文案 100ms–1s 后被英文覆盖，R1a/V13 不成立。

在 `MainFrame` 中新增一个状态栏保护标记（若已有同类 `refreshInFlight_` 则复用）：

```cpp
// src/ui/MainFrame.h  新增成员
std::atomic<bool> statusTextProtected_{false};

// 加载完成后重写一次命令文案
void rewriteRefreshStatusText();
```

```cpp
// src/ui/MainFrame.cpp  新增
void MainFrame::rewriteRefreshStatusText() {
    // 命令文案在加载完成后重写，避免被 "Loaded subscription: ..." 覆盖
    const std::string status = statusTextPending_;
    if (!status.empty()) {
        setStatusText(0, wxString::FromStdString(status));
        statusTextPending_.clear();
    }
}
```

然后在 `onStatusUpdate` 第 1 步改为「记录待重写文案」而非只写一次：

```cpp
    // ── 第 1 步：状态栏人读文案 ────────────────────────────
    // 先写入立即反馈，再登记待重写文案，供加载完成后重写一次
    const std::string status = ui::refresh_cmd::statusTextFor(text);
    if (!status.empty()) {
        statusTextPending_ = status;
        statusTextProtected_ = true;
        setStatusText(0, wxString::FromStdString(status));
    }
```

并在 `ProxyListLoadedEvent` handler（约 `MainFrame.cpp:330-336`）与 `SubscriptionRefreshEvent` handler（约 `:322-327`）的 `setStatusText` **之前**加守卫：

```cpp
        // 刷新命令触发的加载：不覆盖中文命令文案，改为加载完成后重写
        if (statusTextProtected_.exchange(false)) {
            rewriteRefreshStatusText();
        } else {
            setStatusText(0, "Loaded subscription: " + wxString(evt.getSubId()));
        }
```

> **注意**：`statusTextPending_` 为 `std::string` 成员，仅在 UI 线程读写，无需同步。`statusTextProtected_` 用 `std::atomic<bool>` + `exchange` 保证「消费一次即清除」，避免后续无关加载事件误重写。实施前用 `rg -n "refreshInFlight_|Loaded subscription" src/ui/MainFrame.cpp` 确认是否已有可复用的原子标记。

- [ ] **Step 3: 编译**

Run:
```powershell
cmake --build build --parallel 8
```
Expected: **0 error**

- [ ] **Step 4: 全量回归**

Run:
```powershell
ctest -V
```
Expected: 全部既有测试通过（本步仅新增路由分支，不改变既有命令语义）。

- [ ] **Step 5: 提交**

```powershell
git add src/ui/MainFrame.cpp
git commit -m "feat(refresh): route refresh commands in onStatusUpdate with status bar and log sync"
```

---

## Task 3: 补齐 G1–G4/G6 命令发射

**Files:**
- Modify: `src/ui/AppController.cpp`
- Modify: `src/ui/AppController.cpp`（`#include` 区）

**通用约束：**
- `StatusUpdateEvent` 构造签名为 `(int field, const std::string&)`，而命令常量为 `std::string_view` → 发射时必须 `std::string(cmd)` 显式转换。
- **仅成功完成路径发射**；取消 / 异常路径**不发射**（与既有 `REGION_RESOLVE_DONE` 约束一致）。
- 事件投递目标为 `wxGetTopLevelParent(handler)` 或既有 `topLevel` 变量（沿用各函数现有做法）。

- [ ] **Step 1: 引入头文件**

```cpp
#include "RefreshCommands.h"
```

- [ ] **Step 2: G1 — 测试单订阅完成（`doTestSubscription`，约 `:2120`）**

在 `resolveRegionsForValidProxies(subId)` 调用之后追加：

```cpp
if (!cancelRequested_) {
    wxQueueEvent(topLevel,
        new StatusUpdateEvent(0, std::string(ui::refresh_cmd::kRowsChanged)));
}
```

- [ ] **Step 3: G2 — 测试全部完成（约 `:2206`）**

在 `resolveRegionsForValidProxies()`（无 subId 版本）之后追加与 Step 2 相同的两行。

- [ ] **Step 4: G3 — 更新单订阅（`doUpdateSubscription`，约 `:2077`）**

在既有 `StatusUpdateEvent("Update completed: " + subId)` 之后追加**一条**（仅代理列表）：

```cpp
wxQueueEvent(topLevel,
    new StatusUpdateEvent(0, std::string(ui::refresh_cmd::kRowsChanged)));
```

> **不要**追加 `SubscriptionRefreshEvent`（评审 HIGH-3）：其既有语义是「手动刷新 = 加载全部」——`MainFrame.cpp:324` 传**空 subId** → `ProxyListPanel::loadProxies("")` 先 `currentSubId_ = ""` 再 `applySubscriptionFilter("")` → `ProxyListPanel.cpp:290-291` `proxies_ = allProxies_`，即**用户当前订阅的视图被强制重置为「全部」**；且会触发第二次全表异步读。
>
> **订阅面板无需额外处理**（评审 LOW-3）：`MainFrame.cpp:183-189` 的 Bind lambda 已对 `payload.StartsWith("Update completed:")` / `"All subscriptions updated"` 执行 `controller_->loadSubscriptionsAsync(this)`——订阅面板本来就在刷新。规格 §2.2 对 G3/G4「零刷新」的描述夸大，实际只缺代理面板。

- [ ] **Step 5: G4 — 更新全部订阅（`doUpdateAllSubscriptions`，约 `:2098`）**

在既有 `StatusUpdateEvent("All subscriptions updated")` 之后追加与 Step 4 相同的**一条**（仅 `kRowsChanged`）。

- [ ] **Step 6: G6 — 导入订阅（`importSubscription`，约 `:277-285`）**

> **评审 BLOCKING-1b：原片段无法编译。** `class AppController`（`src/ui/AppController.h:87`）是普通类、**不继承 `wxWindow`**，因此片段里的 `wxGetTopLevelParent(this)` 无隐式转换、编译失败。
>
> **修正方案（选 b，与既有签名同构）**：给 `importSubscription` 增补 `wxEvtHandler*` 参数，由调用点传入 handler。

```cpp
// include 侧签名：
// 修改前: bool importSubscription(const std::string& url);
// 修改后: bool importSubscription(const std::string& url, wxEvtHandler* handler);
```

```cpp
// src/ui/AppController.cpp  importSubscription 内，成功分支
if (ok && handler != nullptr) {
    wxQueueEvent(handler,
        new StatusUpdateEvent(0, std::string(ui::refresh_cmd::kRowsChanged)));
}
```

调用点同步改（共 2 处，均在 UI 线程）：
- `src/ui/MainFrame.cpp:1056` → `controller_->importSubscription(url, this)`
- `src/ui/SubscriptionPanel.cpp:511` → `controller_->importSubscription(url, wxGetTopLevelParent(this))`

> `wxQueueEvent` 而非 `wxPostEvent`：同线程下两者皆可，但 `wxPostEvent` 在 `wx/include/wx/event.h:4279` 明确标注 "not thread-safe"，且全仓 45 处统一用 `wxQueueEvent`。

- [ ] **Step 6b: G9（新增，评审 BLOCKING-1）— GUI 数据库同步**

> `doSyncDatabases`（`src/ui/AppController.cpp:2506-2524`）完成后仅发 `StatusUpdateEvent(0, "数据库同步完成")` → 落入 `else` 分支零刷新。而 `syncDatabases` 经**独立连接**写目标库（`src/SubitemUpdaterV2.cpp:1141-1146` `sqlite3_open_v2(targetDbPath, ...)`），`db_` 上的面板缓存看不到这批写入。AutoTask 可达此路径（`main_cli.cpp:801` `steps = {"update","test","dedup","sync"}`）。

在 `doSyncDatabases` 成功分支追加：

```cpp
wxQueueEvent(topLevel,
    new StatusUpdateEvent(0, std::string(ui::refresh_cmd::kRowsChanged)));
```

同时**删除规格 §1.3 的错误断言**「不改变 `-S / -sync` 数据库同步功能（仍为 TODO）」——CLI `-S` 已实现（`src/main_cli.cpp:674-704`），GUI `doSyncDatabases` 也存在；真正 TODO 的只有菜单壳 `MainFrame::onMenuSyncDb`（`:1061-1072`，`syncDatabasesAsync` 调用被注释）。

- [ ] **Step 7: 确认既有接线（已核实，无需改动）**

已核实 `SubscriptionRefreshEvent` 已完整贯通，**本步只需验证，不需要新增绑定**：

```powershell
rg -n "SubscriptionRefreshEvent|wxEVT_SUBSCRIPTION_REFRESH" src/ui
```

预期命中（已确认存在）：
- `src/ui/Events.h:53` 前向声明、`:70` `wxDECLARE_EVENT`、`:182-189` 类定义（构造 `explicit SubscriptionRefreshEvent() : wxEvent(0, wxEVT_SUBSCRIPTION_REFRESH) {}`）
- `src/ui/Events.cpp:17` `wxDEFINE_EVENT`
- `src/ui/SubscriptionPanel.cpp:373` 手动刷新按钮发射（`SubscriptionRefreshEvent evt;`）
- `src/ui/MainFrame.cpp:322` `Bind(wxEVT_SUBSCRIPTION_REFRESH, [this](SubscriptionRefreshEvent&) {...})`

即：本任务只需在 G3/G4/G6 完成点**多发一条 `SubscriptionRefreshEvent`**，接收侧零改动。

- [ ] **Step 8: 编译 + 回归**

Run:
```powershell
cmake --build build --parallel 8
ctest -V
```
Expected: 0 error，全部测试通过。

- [ ] **Step 9: 手工验收（对应 V1–V6）**

使用 `bin/` 下最新构建产物（**禁止 `bin/worker/validproxy.exe`**），配置指向 `test/guindb.db`：

| 编号 | 操作 | 预期 |
|---|---|---|
| V1 | 选中订阅 → 右键「解析地区」（单代理） | Region 列自动更新 |
| V2 | 选中订阅 → 「测试订阅」 | ≤1s 内 Region + Delay 同步 |
| V3 | 「测试全部」 | 同 V2 |
| V4 | 订阅面板 → 右键「更新」 | 订阅列表 + 代理列表均重载 |
| V5 | 订阅面板 → 「更新全部」 | 同 V4 |
| V6 | 「导入订阅」URL | 新代理出现，无需点刷新 |

- [ ] **Step 10: 提交**

```powershell
git add src/ui/AppController.cpp
git commit -m "feat(refresh): emit refresh commands on test/update/import completion paths"
```

---

## Task 4: `db::Transaction` RAII（TDD，含嵌套感知）

**Files:**
- Modify: `include/DatabaseHelper.h`
- Modify: `src/DatabaseHelper.cpp`
- Create: `tests/test_db_transaction.cpp`
- Modify: `CMakeLists.txt`

> **关键约束（规格 §11.2）**：`BEGIN` 前**必须**检查 `sqlite3_get_autocommit()`。项目红线来自 `docs/bugfix/2026-08-13-Bugfix-ProfileExItemDAO-NestedTransaction-v1.0.md`——`updateTestResultBatch` 曾因无条件 `BEGIN` 触发 `cannot start a transaction within a transaction`。同时**不得**在构造失败时 `throw`（`docs/plans/2026-05-13-P4-...` 正因该模式被取消）。

- [ ] **Step 1: 先写失败的测试**

```cpp
// tests/test_db_transaction.cpp
#include <gtest/gtest.h>

#include <sqlite3.h>

#include "DatabaseHelper.h"

namespace {

sqlite3* OpenInMemoryDb() {
    sqlite3* db = nullptr;
    EXPECT_EQ(SQLITE_OK,
              sqlite3_open(":memory:", &db));
    EXPECT_EQ(SQLITE_OK, sqlite3_exec(db,
        "CREATE TABLE T(id INTEGER PRIMARY KEY, v TEXT);",
        nullptr, nullptr, nullptr));
    return db;
}

void CloseDb(sqlite3* db) {
    if (db) sqlite3_close(db);
}

int CountRows(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM T", -1, &stmt, nullptr) != SQLITE_OK) {
        return -1;
    }
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

}  // namespace

TEST(DbTransactionTest, CommitPersistsData) {
    sqlite3* db = OpenInMemoryDb();
    {
        db::Transaction txn(db);
        EXPECT_TRUE(txn.began());
        sqlite3_exec(db, "INSERT INTO T(v) VALUES('a')", nullptr, nullptr, nullptr);
        EXPECT_TRUE(txn.commit());
    }
    EXPECT_EQ(1, CountRows(db));
    CloseDb(db);
}

TEST(DbTransactionTest, DestructorWithoutCommitRollsBack) {
    sqlite3* db = OpenInMemoryDb();
    {
        db::Transaction txn(db);
        sqlite3_exec(db, "INSERT INTO T(v) VALUES('a')", nullptr, nullptr, nullptr);
        // 不调用 commit() → 析构必须 ROLLBACK
    }
    EXPECT_EQ(0, CountRows(db));
    CloseDb(db);
}

TEST(DbTransactionTest, ExplicitRollbackDiscardsData) {
    sqlite3* db = OpenInMemoryDb();
    {
        db::Transaction txn(db);
        sqlite3_exec(db, "INSERT INTO T(v) VALUES('a')", nullptr, nullptr, nullptr);
        EXPECT_TRUE(txn.rollback());
    }
    EXPECT_EQ(0, CountRows(db));
    CloseDb(db);
}

TEST(DbTransactionTest, ParticipatesInOuterTransaction) {
    // 项目红线回归：外层已开启事务时不得再 BEGIN
    sqlite3* db = OpenInMemoryDb();
    EXPECT_EQ(SQLITE_OK, sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr));
    {
        db::Transaction txn(db);
        EXPECT_FALSE(txn.began()) << "外层事务中不应再 BEGIN";
        sqlite3_exec(db, "INSERT INTO T(v) VALUES('outer')", nullptr, nullptr, nullptr);
        EXPECT_FALSE(txn.commit()) << "未自开启事务不应提交";
    }
    // 外层回滚，内层插入一并消失
    EXPECT_EQ(SQLITE_OK, sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr));
    EXPECT_EQ(0, CountRows(db));
    CloseDb(db);
}

TEST(DbTransactionTest, NullDbIsSafe) {
    db::Transaction txn(nullptr);
    EXPECT_FALSE(txn.began());
    EXPECT_FALSE(txn.commit());
    EXPECT_FALSE(txn.rollback());
}

TEST(DbTransactionTest, DoubleCommitReturnsFalse) {
    sqlite3* db = OpenInMemoryDb();
    db::Transaction txn(db);
    sqlite3_exec(db, "INSERT INTO T(v) VALUES('a')", nullptr, nullptr, nullptr);
    EXPECT_TRUE(txn.commit());
    EXPECT_FALSE(txn.commit()) << "二次 commit 应返回 false";
    EXPECT_EQ(1, CountRows(db));
    CloseDb(db);
}
```

- [ ] **Step 2: 运行测试确认失败**

Run:
```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8 --target test_db_transaction
```
Expected: **构建失败**（`db::Transaction` 未定义）。

- [ ] **Step 3: 头文件声明**

在 `include/DatabaseHelper.h` 中追加（`#pragma once` 与现有 include 之后）：

```cpp
// RAII 事务守卫：构造时尝试 BEGIN，析构时若未结束则 ROLLBACK。
// 嵌套感知——BEGIN 前检查 sqlite3_get_autocommit()，已在事务中则进入
// 「参与外层」模式（不 BEGIN、析构不 ROLLBACK），使调用方可安全嵌套在
// updateTestResultBatch 等已自管事务的函数内。
// 构造失败不抛出（沿用 docs/plans/2026-05-13-P4 取消原因的反面教训），
// 而是记录 ERR 日志并进入 no-op 态。
class Transaction {
public:
    explicit Transaction(sqlite3* db);
    ~Transaction();

    bool commit();
    bool rollback();

    // 本守卫是否真正开启了事务（外层已有事务时为 false）
    bool began() const;

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

private:
    sqlite3* db_ = nullptr;
    bool beganTransaction_ = false;
    bool finished_ = false;
};
```

- [ ] **Step 4: 实现**

在 `src/DatabaseHelper.cpp` 中追加（确保已 `#include <sqlite3.h>` 与 `"Logger.h"`）：

```cpp
namespace db {

Transaction::Transaction(sqlite3* db) : db_(db) {
    if (db_ == nullptr) {
        return;
    }
    // 嵌套感知：仅 autocommit 态才开启事务
    if (sqlite3_get_autocommit(db_) == 0) {
        return;
    }
    char* errMsg = nullptr;
    const int rc = sqlite3_exec(db_, "BEGIN TRANSACTION", nullptr, nullptr, &errMsg);
    if (rc == SQLITE_OK) {
        beganTransaction_ = true;
    } else {
        Logger::write(std::string("Transaction BEGIN failed: ") +
                      (errMsg != nullptr ? errMsg : sqlite3_errmsg(db_)),
                      LogLevel::ERR);
        if (errMsg != nullptr) {
            sqlite3_free(errMsg);
        }
    }
}

Transaction::~Transaction() {
    if (beganTransaction_ && !finished_) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    }
}

bool Transaction::commit() {
    if (!beganTransaction_ || finished_) {
        return false;
    }
    char* errMsg = nullptr;
    const int rc = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &errMsg);
    if (rc == SQLITE_OK) {
        finished_ = true;
        return true;
    }
    Logger::write(std::string("Transaction COMMIT failed: ") +
                  (errMsg != nullptr ? errMsg : sqlite3_errmsg(db_)),
                  LogLevel::ERR);
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    if (errMsg != nullptr) {
        sqlite3_free(errMsg);
    }
    return false;
}

bool Transaction::rollback() {
    if (!beganTransaction_ || finished_) {
        return false;
    }
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    finished_ = true;
    return true;
}

bool Transaction::began() const {
    return beganTransaction_;
}

}  // namespace db
```

> **注意**：`include/DatabaseHelper.h` 现有 `db::Database` 类的命名空间包裹方式需保持一致。若该头文件以 `namespace db { class Database {...}; }` 形式书写，把 `Transaction` 放在**同一命名空间块内**。实施前先 `rg -n "namespace db" include/DatabaseHelper.h` 确认。

- [ ] **Step 5: 注册 CMake 目标**

```cmake
add_executable(test_db_transaction tests/test_db_transaction.cpp src/DatabaseHelper.cpp)
target_link_libraries(test_db_transaction PRIVATE gtest gtest_main sqlite3)
target_include_directories(test_db_transaction PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME DbTransactionTest COMMAND test_db_transaction)
```

- [ ] **Step 6: 运行测试确认通过**

Run:
```powershell
ctest -R DbTransaction -V
```
Expected: **6/6 PASSED**（含 `ParticipatesInOuterTransaction` 红线回归）。

- [ ] **Step 7: 提交**

```powershell
git add include/DatabaseHelper.h src/DatabaseHelper.cpp tests/test_db_transaction.cpp CMakeLists.txt
git commit -m "feat(db): add Transaction RAII guard with nested-transaction awareness"
```

---

## Task 5: `RegionBatchResolver` 迁移至 `Transaction`（⬇️ 降级为 P2，见评审）

> **评审结论：移出 P0。** 该路径**已工作且已具备原子性**（`RegionBatchResolver.cpp:390-414` 的 `commitOk` 标志 + 失败 ROLLBACK），迁移**功能收益为 0**，却引入真实的原子性回退风险（见 Step 2 警告）。与 Task 4 一并降级为 P2 纯重构、独立提交、独立回归。
> **P0 保留 Task 1/2/3/6 即可完整交付 R1（面板）、R1a（状态栏）、R1b（日志）、R3（全量重载）+ Task 6 的 R2 安全修复。**

**Files:**
- Modify: `src/RegionBatchResolver.cpp`（`flushRegionBuffer`，约 `:382-414`）

- [ ] **Step 1: 定位现有事务代码**

Run:
```powershell
rg -n "BEGIN TRANSACTION|COMMIT|ROLLBACK|commitOk" src/RegionBatchResolver.cpp
```

- [ ] **Step 2: 替换为 RAII（必须保留 `commitOk`，评审 HIGH-4）**

> ⚠️ **不得写成「循环不变 + `txn.commit()`」**。若丢掉 `commitOk`，中途 `sqlite3_step` 失败会 `break` 后直接 `commit()` → **提交半批 Region**，直接违反 R2「中途失败自动 ROLLBACK，不产生部分写入」。原注释「失败时 txn 析构自动 ROLLBACK」也是错的——析构只在 `beganTransaction_ && !finished_` 时回滚，`commit()` 一旦成功置位 `finished_` 就不再回滚。

```cpp
db::Transaction txn(db_);
bool commitOk = true;
// ... prepare_v2 / bind_text / step / reset 循环 ...
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        Logger::write(std::string("flushRegionBuffer step failed: ") +
                      sqlite3_errmsg(db_), LogLevel::ERR);
        commitOk = false;
        sqlite3_reset(stmt);
        break;          // 必须 break 后走 rollback，不能落到 commit
    }
// ... 循环结束 ...
if (commitOk) {
    txn.commit();
} else {
    txn.rollback();     // step 失败 → 显式回滚整批
}
sqlite3_finalize(stmt);
```

保留既有的「缓冲区 swap 在锁内、flush 在锁外」结构（注释已明确，勿改）。

- [ ] **Step 2b: 顺带修 `auto` 违规（评审 LOW-7）**

`src/RegionBatchResolver.cpp:391` 的 `for (const auto& entry : batch)` 违反 AGENTS.md §1「禁止 auto」。既然改写该函数，顺带改为：

```cpp
for (const std::pair<std::string, std::string>& entry : batch) {
```

（实际元素类型以 `rg -n "batch" src/RegionBatchResolver.cpp` 抄录的声明为准。）

- [ ] **Step 3: 编译 + 回归**

Run:
```powershell
cmake --build build --parallel 8
ctest -V
```
Expected: 0 error；`RegionBatchResolver` 相关测试全绿。

- [ ] **Step 4: 确认无残留裸 BEGIN**

Run:
```powershell
rg -n '"BEGIN TRANSACTION"' src/RegionBatchResolver.cpp
```
Expected: **无命中**。

- [ ] **Step 5: 提交**

```powershell
git add src/RegionBatchResolver.cpp
git commit -m "refactor(db): replace manual BEGIN/COMMIT with Transaction RAII in RegionBatchResolver"
```

---

## Task 6: `Deduplicator::deduplicatePhase0` SQL 参数绑定（TDD）

**Files:**
- Modify: `src/update/Deduplicator.cpp`
- Modify: `tests/test_deduplicator.cpp`

> **范围红线（规格 §11.3）**：**仅**修 Phase0 的字符串拼接。**不**迁移 `Deduplicator::deduplicate()` 的 6-phase 主事务到 `Transaction`——沿用 `docs/plans/2026-05-13-P4-...`（cancelled）记录的治理决策「已正确处理的错误路径，替换收益不足以抵消风险」。

> **已核实的签名事实（勿凭猜测改写）**：`int Deduplicator::deduplicatePhase0()`（`src/update/Deduplicator.cpp:85`）——**无参数，返回 `int`（更新行数）**，subid 从 `config_.dedup_subids[0]`（及 `[1]`）读取。
>
> **W1 实际有两条拼接 SQL，都要修**：
> 1. 主更新：`"UPDATE ProfileItem SET SubId = '" + protectedSubId + "' WHERE IndexId IN (SELECT pi.IndexId FROM ProfileItem pi JOIN ProfileExItem pe ON pi.IndexId = pe.IndexId WHERE pe.Delay > 0 AND pe.Delay != '-1')"`
> 2. **fallback 更新**（`config_.dedup_subids.size() > 1` 时）：`"UPDATE ProfileItem SET SubId = '" + fallbackSubId + "' WHERE SubId = '" + protectedSubId + "' AND IndexId IN (... WHERE pe.Delay <= 0 OR pe.Delay = '-1')"` —— 含**两个**拼接点。

- [ ] **Step 1: 先写失败的回归测试**

```cpp
// tests/test_deduplicator.cpp  追加
// W1 回归：protectedSubId 含单引号时旧实现拼接出非法 SQL。
// deduplicatePhase0() 无参数、subid 取自 config_.dedup_subids，
// 故通过配置注入含单引号的 subid 间接触发。
TEST(DeduplicatorTest, Phase0HandlesSubIdWithSingleQuote) {
    sqlite3* db = OpenTestDb();          // 复用该测试文件既有夹具

    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS ProfileItem("
        "IndexId TEXT PRIMARY KEY, Address TEXT, Port INTEGER, "
        "ConfigType TEXT, Id TEXT, Network TEXT, SubId TEXT);",
        nullptr, nullptr, nullptr);
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS ProfileExItem("
        "IndexId TEXT PRIMARY KEY, Delay TEXT);",
        nullptr, nullptr, nullptr);
    sqlite3_exec(db,
        "INSERT INTO ProfileItem(IndexId, SubId) VALUES('1','src');"
        "INSERT INTO ProfileExItem(IndexId, Delay) VALUES('1','120');",
        nullptr, nullptr, nullptr);

    Deduplicator::Config cfg;
    cfg.dedup_subids.push_back("it's protected");
    Deduplicator dedup(db);
    dedup.setConfig(cfg);               // 以既有夹具的注入方式为准

    EXPECT_GT(dedup.deduplicatePhase0(), 0)
        << "含单引号的 subid 不应导致 SQL 语法错误";

    sqlite3_stmt* stmt = nullptr;
    EXPECT_EQ(SQLITE_OK,
        sqlite3_prepare_v2(db, "SELECT SubId FROM ProfileItem WHERE IndexId='1'",
                           -1, &stmt, nullptr));
    EXPECT_EQ(SQLITE_ROW, sqlite3_step(stmt));
    EXPECT_STREQ("it's protected",
                 reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);
}

// 第二条拼接 SQL 的回归：fallback 分支含两个拼接点
TEST(DeduplicatorTest, Phase0FallbackHandlesSingleQuote) {
    sqlite3* db = OpenTestDb();
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS ProfileItem("
        "IndexId TEXT PRIMARY KEY, SubId TEXT);",
        nullptr, nullptr, nullptr);
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS ProfileExItem("
        "IndexId TEXT PRIMARY KEY, Delay TEXT);",
        nullptr, nullptr, nullptr);
    sqlite3_exec(db,
        "INSERT INTO ProfileItem(IndexId, SubId) VALUES('1','src');"
        "INSERT INTO ProfileExItem(IndexId, Delay) VALUES('1','-1');",
        nullptr, nullptr, nullptr);

    Deduplicator::Config cfg;
    cfg.dedup_subids.push_back("it's protected");
    cfg.dedup_subids.push_back("fallback's");   // 两个 subid 都含单引号
    Deduplicator dedup(db);
    dedup.setConfig(cfg);

    EXPECT_GT(dedup.deduplicatePhase0(), 0)
        << "fallback 分支含单引号时不应语法错误";
}
```

> **注意**：`OpenTestDb()`、`Deduplicator::Config`、`setConfig`、构造器形态**以 `tests/test_deduplicator.cpp` 现有夹具为准**。实施前先 `rg -n "Deduplicator\(" tests/test_deduplicator.cpp` 抄录既有构造与配置注入写法并逐字对齐；若夹具无法注入自定义 `dedup_subids`，则在测试内构造一个含单引号 subid 的最小 config 结构传入。**不要**为了测试放宽生产可见性。

- [ ] **Step 2: 运行确认失败**

Run:
```powershell
ctest -R Deduplicator -V
```
Expected: **FAIL**（旧实现拼接出非法 SQL，`EXPECT_GT` 不成立或 `sqlite3_exec` 返回非 `SQLITE_OK`）。

- [ ] **Step 3: 修实现（两条 SQL 都要改）**

Run:
```powershell
rg -n "protectedSubId|fallbackSubId" src/update/Deduplicator.cpp
```

主更新改为参数绑定：

```cpp
const char* kPrimarySql =
    "UPDATE ProfileItem SET SubId = ? WHERE IndexId IN ("
    "SELECT pi.IndexId FROM ProfileItem pi JOIN ProfileExItem pe "
    "ON pi.IndexId = pe.IndexId WHERE pe.Delay > 0 AND pe.Delay != '-1')";
sqlite3_stmt* stmt = nullptr;
if (sqlite3_prepare_v2(db_, kPrimarySql, -1, &stmt, nullptr) != SQLITE_OK) {
    Logger::write(std::string("Phase0 primary prepare failed: ") +
                  sqlite3_errmsg(db_), LogLevel::ERR);
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    return 0;
}
sqlite3_bind_text(stmt, 1, protectedSubId.c_str(), -1, SQLITE_TRANSIENT);
if (sqlite3_step(stmt) != SQLITE_DONE) {
    Logger::write(std::string("Phase0 primary step failed: ") +
                  sqlite3_errmsg(db_), LogLevel::ERR);
    sqlite3_finalize(stmt);
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    return 0;
}
updated += sqlite3_changes(db_);
sqlite3_finalize(stmt);
```

fallback 更新改为两个绑定参数：

```cpp
const char* kFallbackSql =
    "UPDATE ProfileItem SET SubId = ? WHERE SubId = ? AND IndexId IN ("
    "SELECT pi.IndexId FROM ProfileItem pi JOIN ProfileExItem pe "
    "ON pi.IndexId = pe.IndexId WHERE pe.Delay <= 0 OR pe.Delay = '-1')";
sqlite3_stmt* stmt2 = nullptr;
if (sqlite3_prepare_v2(db_, kFallbackSql, -1, &stmt2, nullptr) != SQLITE_OK) {
    Logger::write(std::string("Phase0 fallback prepare failed: ") +
                  sqlite3_errmsg(db_), LogLevel::ERR);
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    return updated;
}
sqlite3_bind_text(stmt2, 1, fallbackSubId.c_str(), -1, SQLITE_TRANSIENT);
sqlite3_bind_text(stmt2, 2, protectedSubId.c_str(), -1, SQLITE_TRANSIENT);
if (sqlite3_step(stmt2) != SQLITE_DONE) {
    Logger::write(std::string("Phase0 fallback step failed: ") +
                  sqlite3_errmsg(db_), LogLevel::ERR);
} else {
    updated += sqlite3_changes(db_);
}
sqlite3_finalize(stmt2);
```

> **注意**：
> - `WHERE` 条件**逐字保留**既有语义，只改绑定方式。
> - 错误分支的 `ROLLBACK` 是为了对齐既有错误处理风格（该函数运行在 `deduplicate()` 的外层事务内）。若既有代码错误分支用的是 `COMMIT`，**改为 `ROLLBACK`**——这正是 `docs/plans/2026-05-13-P4-...` 记录的语义 bug（prepare 失败路径误用 `COMMIT`）。
> - 保留函数末尾的 `INFO` 日志（含 `updated` 计数）。

- [ ] **Step 4: 运行确认通过**

Run:
```powershell
ctest -R Deduplicator -V
```
Expected: **全部 PASSED**（含新回归用例）。

- [ ] **Step 4b: 修第三处拼接 `blacklist_subid`（评审 HIGH-2，必修）**

> Task 6 原稿（含 v1.1 修正）只覆盖 Phase0 主 SQL + fallback，**完全漏掉** `src/update/Deduplicator.cpp:224`：
> `std::string sql = "UPDATE ProfileItem SET SubId = '" + config_.blacklist_subid + "' "`（同行 236 行还把它拼进日志）。
> 且原 Step 5 的验证命令 `rg -n "protectedSubId.*'"` **结构上不可能命中它**（变量名是 `blacklist_subid`）→ 实施者会得到「无命中 = 已修完」的假绿灯。

同样参数化：

```cpp
const char* kBlacklistSql =
    "UPDATE ProfileItem SET SubId = ? WHERE ...";   // WHERE 条件逐字保留既有语义
sqlite3_stmt* blStmt = nullptr;
if (sqlite3_prepare_v2(db_, kBlacklistSql, -1, &blStmt, nullptr) != SQLITE_OK) {
    Logger::write(std::string("Blacklist phase prepare failed: ") +
                  sqlite3_errmsg(db_), LogLevel::ERR);
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    return 0;
}
sqlite3_bind_text(blStmt, 1, config_.blacklist_subid.c_str(), -1, SQLITE_TRANSIENT);
const int blRc = sqlite3_step(blStmt);
sqlite3_finalize(blStmt);
if (blRc != SQLITE_DONE) {
    Logger::write(std::string("Blacklist phase step failed: ") + sqlite3_errmsg(db_),
                  LogLevel::ERR);
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    return 0;
}
```

并追加回归用例：

```cpp
TEST(DeduplicatorTest, BlacklistPhaseHandlesSingleQuote) {
    sqlite3* db = OpenTestDb();
    // ... 建表与插入同 Phase0 用例 ...
    Deduplicator::Config cfg;
    cfg.blacklist_subid = "bad'sub";
    Deduplicator dedup(db);
    dedup.setConfig(cfg);
    EXPECT_GT(dedup.deduplicateBlacklistPhase(), 0)
        << "blacklist_subid 含单引号时不应语法错误";
}
```

> 方法名以 `rg -n "blacklist" src/update/Deduplicator.cpp src/update/Deduplicator.h` 抄录的实际名称为准。

- [ ] **Step 5: 确认无残留拼接（验证命令已修正）**

Run:
```powershell
rg -n "UPDATE ProfileItem SET SubId" src/update/Deduplicator.cpp
```
Expected: **仅 3 处参数化形式**（`?` 占位符），**无任何** `SET SubId = '` 拼接。

补充交叉检查（防止漏掉其它表的拼接）：
```powershell
rg -n 'sqlite3_exec\(.*\+ *config_|sqlite3_exec\(.*\+ *SubId' src/update/Deduplicator.cpp src/SubitemUpdaterV2.cpp
```
Expected: 仅命中日志字符串拼接，无命中 SQL 拼接。

- [ ] **Step 6: 提交**

```powershell
git add src/update/Deduplicator.cpp tests/test_deduplicator.cpp
git commit -m "fix(dedup): bind protectedSubId as parameter instead of SQL string concatenation"
```

---

## Task 7: P0 全量验证与文档登记

- [ ] **Step 1: 全量构建**

Run:
```powershell
cmake --build build --parallel 8
```
Expected: **0 error, 0 warning**（忽略既有无关告警）。

- [ ] **Step 2: 全量单元测试**

Run:
```powershell
ctest -V
```
Expected: 全部通过。

- [ ] **Step 3: UI 自动化验收**

Run:
```powershell
.\scripts\build-and-test.bat
```
Expected: **ALL TESTS PASSED**（UI 相关变更交付前必须全绿）。

- [ ] **Step 4: 手工验收 V1–V8 + V13–V15**

按规格 §7.3 与 §4.8.5 逐项验证。重点：
- V13：操作完成后状态栏 Field0 **立即**显示人读文案（不必等列表重载）
- V14：`LogPanel` 过滤器为 REPORT 或更宽时出现 `[数据变更]` 行
- V15：`log.console_level` 设为 WARN 时该行**不出现**（预期行为）

- [ ] **Step 5: 更新文档状态**

把 `docs/specs/2026-09-18-Spec-DataReadWriteRefresh-v1.0.md` 的状态字段由 `📝 draft` 改为 `🔄 in-progress`（P0 完成，P1/P2 未开始），并在 `docs/INDEX.md` §7.5 #46 与 `docs/plans/project-plans-tracker.md` 同步状态。

- [ ] **Step 6: 提交**

```powershell
git add docs/specs/2026-09-18-Spec-DataReadWriteRefresh-v1.0.md docs/INDEX.md docs/plans/project-plans-tracker.md
git commit -m "docs(refresh): mark P0 complete in spec and indexes"
```

---

## Task 10–13: P1 / P2（P0 验证通过后独立排期）

| Task | 阶段 | 内容 | 规格章节 |
|---|---|---|---|
| 10 | P1 | `doAutoTaskImpl` progressCallback 步骤完成边沿检测（`step_status` 从 running 跳变）按 `AutoTaskManager::stepNameToType` 映射发命令；`stepNameToType` 提升为公有静态；`EXPORT` 不发；`doAutoTaskImpl` 开头重置静态局部状态 | §4.4 |
| 11 | P1 | `onMenuDedup` 裸 `std::thread(...).detach()` 改 `AsyncOperationGuard{workerThread_, isRunning_, cancelRequested_, this}` + `ScopeGuard`；`DEDUP_OK` 分支删除模态框改状态栏 + REPORT 日志（`DEDUP_FAIL` 保留模态框） | §4.6 |
| 12 | P1 | 删除 3 个 async reader 的内联 PRAGMA 块，统一调用 `service::DatabaseConnectionService::applyPragmas`；`busy_timeout` 5000→10000ms | §11.1 |
| 13 | P1/P2 | `doResolveSingleProxyRegion` 复用 `RegionBatchResolver` 缓冲获得事务；P2 读路径（删 `loadProxyResults` 被丢弃的 `computeBatch`、新增 `ProfileExItemDAO::getAllByIndexIds` 批量 `IN` 查询分批 ≤500、`refreshResults` 改 `getRunningDurationsAsync`） | §4.5.4 / §4.7 |

---

## Self-Review 记录

| 检查项 | 结论 |
|---|---|
| **规格覆盖** | R1（面板）→ Task 3；R1a（状态栏）/R1b（日志窗口）→ Task 2；R2（安全写入）→ Task 4/5/6；R3（全量写入）→ Task 3 走 `reloadFromDatabase` 全量重载。G1–G6 → Task 3；G7/G8 → Task 11（P1）；G5 → Task 10（P1） |
| **占位符扫描** | 无 TBD/TODO；所有代码步骤含完整代码块。两处标注「以现有代码为准」的地方（`GetCurrentSubId` 访问器、`deduplicatePhase0` 的 WHERE 条件）是**刻意的实现时对齐提示**，非占位符——因为它们依赖现有代码的确切形态，硬写会引入错误 |
| **类型一致性** | `RefreshCommands.h` 声明 `std::string statusTextFor(std::string_view)` → `.cpp` 实现签名一致；`Transaction` 声明的 4 个方法（`commit`/`rollback`/`began`/删除拷贝）与实现一致；测试文件引用的 `db::Transaction::began()` 已声明 |
| **嵌套事务红线** | Task 4 `Transaction` 构造含 `sqlite3_get_autocommit` 检查 + `ParticipatesInOuterTransaction` 回归用例 ✅ |
| **禁用 auto** | 全部代码块显式类型，无 `auto` ✅ |
| **PRAGMA 误判撤销** | 未出现 `applyDefaultPragma` 新入口；Task 12 只做收敛 ✅ |
| **Deduplicator 迁移范围** | 仅 Task 6 改 SQL 绑定，不迁移主事务（遵循 §11.3 治理决策）✅ |
| **测试先行** | Task 1/4/6 均为 TDD（先写失败测试 → 确认失败 → 实现 → 确认通过）✅ |
| **可回滚性** | 6 个独立 commit，P0 内部互不依赖结构变更；P1/P2 仅依赖 Task 1 的常量，回滚后新命令退化为 WARN 日志无副作用 ✅ |

---

---

## 评审记录（v1.0 → v1.1，2026-09-18）

实施前对照真实代码逐项核实引用点，发现并修复以下问题。评审方式=逐项 `rg` / 源文件实证核对（评审子代理因限流未能运行，改由逐项核实替代）。

| # | 级别 | 位置 | 问题 | 核实证据 | 处置 |
|---|---|---|---|---|---|
| 1 | **BLOCKING** | Task 2 | 处理器签名写成 `wxEvtCommand&` + `event.GetText().ToStdString()`，实际为 `StatusUpdateEvent&` + `getText()`（返回 `std::string`） | `MainFrame.cpp:1494` `void MainFrame::onStatusUpdate(StatusUpdateEvent& event)`；`:1495` `wxString text = event.getText();`；`Events.h:155` `std::string getText() const` | 已改为正确签名与取值方式 |
| 2 | **BLOCKING** | Task 6 | 测试调用 `Deduplicator(db).deduplicatePhase0(protectedSubId)` 并断言 bool，实际**无参数、返回 `int`**，subid 从 `config_.dedup_subids` 读取 | `src/update/Deduplicator.cpp:85` `int Deduplicator::deduplicatePhase0()`；体内 `std::string protectedSubId = config_.dedup_subids[0];` | 已改为经 config 注入单引号 subid 间接触发，断言 `EXPECT_GT(..., 0)` |
| 3 | **HIGH** | Task 2 | `proxyPanel_->GetCurrentSubId()` **不存在**，凭空捏造 | `rg GetCurrentSubId` 在 `ProxyListPanel.h`/`MainFrame.*` 无命中；真实来源 `MainFrame.cpp:1501` `subPanel_->getSelectedSubId()`；`ProxyListPanel.h:112` 仅有私有 `currentSubId_` | 已改为 `subPanel_->getSelectedSubId()`，并显式标注「不要新增访问器」 |
| 4 | **HIGH** | Task 6 | 只覆盖了 Phase0 的**第一条**拼接 SQL，漏掉 fallback 分支（含**两个**拼接点：`fallbackSubId` + `protectedSubId`） | `Deduplicator.cpp` 内 `config_.dedup_subids.size() > 1` 分支的 `"UPDATE ProfileItem SET SubId = '" + fallbackSubId + "' WHERE SubId = '" + protectedSubId + "'"` | 已补第二条 SQL 的参数绑定代码 + 独立回归用例 `Phase0FallbackHandlesSingleQuote` |
| 5 | **MEDIUM** | Task 3 Step 7 | 原写「若未绑定则补上」，实际**已绑定**，会诱导实施者重复绑定（`Bind` 重复注册会双触发刷新） | `MainFrame.cpp:322` 已 `Bind(wxEVT_SUBSCRIPTION_REFRESH, ...)` | 已改为「已核实，无需改动，仅验证」 |
| 6 | **LOW** | Task 2 | 丢失既有 `subPanel_` / `proxyPanel_` 空指针守卫 | `MainFrame.cpp:1497/1500/1518` 均有守卫 | 已恢复全部守卫 |
| 7 | **LOW** | Task 6 | 原错误分支计划返回 `false`，但该函数返回 `int`；且既有错误路径误用 `COMMIT` | 函数返回 `int`；`docs/plans/2026-05-13-P4-...` 记录「prepare 失败路径用 COMMIT 而非 ROLLBACK」语义 bug | 已改为 `return 0` / `return updated` + `ROLLBACK` |
| 8 | **LOW（记录，不改）** | 范围外 | `DatabaseHelper.h` 的 `db::Database::open()`（`:26-37`）同样未应用 PRAGMA | 已确认存在 | 不纳入本期。沿用 `docs/plans/2026-05-13-P4-...` 决策「DatabaseHelper.h 留作死代码清理的后续计划」 |

### 评审确认无误的引用点

| 引用点 | 核实结果 |
|---|---|
| `StatusUpdateEvent(int field = 0, const std::string& text = "")` | ✅ `Events.h:149`，计划的 `std::string(cmd)` 显式转换正确 |
| `SubscriptionRefreshEvent` 类 + 事件 + 绑定 | ✅ `Events.h:182`、`Events.cpp:17`、`MainFrame.cpp:322` |
| `db` 命名空间包裹方式 | ✅ `DatabaseHelper.h` 用 `#ifndef DB_HELPER_H` 宏卫 + `namespace db { class Database {...} }`，`Transaction` 应置于同一 `namespace db` 块内 |
| `test_config_reader` CMake 写法 | ✅ `CMakeLists.txt:657/663`，`PRIVATE gtest_main gtest ...`；计划片段结构同构 |
| `DatabaseConnectionService::applyPragmas` 存在且被 `switchDatabase:169` 调用 | ✅ 已在规格 §11.1 核实 |
| `RegionBatchResolver::flushRegionBuffer` 事务位置 | ⚠️ **未能核实具体行号**（`:362` 为规格阶段的估算），Task 5 Step 1 已要求实施时先 `rg -n "BEGIN TRANSACTION|COMMIT|ROLLBACK"` 定位 |

### 结论

**APPROVE WITH CHANGES** —— 上表 #1–#7 已全部就地修复。批准前必须修的最小集合 = #1、#2（阻断级，否则 Task 2/Task 6 直接编译失败或测试无法运行）。

---

---

## 第二轮独立评审（v1.1 → v1.2，2026-09-18）

由独立评审代理执行（规格 631 行 + 计划 1047 行全文读取 + 逐项源码实证）。**结论：APPROVE WITH CHANGES**——架构方向正确（写入方声明、UI 方执行；命令常量集中化；嵌套事务感知；§11 三次自我修正体现了对既有资产的尊重），但发现 4 项必修 + 若干会致用户可见回归的建议项。**以下 7 项已就地修复**，2 项移出 P0。

| # | 级别 | 位置 | 问题 | 证据 | 处置 |
|---|---|---|---|---|---|
| 1 | **BLOCKING** | Task 3 Step 6 | `importSubscription` 内 `wxGetTopLevelParent(this)` **无法编译**——`class AppController`（`AppController.h:87`）是普通类、不继承 `wxWindow` | `rg` 确认 AppController 无 `wxWindow` 基类 | 已改为给 `importSubscription` 增补 `wxEvtHandler*` 参数，由 2 个调用点（`MainFrame.cpp:1056`、`SubscriptionPanel.cpp:511`）传入 |
| 2 | **BLOCKING** | Task 2 | 状态栏中文文案被后续加载事件**无条件覆盖**：`ProxyListLoadedEvent` handler（`MainFrame.cpp:335`）写 `"Loaded subscription: ..."`；`SubscriptionRefreshEvent` handler（`:326`）写 `"Refresh: loaded all proxies"` → R1a/V13 不成立 | 两处 `setStatusText` 无守卫 | 已新增 Step 2b：`statusTextProtected_`（atomic）+ `statusTextPending_` + `rewriteRefreshStatusText()`，加载完成后重写一次命令文案 |
| 3 | **HIGH** | Task 5 | 原片段写成「循环不变 + `txn.commit()`」，丢掉 `commitOk` 后中途 `sqlite3_step` 失败会 `break` 再 `commit()` → **提交半批 Region**，违反 R2 | 现有代码 `RegionBatchResolver.cpp:390-414` 有 `commitOk` 标志 + 失败 ROLLBACK，是原子的；且原注释「析构自动 ROLLBACK」本身错误（`commit()` 成功置位 `finished_` 后析构不再回滚） | 已改为保留 `commitOk`，step 失败走 `txn.rollback()`；Task 5 **整体验降级为 P2** |
| 4 | **HIGH** | Task 6 | **漏第三处 SQL 拼接** `Deduplicator.cpp:224` 的 `config_.blacklist_subid`；且原验证命令 `rg -n "protectedSubId.*'"` **结构上不可能命中它**（变量名不同）→ 假绿灯 | `Deduplicator.cpp:224` 与日志行 `:236` | 已新增 Step 4b（参数化 + 回归用例）；验证命令改为 `rg -n "UPDATE ProfileItem SET SubId"`（预期仅 3 处参数化） |
| 5 | **HIGH** | Task 3 Step 4/5/6 | G3/G4/G6 发 `SubscriptionRefreshEvent` 会**重置用户订阅视图为「全部」**（`MainFrame.cpp:324` 传空 subId → `ProxyListPanel.cpp:177/183/290-291` `proxies_ = allProxies_`）并造成**双重全表读** | 已核实视图重置链路 | 已删除全部 `SubscriptionRefreshEvent` 发射，G3/G4/G6 只发 `kRowsChanged` |
| 6 | **BLOCKING** | 规格 §1.3 / 缺口表 | 规格称 `-S / -sync`「仍为 TODO」**为假**——CLI 已实现（`main_cli.cpp:674-704`），GUI `doSyncDatabases`（`AppController.cpp:2506-2524`）也存在且只发状态消息 → **零刷新**；且 `syncDatabases` 走独立连接写目标库，`db_` 缓存看不到 | AutoTask 可达此路径（`main_cli.cpp:801` steps 含 `sync`） | 已新增 Task 3 Step 6b（G9 补 `kRowsChanged`）；规格 §1.3 该条待删（真正 TODO 的仅菜单壳 `onMenuSyncDb:1061-1072`） |
| 7 | **LOW** | Task 2 第 2 步 | `[数据变更]` REPORT 未覆盖 `REGION_RESOLVE_DONE` → 违背 R1b，V14 在地区解析场景失败 | `RegionBatchResolver.cpp:374` 写 `ProfileItem.Region` | 已把 `kRegionResolveDone` 加入日志条件 |

### 第二轮确认无需改动的判定

| 判定 | 结论 | 证据 |
|---|---|---|
| 分享链接导出 | ✅ 不需要刷新 | `ShareLinkExportService.cpp:23-59` 仅 `SELECT` + `ofstream`，规格 §4.4「EXPORT 不刷新」正确 |
| 独立代理启停 → `updateStartupTime` | ✅ 已覆盖 | `AppController.cpp:1162/1766/2009` 写后发 `StandaloneProxyEvent` → `MainFrame.cpp:230-238` `refreshResults()` |
| 在线代理测试 | ✅ 已覆盖 | `AppController.cpp:2282-2341` → `TestOnlineProxiesEvent` → `ProxyListPanel.cpp:542-544` |
| G1/G2 不会新增 UI 卡顿 | ✅ | `doTestSubscription`/`doTestAllProxies` 完成时既有 `MainFrame.cpp:213` 已在 UI 线程 `refreshResults()`；计划追加的是**异步** `kRowsChanged`，方向正确 |
| `statusTextFor()` 集中映射 | ✅ 不过度 | 全仓 43 处 `StatusUpdateEvent`，集中映射是收敛方向；纯函数可单测 |
| `kStatusResolved` | ⚠️ 被使用但未落地 | `MainFrame.cpp:178` `payload.StartsWith("RESOLVE_REGION_START")`、`AppController.cpp:2609/2616` 发射——**Task 2 未把 `:178` 字面量替换为常量**，已记为后续项 |
| `DATA_` 前缀早警 | ✅ 当前可靠 | 已枚举全部 43+ 载荷（英文句子 / `ERR:` / `CANCELLED` / `NOTFOUND` / `FOUND:` / `REJECT:` / `RESOLVE_REGION_START` / `DEDUP_*` / `REGION_RESOLVE_DONE` / 中文消息），**无一项以 `DATA_` 开头**，中文在字节层也不可能；需在 `RefreshCommands.h` 注释固化该约定 |
| `Transaction` 外层回滚一致性 | ✅ 正确 | autocommit==0 时守卫不 BEGIN/不提交/不回滚，内层写入与外层同事务；`ParticipatesInOuterTransaction` 用例断言的正是此点 |
| P0 依赖顺序 | ✅ 无断裂 | Task 3 与 Task 2 必须同 commit（否则 `DATA_ROWS_CHANGED` 会经 `else` 分支作为字面量显示在状态栏）；Task 6 独立；Task 7 收尾 |

### 第二轮建议项（不阻断 P0，记录待办）

1. **§4.8.4 时延表**：`DATA_RESULTS_CHANGED` → `refreshResults()` 是**同步 UI 线程**双全表读（`ProxyListPanel.cpp:194/201`），不应并入「异步 100ms–1s」。P1 的 `TEST_ALL → kResultsChanged` 映射应改走 `refreshResultsFor(indexIds)` 或既有 `RunningDurationsLoadedEvent` 异步通道（`ProxyListPanel.cpp:350`），否则与 `2026-06-11-RefreshFreeze` 的触发条件重合。
2. **规格 §8 风险行失效**：「`refreshResults()` 由 G3/G4 触发后变为高频路径」——G3/G4 从不发 `kResultsChanged`，该行与 §11.4 自相矛盾，应删除。
3. **§4.4 静态局部变量方案不可实现**：progressCallback 是 lambda（`AppController.cpp:2801-2809`），其内部 `static` 作用域仅限 lambda 体，`doAutoTaskImpl`（`:2794-2824`）**词法上访问不到** → 应提升为 `doAutoTaskImpl` 局部变量并按引用捕获。
4. **`commit()` 返回值契约歧义**：「参与外层」与「COMMIT 失败」都返回 `false`。建议改返回 `enum class TxnOutcome { Committed, Participated, Failed }`，或参与模式返回 `true`。当前仅 Task 5 一个迁移点且不检查返回值，风险潜伏；另 `commit()` 失败分支应置 `finished_ = true` 避免析构二次 ROLLBACK。
5. **§2.2 G3/G4 描述夸大**：`MainFrame.cpp:183-189` 已对 `"Update completed:"` / `"All subscriptions updated"` 执行 `loadSubscriptionsAsync`，**订阅面板本来就在刷新**，缺的只有代理面板。
6. **定位修正**：`importSubscription` 实际在 `AppController.cpp:277-285`（非 `:2100`）；`doTestAllProxies` 在 `:2191`（非 `:2206`）；真正的 `doTestOnlineProxies`（`:2263-2341`）已走 `TestOnlineProxiesEvent`，**不需要补命令**。
7. **`syncDatabases` 是第三处裸 `BEGIN`**（`SubitemUpdaterV2.cpp:1165`），§11.3 收窄范围后未登记于 §2.3 现状表。
8. **`kDedupOk` 映射文案三重复述**：先写中文 → 弹模态框 → 再被 `"Dedup completed."` 覆盖。建议 P0 期间 `kDedupOk` 映射留空，P1 去模态框时再启用。

### 最终结论

**APPROVE WITH CHANGES**（两轮评审合计 15 项发现，4 项阻断/高危已全部修复，2 项建议移出 P0）。

**P0 最终范围：Task 1 / 2（含 Step 2b）/ 3（含 Step 6b，G1–G4/G6/G9）/ 6（含 Step 4b）/ 7。**
**P2：Task 4 + Task 5**（事务 RAII 与其唯一迁移点，独立提交 + 独立回归）。

---

*计划创建: 2026-09-18 (v1.2，含两轮评审修正) | 依据: docs/specs/2026-09-18-Spec-DataReadWriteRefresh-v1.0.md (v1.1) | 分支: Optimize-data-read-write-refresh*
