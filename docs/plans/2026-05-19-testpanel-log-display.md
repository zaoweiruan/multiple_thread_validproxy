# Plan: 将所有日志移入 TestPanel 中显示（修订版 v2）

> **日期**：2026-05-19  
> **状态**：📝 草稿（已修正评审 P1/P2 问题）  
> **影响范围**：`TestPanel`、`AppController`、`Logger`、`MainFrame`、`CMakeLists.txt`  
> **评审版本**：v1 评审见 `docs/code-reviews/2026-05-19-testpanel-log-display-review.md`

---

## 背景与目标

### 当前问题

- `Logger::write()` 有两条输出路径：**文件** + **UI 回调**（`logCallback_`）
- UI 回调当前只注册给 `LogPanel`，但 `LogPanel` **从未加入 `initPanels()` 布局**，实际全部日志最终只有文件路径
- `TestPanel` 只展示代理测试结果（Remarks / Address / Delay / Message 四列），**"OK 625ms" / "XRAY_ERROR" / "CONFIG_ERROR" 等过程日志只在文件里**
- 用户期望测试完成前就能在测试窗口里看到测试日志

### 目标

将 **ProxyBatchTester / XrayInstance / AppController 测试路径**的所有 `Logger::write` 输出实时显示在 `TestPanel` 内的只读文本区，与结果列表共享同一界面。

### 非目标

- 不删除文件日志（文件路径继续写入，作为审计记录）
- 不迁移订阅更新 / 应用生命周期等非测试日志（非测试日志维持原样，走 LogPanel）
- 不改变 `LogLevel` 枚举 / `Logger` 静态接口（除 callback 机制）
- 不引入大体积第三方库（纯 wxWidgets）

---

## 成功标准

- [ ] `TestPanel` 新增一个只读 `wxTextCtrl` 日志区，**实时**显示测试过程中的 `Logger` 文本
- [ ] 订阅测试 / 批量测试 / 单代理测试三种场景下，日志区均能正确刷新
- [ ] `TestPanel` 日志行限制在 **500 行**；超出时删除最旧行（`Remove(start, end)`），**不清空全部**
- [ ] 文件日志 `bin/log/ui_*.log` **继续正常写入**，内容不减少
- [ ] Build + Tests（3/3）无回归
- [ ] 连续启动/关闭 GUI 5 次无崩溃

---

## 技术决策记录（基于评审修正）

### TD-1：Logger callback 机制 ── Save/Restore 模式

**评审问题 P1-1**：原计划使用 `ScopedLogCallback` RAII + `setLogCallback()`，但 `setLogCallback` 是**覆盖写入**，没有保存上一个回调的能力，RAII 析构时无法原样还原。

**决策**：为 `Logger` 增加 `pushCallback()` / `popCallback()` 两个高层 API，内部维护一个 `std::vector<LogCallback>` 栈。

```
Logger::pushCallback(cb)    → 栈压入 cb；若栈有 ≥2 个元素，则同时调用 cb 作为新激活者
Logger::popCallback()       → 栈弹出顶层；底层 cb（前序保存者）自动恢复为当前 logCallback_
                                         [0]        [1]        ← pop() → 恢复 [0]
TestPanel Entry: pushCallback(TestPanelCb)
LogPanel  Entry: pushCallback(LogPanelCb)
Stack:  [LogPanelCb] [TestPanelCb]         ← 只有 TestPanelCb 被激活
                         ↑  active

TestPanel Exit: popCallback()
Stack:  [LogPanelCb]                      ← 恢复 LogPanelCb 自动激活
          ↑  active
```

好处：
- 两个消费者（`TestPanel` + `LogPanel`）可以**共存**，测试期间 `LogPanel` 仍能收到事件（只是测试期间不显示到其自己的窗口）
- RAII `ScopedCallback` 只需要调用 push/pop，不需要自己备份回调指针

**相关评审项**：P1-1 / P2-5（ScopedLogCallback 语义）/

---

### TD-2：跨线程 UI 更新 ── `wxQueueEvent`，避免 `wxMutexGuiLocker`

**评审问题 P1-3**：`wxMutexGuiLocker` **不是线程调度机制**，它只允许在**主线程内**使用，从 worker thread 直接调用 UI 对象会导致未定义行为。

**决策**：`Logger` 回调（`logCallback_`）运行位置不确定（可能是 worker thread），因此 `TestPanel` 的 `logCallback_` lambda **必须通过 `wxQueueEvent` 或 `CallAfter` 调度到主线程**：

