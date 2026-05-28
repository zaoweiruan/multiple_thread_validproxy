# 更新订阅不可再入与取消功能 — 实施计划

> **For agentic workers:** Use subagent-driven-development to implement plan task-by-task.

**目标:** 为订阅更新添加协作式取消功能，复用现有取消按钮，使更新过程不可再入且可被用户中断。

**架构:** 将 `cancelRequested_` 标志从 AppController 传递到 SubitemUpdaterV2，在关键检查点定期检测。复用现有取消工具栏按钮，根据当前操作动态切换文字（"取消测试"/"停止更新"）。为所有 `doXxx` 方法添加 RAII ResetGuard 确保异常安全。

**Tech Stack:** C++17, wxWidgets 3.2, SQLite3, std::atomic, std::thread

---

### Task 1: SubitemUpdaterV2 — 添加取消标志和构造参数

**Files:**
- Modify: `include/SubitemUpdaterV2.h:99-109`
- Modify: `src/SubitemUpdaterV2.cpp:154-161`

- [ ] **Step 1: 在 SubitemUpdaterV2.h 中添加 externalCancel_ 成员和 isCancelled() 方法**

将 `<atomic>` 加入包含列表，然后在 `private` 成员区域 `xrayJob_` 之后添加：

```cpp
// include/SubitemUpdaterV2.h 改动
#include <atomic>  // 在文件顶部已有 includes 中添加

// 在 private 成员末尾 xrayJob_ 之后添加：
std::atomic<bool>* externalCancel_{nullptr};

// 在 public: 或 private: 中添加方法：
bool isCancelled() const {
    return externalCancel_ && externalCancel_->load();
}
```

在类定义末尾 `xrayJob_` 后插入 `externalCancel_`。

- [ ] **Step 2: 修改构造函数签名，添加 externalCancel 参数**

```cpp
// SubitemUpdaterV2.h 构造函数声明
SubitemUpdaterV2(sqlite3* db,
                const std::string& xrayPath,
                const config::AppConfig& config,
                std::ofstream* logOut = nullptr,
                const std::string& baseDir = "",
                std::atomic<bool>* externalCancel = nullptr);
```

- [ ] **Step 3: 更新 SubitemUpdaterV2.cpp 构造函数实现**

```cpp
// src/SubitemUpdaterV2.cpp 构造函数 (第 154-161 行)
SubitemUpdaterV2::SubitemUpdaterV2(sqlite3* db,
                                    const std::string& xrayPath,
                                    const config::AppConfig& config,
                                    std::ofstream* logOut,
                                    const std::string& baseDir,
                                    std::atomic<bool>* externalCancel)
    : db_(db), xrayPath_(xrayPath), config_(config), logOut_(logOut), baseDir_(baseDir),
      xrayMgr_(nullptr), proxyFinder_(nullptr), xrayProcessId_(0), xrayJob_(nullptr),
      externalCancel_(externalCancel) {
}
```

- [ ] **Step 4: Build 验证编译通过**

```bash
Remove-Item "E:\eclipse_workspace\multiple_thread_validproxy\build\CMakeFiles\validproxy.dir\src\SubitemUpdaterV2.cpp.obj" -Force
cd E:\eclipse_workspace\multiple_thread_validproxy; cmake --build build --parallel 8
```
Expected: Build succeeds (may fail if other files haven't been updated yet — that's OK, Task 2 will fix callers)

---

### Task 2: SubitemUpdaterV2 — 在 `run()` 和 `runSingle()` 中添加取消检查点

**Files:**
- Modify: `src/SubitemUpdaterV2.cpp:163-366`

- [ ] **Step 1: 在 `run()` Direct Phase 循环中添加取消检查**

在 `run()` 第 219 行 for 循环开始处，每次迭代检查：

```cpp
for (size_t i = 0; i < enabledSubs.size(); ++i) {
    if (isCancelled()) {
        Logger::write("INFO: Update cancelled by user during direct phase", LogLevel::REPORT);
        break;
    }
    // ... 现有代码（shouldSkipUpdate 等）
```

- [ ] **Step 2: 在 `run()` Direct Phase fetch 之后/update 之前检查取消**

```cpp
std::string content = fetchUrl(sub.url);
if (isCancelled()) {
    Logger::write("INFO: Update cancelled after fetch", LogLevel::REPORT);
    break;
}
if (!content.empty()) {
```

- [ ] **Step 3: 在 `run()` Proxy Phase 循环中添加取消检查**

在 Proxy Phase（在 `runDirectPhase` 块之后，第 250 行附近）的 for 循环中也添加相同的取消检查：

