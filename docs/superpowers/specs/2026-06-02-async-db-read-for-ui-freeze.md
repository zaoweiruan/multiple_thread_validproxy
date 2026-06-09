# 异步化 UI 层 DB 读操作解决订阅更新时界面冻结

- **日期:** 2026-06-02
- **作者:** Kilo (AI 辅助)
- **状态:** 已批准

## 1. 问题描述

UI 程序更新订阅时，如果用户在订阅窗口点击记录切换选择，界面会长时间停滞、无法响应用户操作。

### 根因分析

```
工作线程 (SubitemUpdaterV2)       UI 线程 (MainFrame)
         │                                │
         │  updateProfileItems()          │
         │  ├── BEGIN TRANSACTION ────────┤
         │  ├── INSERT ...                │
         │  ├── ...                       │
         │  │                     用户点击订阅行
         │  │                     → onSelectionChanged()
         │  │                       → wxEVT_SUBSCRIPTION_SELECTED
         │  │                         → proxyPanel_->loadProxies(subId)
         │  │                           → controller_->loadProxies()  [DB READ]
         │  │                             → sqlite3_step()  ★★★ 阻塞 ★★★
         │  │                                (同一 sqlite3* 连接，
         │  │                                 写事务未完结，
         │  │                                 读操作无法进行)
         │  ├── INSERT ...                │
         │  └── COMMIT ───────────────────┤
         │                     sqlite3_step() 返回 → UI 恢复
```

核心问题：
1. `AppController` 和 `SubitemUpdaterV2` 共享同一个 `sqlite3*` 连接
2. SQLite 单连接不支持并发操作——写事务期间该连接上任何读操作必须等待
3. `proxyPanel_->loadProxies()` 在 UI 线程直接执行 DB 查询，阻塞了 wxWidgets 事件循环

## 2. 设计目标

- **操作互不阻塞**：工作线程写订阅数据和 UI 线程读取代理列表互不等待
- **UI 零阻塞**：所有 DB 读操作不在 UI 线程执行
- **最小改动**：不改变现有架构和业务逻辑
- **向后兼容**：现有 CLI 模式和其他调用方不受影响

## 3. 架构改动

### 3.1 新增自定义事件

**文件:** `src/ui/Events.h` / `src/ui/Events.cpp`

#### `ProxyListLoadedEvent`

```cpp
// 事件类型声明
wxDECLARE_EVENT(wxEVT_PROXY_LIST_LOADED, ProxyListLoadedEvent);

class ProxyListLoadedEvent : public wxEvent {
public:
    ProxyListLoadedEvent(const std::string& subId,
                        std::vector<db::models::Profileitem> proxies,
                        std::vector<db::models::ProfileExItem> exItems);

    wxEvent* Clone() const override;
    const std::string& getSubId() const;
    const std::vector<db::models::Profileitem>& getProxies() const;
    const std::vector<db::models::ProfileExItem>& getExItems() const;

    // 移动语义获取数据（避免拷贝）
    std::vector<db::models::Profileitem> takeProxies();
    std::vector<db::models::ProfileExItem> takeExItems();

private:
    std::string subId_;
    std::vector<db::models::Profileitem> proxies_;
    std::vector<db::models::ProfileExItem> exItems_;
};
```

#### `SubListLoadedEvent`

```cpp
// 事件类型声明
wxDECLARE_EVENT(wxEVT_SUB_LIST_LOADED, SubListLoadedEvent);

class SubListLoadedEvent : public wxEvent {
public:
    SubListLoadedEvent(std::vector<db::models::Subitem> subs,
                       std::unordered_map<std::string, int> proxyCounts);

    wxEvent* Clone() const override;
    std::vector<db::models::Subitem> takeSubs();
    std::unordered_map<std::string, int> takeProxyCounts();

private:
    std::vector<db::models::Subitem> subs_;
    std::unordered_map<std::string, int> proxyCounts_;
};
```

### 3.2 AppController — 新增异步读方法

**文件:** `include/AppController.h` / `src/ui/AppController.cpp`

新增方法签名：

```cpp
// 异步读取代理列表：打开独立连接，查询后通过 wxQueueEvent 回传
void loadProxiesAsync(const std::string& subId, wxEvtHandler* handler);

// 异步读取订阅列表：打开独立连接，查询后通过 wxQueueEvent 回传
void loadSubscriptionsAsync(wxEvtHandler* handler);
```

#### `loadProxiesAsync` 实现