```cpp
// TestPanel::initLogCtrl() 完成的注册
Logger::setLogCallback([this](const std::string& msg, LogLevel level) {
    if (this) {
        // marshal to main thread
        wxQueueEvent(this, new LogMessageEvent(msg, level));
    }
});
```

`m_logCtrl_` 的 `AppendText` / `ShowPosition` **仅在 `onLogMessage()` 事件句柄内调用**，调用方已在主线
程，不再额外的刷新锁。

**相关评审项**：P1-3

---

### TD-3：行数裁剪 ── `Remove()` 而非 `Clear()`

**评审问题 P1-2**：原计划 `SetSelection(start, end); Clear()` 会清空**所有**文本，导致用户丢失已有日志；且逻辑顺序错误（置选择再清空，保留最

旧 100 行**，使用 `wxTextCtrl::Remove(start, end)`：

```cpp
// 裁剪最旧 100 行，保留追加位置
int lineCount = logCtrl_->GetNumberOfLines();
if (lineCount > MAX_LOG_LINES) {
    wxTextPos start = logCtrl_->XYToPosition(0, lineCount - MAX_LOG_LINES);
    logCtrl_->Remove(start, logCtrl_->GetLastFrontPosition());
}
```

`Remove(start, end)` 仅删除 `[start, end)` 区间，不影响未裁剪区域。

**相关评审项**：！P1-2 / P1-3

---

### TD-4：`LogPanel` 回调 ── 测试期间仍保持活跃

原计划想 `LogPanel` 只在测试期间关闭事件绑定，该方案破坏 `LogPanel` 其他日志捕获，造成功能退化。

**决策**：`LogPanel` 不受测试阶段影响，始终维持其回调注册。测试阶段的 `Logger` 输出会同时进入 `LogPanel` 区** `和`TestPanel`

`缓冲区**。因为 `LogPanel` 当前无可见布局（`initPanels()`），这仅为"存在即合理"的无本质贡献，但反过来不会删除，维持现状。

**相关评审项**：P2-4

---

## 实施步骤（修正版 S0~S5）

### **前置增强：S0 — Logger 增加 `pushCallback` / `popCallback` API**

**文件**：`include/Logger.h` + `src/Logger.cpp`

**Header 新增声明**：

```cpp
class Logger {
public:
    using LogCallback = std::function<void(const std::string&, LogLevel)>;
    static void setLogCallback(LogCallback cb);   // 保留，用于初始注册
    static void clearLogCallback();                // 保留
    // 新增
    static void pushCallback(LogCallback cb);      // 压栈；成为当前激活回调
    static void popCallback();                      // 弹栈；恢复前一个回调
private:
    static std::vector<LogCallback> callbackStack_;  // 新增：回调栈
    static LogCallback logCallback_;                 // 当前激活的回调（顶层）
};
```

**Impl 新增实现**：

```cpp
void Logger::pushCallback(LogCallback cb) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callbackStack_.push_back(std::move(cb));
    // 顶层栈元素同步为当前激活回调，内部写路径不变
}
void Logger::popCallback() {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (!callbackStack_.empty()) {
        callbackStack_.pop_back();
    }
    // 恢复为栈底或 null
}
```

- `setLogCallback` 与 `clearLogCallback` 保留，VM 内滚动期间该路径↗️
- `pushCallback`/`popCallback` 作为新增专用入口供 TestPanel + LogPanel 使用

---

### **S1 — TestPanel.h：新增日志组件声明**

```cpp
class TestPanel : public wxPanel {
public:
    explicit TestPanel(wxWindow* parent);
    void startTest(const std::string& subId);
    void cancelTest();
    bool isRunning() const { return isRunning_; }

    // 日志 API
    void appendLogLine(const std::string& line);   // 从 Logger callback 调用
    void resetLog();                                // 开始测试时清空日志

    static constexpr int MAX_LOG_LINES = 500;

private:
    // ...
    wxTextCtrl* logCtrl_;           // 新增：只读日志区

    wxDECLARE_EVENT_TABLE();
};
```

需要包含：`#include <wx/textctrl.h>`（评审 P3-10）

---

### **S2 — TestPanel.cpp：日志区实现**