```cpp
if (runProxyPhase) {
    Logger::write("...Proxy phase...");
    for (size_t i = 0; i < failedSubs.size(); ++i) {
        if (isCancelled()) {
            Logger::write("INFO: Update cancelled by user during proxy phase", LogLevel::REPORT);
            break;
        }
        // ... existing fetchUrlViaProxy/getProxyPorts logic
    }
}
```

- [ ] **Step 4: 在 `runSingle()` 中添加取消检查**

在 `runSingle()` 的 `updateWithStrategy()` 调用之前检查：

```cpp
// runSingle() 第 350-351 行之间
if (isCancelled()) {
    Logger::write("INFO: Single update cancelled by user: " + subId, LogLevel::REPORT);
    return false;
}
```

- [ ] **Step 5: Build 验证**

```bash
cd E:\eclipse_workspace\multiple_thread_validproxy; cmake --build build --parallel 8
```
Expected: Build succeeds

---

### Task 3: AppController — 添加 ResetGuard + 传递取消标志

**Files:**
- Modify: `src/ui/AppController.h:48-49`
- Modify: `src/ui/AppController.cpp:386-633`

- [ ] **Step 1: 重命名 `cancelTest()` → `cancelOperation()`**

AppController.h 第 48 行：
```cpp
// 原：
void cancelTest();
// 改为：
void cancelOperation();
```

AppController.cpp 第 217-226 行：
```cpp
// 原函数名
void AppController::cancelOperation() {  // 原名 cancelTest
    cancelRequested_ = true;
    // ... 现有日志
}
```

- [ ] **Step 2: 为 `doUpdateSubscription` 添加 ResetGuard 并传递取消标志**

```cpp
void AppController::doUpdateSubscription(const std::string& subId, wxEvtHandler* wxHandler) {
    ResetGuard _rg{isRunning_};  // 新增
    
    update::SubitemUpdaterV2 updater(db_, config_.xray_executable, config_, nullptr, "",
                                     &cancelRequested_);  // 新增参数
    // ... 其余不变
}
```

注意：需要在 `doUpdateSubscription` 开头定义 ResetGuard 结构体（如果尚未定义）。检查 `doFindFirstProxy` 中已有的定义，将其提取到类级别或复制到每个方法中。

实际上，最好的方式是在 AppController.h 中定义 ResetGuard 嵌套结构体，这样所有方法都能使用：

```cpp
// AppController.h 中 class AppController 内部的 private: 区域
struct ResetGuard {
    std::atomic<bool>& flag;
    ~ResetGuard() { flag = false; }
};
```

- [ ] **Step 3: 为 `doUpdateAllSubscriptions` 添加 ResetGuard 并传递取消标志**

```cpp
void AppController::doUpdateAllSubscriptions(wxEvtHandler* wxHandler) {
    ResetGuard _rg{isRunning_};  // 新增
    
    update::SubitemUpdaterV2 updater(db_, config_.xray_executable, config_, nullptr, "",
                                     &cancelRequested_);  // 新增参数
    // ... 其余不变
}
```

- [ ] **Step 4: 为 `doTestSubscription` 添加 ResetGuard**

```cpp
void AppController::doTestSubscription(const std::string& subId, wxEvtHandler* wxHandler) {
    ResetGuard _rg{isRunning_};  // 新增
    // ... 其余不变，删除末尾的 isRunning_ = false;
}
```
记得删除这些方法末尾现有的 `isRunning_ = false;`，因为 ResetGuard 的析构函数会处理。

- [ ] **Step 5: 为 `doTestSingleProxy` 添加 ResetGuard**

```cpp
void AppController::doTestSingleProxy(const std::string& indexId, wxEvtHandler* wxHandler) {
    ResetGuard _rg{isRunning_};  // 新增
    // ... 其余不变，删除末尾的 isRunning_ = false;
}
```

- [ ] **Step 6: 为 `doTestAllProxies` 添加 ResetGuard**

```cpp
void AppController::doTestAllProxies(wxEvtHandler* wxHandler) {
    ResetGuard _rg{isRunning_};  // 新增
    // ... 其余不变，删除末尾的 isRunning_ = false;
}
```

- [ ] **Step 7: 为 `doSyncDatabases` 添加 ResetGuard**

```cpp
void AppController::doSyncDatabases(wxEvtHandler* wxHandler) {
    ResetGuard _rg{isRunning_};  // 新增
    // ... 其余不变，删除末尾的 isRunning_ = false;
}
```

- [ ] **Step 8: 将 `ResetGuard` 定义移动到 AppController.h**

将现有的局部定义（在 AppController.cpp 第 500-501 行）提升为 AppController.h 中的嵌套结构体：

