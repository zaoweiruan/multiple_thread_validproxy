# 实施计划：功能 6、7、10

> 计划状态: completed
> 对应 feature-status.md 项:
> - **6**: 代理右键 → "Test This Proxy"（单代理测试）
> - **7**: 订阅右键 → "Test"（串联到 TestPanel）
> - **10**: 代理列表列排序

---

## 一、Item 6 — 单代理测试

### 现状

```cpp
void ProxyListPanel::onTestProxy(wxCommandEvent&) {
    wxDataViewItem item = listCtrl_->GetSelection();
    if (!item.IsOk()) return;
    unsigned int row = model_->GetRow(item);
    auto* proxy = model_->getProxy(row);
    if (proxy) {
        wxMessageBox("Single proxy test: " + proxy->remarks, "Info", wxOK | wxICON_INFORMATION);
    }
}
```

只弹信息框，不执行实际测试。

### 目标

右键 "Test This Proxy" → 启动 Xray 实例 → 运行 `ProxyTester` → 将结果显示在 TestPanel 结果列表中。

### 方案

**不启动独立 xray 实例**（太重），改为：

1. 复用现有的 `ProxyBatchTester` 但要提供精确的外部袜子端口
2. 或者提取 `ProxyTester` 的 `test(socksPort)` 方法并使用 `XrayManager` 现有实例

**推荐方案**：因为 `ProxyBatchTester` 内部已经实现了多 xray 实例 + ProxyTester 的完整流程，而单代理测试需要：
- 一个 xray 实例运行该代理配置
- 通过 socksPort 做 curl 测试

修改流程：

```
用户右键 → onTestProxy
  → 获取当前选中代理 Profileitem
  → 已有 XrayManager? 没有则启动一个（或复用 Finder 创建的）
  → ConfigGenerator::generateConfig(proxy) 生成 JSON
  → 写入临时文件
  → XrayManager::start(1, socksPort, apiPort) 启动一个 xray 实例
  → ProxyTester(manager, testUrl, timeout)::test(socksPort)
  → 显示结果到 TestPanel 结果列表
  → TestPanel.startTest("Single: " + proxy->remarks)
  → 插入一行到 resultList_
```

### 涉及文件

| 文件 | 改动 |
|------|------|
| `src/ui/ProxyListPanel.cpp` | `onTestProxy`：实现完整测试流程 |
| `src/ui/ProxyListPanel.h` | 可能新增方法 / 成员 |
| `src/ui/TestPanel.h/cpp` | 新增 `addResult()` 方法，允许外部插入单条结果 |
| `src/ui/MainFrame.cpp` | 可能暴露 XrayManager 引用给 ProxyListPanel |

### 关键接口

```cpp
// ProxyTester 现有 API：
ProxyTester(XrayManager* manager, const std::string& testUrl, int timeoutMs);
TestResult test(int socksPort);  // 通过 socks5 代理测试 testUrl

// TestPanel 新增
void addResult(const wxString& remarks, const wxString& address,
               const wxString& delay, const wxString& message);
```

### 风险

- 单代理测试时启动 xray 有一定延迟（~500ms）
- 需要确保测试完成后释放 xray 实例，不干扰批量测试
- `ProxyTester` 需要 `XrayManager` 先有实例；若 Manager 未初始化需初始化

---

## 二、Item 7 — 订阅右键串联 TestPanel

### 现状

```cpp
void SubscriptionPanel::onTestSubscription(wxCommandEvent&) {
    std::string subId = getSelectedSubId();
    if (!subId.empty()) {
        wxCommandEvent evt;
        wxPostEvent(GetParent(), evt);  // 事件 ID=0，MainFrame 不处理
    }
}
```

事件未绑定，没有任何效果。

### 目标

订阅右键 → "Test" → 触发代理批量测试 → TestPanel 展示进度和结果。

### 方案

有两种实现方式，推荐方案 A：

#### 方案 A（推荐）：通过 MainFrame 转发

`SubscriptionPanel` 直接调用 MainFrame 的测试入口：

```cpp
void SubscriptionPanel::onTestSubscription(wxCommandEvent&) {
    std::string subId = getSelectedSubId();
    if (!subId.empty()) {
        // 通知 MainFrame 触发测试
        wxQueueEvent(GetParent(),
            new SubscriptionTestEvent(subId));
    }
}
```

MainFrame 绑定 `wxEVT_SUBSCRIPTION_TEST`：

```cpp
Bind(wxEVT_SUBSCRIPTION_TEST, [this](SubscriptionTestEvent& evt) {
    std::string subId = evt.getSubId();
    if (!subId.empty()) {
        testPanel_->startTest(subId);
        controller_->testSubscriptionAsync(subId, this);
    }
});
```

或者更简单：在 SubscriptionPanel 中保存 MainFrame 指针，直接调用 `mainFrame->startTest(subId)`。

#### 方案 B（简化）：直接通过父窗口转发

`wxPostEvent` 改为 `wxQueueEvent` + 自定义事件，MainFrame 在构造函数中 Bind 该事件。

