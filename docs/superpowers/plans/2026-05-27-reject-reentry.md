# 操作再入保护实现方案

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 阻止用户在长时间运行操作（测试/更新/查找）进行中的时候再次触发新操作，消除 UI 冻结，并给出清晰提示。

**Architecture:** 所有 7 个异步操作共享 `AppController::workerThread_`。将当前阻塞 UI 的 `workerThread_.join()` 替换为提前返回 + 事件通知，通过 `StatusUpdateEvent("REJECT:...")` 触发 MainFrame 弹出消息框。

**Tech Stack:** C++17, wxWidgets

---

## 文件变更

| 文件 | 变更 |
|------|------|
| `src/ui/AppController.cpp` | 7 个 async 方法：`join()` → 检查 `joinable()` + 发 REJECT 事件 + return |
| `src/ui/MainFrame.cpp` | StatusUpdateEvent handler 添加 `"REJECT:"` 前缀处理 → 弹出 wxMessageBox |

### Task 1: AppController.cpp — 统一处理再入保护

**修改文件：** `src/ui/AppController.cpp`

**当前模式（7 处，第 97-201 行 + 第 510-513 行）：**

```cpp
void AppController::testSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler) {
    cancelRequested_ = false;
    if (workerThread_.joinable()) workerThread_.join();   // ← BLOCKS UI
    workerThread_ = std::thread(&AppController::doTestSubscription, this, subId, wxHandler);
}
```

**修改后：**

```cpp
void AppController::testSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler) {
    if (workerThread_.joinable()) {
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0,
                "REJECT:Another operation is already in progress. Please wait or cancel it first."));
        }
        return;
    }
    cancelRequested_ = false;
    workerThread_ = std::thread(&AppController::doTestSubscription, this, subId, wxHandler);
}
```

- [ ] **Step 1: testSubscriptionAsync** — 第 147-151 行

- [ ] **Step 2: testSingleProxyAsync** — 第 153-157 行

- [ ] **Step 3: updateSubscriptionAsync** — 第 97-101 行

- [ ] **Step 4: updateAllSubscriptionsAsync** — 第 103-107 行

- [ ] **Step 5: findFirstProxyAsync** — 第 192-196 行

- [ ] **Step 6: findBestProxyAsync** — 第 198-202 行

- [ ] **Step 7: findProxyByIndexIdAsync** — 第 510-514 行（注意 lambda 形式）

**findProxyByIndexIdAsync 特殊处理**（第 510-514 行，lambda 形式）：

```cpp
// 修改前
void AppController::findProxyByIndexIdAsync(const std::string& indexId, wxEvtHandler* wxHandler) {
    cancelRequested_ = false;
    if (workerThread_.joinable()) workerThread_.join();
    workerThread_ = std::thread([this, indexId, wxHandler]() {
        // ...
    });
}

// 修改后
void AppController::findProxyByIndexIdAsync(const std::string& indexId, wxEvtHandler* wxHandler) {
    if (workerThread_.joinable()) {
        if (wxHandler) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0,
                "REJECT:Another operation is already in progress. Please wait or cancel it first."));
        }
        return;
    }
    cancelRequested_ = false;
    workerThread_ = std::thread([this, indexId, wxHandler]() {
        // ...
    });
}
```

- [ ] **Step 8: 构建验证**

```bash
cmake --build build --parallel 8
```

### Task 2: MainFrame.cpp — 显示再入拒入消息框

**修改文件：** `src/ui/MainFrame.cpp`

**当前 handler（第 127-148 行）** 处理 "FOUND:"、"NOTFOUND"、"ERR:" 三种前缀。添加 "REJECT:" 处理：

```cpp
Bind(wxEVT_STATUS_UPDATE, [this](StatusUpdateEvent& evt) {
    wxString payload = evt.getText();
    if (payload.StartsWith("FOUND:")) {
        // ... existing ...
    } else if (payload == "NOTFOUND") {
        // ... existing ...
    } else if (payload.StartsWith("ERR:")) {
        // ... existing ...
    } else if (payload.StartsWith("REJECT:")) {             // ← 新增
        wxMessageBox(payload.Mid(7), "Operation Busy",
                     wxOK | wxICON_INFORMATION, this);
    } else {
        onStatusUpdate(evt);
    }
});
```

- [ ] **Step 1: MainFrame.cpp** — 添加 `"REJECT:"` 分支

- [ ] **Step 2: 构建验证**

```bash
cmake --build build --parallel 8
```

### Task 3: 测试

- [ ] **Step 1: 运行单元测试**

```bash
cd build && ctest -V --output-on-failure
```

