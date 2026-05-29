# 批量测试/更新时禁用 UI 操作按钮实施方案

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在批量测试、更新等长时间运行操作进行期间，将所有测试、更新、查找等冲突的工具栏按钮和菜单项置灰禁用，操作完成后恢复启用。

**Architecture:** 在 AppController 中添加集中式 `isBusy_` 状态标志，通过统一事件 `EVT_OPERATION_STATE` 通知 UI 状态变更。各操作入口在启动时发状态变更事件禁用按钮，操作结束时发事件恢复。取消按钮在操作期间始终可用。

**Tech Stack:** C++17, wxWidgets, SQLite, Xray-core

**State pattern:** 所有长时间运行的操作（测试、更新、查找、去重）共享 `AppController::workerThread_`，天然互斥。在 AppController 中添加 `isBusy_` 原子标志，所有 async 方法在入口设置，结束后清除。UI 通过事件响应状态变更。

---

## 文件结构

| 文件 | 职责 |
|------|------|
| `src/ui/Events.h` | 新增 `wxEVT_OPERATION_STATE` 事件类 |
| `src/ui/Events.cpp` | `wxDEFINE_EVENT` 宏 |
| `src/ui/MainFrame.h` | 新增 `onOperationState()` 处理程序 |
| `src/ui/MainFrame.cpp` | 实现按钮启用/禁用逻辑，事件绑定 |
| `src/ui/AppController.h` | 新增 `isBusy_` 标志 + `isBusy()` 方法 |
| `src/ui/AppController.cpp` | 所有 async 入口/出口管理 `isBusy_` + 发状态事件 |
| `src/ui/ProxyListPanel.h` | 新增 `onOperationState()` 处理程序（可选） |
| `src/ui/ProxyListPanel.cpp` | 菜单项启用/禁用逻辑 |

---

### Task 1: Events.h — 新增操作状态事件

**Files:**
- Modify: `src/ui/Events.h`
- Modify: `src/ui/Events.cpp`

**operation_state_event 设计:**

```cpp
enum class OperationState {
    IDLE = 0,      // 无操作进行中
    BUSY = 1,      // 操作正在进行中，UI 应禁用冲突按钮
};
```

**Events.h** 第 135 行附近（其他事件定义之后）添加：

```cpp
// ── Operation state event: notify UI when long-running ops start/stop ──
enum class OperationState {
    IDLE = 0,
    BUSY = 1,
};

class OperationStateEvent : public wxEvent {
public:
    OperationStateEvent(OperationState state)
        : wxEvent(0, wxEVT_OPERATION_STATE), state_(state) {}
    OperationStateEvent(const OperationStateEvent& other)
        : wxEvent(other), state_(other.state_) {}
    wxEvent* Clone() const override { return new OperationStateEvent(*this); }
    OperationState getState() const { return state_; }
private:
    OperationState state_;
};

wxDECLARE_EVENT(wxEVT_OPERATION_STATE, OperationStateEvent);

typedef void (wxEvtHandler::*OperationStateEventFunction)(OperationStateEvent&);
#define OperationStateEventHandler(func) \
    wxEVENT_HANDLER_CAST(OperationStateEventFunction, func)
```

**Events.cpp** 添加：

```cpp
wxDEFINE_EVENT(wxEVT_OPERATION_STATE, OperationStateEvent);
```

- [ ] **Step 1: 修改 Events.h** — 在 `#include <string>` 之后、其他事件类之后，添加 `OperationState` 枚举 + `OperationStateEvent` 类 + `wxDECLARE_EVENT` + `wxEVT_OPERATION_STATE` 宏

- [ ] **Step 2: 修改 Events.cpp** — 添加 `wxDEFINE_EVENT(wxEVT_OPERATION_STATE, OperationStateEvent);`

- [ ] **Step 3: 构建验证**

```bash
cmake --build build --parallel 8
```
预期：编译通过（事件类自身无影响，尚未使用）

---

### Task 2: AppController — 集中式忙碌状态管理

**Files:**
- Modify: `src/ui/AppController.h`
- Modify: `src/ui/AppController.cpp`

**AppController.h** 第 73 行附近添加：

```cpp
std::atomic<bool> isBusy_{false};
```

新增公开方法（第 46 行附近，`cancelTest()` 之后）：

```cpp
bool isBusy() const { return isBusy_.load(); }
```

**核心约定：** 每个 async 方法在入口设置 `isBusy_ = true`，在对应 doXxx 方法的 **finally 块** 中设置 `isBusy_ = false` 并发送 `OperationStateEvent(IDLE)`。