```cpp
// AppController.h — class AppController 内部，private 区域
struct ResetGuard {
    std::atomic<bool>& flag;
    ~ResetGuard() { flag = false; }
};
```

然后在 AppController.cpp 中删除 `doFindFirstProxy` 和 `doFindBestProxy` 中的局部 ResetGuard 定义。

- [ ] **Step 9: Build 验证**

```bash
cd E:\eclipse_workspace\multiple_thread_validproxy; cmake --build build --parallel 8
```
Expected: Build succeeds

---

### Task 4: MainFrame — 动态取消按钮

**Files:**
- Modify: `src/ui/MainFrame.h:74`
- Modify: `src/ui/MainFrame.cpp:46,78,145-151,281-282,509-536,627-629`

- [ ] **Step 1: 重命名按钮 ID**

MainFrame.h 第 74 行：
```cpp
// 原：
void onToolCancelTest(wxCommandEvent& event);
// 改为：
void onToolCancel(wxCommandEvent& event);
```

MainFrame.cpp 第 46 行：
```cpp
// 原：
ID_TOOL_CANCEL_TEST   = wxID_HIGHEST + 208,
// 改为：
ID_TOOL_CANCEL        = wxID_HIGHEST + 208,
```

MainFrame.cpp 第 78 行（事件表）：
```cpp
// 原：
EVT_MENU(ID_TOOL_CANCEL_TEST, MainFrame::onToolCancelTest)
// 改为：
EVT_MENU(ID_TOOL_CANCEL, MainFrame::onToolCancel)
```

- [ ] **Step 2: 更新工具栏初始化**

MainFrame.cpp 第 281-282 行：
```cpp
// 原：
tb->AddTool(ID_TOOL_CANCEL_TEST, wxEmptyString, cancelBmp, cancelDisabled, wxITEM_NORMAL, "取消测试");
tb->EnableTool(ID_TOOL_CANCEL_TEST, false);
// 改为：
tb->AddTool(ID_TOOL_CANCEL, wxEmptyString, cancelBmp, cancelDisabled, wxITEM_NORMAL, "取消");
tb->EnableTool(ID_TOOL_CANCEL, false);
```

- [ ] **Step 3: 添加辅助方法设置按钮状态**

在 MainFrame 类中添加两个辅助方法 + 在 MainFrame.cpp 中实现：

```cpp
// MainFrame.h — private: 区域添加
void setCancelButton(const wxString& label, bool enabled);

// MainFrame.cpp 实现
void MainFrame::setCancelButton(const wxString& label, bool enabled) {
    wxToolBar* tb = GetToolBar();
    if (!tb) return;
    tb->SetToolShortHelp(ID_TOOL_CANCEL, label);
    tb->SetToolNormalBitmap(ID_TOOL_CANCEL, ToolbarIcons::load("tool_cancel"));
    wxBitmap disabledBmp = ToolbarIcons::loadDisabled("tool_cancel");
    // wxToolBar doesn't have SetToolDisabledBitmap in older wx versions, but we use EnableTool
    tb->EnableTool(ID_TOOL_CANCEL, enabled);
}
```

注意：较新 wxWidgets 版本有 `SetToolDisabledBitmap`。为了兼容，我们只使用 `EnableTool` 来控制启用/禁用。工具提示（shortHelp）在按钮悬停时显示文字。

实际上更简单的方式是仅使用 `EnableTool` + `SetToolShortHelp` 来实现动态效果。让我们简化：

```cpp
void MainFrame::setCancelButton(const wxString& label, bool enabled) {
    wxToolBar* tb = GetToolBar();
    if (!tb) return;
    tb->SetToolShortHelp(ID_TOOL_CANCEL, label);
    tb->EnableTool(ID_TOOL_CANCEL, enabled);
}
```

- [ ] **Step 4: 更新操作启动处设置按钮**

所有启动异步操作的地方都调用 `setCancelButton`：

**`onMenuUpdateAll`** (第 424 行)：
```cpp
void MainFrame::onMenuUpdateAll(wxCommandEvent&) {
    setStatusText(0, "Updating all subscriptions...");
    controller_->updateAllSubscriptionsAsync(this);
    setCancelButton("停止更新", true);
}
```

**`onTestSubscription`** (第 513 行)：
```cpp
void MainFrame::onTestSubscription(SubscriptionTestEvent& evt) {
    controller_->testSubscriptionAsync(evt.getSubId(), this);
    setStatusText(0, "Testing subscription…");
    setCancelButton("取消测试", true);
}
```