```cpp
void AppController::loadProxiesAsync(const std::string& subId, wxEvtHandler* handler) {
    std::string dbPath = config_.database_path;
    std::string subIdCopy = subId;

    std::thread([dbPath, subIdCopy, handler]() {
        // 1. 打开独立连接
        sqlite3* readerDb = nullptr;
        if (sqlite3_open(dbPath.c_str(), &readerDb) != SQLITE_OK) {
            if (readerDb) sqlite3_close(readerDb);
            // 失败时发送空数据
            wxQueueEvent(handler, new ProxyListLoadedEvent(subIdCopy, {}, {}));
            return;
        }
        sqlite3_busy_timeout(readerDb, 5000);
        sqlite3_exec(readerDb, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);

        // 2. 执行 DB 查询
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

        // 3. 关闭连接
        sqlite3_close(readerDb);

        // 4. 投递事件到 UI 线程
        wxQueueEvent(handler, new ProxyListLoadedEvent(subIdCopy,
                     std::move(proxies), std::move(exItems)));
    }).detach();
}
```

#### `loadSubscriptionsAsync` 实现

```cpp
void AppController::loadSubscriptionsAsync(wxEvtHandler* handler) {
    std::string dbPath = config_.database_path;

    std::thread([dbPath, handler]() {
        // 1. 打开独立连接
        sqlite3* readerDb = nullptr;
        if (sqlite3_open(dbPath.c_str(), &readerDb) != SQLITE_OK) {
            if (readerDb) sqlite3_close(readerDb);
            wxQueueEvent(handler, new SubListLoadedEvent({}, {}));
            return;
        }
        sqlite3_busy_timeout(readerDb, 5000);
        sqlite3_exec(readerDb, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);

        // 2. 执行 DB 查询
        db::models::SubitemDAO subDao(readerDb);
        auto subs = subDao.getAll();

        db::models::ProfileitemDAO proxyDao(readerDb);
        auto proxyCounts = proxyDao.countBySubId();

        // 3. 关闭连接
        sqlite3_close(readerDb);

        // 4. 投递事件到 UI 线程
        wxQueueEvent(handler, new SubListLoadedEvent(
            std::move(subs), std::move(proxyCounts)));
    }).detach();
}
```

### 3.3 MainFrame — 事件绑定的变更

**文件:** `src/ui/MainFrame.cpp`

#### 变更 A：订阅选择事件处理

当前 （第 174-180 行）：
```cpp
Bind(wxEVT_SUBSCRIPTION_SELECTED, [this](SubscriptionSelectedEvent& evt) {
    std::string subId = evt.getSubId();
    if (proxyPanel_) {
        proxyPanel_->loadProxies(subId);   // ← UI 线程同步 DB 读
    }
    ...
});
```

改为：
```cpp
Bind(wxEVT_SUBSCRIPTION_SELECTED, [this](SubscriptionSelectedEvent& evt) {
    std::string subId = evt.getSubId();
    if (proxyPanel_) {
        controller_->loadProxiesAsync(subId, this);  // ← 异步读取
    }
    setStatusText(0, "Loading subscription: " + wxString(subId));
});
```

#### 变更 B：更新完成后的订阅列表刷新

当前 （第 142-148 行）：
```cpp
if (payload.StartsWith("Update completed:") ||
    payload.StartsWith("All subscriptions updated") ||
    payload.StartsWith("Update (all)")) {
    if (subPanel_) {
        subPanel_->loadSubscriptions();    // ← UI 线程同步 DB 读
    }
}
```

改为：
```cpp
if (payload.StartsWith("Update completed:") ||
    payload.StartsWith("All subscriptions updated") ||
    payload.StartsWith("Update (all)")) {
    if (subPanel_ && controller_) {
        controller_->loadSubscriptionsAsync(this);  // ← 异步读取
    }
}
```

#### 变更 C：新增事件绑定处理

```cpp
// ProxyListLoadedEvent — 代理列表数据就绪，更新 UI
Bind(wxEVT_PROXY_LIST_LOADED, [this](ProxyListLoadedEvent& evt) {
    if (proxyPanel_) {
        proxyPanel_->loadProxies(evt.takeProxies(), evt.takeExItems(), evt.getSubId());
    }
    setStatusText(0, "Loaded subscription: " + wxString(evt.getSubId()));
});

// SubListLoadedEvent — 订阅列表数据就绪，更新 UI
Bind(wxEVT_SUB_LIST_LOADED, [this](SubListLoadedEvent& evt) {
    if (subPanel_) {
        subPanel_->loadSubscriptions(evt.takeSubs(), evt.takeProxyCounts());
    }
});
```