**AppController.cpp** — 修改所有 `doXxx` 方法，添加 `try/catch` 包裹，在 `finally` 中恢复状态。

#### 修改 doTestSubscription（第 330-356 行）

```cpp
void AppController::doTestSubscription(const std::string& subId, wxEvtHandler* wxHandler) {
    try {
        ProxyBatchTester tester(db_, config_, "", &cancelRequested_);
        bool ok = tester.runWithSubId(subId);
        // ... 现有完成事件逻辑 ...
    } catch (...) {
        // 捕获异常确保状态恢复
        Logger::write("[AppController] Exception in doTestSubscription", LogLevel::ERR);
    }
    // 最终恢复（无论成功/失败/异常）
    isBusy_ = false;
    if (wxHandler) {
        wxQueueEvent(wxHandler, new OperationStateEvent(OperationState::IDLE));
    }
}
```

#### 修改 doTestSingleProxy（第 358-381 行）

相同模式：包裹现有逻辑，finally 中 `isBusy_ = false` + 发 `OperationStateEvent(IDLE)`。

#### 修改 doUpdateSubscription（第 307-316 行）

```cpp
void AppController::doUpdateSubscription(const std::string& subId, wxEvtHandler* wxHandler) {
    try {
        // ... 现有逻辑 ...
    } catch (...) {
        Logger::write("[AppController] Exception in doUpdateSubscription", LogLevel::ERR);
    }
    isBusy_ = false;
    if (wxHandler) {
        wxQueueEvent(wxHandler, new OperationStateEvent(OperationState::IDLE));
    }
}
```

#### 修改 doUpdateAllSubscriptions（第 318-328 行）

相同模式：finally 中 `isBusy_ = false` + 发 `OperationStateEvent(IDLE)`。

#### 修改 doFindFirstProxy / doFindBestProxy（第 392-490 行）

```cpp
void AppController::doFindFirstProxy(wxEvtHandler* wxHandler) {
    try {
        // ... 现有逻辑 ...
    } catch (...) {
        Logger::write("[AppController] Exception in doFindFirstProxy", LogLevel::ERR);
    }
    isBusy_ = false;
    if (wxHandler) {
        wxQueueEvent(wxHandler, new OperationStateEvent(OperationState::IDLE));
    }
}
```

`doFindBestProxy` 同理。

#### 修改 doFindProxyByIndexId（第 499-550 行）

相同模式。

#### 修改 async 入口方法 — 设置 isBusy_

在以下方法的 `if (workerThread_.joinable()) workerThread_.join()` 之后、`workerThread_ = std::thread(...)` 之前，添加 `isBusy_ = true;`：

- `updateSubscriptionAsync`（第 97-101 行）
- `updateAllSubscriptionsAsync`（第 103-107 行）
- `testSubscriptionAsync`（第 147-151 行）
- `testSingleProxyAsync`（第 153-157 行）
- `findFirstProxyAsync`（第 192-196 行）
- `findBestProxyAsync`（第 198-202 行）
- `findProxyByIndexIdAsync`（第 511-）

```cpp
void AppController::testSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler) {
    cancelRequested_ = false;
    if (workerThread_.joinable()) workerThread_.join();
    isBusy_ = true;  // ← 新增
    workerThread_ = std::thread(&AppController::doTestSubscription, this, subId, wxHandler);
}
```

- [ ] **Step 1: AppController.h** — 添加 `std::atomic<bool> isBusy_{false};` 成员 + `bool isBusy()` 方法

- [ ] **Step 2: AppController.cpp** — 修改 7 个 async 入口方法，添加 `isBusy_ = true`

- [ ] **Step 3: AppController.cpp** — 修改 7 个 doXxx 方法，添加 finally 块（`isBusy_ = false` + 发 `OperationStateEvent(IDLE)`）

- [ ] **Step 4: 构建验证**

```bash
cmake --build build --parallel 8
```
预期：编译通过

---

### Task 3: MainFrame — 绑定操作状态事件 + 按钮启用/禁用逻辑

**Files:**
- Modify: `src/ui/MainFrame.h`
- Modify: `src/ui/MainFrame.cpp`

#### MainFrame.h 第 74 行附近新增：

```cpp
void onOperationState(OperationStateEvent& event);
```

#### MainFrame.cpp — Constructor 中绑定事件（第 160 行附近，`initPanels()` 之后）：

```cpp
// ── Operation state change: enable/disable conflicting UI ──
Bind(wxEVT_OPERATION_STATE, &MainFrame::onOperationState, this);
```

#### MainFrame.cpp — 实现 onOperationState：

