# 异步化 UI 层 DB 读操作解决订阅更新时界面冻结 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 后台更新订阅时用户点击订阅行不阻塞 UI——将 DB 读操作从 UI 线程移到独立线程+独立连接。

**Architecture:** 新增 `ProxyListLoadedEvent` 和 `SubListLoadedEvent` 两个自定义 wxEvent，`AppController` 新增 `loadProxiesAsync`/`loadSubscriptionsAsync` 方法（内部 `std::thread` + 独立 `sqlite3*` 连接），`ProxyListPanel`/`SubscriptionPanel` 新增接收预加载数据的重载 `loadProxies`/`loadSubscriptions`，`MainFrame` 改为异步绑定。

**Tech Stack:** C++17, wxWidgets 3.2+, SQLite3, MinGW/GCC, CMake + Ninja

---

## 文件结构

| 文件 | 角色 | 变更类型 |
|------|------|---------|
| `src/ui/Events.h` | 自定义事件声明 | 修改：新增 2 个事件类 + 2 个 wxDECLARE_EVENT |
| `src/ui/Events.cpp` | 自定义事件定义 | 修改：新增 2 个 wxDEFINE_EVENT |
| `src/ui/AppController.h` | 控制器接口 | 修改：新增 2 个方法声明 |
| `src/ui/AppController.cpp` | 控制器实现 | 修改：新增 `loadProxiesAsync` / `loadSubscriptionsAsync` 实现 |
| `src/ui/ProxyListPanel.h` | 代理列表面板接口 | 修改：新增重载 `loadProxies(proxies, exItems, subId)` |
| `src/ui/ProxyListPanel.cpp` | 代理列表面板实现 | 修改：新增重载实现，提取公共 UI 更新逻辑 |
| `src/ui/SubscriptionPanel.h` | 订阅列表面板接口 | 修改：新增重载 `loadSubscriptions(subs, counts)` |
| `src/ui/SubscriptionPanel.cpp` | 订阅列表面板实现 | 修改：新增重载实现，提取公共 UI 更新逻辑 |
| `src/ui/MainFrame.cpp` | 主窗口事件绑定 | 修改：2 处事件处理改为异步 + 2 处新增事件绑定 |

---

### Task 1: 新增 ProxyListLoadedEvent 和 SubListLoadedEvent

**Files:**
- Modify: `src/ui/Events.h:1-177`
- Modify: `src/ui/Events.cpp:1-8`

**Step 1.1: 在 Events.h 中添加事件声明**

在 `wxDECLARE_EVENT(wxEVT_PROXY_SELECTION, ProxySelectionEvent);` 之后添加：

```cpp
class ProxyListLoadedEvent;
class SubListLoadedEvent;

wxDECLARE_EVENT(wxEVT_PROXY_LIST_LOADED, ProxyListLoadedEvent);
wxDECLARE_EVENT(wxEVT_SUB_LIST_LOADED, SubListLoadedEvent);
```

在 `UIEventId` 枚举中添加：
```cpp
PROXY_LIST_LOADED,
SUB_LIST_LOADED
```

在 `ProxySelectionEvent` 类定义之后添加新的事件类定义：

```cpp
// ---------------------------------------------------------------
// ProxyListLoadedEvent — proxy list data ready from async reader
// ---------------------------------------------------------------
class ProxyListLoadedEvent : public wxEvent {
public:
    ProxyListLoadedEvent(const std::string& subId,
                        std::vector<db::models::Profileitem> proxies,
                        std::vector<db::models::ProfileExItem> exItems)
        : wxEvent(0, wxEVT_PROXY_LIST_LOADED),
          subId_(subId),
          proxies_(std::move(proxies)),
          exItems_(std::move(exItems)) {}

    wxEvent* Clone() const override { return new ProxyListLoadedEvent(*this); }

    const std::string& getSubId() const { return subId_; }
    const std::vector<db::models::Profileitem>& getProxies() const { return proxies_; }
    const std::vector<db::models::ProfileExItem>& getExItems() const { return exItems_; }

    std::vector<db::models::Profileitem> takeProxies() { return std::move(proxies_); }
    std::vector<db::models::ProfileExItem> takeExItems() { return std::move(exItems_); }

private:
    std::string subId_;
    std::vector<db::models::Profileitem> proxies_;
    std::vector<db::models::ProfileExItem> exItems_;
};

// ---------------------------------------------------------------
// SubListLoadedEvent — subscription list data ready from async reader
// ---------------------------------------------------------------
class SubListLoadedEvent : public wxEvent {
public:
    SubListLoadedEvent(std::vector<db::models::Subitem> subs,
                      std::unordered_map<std::string, int> proxyCounts)
        : wxEvent(0, wxEVT_SUB_LIST_LOADED),
          subs_(std::move(subs)),
          proxyCounts_(std::move(proxyCounts)) {}

    wxEvent* Clone() const override { return new SubListLoadedEvent(*this); }

    std::vector<db::models::Subitem> takeSubs() { return std::move(subs_); }
    std::unordered_map<std::string, int> takeProxyCounts() { return std::move(proxyCounts_); }

private:
    std::vector<db::models::Subitem> subs_;
    std::unordered_map<std::string, int> proxyCounts_;
};
```