### 涉及文件

| 文件 | 改动 |
|------|------|
| `src/ui/SubscriptionPanel.cpp` | `onTestSubscription`：发送 `SubscriptionTestEvent` |
| `src/ui/Events.h` | 新增 `wxEVT_SUBSCRIPTION_TEST` 和 `SubscriptionTestEvent` 类 |
| `src/ui/MainFrame.cpp` | 构造函数中 `Bind(wxEVT_SUBSCRIPTION_TEST, ...)` → 调用 `onToolTest` 或直接启动测试 |

### 关键接口

```cpp
// Events.h 新增
wxDECLARE_EVENT(wxEVT_SUBSCRIPTION_TEST, SubscriptionTestEvent);

class SubscriptionTestEvent : public wxEvent {
public:
    SubscriptionTestEvent(const std::string& subId);
    wxEvent* Clone() const override;
    std::string getSubId() const { return subId_; }
private:
    std::string subId_;
};
```

### 无需改动

- `MainFrame::onToolTest` 已有完整测试流程（`testSubscriptionAsync` + TestPanel）
- `AppController::testSubscriptionAsync` 已有完整实现
- `TestPanel::onProgress` 已有进度处理

---

## 三、Item 10 — 代理列表列排序

### 现状

`ProxyDataViewModel` 继承自 `wxDataViewVirtualListModel`。列创建时指定了 `wxDATAVIEW_COL_SORTABLE` 标志。`onColumnHeaderClick` 仅调用 `event.Skip()`。

但 `wxDataViewVirtualListModel` 默认的 `Compare()` 实现返回 0（不排序），需要覆盖才能启用排序。

### 目标

点选列头时按该列排序（升序/降序交替），再次点击切换方向。

### 方案

1. 在 `ProxyDataViewModel` 中覆盖 `Compare()` 方法
2. 调用 `wxDataViewCtrl::GetSortingColumn()` 获取当前排序列和方向
3. 根据列索引比较两个 `Profileitem` 对应字段

```cpp
int ProxyDataViewModel::Compare(const wxDataViewItem& item1,
                                const wxDataViewItem& item2,
                                unsigned int column,
                                bool ascending) const override
{
    unsigned int row1 = GetRow(item1);
    unsigned int row2 = GetRow(item2);
    if (row1 >= proxies_.size() || row2 >= proxies_.size()) return 0;

    const auto& a = proxies_[row1];
    const auto& b = proxies_[row2];

    int cmp = 0;
    switch (column) {
        case COL_REMARKS:  cmp = a.remarks.compare(b.remarks); break;
        case COL_CONFIGTYPE: cmp = a.configtype.compare(b.configtype); break;
        case COL_ADDRESS:  cmp = a.address.compare(b.address); break;
        case COL_PORT:     cmp = compareInt(a.port, b.port); break;
        case COL_DELAY:    cmp = compareDelay(row1, row2); break;
        // ... 其余列
        default: cmp = 0;
    }
    return ascending ? cmp : -cmp;
}
```

需要处理类型转换：
- `COL_PORT`：字符串转整数比较
- `COL_DELAY`/`COL_SPEED`：从 `exItems_` 中查找后转整数比较
- `COL_CONFIGTYPE`：可按数字值比较（"1"<"2"<...）或按字符串

可以引入辅助函数：
```cpp
static int compareStrings(const std::string& a, const std::string& b);
static int compareInts(const std::string& a, const std::string& b);
```

### 涉及文件

| 文件 | 改动 |
|------|------|
| `src/ui/ProxyListPanel.h` | `ProxyDataViewModel` 新增 `Compare()` 覆盖声明 |
| `src/ui/ProxyListPanel.cpp` | 实现 `Compare()` 方法，按列字段比较 |

### 无需改动

- 列已标记 `wxDATAVIEW_COL_SORTABLE`
- `onColumnHeaderClick` 调用 `event.Skip()` 即可让 wxWidgets 内置机制处理排序 UI

### 边界情况

- 延迟为空（`"-"`）：排在末尾
- 字符串忽略大小写
- 大量数据时排序性能：`Compare` 在排序过程中会被多次调用，对于 5 万条数据需要 O(n log n) 次比较，
但由于每条比较是 O(1) 的字段比较，不会成为瓶颈

---

## 四、工作量估算

| 项 | 文件改动数 | 预估工时 | 依赖 |
|----|-----------|---------|------|
| Item 6 | 3-4 个文件 | 2-3 小时 | 需理解 `ProxyTester` + `XrayManager` 单实例启动流程 |
| Item 7 | 3 个文件 | 0.5 小时 | `Events.h` 新增自定义事件；MainFrame Bind |
| Item 10 | 2 个文件 | 1 小时 | `ProxyDataViewModel::Compare` + 辅助比较函数 |

**合计**：约 4 小时

## 五、执行顺序

```
Item 7 (最小改动，无风险) → Item 10 (独立，不影响其他) → Item 6 (最复杂，最后)
```
