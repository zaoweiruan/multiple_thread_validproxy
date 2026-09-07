# ProxyListPanel 右键菜单「测试在线代理」功能设计

- **版本**: 1.1
- **日期**: 2026-09-02
- **状态**: Draft / 待评审
- **涉及模块**: ProxyListPanel, AppController, ProxyTester, Events

---

## 1. 目标

在 ProxyListPanel 右键菜单中新增「测试在线代理」菜单项，对**当前正在运行的独立代理进程（standalone proxy processes，即"在线代理"）**执行连通性测试，获取各代理时延，将测试结果写入历史表（ProfileExItem / ProfileExItemDAO），完成后刷新代理列表并弹窗提示测试失败的代理列表。

## 2. 背景

当前 ProxyListPanel 右键菜单已支持「测试此代理」（单代理测试，`ID_CONTEXT_TEST_PROXY`）与「批量解析地区」（`ID_CONTEXT_BATCH_RESOLVE_REGION`），但缺少一个「仅对当前在线（正在运行的）独立代理进程批量测试」的入口。

用户定义「在线代理」为**当前正在运行的独立代理进程**（经 `开始代理` / 悬浮窗监控启动，见 `docs/specs/2026-08-24-Spec-StandaloneFloatingWidget-v1.1.md`）。

需求：复用启动代理后的连通性测试功能（curl handle 经 SOCKS5 代理测试目标 URL，即 `ProxyTester::test` → `UrlFetcher::fetchViaProxyStatus` 路径），对所有在线独立代理进程测试获取时延，更新历史表，弹窗提示失败列表。

## 3. 设计约束

| 约束 | 说明 |
|------|------|
| 复用连通性测试 | 测试逻辑沿用 `ProxyTester::test(socksPort, ...)`（curl handle 经 SOCKS5 代理测试 `config.test.url`，返回 `TestResult{success, latencyMs, errorMsg}`，含时延）——与 `startStandaloneProxy` 启动后连通性验证本质同源 |
| 不启动新进程 | 对**已在运行**的独立代理进程按其 SOCKS 端口直接测试，不重复拉起 xray/sing-box |
| 结果写历史表 | 测试结果经 `ProfileExItemDAO::updateTestResult` 写入 ProfileExItem 表 |
| 不阻塞 UI 线程 | 异步 worker 线程执行，完成后经事件回 UI 线程刷新 |
| 防重入 | 复用 `isRunning()` 检查，操作进行中拒绝再次触发 |
| 禁止 `auto` | 全栈代码禁止 `auto` 类型推导（项目核心约束） |

## 4. 「在线代理」语义（v1.1 修订）

**「在线代理」= 当前正在运行的独立代理进程**（standalone proxy processes）。

数据来源：`AppController::getWatchedStandaloneMonitors()`（`AppController.h:108`）返回 `std::vector<StandaloneMonitorRow>`，每行含：

```cpp
struct StandaloneMonitorRow {
    std::string indexId;
    std::string host;        // ProfileItem.Address；空当 profile 缺失
    std::string startedAt;   // "yyyy-MM-dd HH:mm:ss"；空当未知
    int64_t durationMs = 0;  // 实时已运行时长
    int socksPort = 0;       // 0 = 端口未知
    int64_t pid = -1;
};
```

`getWatchedStandaloneMonitors()` 语义：仅包含 `running && managed` 的独立代理进程（锁内快照 `standaloneProxies_`，再与 `proxy_runtime_history` 进行中会话联结补 pid/startedAt）。UI 线程可安全调用。

> **注意（v1.1）**：原 v1.0 方案将「在线代理」定义为 SQL 过滤（`consecutive_failures between 0 and 0`）的代理并用 `ProxyBatchTester::run()` 批量测试。经用户确认，**修订为独立代理进程**语义。因此 **不再使用 `ProxyBatchTester`**，改为对运行中独立代理进程逐个测试。

### 4.1 测试方式

对每个运行中的独立代理进程，取其 `socksPort`，调用 `ProxyTester::test(socksPort, ...)`。该函数：

- 使用 curl handle 经 `http://127.0.0.1:<socksPort>` SOCKS5 代理请求 `config.test.url`（`ProxyTester.cpp:20-27`）
- 返回 `TestResult{ success, latencyMs, errorMsg }`（`ProxyTester.cpp:37-39`），`latencyMs = curl.getTotalTime()*1000`
- 成功判定：HTTP 状态码 200/204（与 `ConnectivityVerifier::verify` 一致）

```cpp
ProxyTester tester(nullptr, config_.test_url, config_.test_timeout_ms);
TestResult r = tester.test(mon.socksPort, &cancelRequested_, nullptr);
```

## 5. 类设计

### 5.1 ProxyListPanel 变更

#### 5.1.1 菜单 ID 枚举（`src/ui/ProxyListPanel.cpp` 第 24-33 行）