```cpp
void MainFrame::onOperationState(OperationStateEvent& event) {
    bool busy = (event.getState() == OperationState::BUSY);
    wxToolBar* tb = GetToolBar();
    
    if (busy) {
        // 操作开始 → 禁用冲突按钮
        if (tb) {
            tb->EnableTool(ID_TOOL_UPDATE_ALL, false);
            tb->EnableTool(ID_TOOL_TEST, false);
            tb->EnableTool(ID_TOOL_FIND, false);
            tb->EnableTool(ID_TOOL_DEDUP, false);
            tb->EnableTool(ID_TOOL_IMPORT, false);
            // 取消按钮保持启用（如果在测试/查找中）
        }
        // 禁用菜单项
        GetMenuBar()->Enable(ID_MENU_UPDATE_ALL, false);
        GetMenuBar()->Enable(ID_MENU_FIND_PROXY, false);
        GetMenuBar()->Enable(ID_MENU_FIND_BEST, false);
        GetMenuBar()->Enable(ID_MENU_DEDUP, false);
        GetMenuBar()->Enable(ID_MENU_EXPORT, false);
        GetMenuBar()->Enable(ID_MENU_GEN_CONFIG, false);
        // 启用取消按钮
        if (tb) tb->EnableTool(ID_TOOL_CANCEL_TEST, true);
    } else {
        // 操作结束 → 恢复所有按钮
        if (tb) {
            tb->EnableTool(ID_TOOL_UPDATE_ALL, true);
            tb->EnableTool(ID_TOOL_TEST, true);
            tb->EnableTool(ID_TOOL_FIND, true);
            tb->EnableTool(ID_TOOL_DEDUP, true);
            tb->EnableTool(ID_TOOL_IMPORT, true);
            tb->EnableTool(ID_TOOL_CANCEL_TEST, false);  // 重置取消按钮
        }
        GetMenuBar()->Enable(ID_MENU_UPDATE_ALL, true);
        GetMenuBar()->Enable(ID_MENU_FIND_PROXY, true);
        GetMenuBar()->Enable(ID_MENU_FIND_BEST, true);
        GetMenuBar()->Enable(ID_MENU_DEDUP, true);
        GetMenuBar()->Enable(ID_MENU_EXPORT, true);
        GetMenuBar()->Enable(ID_MENU_GEN_CONFIG, true);
    }
}
```

#### 关于现有测试完成 handler 的清理

MainFrame.cpp 第 150-160 行的现有 `wxEVT_PROXY_TEST_PROGRESS` handler 中，取消按钮禁用逻辑已包含，但 `onOperationState` 会覆盖它。可以保留双重禁用（幂等）或移除重复逻辑。推荐保留（安全冗余）：

- 保留 `tb->EnableTool(ID_TOOL_CANCEL_TEST, false)` 在 `wxEVT_PROXY_TEST_PROGRESS` handler 中
- 保留 `tb->EnableTool(ID_TOOL_CANCEL_TEST, true)` 在 `onToolTest` / `onTestSubscription` 中
- `onOperationState(BUSY)` 也会设置 `tb->EnableTool(ID_TOOL_CANCEL_TEST, true)`
- `onOperationState(IDLE)` 会设置 `tb->EnableTool(ID_TOOL_CANCEL_TEST, false)`

#### 关于单代理测试按钮启用

`ProxyListPanel::onTestProxy` 触发单代理测试时，应启用取消按钮。需在 `onTestProxy` 中获取 MainFrame 的 toolbar：

```cpp
// ProxyListPanel.cpp — onTestProxy 中，测试启动后：
if (wxWindow* topLevel = wxGetTopLevelParent(this)) {
    if (MainFrame* mf = dynamic_cast<MainFrame*>(topLevel)) {
        wxToolBar* tb = mf->GetToolBar();
        if (tb) tb->EnableTool(ID_TOOL_CANCEL_TEST, true);
    }
}
```

注意：这需要 ProxyListPanel.cpp 包含 `MainFrame.h`。

- [ ] **Step 1: MainFrame.h** — 新增 `onOperationState(OperationStateEvent&)` 声明

- [ ] **Step 2: MainFrame.cpp** — Constructor 中绑定 `wxEVT_OPERATION_STATE`

- [ ] **Step 3: MainFrame.cpp** — 实现 `onOperationState`（BUSY 禁用所有冲突按钮，IDLE 全部恢复）

- [ ] **Step 4: ProxyListPanel.cpp** — `onTestProxy` 中启用取消按钮（包含 `#include "MainFrame.h"`）