**`onToolTest`** (第 520 行)：
```cpp
void MainFrame::onToolTest(wxCommandEvent& event) {
    controller_->testAllProxiesAsync(this);
    setStatusText(0, "Testing all proxies…");
    setCancelButton("取消测试", true);
}
```

**`onMenuFindProxy`** (第 436 行)：
```cpp
void MainFrame::onMenuFindProxy(wxCommandEvent&) {
    setStatusText(0, "Finding first working proxy…");
    controller_->findFirstProxyAsync(this);
    setCancelButton("取消查找", true);
}
```

**`onMenuFindBest`** (第 441 行)：
```cpp
void MainFrame::onMenuFindBest(wxCommandEvent&) {
    setStatusText(0, "Finding best proxy…");
    controller_->findBestProxyAsync(this);
    setCancelButton("取消查找", true);
}
```

**`onToolSync`** (第 554 行)：
```cpp
void MainFrame::onToolSync(wxCommandEvent&) {
    setStatusText(0, "同步中…");
    if (controller_) {
        controller_->syncDatabasesAsync(this);
        setCancelButton("取消同步", true);
    }
}
```

- [ ] **Step 5: 更新取消按钮事件处理**

```cpp
// MainFrame.cpp — 替换 onToolCancelTest
void MainFrame::onToolCancel(wxCommandEvent&) {
    if (controller_) {
        controller_->cancelOperation();
    }
    setCancelButton("正在取消…", false);
    setStatusText(0, "正在取消操作…");
}
```

- [ ] **Step 6: 更新操作完成时禁用按钮**

在 `onStatusUpdate` 和 `wxEVT_PROXY_TEST_PROGRESS` 处理中添加禁用：

`wxEVT_PROXY_TEST_PROGRESS` (第 145-154 行)：
```cpp
Bind(wxEVT_PROXY_TEST_PROGRESS, [this](ProxyTestProgressEvent& evt) {
    if (evt.isCompleted() && proxyPanel_) {
        proxyPanel_->refreshResults();
    }
    if (evt.isCompleted()) {
        setCancelButton("取消", false);
        setStatusText(0, "Test completed");
    }
});
```

`onStatusUpdate` (第 627-629 行)——扩展以处理完成消息：
```cpp
void MainFrame::onStatusUpdate(StatusUpdateEvent& event) {
    wxString text = event.getText();
    setStatusText(0, text);
    // Check if this is a completion message — disable cancel button
    if (text.StartsWith("Update completed") || text.StartsWith("Update failed") ||
        text.StartsWith("All subscriptions") || text.StartsWith("Update (all)") ||
        text.StartsWith("CANCELLED") || text.StartsWith("数据库同步完成") ||
        text.StartsWith("数据库同步失败")) {
        setCancelButton("取消", false);
    }
}
```

- [ ] **Step 7: Build 验证**

```bash
cd E:\eclipse_workspace\multiple_thread_validproxy; cmake --build build --parallel 8
```

Expected: Build succeeds

---

### Task 5: 测试和验证

**Files:**
- Test in `build/` directory

- [ ] **Step 1: 运行单元测试**

```bash
cd E:\eclipse_workspace\multiple_thread_validproxy\build; ctest -V
```

Expected: 3/3 tests pass

- [ ] **Step 2: 运行 GUI 验证**

```bash
cd E:\eclipse_workspace\multiple_thread_validproxy; Start-Process -NoNewWindow .\bin\validproxy.exe -ArgumentList "-ui"
```

手动验证（在 GUI 中观察）：
1. 点击"更新全部" → 按钮变为"停止更新"，启用
2. 更新完成 → 按钮恢复到"取消"，禁用
3. 点击"测试全部" → 按钮变为"取消测试"，启用
4. 测试完成 → 按钮恢复
5. 更新过程中点击"停止更新" → 更新中断，按钮禁用
6. 更新过程中尝试其他操作 → 收到 "REJECT" 提示

- [ ] **Step 3: 提交**

```bash
git add include/SubitemUpdaterV2.h src/SubitemUpdaterV2.cpp src/ui/AppController.h src/ui/AppController.cpp src/ui/MainFrame.h src/ui/MainFrame.cpp
git commit -m "feat: 订阅更新可取消 + 动态取消按钮

- 为 SubitemUpdaterV2 添加 externalCancel_ 取消标志和检查点
- 在 run() 循环和 runSingle() 中添加协作式取消检查
- 为所有 doXxx 方法添加 RAII ResetGuard 确保异常安全
- 重命名 cancelTest() → cancelOperation()，语义更通用
- 取消按钮动态切换文字：取消测试/停止更新/取消查找/取消同步
- 所有异步操作启动时启用按钮，完成时自动禁用"
```