- [ ] **Step 2: 手动验证**
  - 启动 GUI：`bin/validproxy -ui`
  - 点击"测试" → 立即再点"测试" → 弹出 "Operation Busy" 消息框，不冻结
  - 测试中点击"更新" → 弹出消息框
  - 测试中点击"查找" → 弹出消息框
  - 测试正常完成 → 可再次操作
  - 测试中点"取消" → 测试停止 → 可再次操作

---

## 效果

| 场景 | 修改前 | 修改后 |
|------|--------|--------|
| 测试中再点测试 | UI 冻结 → 等待 → 又启动测试 | 弹出提示框，不启动 |
| 测试中点更新 | UI 冻结 → 等待 → 启动更新 | 弹出提示框，不启动 |
| 更新中点测试 | UI 冻结 → 等待 → 启动测试 | 弹出提示框，不启动 |
| 更新中再点更新 | UI 冻结 → 等待 → 重复更新 | 弹出提示框，不启动 |
| 测试中点取消→再点测试 | 取消后 UI 恢复，可正常测试 | 取消后 UI 恢复，可正常测试 |

---

## Bug Fix: 取消/完成后操作被永久拦截

**Bug ID:** `reentry-permanent-block`

**发现日期:** 2026-05-27

**现象:** 用户点击"取消"按钮中断测试后，所有后续再入操作被永久拦截，即使 `joinable()` 为 false 也无法恢复。正常完成的测试也会导致下一次操作失败。

### 根因

`std::thread::joinable()` 在线程函数返回后仍返回 `true`，只有调用 `join()` 或 `detach()` 才能消费线程状态。

原始设计（如 `testSubscriptionAsync` 原始版本／无 `isRunning_` 时）：
```cpp
void AppController::testSubscriptionAsync(...) {
    cancelRequested_ = false;
    if (workerThread_.joinable()) workerThread_.join();  // join() 消费线程状态
    workerThread_ = std::thread(...);
}
```

**问题流（取消后）：**
1. `cancelTest()` → `cancelRequested_ = true`
2. doTestSubscription 退出（不 join）
3. `workerThread_` 线程终止但未被 join → `joinable() == true`
4. 下次调用 testSubscriptionAsync：
   - `cancelRequested_ = false` （重置）
   - `if (workerThread_.joinable())` ✅ true
   - `workerThread_.join()` ← 正常消费，应该 OK

**问题流（正常完成）：**
1. doTestSubscription 返回 → 线程终止
2. - 没有 join() 调用 —
3. 下次操作检查 `joinable()` → true
4. 拒绝：显示 "操作正在进行中"

实际上在第一次提交的 reentry 保护中（无 `isRunning_`），所有 7 个 async 方法改为：
```cpp
if (workerThread_.joinable()) {
    // 发出 REJECT 事件并 return，不再 join()
    return;
}
```

这就制造了死锁：`joinable()` 在未 join 的已完成线程上永远为 `true`。

### 修复方案

添加 `std::atomic<bool> isRunning_{false}` 标志来区分"线程仍在运行"和"线程已完成但未 join"：

**`AppController.h` — 新增字段：**
```cpp
std::atomic<bool> cancelRequested_{false};
std::atomic<bool> isRunning_{false};          // ← 新增
std::thread workerThread_;
```

**Async 入口模式（所有 7 个 async 方法）：**
```cpp
void AppController::testSubscriptionAsync(const std::string& subId, wxEvtHandler* wxHandler) {
    if (workerThread_.joinable()) {
        if (isRunning_) {                      // 真正的运行中
            // 拒绝
            return;
        }
        workerThread_.join();                  // 已完成但未 join → 清理
    }
    cancelRequested_ = false;
    isRunning_ = true;                         // 标记运行开始
    workerThread_ = std::thread(&AppController::doTestSubscription, this, subId, wxHandler);
}
```

**Worker 方法末尾（所有 7 个 doXxx／lambda worker）：**
```cpp
void AppController::doTestSubscription(const std::string& subId, wxEvtHandler* wxHandler) {
    // ... 原有逻辑 ...
    isRunning_ = false;  // ← 标记运行结束
}
```

对于含有多个提前返回路径的 worker（`doFindFirstProxy`、`doFindBestProxy`），在每个 `return` 前加 `isRunning_ = false`，或在函数末尾正常退出时设置。

### 受影响文件

| 文件 | 变更 |
|------|------|
| `src/ui/AppController.h` | 添加 `std::atomic<bool> isRunning_{false}` 字段 |
| `src/ui/AppController.cpp` | 7 个 async 入口添加 `isRunning_` 组合判断；6 个 doXxx 方法末尾设 `isRunning_ = false`；`findProxyByIndexIdAsync` lambda 末尾设 `isRunning_ = false` |

### 验证

- [x] 构建通过
- [x] 单元测试 3/3 通过
- [ ] 手动验证：取消后能再次操作、正常完成后能再次操作