注意：需要在 `#include "Logger.h"` 之后添加 `#include <unordered_map>` 和 `#include "Profileitem.h"`、`#include "ProfileExItem.h"`、`#include "Subitem.h"`（如果 `Profileitem.h` 和 `Subitem.h` 尚未被包含）。

实际 Events.h 头部已有 `#include <string>` 但未包含 vector/map 和模型头文件。需要添加：
```cpp
#include <vector>
#include <unordered_map>
#include "Profileitem.h"
#include "ProfileExItem.h"
#include "Subitem.h"
```

**Step 1.2: 在 Events.cpp 中添加事件定义**

在文件末尾添加：
```cpp
wxDEFINE_EVENT(wxEVT_PROXY_LIST_LOADED, ProxyListLoadedEvent);
wxDEFINE_EVENT(wxEVT_SUB_LIST_LOADED, SubListLoadedEvent);
```

---

### Task 2: AppController 新增异步读方法

**Files:**
- Modify: `src/ui/AppController.h:1-90`
- Modify: `src/ui/AppController.cpp:1-815`

**Step 2.1: 在 AppController.h 中添加方法声明**

在 `// Subscriptions` 部分添加：
```cpp
void loadSubscriptionsAsync(wxEvtHandler* handler);
```

在 `// Proxies` 部分添加：
```cpp
void loadProxiesAsync(const std::string& subId, wxEvtHandler* handler);
```

**Step 2.2: 在 AppController.cpp 中添加 `loadProxiesAsync` 实现**

在 `loadProxies` 方法（约第 200 行）之后添加：

```cpp
void AppController::loadProxiesAsync(const std::string& subId, wxEvtHandler* handler) {
    std::string dbPath = config_.database_path;
    std::string subIdCopy = subId;

    std::thread([dbPath, subIdCopy, handler]() {
        sqlite3* readerDb = nullptr;
        if (sqlite3_open(dbPath.c_str(), &readerDb) != SQLITE_OK) {
            Logger::write("loadProxiesAsync: Failed to open DB: " + dbPath, LogLevel::ERR);
            if (readerDb) sqlite3_close(readerDb);
            wxQueueEvent(handler, new ProxyListLoadedEvent(subIdCopy, {}, {}));
            return;
        }
        sqlite3_busy_timeout(readerDb, 5000);
        sqlite3_exec(readerDb, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);

        db::models::ProfileitemDAO dao(readerDb);
        std::vector<db::models::Profileitem> allProxies = dao.getAll();

        std::vector<db::models::Profileitem> proxies;
        if (subIdCopy.empty()) {
            proxies = std::move(allProxies);
        } else {
            std::copy_if(allProxies.begin(), allProxies.end(), std::back_inserter(proxies),
                [&subIdCopy](const db::models::Profileitem& p) { return p.subid == subIdCopy; });
        }

        db::models::ProfileExItemDAO exDao(readerDb);
        std::vector<db::models::ProfileExItem> exItems = exDao.getAll();

        sqlite3_close(readerDb);

        wxQueueEvent(handler, new ProxyListLoadedEvent(subIdCopy,
                     std::move(proxies), std::move(exItems)));
    }).detach();
}
```

**Step 2.3: 在 AppController.cpp 中添加 `loadSubscriptionsAsync` 实现**

在 `loadSubscriptionsAsync` 方法之后添加：

```cpp
void AppController::loadSubscriptionsAsync(wxEvtHandler* handler) {
    std::string dbPath = config_.database_path;

    std::thread([dbPath, handler]() {
        sqlite3* readerDb = nullptr;
        if (sqlite3_open(dbPath.c_str(), &readerDb) != SQLITE_OK) {
            Logger::write("loadSubscriptionsAsync: Failed to open DB: " + dbPath, LogLevel::ERR);
            if (readerDb) sqlite3_close(readerDb);
            wxQueueEvent(handler, new SubListLoadedEvent({}, {}));
            return;
        }
        sqlite3_busy_timeout(readerDb, 5000);
        sqlite3_exec(readerDb, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);

        db::models::SubitemDAO subDao(readerDb);
        auto subs = subDao.getAll();

        db::models::ProfileitemDAO proxyDao(readerDb);
        auto proxyCounts = proxyDao.countBySubId();

        sqlite3_close(readerDb);

        wxQueueEvent(handler, new SubListLoadedEvent(std::move(subs), std::move(proxyCounts)));
    }).detach();
}
```