新增：

```cpp
ID_CONTEXT_TEST_ONLINE_PROXIES = wxID_HIGHEST + 408,
```

#### 5.1.2 事件表（第 36-43 行）

新增：

```cpp
EVT_MENU(ID_CONTEXT_TEST_ONLINE_PROXIES, ProxyListPanel::onTestOnlineProxies),
```

#### 5.1.3 onContextMenu（约第 440-490 行）

在现有菜单项（如「批量解析地区」）之后追加：

```cpp
menu.Append(ID_CONTEXT_TEST_ONLINE_PROXIES, "测试在线代理");
```

#### 5.1.4 onTestOnlineProxies（新方法）

仿照 `onBatchResolveRegion` / `onTestProxy` 模式：

```cpp
void ProxyListPanel::onTestOnlineProxies(wxCommandEvent& event) {
    syncToolbarState();
    if (controller_->isRunning()) {
        wxMessageBox("操作进行中，请稍候", "提示", wxOK | wxICON_INFORMATION, this);
        return;
    }
    controller_->testOnlineProxiesAsync(this);
}
```

### 5.2 AppController 变更

#### 5.2.1 公开接口（`AppController.h`）

新增：

```cpp
void testOnlineProxiesAsync(wxEvtHandler* handler);
```

#### 5.2.2 私有方法（`AppController.h`）

新增：

```cpp
void doTestOnlineProxies(wxEvtHandler* handler);
```

#### 5.2.3 实现（`AppController.cpp`）

`testOnlineProxiesAsync` 仿照 `testAllProxiesAsync`（第 448-464 行）启动 worker 线程：

```cpp
void AppController::testOnlineProxiesAsync(wxEvtHandler* handler) {
    if (isRunning_) {
        if (handler) {
            wxQueueEvent(handler, new StatusUpdateEvent(0, "操作进行中，请稍候"));
        }
        return;
    }
    isRunning_ = true;
    cancelRequested_ = false;
    workerThread_ = std::thread(&AppController::doTestOnlineProxies, this, handler);
}
```

`doTestOnlineProxies` 核心逻辑（异步 worker 线程）：

```cpp
void AppController::doTestOnlineProxies(wxEvtHandler* handler) {
    ScopeGuard<std::atomic<bool>> _guard{isRunning_};
    try {
        // 1) 枚举当前运行的独立代理进程（锁内快照，UI 线程安全）
        const std::vector<StandaloneMonitorRow> monitors = getWatchedStandaloneMonitors();
        int total = static_cast<int>(monitors.size());

        // 2) 逐个按 SOCKS 端口做连通性测试（复用 ProxyTester::test，含时延）
        ProxyTester tester(nullptr, config_.test_url, config_.test_timeout_ms);
        std::vector<std::string> failedIndexIds;
        int success = 0;
        for (std::size_t i = 0; i < monitors.size(); ++i) {
            if (cancelRequested_) break;
            TestResult r = tester.test(monitors[i].socksPort, &cancelRequested_, nullptr);
            // 3) 写历史表（ProfileExItem）
            exDao_.updateTestResult(monitors[i].indexId, r.latencyMs, r.success, r.errorMsg);
            if (r.success) {
                ++success;
            } else {
                if (monitors[i].socksPort <= 0) {
                    r.errorMsg = "socks port unknown";   // 端口未知（config 解析失败）
                }
                failedIndexIds.push_back(monitors[i].indexId);
            }
        }

        // 4) 投递完成事件（含失败列表）
        wxQueueEvent(handler, new TestOnlineProxiesEvent(
            failedIndexIds, total, success, static_cast<int>(failedIndexIds.size())));
    } catch (const std::exception& e) {
        if (handler) {
            wxQueueEvent(handler, new StatusUpdateEvent(0, "ERR:" + std::string(e.what())));
        }
    }
}
```

> **说明**：`getWatchedStandaloneMonitors()` 已含 `socksPort`，无需再查 `getStandaloneSocksPort`。端口 `<= 0` 视为该进程 ports 未知（config 解析失败，见 §9 风险），直接判失败。
>
> **实现注意**：`AppController.cpp` 当前未 `#include "ProxyTester.h"`（仅含 `ProxyBatchTester.h`），实施时需新增该 include；`ProxyTester` 构造传 `nullptr` manager 即可（`test()` 仅经 SOCKS 端口测试，不使用 manager）。

### 5.3 事件与 UI 刷新

#### 5.3.1 完成事件

新增专用事件 `TestOnlineProxiesEvent`（`src/ui/Events.h` / `src/ui/Events.cpp`），携带：

```cpp
class TestOnlineProxiesEvent : public wxEvent {
public:
    TestOnlineProxiesEvent(const std::vector<std::string>& failedIndexIds,
                           int total, int success, int failed);
    // getters: getFailedIndexIds() / getTotal() / getSuccess() / getFailed()
};
wxDECLARE_EVENT(wxEVT_TEST_ONLINE_PROXIES, TestOnlineProxiesEvent);
```

