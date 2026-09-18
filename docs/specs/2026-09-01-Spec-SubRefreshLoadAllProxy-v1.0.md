# Spec: 订阅面板「刷新」时加载全部代理（loadallproxy）

- 日期: 2026-09-01
- 模块: SubscriptionPanel / Events / MainFrame / AppController
- 关联: 即时订阅切换 `docs/specs/2026-08-24-Spec-ProxyListPanel-InstantSubSwitch-v1.0.md`
- 规范: C++17，禁止 `auto` 类型推导；修改最少代码、保持既有事件解耦风格一致

## 1. 背景与目标

用户需求（原文）：**「调整功能：订阅窗口『刷新』时需要 loadallproxy」**。

当前订阅面板（`SubscriptionPanel`）右键菜单选择「刷新」时，仅重新加载订阅源列表
（`onRefreshSubscription` → `loadSubscriptions()`），**不会**刷新代理列表面板
（`ProxyListPanel`）。用户希望「刷新」除了重载订阅源之外，同时**加载全部代理**到
代理列表，使刷新操作一次性同步订阅源与代理列表两处视图。

## 2. 现状调研

- `SubscriptionPanel::onRefreshSubscription(wxCommandEvent&)`（`src/ui/SubscriptionPanel.cpp:338-340`）
  **仅调用 `loadSubscriptions()`**，不触达代理列表。
- 事件解耦既定模式（`src/ui/SubscriptionPanel.cpp:306-310`，`onSelectionChanged`）：
  ```cpp
  wxWindow* topLevel = wxGetTopLevelParent(this);
  if (topLevel) {
      SubscriptionSelectedEvent evt(subId);
      wxPostEvent(topLevel, evt);
  }
  ```
- `MainFrame` 以 `Bind(wxEVT_XXX, lambda)` 订阅面板事件并调用 `proxyPanel_` /
  `controller_` 成员（`src/ui/MainFrame.cpp:285-302`）。
- **已存在「加载全部代理」现成模式**（`src/ui/MainFrame.cpp:1188`，切换数据库场景）：
  ```cpp
  controller_->loadProxiesAsync("", this);
  ```
  其中空 `subId` 表示全表 `getAll()`；`loadProxiesAsync` 后台线程读取，完成后经
  `wxEVT_PROXY_LIST_LOADED` 事件投递回 `MainFrame`，最终调用
  `proxyPanel_->loadProxies(...)` 刷新视图。
- **不存在** `SubscriptionRefreshEvent` / `wxEVT_SUBSCRIPTION_REFRESH`（已 grep 确认无）。

## 3. 方案

遵循既有「自定义事件 → MainFrame Bind → proxyPanel_/controller_ 调用」解耦模式，
新增 `SubscriptionRefreshEvent`：

1. **Events.h / Events.cpp**：新增 `SubscriptionRefreshEvent`（语义=「订阅列表已刷新」，
   无负载或携带极简负载）+ `wxDECLARE_EVENT(wxEVT_SUBSCRIPTION_REFRESH, ...)` /
   `wxDEFINE_EVENT(...)`。
2. **SubscriptionPanel::onRefreshSubscription**：`loadSubscriptions()` 之后，仿
   `onSelectionChanged` 用 `wxGetTopLevelParent(this)` + `wxPostEvent(topLevel,
   SubscriptionRefreshEvent())` 广播。同时补上与其他订阅操作一致的重入检查
   （`controller_ && controller_->isRunning()` → `wxMessageBox` + return），避免
   「刷新」在批量测试进行中并发触发全量加载。
3. **MainFrame 构造**：`Bind(wxEVT_SUBSCRIPTION_REFRESH, [this](SubscriptionRefreshEvent&) {
   if (proxyPanel_ && controller_) controller_->loadProxiesAsync("", this); })`，
   即空 `subId` 全量加载全部代理（loadallproxy）。

> 不复用 `wxEVT_SUBSCRIPTION_SELECTED`（语义为「切换过滤」而非「全量加载」）；
> 采用与 `LocateProxyEvent` / `StandaloneProxyEvent` 一致的事件-Bind-调用解耦模式。

## 4. 变更清单

| 文件 | 变更 |
| :--- | :--- |
| `src/ui/Events.h` | 新增 `class SubscriptionRefreshEvent;` 前向声明 + `wxDECLARE_EVENT(wxEVT_SUBSCRIPTION_REFRESH, SubscriptionRefreshEvent)` + 类定义（无负载，含 `Clone()`） |
| `src/ui/Events.cpp` | `wxDEFINE_EVENT(wxEVT_SUBSCRIPTION_REFRESH, SubscriptionRefreshEvent);` |
| `src/ui/SubscriptionPanel.cpp` | `onRefreshSubscription` 增加重入检查 + `loadSubscriptions()` 后经 `wxGetTopLevelParent` 投递 `SubscriptionRefreshEvent` |
| `src/ui/MainFrame.cpp` | 构造中 `Bind(wxEVT_SUBSCRIPTION_REFRESH, ...)` 调用 `controller_->loadProxiesAsync("", this)` |
| `AppController` / `ProxyListPanel` | 无需改动（加载全部代理能力已具备） |

## 5. 验证

- [x] 构建 0 error：`cmake --build build --parallel 8`（14/14 目标，含 `validproxy.exe` 链接成功）
- [x] 事件模块单测通过：`tests\test_log_statistics_event.exe`（重编译 Events.cpp 后 2/2 PASS，验证新增 `wxDEFINE_EVENT` 编译链接正确）
- [x] 非网络单测无回归：`test_utils`(47)/`test_config_reader`(43)/`test_logger`(22)/`test_ipv6_formatter`(7)/`test_uri_codec`(13)/`test_sharelink`(21)/`test_share_uri_builders`(20)/`test_port_manager`(6) 全 PASS
- [ ] 手动 GUI 验证（CI 无法点击右键菜单，建议人工确认）：
      订阅面板右键某订阅 → 「刷新」 → 订阅源列表重载 **且** 代理列表刷新为全部代理
- 说明：`ctest -V` 全量含网络依赖测试（`CurlEasyHandleTest` 等发起真实 HTTP）在本终端环境挂起（无外网/沙箱），非本变更引入；`UITests.exe` 需交互桌面亦同。

## 6. 限制 / 后续

- 「刷新」触发的全量代理加载为异步（后台线程），期间 UI 不阻塞。
- 若后续需要「刷新后保持当前订阅过滤视图」，可改投递的事件携带 `subId` 并在
  MainFrame 侧改名调用 `loadProxiesAsync(subId, this)`。

## 7. 关联文档

- 即时订阅切换先例：`docs/specs/2026-08-24-Spec-ProxyListPanel-InstantSubSwitch-v1.0.md`