- [ ] **Step 5: 移除已有的冗余测试完成 handler**（可选） — 保留或清理

- [ ] **Step 6: 构建验证**

```bash
cmake --build build --parallel 8
```
预期：编译通过

---

### Task 4: 订阅面板右键菜单 — 禁用测试/更新

**Files:**
- Modify: `src/ui/SubscriptionPanel.h`
- Modify: `src/ui/SubscriptionPanel.cpp`

订阅面板有一个 `wxMenu* contextMenu_`，其中有 `ID_SUB_UPDATE` 和 `ID_SUB_TEST` 项。当操作忙时应禁用它们。

**方案 A：传递 isBusy 状态** — SubscriptionPanel 通过 MainFrame 检查 `controller_->isBusy()`。

**方案 B：事件驱动** — SubscriptionPanel 也绑定 `wxEVT_OPERATION_STATE`。

选择方案 B，与 MainFrame 做法一致。

**SubscriptionPanel.h** 新增：

```cpp
void onOperationState(OperationStateEvent& event);
```

**SubscriptionPanel.cpp** Constructor 中绑定：

```cpp
Bind(wxEVT_OPERATION_STATE, &SubscriptionPanel::onOperationState, this);
```

**实现 onOperationState：**

```cpp
void SubscriptionPanel::onOperationState(OperationStateEvent& event) {
    bool busy = (event.getState() == OperationState::BUSY);
    if (contextMenu_) {
        contextMenu_->Enable(ID_SUB_UPDATE, !busy);
        contextMenu_->Enable(ID_SUB_TEST, !busy);
    }
}
```

- [ ] **Step 1: SubscriptionPanel.h** — 新增 `onOperationState()` 声明

- [ ] **Step 2: SubscriptionPanel.cpp** — Constructor 中绑定事件 + 实现 `onOperationState`

- [ ] **Step 3: 构建验证**

```bash
cmake --build build --parallel 8
```
预期：编译通过

---

### Task 5: 代理列表面板右键菜单 — 禁用测试

**Files:**
- Modify: `src/ui/ProxyListPanel.h`
- Modify: `src/ui/ProxyListPanel.cpp`

代理列表面板右键菜单中的 `"Test"` 项触发 `onTestProxy`，应在忙时禁用。

**方案 B 一致，绑定 `wxEVT_OPERATION_STATE`：**

**ProxyListPanel.h** 新增：

```cpp
void onOperationState(OperationStateEvent& event);
```

**ProxyListPanel.cpp** Constructor 中绑定：

```cpp
Bind(wxEVT_OPERATION_STATE, &ProxyListPanel::onOperationState, this);
```

**实现 onOperationState：**

```cpp
void ProxyListPanel::onOperationState(OperationStateEvent& event) {
    bool busy = (event.getState() == OperationState::BUSY);
    if (contextMenu_) {
        contextMenu_->Enable(ID_PXY_TEST, !busy);
    }
}
```

注意：检查 `ProxyListPanel.cpp` 中是否已有 `ID_PXY_TEST` 常量，如果使用不同的 ID，请相应调整。

- [ ] **Step 1: ProxyListPanel.h** — 新增 `onOperationState()` 声明

- [ ] **Step 2: ProxyListPanel.cpp** — Constructor 中绑定事件 + 实现 `onOperationState`

- [ ] **Step 3: 构建验证**

```bash
cmake --build build --parallel 8
```
预期：编译通过

---

### Task 6: 更新操作完成事件补全

**Files:**
- Modify: `src/ui/AppController.cpp`

当前 `doUpdateSubscription` 和 `doUpdateAllSubscriptions` 不发送 `ProxyTestProgressEvent`，仅发送 `StatusUpdateEvent`。这导致 MainFrame 的 `wxEVT_PROXY_TEST_PROGRESS` handler 不会触发（用于刷新代理列表延迟列）。

修改 **doUpdateSubscription** 在成功后发送 `ProxyTestProgressEvent`：