```cpp
// constructor
TestPanel::TestPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY), /* ... */ {
    // ... 现有 sizer 布局 ...

    wxFont monoFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    logCtrl_ = new wxTextCtrl(this, wxID_ANY, "",
                               wxDefaultPosition, wxDefaultSize,
                               wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH);
    logCtrl_->SetFont(monoFont);
    logCtrl_->SetBackgroundColour(wxColour(30, 30, 30));   // 深色背景，接近 console
    topSizer->Add(logCtrl_, 0, wxEXPAND | wxALL, 2);
    SetSizer(topSizer);

    // 注册日志回调（通过 wxQueueEvent 调度到 main thread）
    Logger::pushCallback([this](const std::string& msg, LogLevel level) {
        if (this) {
            wxQueueEvent(this, new LogMessageEvent(msg, level));
        }
    });
}

TestPanel::~TestPanel() {
    Logger::popCallback();   // 注销
}

void TestPanel::appendLogLine(const std::string& line) {
    // 运行在主线程，通过 wxQueueEvent 调用
    logCtrl_->AppendText(line + "\n");
    // 行数裁剪（P1-2 修复）
    int lineCount = logCtrl_->GetNumberOfLines();
    if (lineCount > MAX_LOG_LINES + 100) {           // 触发阈值 = MAX + 缓冲
        wxTextPos start = logCtrl_->XYToPosition(0, lineCount - MAX_LOG_LINES);
        wxTextPos end   = logCtrl_->GetLastPosition();
        logCtrl_->Remove(start, end);                // ← 用 Remove()，不是 Clear()
    }
    logCtrl_->ShowPosition(logCtrl_->GetLastPosition());
}

void TestPanel::resetLog() {
    logCtrl_->Clear();
}
```

事件表新增绑定（`TestPanel` 接收 `wxEVT_LOG_MESSAGE`）：

```cpp
wxBEGIN_EVENT_TABLE(TestPanel, wxPanel)
    EVT_BUTTON(wxID_ANY, TestPanel::onCancel)
wxEND_EVENT_TABLE()

// 或 Bind
Bind(wxEVT_LOG_MESSAGE, [this](LogMessageEvent& evt) {
    appendLogLine(evt.getMessage());   // 只追加文本，不做颜色着色（P3-8）
});
```

**为什么不着色**：每行不同颜色会导致视觉混乱；`TestPanel` 日志区是**技术缓冲**，保留纯文本最利于复制帖出 debug。

---

### **S3 — TestPanel 事件路由绑定**

AppController 的测试入口 (`doTestSubscription` / `doTestSingleProxy`) 前，`ScopedCallback` 实例化会对这步局部开关影响维护。

```cpp
// TestPanel 初始化时 push 自己的回调，用户生命周期持续（pop 在 ~TestPanel）
// 但 resetLog() 只重置文本，Logger 链路不必在此开关
```

不需额外显式开关，TestPanel 构造时 `pushCallback` 后，构造的序贯线程会收到来自 Logger 的文本消息。析构时 `popCallback` 恢复前序状态。

---

### **S4 — AppController：回调生命周期确认**

`AppController::doTestSingleProxy` / `doTestSubscription` **不需要**额外 `push/pop` 调用，因为两者已有 TestPanel 构造时 push 的日志回调，且析构时 pop：

```cpp
void AppController::doTestSingleProxy(const std::string& indexId, wxEvtHandler* wxHandler) {
    // TestPanel 构造时已 pushCallback，routes all Logger::write to TestPanel logCtrl_
    // 订阅入口已完成，todo worker+Result transfer
    ProxyBatchTester tester(db_, config_, "");
    bool ok = tester.runWithIndexId(indexId);
    // ...
}
```

---

### **S5 — MainFrame：LogPanel 清理**

原计划想移除 `logPanel_`，但代码搜索表明 `logPanel_` **从未被创建或加入 sizer**（只声明了成员指针），initPanels() 中从未写入。**不需要做任何删除操作**。

```cpp
void MainFrame::initPanels() {
    // ... 现有布局（subscriptionPanel + testPanel + proxyPanel）
    // logPanel_ 成员继续保留（声明不变），无需任何改动
}
```

---

## Step-by-Step Summary