---

### Task 3: ProxyListPanel 新增预加载数据接口

**Files:**
- Modify: `src/ui/ProxyListPanel.h:1-70`
- Modify: `src/ui/ProxyListPanel.cpp:1-340`

**Step 3.1: 在 ProxyListPanel.h 中添加重载声明**

```cpp
void loadProxies(const std::string& subId = "");
void loadProxies(std::vector<db::models::Profileitem> proxies,
                 std::vector<db::models::ProfileExItem> exItems,
                 const std::string& subId);
void refreshResults();
```

**Step 3.2: 在 ProxyListPanel.cpp 中提取公共 UI 更新逻辑**

当前 `loadProxies(const std::string& subId)` 包含 DB 读取 + UI 更新（第 70-114 行）。提取 UI 更新部分为私有方法 `updateProxyList`。

在 `selectFirstProxy` 之前（约第 114 行）添加新方法：

```cpp
void ProxyListPanel::updateProxyList(const std::vector<db::models::Profileitem>& proxies,
                                      const std::vector<db::models::ProfileExItem>& exItems,
                                      const std::string& subId) {
    currentSubId_ = subId;
    allProxies_ = proxies;

    Logger::write("[DIAG] ProxyListPanel::updateProxyList(subId=" + subId + "): proxies="
                  + std::to_string(proxies.size()), LogLevel::TRACE);

    sortState_.column = -1;
    sortState_.direction = SortDirection::None;
    proxies_ = proxies;
    exItems_ = exItems;

    Logger::write("[DIAG] ProxyListPanel::updateProxyList: exItems_="
                  + std::to_string(exItems_.size()), LogLevel::TRACE);

    model_->setData(&proxies_, &exItems_);
    model_->Reset(0);
    model_->Reset(static_cast<unsigned int>(proxies_.size()));
    model_->detectIdOffset();

    Logger::write("[DIAG] ProxyListPanel::updateProxyList: model_->GetCount()="
                  + std::to_string(model_->GetCount()), LogLevel::TRACE);

    if (!proxies_.empty()) {
        if (!listCtrl_->GetSelection().IsOk()) {
            selectFirstProxy();
        }
    }
}
```

修改 `loadProxies(const std::string& subId)` 调用此新方法：

```cpp
void ProxyListPanel::loadProxies(const std::string& subId) {
    std::vector<db::models::Profileitem> proxies = controller_->loadProxies(subId);
    std::vector<db::models::ProfileExItem> exItems = controller_->loadProxyResults();
    updateProxyList(proxies, exItems, subId);
}
```

新增预加载数据版本：

```cpp
void ProxyListPanel::loadProxies(std::vector<db::models::Profileitem> proxies,
                                  std::vector<db::models::ProfileExItem> exItems,
                                  const std::string& subId) {
    updateProxyList(proxies, exItems, subId);
}
```

在 `ProxyListPanel.h` 的 private 部分添加：
```cpp
void updateProxyList(const std::vector<db::models::Profileitem>& proxies,
                     const std::vector<db::models::ProfileExItem>& exItems,
                     const std::string& subId);
```

---

### Task 4: SubscriptionPanel 新增预加载数据接口

**Files:**
- Modify: `src/ui/SubscriptionPanel.h:1-48`
- Modify: `src/ui/SubscriptionPanel.cpp:1-295`

**Step 4.1: 在 SubscriptionPanel.h 中添加重载声明**

```cpp
void loadSubscriptions();
void loadSubscriptions(const std::vector<db::models::Subitem>& subs,
                       const std::unordered_map<std::string, int>& proxyCounts);
```

**Step 4.2: 在 SubscriptionPanel.cpp 中提取公共 UI 更新逻辑**

当前 `loadSubscriptions()` 方法（第 82-107 行）包含 DB 读取 + UI 更新。

提取 UI 更新部分：

在 `formatUpdateTime` 之前（约第 109 行）添加：