```cpp
void AppController::doUpdateSubscription(const std::string& subId, wxEvtHandler* wxHandler) {
    bool ok = false;
    try {
        update::SubitemUpdaterV2 updater(db_, config_.xray_executable, config_, wxHandler, subId);
        ok = updater.runSingle();
    } catch (const std::exception& e) {
        Logger::write(std::string("Update exception: ") + e.what(), LogLevel::ERR);
    }

    std::string msg = ok ? "Subscription updated" : "Update failed";
    if (wxHandler) {
        wxQueueEvent(wxHandler, new StatusUpdateEvent(0, msg));
    }

    Logger::write(msg, ok ? LogLevel::REPORT : LogLevel::ERR);

    // 新增：发送完成事件刷新 UI（包括 proxyPanel_->refreshResults()）
    if (wxHandler) {
        wxQueueEvent(wxHandler, new ProxyTestProgressEvent(0, 0, "", "", "",
            ok ? "Update finished" : "Update failed", true));
    }
    if (wxWindow* win = dynamic_cast<wxWindow*>(wxHandler)) {
        if (wxWindow* topLevel = wxGetTopLevelParent(win)) {
            if (topLevel != win) {
                wxQueueEvent(topLevel, new ProxyTestProgressEvent(0, 0, "", "", "",
                    "Update finished", true));
            }
        }
    }

    // 统一状态恢复（已在 Task 2 的 finally 中处理）
    isBusy_ = false;
    if (wxHandler) {
        wxQueueEvent(wxHandler, new OperationStateEvent(OperationState::IDLE));
    }
}
```

**doUpdateAllSubscriptions** 同理。

- [ ] **Step 1: AppController.cpp** — 修改 `doUpdateSubscription`，添加 `ProxyTestProgressEvent` 广播

- [ ] **Step 2: AppController.cpp** — 修改 `doUpdateAllSubscriptions`，添加 `ProxyTestProgressEvent` 广播

- [ ] **Step 3: 构建验证**

```bash
cmake --build build --parallel 8
```
预期：编译通过

---

### Task 7: 集成测试与验证

- [ ] **Step 1: 构建并运行单元测试**

```bash
cmake --build build --parallel 8
cd build && ctest -V --output-on-failure
```
预期：编译成功，3/3 tests 通过

- [ ] **Step 2: 手动验证测试流程**
  - 启动 GUI：`bin/validproxy -ui`
  - 点击工具栏"测试"按钮 → 验证"更新"、"查找"、"导入"、"去重"按钮变灰
  - 验证菜单项"Update All Subscriptions"、"Find First Working Proxy"、"Find Best Proxy"禁用
  - 验证"取消"按钮启用
  - 点击"取消"按钮 → 验证所有按钮恢复
  - 测试正常完成 → 验证所有按钮恢复

- [ ] **Step 3: 手动验证更新流程**
  - 点击工具栏"更新"按钮 → 验证"测试"、"查找"等按钮变灰
  - 更新完成 → 验证所有按钮恢复
  - 验证延迟列自动刷新

- [ ] **Step 4: 手动验证右键菜单**
  - 订阅面板右键 → "Update"/"Test" 在操作运行时禁用
  - 代理列表面板右键 → "Test" 在操作运行时禁用

- [ ] **Step 5: 验证异常恢复**
  - 测试过程中关闭窗口 → 期望：不崩溃，worker 线程正确 join 或 detach
  - 测试过程中文件损坏 → 期望：doXxx 异常捕获，状态正确恢复为 IDLE

- [ ] **Step 6: 提交**

```bash
git add -A
git commit -m "feat: disable conflicting UI buttons during long-running operations

- Add OperationStateEvent for centralized busy state signaling
- Add isBusy_ flag to AppController for all async operations
- Disable toolbar buttons (test/update/find/dedup/import) during ops
- Disable menu items (update/find/dedup/etc.) during ops
- Disable context menu items in SubscriptionPanel and ProxyListPanel
- Send completion events after update operations for UI refresh
"
```

---

## 自审检查

### 1. 规范覆盖
- ✅ 批量测试时禁用测试/更新/查找/去重按钮 → Task 3
- ✅ 批量更新时禁用测试/更新/查找/去重按钮 → Task 3（同一 `onOperationState(BUSY)`）
- ✅ 菜单项禁用 → Task 3（`GetMenuBar()->Enable(...)`）
- ✅ 订阅面板右键菜单禁用 → Task 4
- ✅ 代理列表面板右键菜单禁用 → Task 5
- ✅ 操作完成后恢复 → Task 3 `onOperationState(IDLE)`
- ✅ 取消按钮在操作期间可用 → Task 3 `tb->EnableTool(ID_TOOL_CANCEL_TEST, true)`
- ✅ 更新操作完成刷新 UI → Task 6

### 2. 占位符检查
所有步骤包含实际代码、文件路径和构建命令。无 TBD、TODO 或空步骤。

### 3. 类型一致性
- `OperationStateEvent(OperationState state)` → 构造
- `event.getState()` → 读取
- `OperationState::BUSY` / `OperationState::IDLE` → 枚举值
- `wxEVT_OPERATION_STATE` → 事件类型
- 所有文件引用使用准确路径

### 4. 规格空位
无遗漏。