| Phase | File | Change |
|---|---|---|
| **S0** | `include/Logger.h`, `src/Logger.cpp` | `pushCallback()` / `popCallback()`；内部 `vector<LogCallback> callbackStack_` |
| **S1** | `src/ui/TestPanel.h` | 新增 `wxTextCtrl* logCtrl_` + `appendLogLine()/resetLog()` + `MAX_LOG_LINES = 500`；add `#include <wx/textctrl.h>` |
| **S2** | `src/ui/TestPanel.cpp` | `initLogCtrl()` 创建 `logCtrl_` (dark mono)；`Bind(wxEVT_LOG_MESSAGE, ...)` + `appendLogLine()`；`startTest()` 调 `resetLog()`；`

: TestPanel dtor pop | null ctx in TestPanel for restoring callback
| **S3** | — | AppController 无需 `TestPanel` 显式 push/pop（构造已处理） |
| **S4** | — | MainFrame `initPanels()` 无更动（`logPanel_` 未实例） |
| **S5** | `docs/reports/` | Build + Tests + GUI 验证报告 |

每阶段独立 Build → ctest → 推进。

---

## 行数裁剪精确逻辑

```
MAX_LOG_LINES   = 500       // 目标行数
CROP_THRESHOLD  = 600       // 触发裁剪门限（比目标多 100 行缓冲）
RemoveDelta     = 100       // 每次删除行数

logCtrl_->GetNumberOfLines() 获取当前行数
if (lineCount > CROP_THRESHOLD):
    删除区间 [0, XYToPosition(0, lineCount - MAX_LOG_LINES))
         = 保留最近的 MAX_LOG_LINES 行，丢弃最旧的 (lineCount - MAX_LOG_LINES) 行
    ShowPosition(last)         // 保持原位（末尾）
```

---

## 回调生命周期图（精确）

```
Logger::write()
  ── callbackMutex_ ──
    logCallback_(fullMsg, level)   ← TestPanel lambda
      ── wxQueueEvent() ──>
        TestPanel::onLogMessage()  ← 主线程
          logCtrl_->AppendText(line + "\n")
          [行数裁剪]
          ShowPosition(last)
```

错误路径时序呈现：

| 时序 | 操作 |
|---|---|
| T1 | `TestPanel` 构造 → `pushCallback(cb1)` |
| T2 | 多天前 `LogPanel` 构造 → `pushCallback(cb2)` → 激活 `cb1` |
| T3 | 测试运行 → 大量 `Logger::write` → `TestPanel` 日志区滚动 |
| T4 | `TestPanel` 析构 → `popCallback()` → 激活 `cb2` 日志中断后端激活 `cb2`（LogPanel 回调，不同意） |

---

## 线程安全

| 场景 | 当前方案 | 说明 |
|---|---|---|
| `Worker thread` 写日志 | Logger → callback → `wxQueueEvent` →主线程 | 无数据争 |
| `TestPanel` 析构后 callback 还存在 | RAII 析构函数 `Logger::popCallback() || TestPanel` | 析构函数在对象析构后安全 |
| `LogPanel:: logCtrl_ 同时不接受 `wxLogEvent` | TestPanel::`logCtrl_` 仅写得自 `TestPanel` 的 `onLogMessage` 句柄 | 无可竞争 |

---

## 回滚方案

- **S0**：`pushCallback/popCallback` 若有不兼容，恢复为单一 `logCallback_`；TestPanel 的回
调降级为 `wxQueueEvent` + `setLogCallback`
- **S2**：行数裁剪修改为 Fix 初始值
- **所有阶段**：`Logger.h/.cpp` 改动无破坏性向后兼容

---

## 风险

| 风险 | 概率 | 缓解 |
|---|---|---|
| `pushCallback/popCallback` 平衡错误（少 pop）导致回调不恢复 | 低 | `popCallback()` 接受空栈无 crash |
| `wxTextCtrl` 500 行内存占用超出预期（繁体中文每行占用大） | 低 | 行数逻辑保证实际字节数可控 |
| `wxTextCtrl::Remove(start, end)` pos 计算有误 | 低 | 编译后单元测试检查 Remove 后的行数 |
| `TestPanel` 构造早于 `Logger::init()` | 低 | GUI 启动序列保证 Logger::init() 优先（MainFrame 创建前完成） |

---

## 附录 A：Logger callback 栈实现参考

```cpp
// Logger.h
static std::vector<LogCallback> callbackStack_;

// Logger.cpp
void Logger::pushCallback(LogCallback cb) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callbackStack_.push_back(std::move(cb));
}
void Logger::popCallback() {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (!callbackStack_.empty()) callbackStack_.pop_back();
}
// write() 调用顶层（或 nullptr）：
{
    std::lock_guard<std::mutex> cbLock(callbackMutex_);
    auto cb = callbackStack_.empty() ? nullptr : callbackStack_.back();
    if (cb) cb(fullMsg, level);
}
```