```cpp
void SubscriptionPanel::updateSubscriptionList(const std::vector<db::models::Subitem>& subs,
                                                const std::unordered_map<std::string, int>& proxyCounts) {
    subs_ = subs;
    store_->DeleteAllItems();

    int rowNum = 1;
    for (const auto& sub : subs_) {
        wxVector<wxVariant> row;
        row.push_back(wxVariant(wxString::Format("%d", rowNum++)));
        row.push_back(wxVariant(sub.enabled == "1"));
        row.push_back(wxVariant(sub.remarks));

        auto it = proxyCounts.find(sub.id);
        int count = (it != proxyCounts.end()) ? it->second : 0;
        row.push_back(wxVariant(wxString::Format("%d", count)));

        wxString updateTimeStr = formatUpdateTime(sub.updatetime);
        row.push_back(wxVariant(updateTimeStr));
        store_->AppendItem(row);
    }
}
```

修改 `loadSubscriptions()` 调用此新方法：

```cpp
void SubscriptionPanel::loadSubscriptions() {
    auto subs = controller_->loadSubscriptions();
    auto proxyCounts = controller_->countProxiesBySubId();
    updateSubscriptionList(subs, proxyCounts);
}
```

新增预加载数据版本：

```cpp
void SubscriptionPanel::loadSubscriptions(const std::vector<db::models::Subitem>& subs,
                                           const std::unordered_map<std::string, int>& proxyCounts) {
    updateSubscriptionList(subs, proxyCounts);
}
```

在 `SubscriptionPanel.h` 的 private 部分添加：
```cpp
void updateSubscriptionList(const std::vector<db::models::Subitem>& subs,
                            const std::unordered_map<std::string, int>& proxyCounts);
```

---

### Task 5: MainFrame 事件绑定改为异步

**Files:**
- Modify: `src/ui/MainFrame.cpp:1-696`

**Step 5.1: 修改订阅选择事件处理（约第 174-180 行）**

当前：
```cpp
Bind(wxEVT_SUBSCRIPTION_SELECTED, [this](SubscriptionSelectedEvent& evt) {
    std::string subId = evt.getSubId();
    if (proxyPanel_) {
        proxyPanel_->loadProxies(subId);
    }
    setStatusText(0, "Loaded subscription: " + wxString(subId));
});
```

改为：
```cpp
Bind(wxEVT_SUBSCRIPTION_SELECTED, [this](SubscriptionSelectedEvent& evt) {
    std::string subId = evt.getSubId();
    if (proxyPanel_ && controller_) {
        controller_->loadProxiesAsync(subId, this);
    }
    setStatusText(0, "Loading subscription: " + wxString(subId));
});
```

**Step 5.2: 修改更新完成后的刷新（约第 142-148 行）**

当前：
```cpp
if (payload.StartsWith("Update completed:") ||
    payload.StartsWith("All subscriptions updated") ||
    payload.StartsWith("Update (all)")) {
    if (subPanel_) {
        subPanel_->loadSubscriptions();
    }
}
```

改为：
```cpp
if (payload.StartsWith("Update completed:") ||
    payload.StartsWith("All subscriptions updated") ||
    payload.StartsWith("Update (all)")) {
    if (subPanel_ && controller_) {
        controller_->loadSubscriptionsAsync(this);
    }
}
```

**Step 5.3: 添加 ProxyListLoadedEvent 绑定**

在 `wxEVT_SUBSCRIPTION_SELECTED` 绑定之后添加：

```cpp
Bind(wxEVT_PROXY_LIST_LOADED, [this](ProxyListLoadedEvent& evt) {
    if (proxyPanel_) {
        proxyPanel_->loadProxies(evt.takeProxies(), evt.takeExItems(), evt.getSubId());
    }
    setStatusText(0, "Loaded subscription: " + wxString(evt.getSubId()));
});
```

**Step 5.4: 添加 SubListLoadedEvent 绑定**

在 `wxEVT_PROXY_LIST_LOADED` 绑定之后添加：

```cpp
Bind(wxEVT_SUB_LIST_LOADED, [this](SubListLoadedEvent& evt) {
    if (subPanel_) {
        subPanel_->loadSubscriptions(evt.takeSubs(), evt.takeProxyCounts());
    }
});
```

---

### Task 6: 构建和验证

**Step 6.1: 编译项目**

```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
```

**Step 6.2: 运行单元测试**

```bash
ctest -V
```

**Step 6.3: 验证功能**

手动测试场景：
1. 启动 GUI：`./build/validproxy`
2. 选择一个订阅，点击「更新订阅」
3. 更新期间，点击其他订阅行
4. 确认：UI 不冻结，状态栏显示 "Loading subscription: ..."
5. 更新完成后，订阅列表的代理计数应自动刷新