### 3.4 ProxyListPanel — 新增预加载数据接口

**文件:** `src/ui/ProxyListPanel.cpp` / `src/ui/ProxyListPanel.h`

当前 `loadProxies(const std::string& subId)` 同时包含 DB 读和 UI 更新。

将其拆分为两个版本：

```cpp
// 保留原始接口（用于 CLI 模式或同步场景）
void loadProxies(const std::string& subId);

// 新增预加载数据版本（UI 异步场景使用）
void loadProxies(std::vector<db::models::Profileitem> proxies,
                 std::vector<db::models::ProfileExItem> exItems,
                 const std::string& subId);
```

实现中 `loadProxies(subId)` 保留原有从 DB 读的逻辑不变，而 `loadProxies(proxies, exItems, subId)` 直接使用传入的数据，执行原有的 UI 更新逻辑（设置 `currentSubId_`、更新 `proxies_`、`exItems_`、通知 model 等）。

### 3.5 SubscriptionPanel — 新增预加载数据接口

**文件:** `src/ui/SubscriptionPanel.cpp` / `src/ui/SubscriptionPanel.h`

同理：

```cpp
// 保留原有接口
void loadSubscriptions();

// 新增预加载数据版本
void loadSubscriptions(const std::vector<db::models::Subitem>& subs,
                       const std::unordered_map<std::string, int>& proxyCounts);
```

### 3.6 影响分析：同步调用点

以下调用点仍使用同步版本 `loadSubscriptions()`：

| 位置 | 场景 | 是否安全 |
|------|------|---------|
| `SubscriptionPanel::onRefreshSubscription` | 用户点击「刷新」 | 是——没有后台写操作 |
| `SubscriptionPanel::onEditSubscription` | 编辑后刷新 | 是——编辑后刷新 |
| `SubscriptionPanel::onDeleteSubscription` | 删除后刷新 | 是——删除后刷新 |
| `SubscriptionPanel::onImportSubscription` | 导入后刷新 | 是——导入后刷新（但导入本身也在 UI 线程！）|
| `MainFrame::initPanels()` | 启动时加载 | 是——无并发 |
| `ProxyListPanel::refreshResults()` | 测试完成后刷新延迟列 | 是——专用轻量接口 |

## 4. 数据流图

```
用户操作                    工作线程                     UI 线程
───────                    ────────                     ────────
                                          点击订阅行
                                            │
                                            ▼
                                        onSelectionChanged()
                                            │
                                            ▼
                                        wxPostEvent(SubscriptionSelectedEvent)
                                            │
                                            ▼
                                        MainFrame 处理
                                            │
                                            ▼
                                        loadProxiesAsync(subId)
                                            │
                                            ▼
                  ┌────────── std::thread ──────────┐
                  │  打开独立 sqlite3*               │
                  │  ProfileitemDAO::getAll()          │
                  │  ProfileExItemDAO::getAll()      │
                  │  关闭 sqlite3*                   │
                  │  wxQueueEvent(ProxyListLoaded) ──┤
                  └──────────────────────────────────┤
                                            │
                                            ▼
                                        MainFrame 处理
                                            │
                                            ▼
                                        proxyPanel_->loadProxies(data)
                                            │
                                            ▼
                                        wxDataViewCtrl 模型更新
                                            │
                                            ▼
                                        UI 即时响应
```

## 5. 已考虑的风险

| 风险 | 缓解措施 |
|------|---------|
| 多个 `loadProxiesAsync` 同时运行 | 每次点击创建一个独立线程，无需同步——各自独立打开连接，返回各自结果。最后一次 `ProxyListLoadedEvent` 覆盖显示 |
| 用户关闭窗口时后台读线程仍在运行 | 读线程不访问 `AppController` 成员，仅操作本地变量和连接。线程被 detach 会自行退出 |
| `config_.database_path` 在读线程启动后被修改 | `loadProxiesAsync` 在调用时通过值捕获 `dbPath`，后续修改不影响正在运行的线程 |
| `SubitemUpdaterV2` 的写操作与读线程冲突 | 使用独立连接 + WAL 模式，SQLite 允许多连接同时读写。读线程看到的是写操作开始前的快照 |
| 大量数据导致读线程耗时较长 | 读线程不阻塞 UI；用户可继续操作。后续点击会再次触发新读线程，旧线程结果到达后将被新的覆盖 |