避免污染现有 `ProxyTestProgressEvent` 语义。

#### 5.3.2 ProxyListPanel 事件绑定与处理

`ProxyListPanel` 构造时 `Bind(wxEVT_TEST_ONLINE_PROXIES, ...)`，收到后：

1. `refreshResults()` 刷新列表（`controller_->loadProxyResults()` + `model_->rebuildMaps()` + `setRunningDurations` + `listCtrl_->Refresh()`）
2. 若 `failedIndexIds` 非空，`wxMessageBox` 弹窗列出失败代理（限制显示条数，最多 20 条 + "…等 N 条"；显示 `indexId`）
3. 若全部成功（`failed == 0`），弹窗提示「全部在线代理测试通过」

## 6. 文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/ui/Events.h` / `src/ui/Events.cpp` | 修改 | 新增 `TestOnlineProxiesEvent` + `wxEVT_TEST_ONLINE_PROXIES` |
| `src/ui/ProxyListPanel.h` | 修改 | 新增 `onTestOnlineProxies` 声明 + 事件绑定 |
| `src/ui/ProxyListPanel.cpp` | 修改 | 菜单 ID、事件表、onContextMenu、onTestOnlineProxies、事件处理 |
| `src/ui/AppController.h` | 修改 | 新增 `testOnlineProxiesAsync`/`doTestOnlineProxies` |
| `src/ui/AppController.cpp` | 修改 | 实现上述方法 |
| `docs/design/2026-09-02-Design-ProxyListPanel-TestOnlineProxies-v1.0.md` | 修改 | 本设计文档（v1.1 修订「在线代理」=独立代理进程） |

> **v1.1 变更**：移除 `include/ProxyBatchTester.h` / `src/ProxyBatchTester.cpp`（原 v1.0 方案新增失败收集的变更不再需要，因不再使用 `ProxyBatchTester`）。

## 7. 备选方案

| 方案 | 说明 | 取舍 |
|------|------|------|
| A. 复用 `ProxyTester::test(socksPort)` 对运行中进程测试 | **推荐**。不启动新进程，直接经 SOCKS 端口测试；返回 `TestResult` 含时延 | 需在各独立代理的 SOCKS 端口上逐个（可串行或小并发）执行 |
| B. 复用 `ConnectivityVerifier::verify` | 与 `startStandaloneProxy` 启动验证完全一致，但**仅返回 bool，无时延** | 不符合"获取时延写历史表"需求，否决 |
| C. `ProxyBatchTester::run()`（SQL 过滤在线代理） | v1.0 方案；批量测试"有效"代理 | 经用户确认，"在线代理"=独立代理进程，否决 |

## 8. 测试策略

- **单元测试**（`tests/`）：
  - `TestOnlineProxiesEvent`：验证事件字段（failedIndexIds/total/success/failed）正确
- **集成/手动验证**：
  - 右键菜单出现「测试在线代理」项
  - 点击后异步执行，不阻塞 UI
  - 对运行中的独立代理进程测试，列表刷新，失败代理弹窗提示
  - 历史表（ProfileExItem）中 delay/message 更新
  - 操作进行中再次触发被拒绝（防重入）

## 9. 风险与注意事项

| 风险 | 缓解 |
|------|------|
| 独立代理进程在测试期间被停止/退出 | `getWatchedStandaloneMonitors()` 仅快照当下运行集；测试失败即记入失败列表，进程退出导致的失败属正常结果 |
| `socksPort == 0`（config 解析失败） | 判失败，errorMsg 标记 "socks port unknown" |
| 测试时延慢导致整体耗时 | 每个 `ProxyTester::test` 有 `timeout_ms` 上限（`config.test.timeout_ms`，默认 5000）；可选后续小并发优化，v1.0 串行即可 |
| 事件在 worker 线程投递 | 使用 `wxQueueEvent` 投递到 UI 线程，避免跨线程直接操作 UI |
| 与现有批量测试/独立代理启动并发 | `isRunning_` 防重入，`AsyncOperationGuard` 保护 worker 生命周期 |
| 禁止 `auto` | 所有新增代码显式类型，不使用 `auto` |

## 10. 验收标准

1. 右键菜单新增「测试在线代理」项（`ID_CONTEXT_TEST_ONLINE_PROXIES = wxID_HIGHEST + 408`）
2. 点击后异步测试**当前正在运行的独立代理进程**，UI 不阻塞
3. 测试结果（时延/成败）写入 ProfileExItem 历史表
4. 完成后 ProxyListPanel 刷新
5. 失败代理列表弹窗提示（全部成功则提示通过）
6. 操作进行中防重入
7. 构建 0 error，ctest 全绿
