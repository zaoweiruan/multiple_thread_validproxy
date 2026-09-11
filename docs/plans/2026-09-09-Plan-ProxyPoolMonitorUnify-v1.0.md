# Plan: 统一代理池与代理监控窗口（ProxyPoolMonitorUnify）v1.0

> 依据：`docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md`
> 日期：2026-09-09
> 状态：待执行

## Goal

1. 将独立代理池（StandalonePoolDialog）的全部管理功能移入悬浮窗 Panel（StandaloneFloatingWidget）。
2. 监控表新增「类型」列（独立 / 代理池），右键菜单统一。
3. 废弃并删除 `StandalonePoolDialog`，清理 MainFrame 中相关菜单/事件/成员。
4. 文档登记至 `docs/INDEX.md`。

## Architecture

```
AppController::getUnifiedMonitorRows()   ← 唯一新增业务接口（数据层）
   ├── getWatchedStandaloneMonitors()    （独立代理，standaloneMutex_ 快照）
   └── getPoolMembers()                  （池成员，poolMutex_ 快照，含新增 host）

StandaloneFloatingWidget（悬浮窗，Phase 2 改造）
   ├── 8 列列表（类型/标识/Host/监听端口/状态/延迟/失败次数/PID）
   ├── 顶部控件区（状态文本 + 启动/停止池 + 添加代理 + 刷新 + 3 checkbox）
   ├── 统一右键（关闭代理/删除成员/定位到代理列表/测试在线代理/退出）
   └── 自消费 wxEVT_POOL_MEMBERS_UPDATED（timer 兜底）

MainFrame（清理）
   └── 删除 ID_MENU_OPEN_POOL / onMenuOpenPool / onPoolMembersUpdated / poolDialog_
```

## Tech Stack

- C++17（**禁止 `auto` 类型推导**，AGENTS.md 核心约束）
- wxWidgets 3.2+ (wxMSW)
- CMake + Ninja（Debug）
- Google Test（tests/）+ Catch2 UIA（tests/ui/）

---

## Phase 1 — 数据层

### Task 1.1: PoolMemberView 增加 host 字段

**文件**：`include/StandaloneProxyPool.h`

在 `struct PoolMemberView` 中增加 `std::string host;`：

```cpp
struct PoolMemberView {
    std::string indexId;
    std::string tag;
    std::string host;          // 新增：成员代理地址（profile.address）
    std::string state;
    long long lastDelayMs;
    bool lastAlive;
    std::string lastError;
    int failStreak;
    bool probed;
};
```

### Task 1.2: snapshotMembers 填充 host

**文件**：`src/StandaloneProxyPool.cpp`（snapshotMembers :142-159）

在构造 `PoolMemberView v{...}` 时增加 `v.host = m.profile.address;`：

```cpp
PoolMemberView v;
v.indexId = m.indexId;
v.tag = m.tag;
v.host = m.profile.address;   // 新增
v.state = ...;
v.lastDelayMs = m.lastDelayMs;
v.lastAlive = m.lastAlive;
v.lastError = m.lastError;
v.failStreak = m.failStreak;
v.probed = m.probed;
out.push_back(v);
```

### Task 1.3: AppController.h 新增 MonitorType / UnifiedMonitorRow / getUnifiedMonitorRows

**文件**：`src/ui/AppController.h`

在 `StandaloneMonitorRow` 定义附近新增：

```cpp
enum class MonitorType {
    Standalone,
    Pool
};

struct UnifiedMonitorRow {
    MonitorType type;
    std::string indexId;
    std::string tag;
    std::string host;
    int socksPort;
    int64_t pid;
    int64_t durationMs;
    std::string state;
    long long lastDelayMs;
    bool lastAlive;
    int failStreak;
    std::string lastError;
};

// 公开接口（唯一新增业务接口）
std::vector<UnifiedMonitorRow> getUnifiedMonitorRows();
```

### Task 1.4: AppController.cpp 实现 getUnifiedMonitorRows

**文件**：`src/ui/AppController.cpp`（放在池接口区之后，约 :1898 后）

```cpp
std::vector<UnifiedMonitorRow> AppController::getUnifiedMonitorRows() {
    std::vector<UnifiedMonitorRow> rows;

    // 1) 独立代理（standaloneMutex_ 内快照）
    const std::vector<StandaloneMonitorRow> standalone = getWatchedStandaloneMonitors();
    for (const StandaloneMonitorRow& s : standalone) {
        UnifiedMonitorRow r;
        r.type = MonitorType::Standalone;
        r.indexId = s.indexId;
        r.host = s.host;
        r.socksPort = s.socksPort;
        r.pid = s.pid;
        r.durationMs = s.durationMs;
        r.state = L"运行中";   // 见下方编码说明
        rows.push_back(r);
    }

    // 2) 池成员（poolMutex_ 内快照）
    const std::vector<proxy::PoolMemberView> members = getPoolMembers();
    for (const proxy::PoolMemberView& m : members) {
        UnifiedMonitorRow r;
        r.type = MonitorType::Pool;
        r.indexId = m.indexId;
        r.tag = m.tag;
        r.host = m.host;
        r.state = m.state;
        r.lastDelayMs = m.lastDelayMs;
        r.lastAlive = m.lastAlive;
        r.failStreak = m.failStreak;
        r.lastError = m.lastError;
        rows.push_back(r);
    }

    return rows;
}
```

> **编码说明**：`UnifiedMonitorRow::state` 使用 `std::string`（UTF-8）。独立行 state 填 `"\u8fd0\u884c\u4e2d"`（运行中）；池行 state 直接取 `m.state`（"active"/"remove-requested"/"draining"）。UI 层显示时对池行做映射（active→运行中 等，见 Task 2.4）。

### Task 1.5: getUnifiedMonitorRows 单元测试

**文件**：`tests/TestUnifiedMonitorRows.cpp`（新增，Google Test）

依赖 `test/guindb.db`（测试数据库）。注入一条独立代理 + 一个池成员，断言：

- 行数 = 2
- 第一行 type == MonitorType::Standalone，state == "运行中"
- 第二行 type == MonitorType::Pool，host 非空
- 排序：Standalone 在前

> 若独立代理注入依赖真实进程，则退化为仅验证池成员映射 + 空独立列表场景（见测试代码注释）。

---

## Phase 2 — UI 层

### Task 2.1: StandaloneFloatingWidget.h 扩展

**文件**：`src/ui/StandaloneFloatingWidget.h`

1. `rows_` 类型改为 `std::vector<UnifiedMonitorRow>`（需 include AppController.h 已含）。
2. 新增私有成员：

```cpp
wxButton* startStopBtn_{nullptr};   // 启动池/停止池
wxButton* addBtn_{nullptr};         // 添加代理
wxButton* refreshBtn_{nullptr};     // 刷新
wxStaticText* poolStatusText_{nullptr};  // 代理池状态文本
wxCheckBox* reportChk_{nullptr};    // 上报健康
wxCheckBox* pruneChk_{nullptr};     // 自动剔除死亡
wxCheckBox* optimizeChk_{nullptr};  // 自动优化
```

3. 新增事件处理器声明：

```cpp
void onStartStopPool(wxCommandEvent& event);
void onAddPoolMember(wxCommandEvent& event);
void onRefreshPool(wxCommandEvent& event);
void onToggleReport(wxCommandEvent& event);
void onTogglePrune(wxCommandEvent& event);
void onToggleOptimize(wxCommandEvent& event);
void onMenuRemovePoolMember(wxCommandEvent& event);
void onPoolMembersUpdated(PoolMembersUpdatedEvent& event);
void updatePoolStatusText();
```

4. include 增加：`wx/button.h`、`wx/checkbox.h`、`wx/stattext.h`。

### Task 2.2: StandaloneFloatingWidget.cpp 匿名命名空间扩展

**文件**：`src/ui/StandaloneFloatingWidget.cpp`

1. `ColumnId` 扩展为 8 列：

```cpp
enum ColumnId {
    COL_TYPE = 0,        // 类型 60
    COL_INDEX_ID,        // 标识 200
    COL_HOST,            // Host 130
    COL_SOCKS_PORT,      // 监听端口 80
    COL_STATE,           // 状态 90
    COL_DELAY_MS,        // 延迟(ms) 80
    COL_FAIL_STREAK,     // 失败次数 60
    COL_PID              // PID 80
};
```

2. `MenuId` 增加：

```cpp
ID_MENU_REMOVE_POOL_MEMBER = wxID_HIGHEST + 502,
```

### Task 2.3: 构造函数改造（8 列 + 控件区 + Bind）

**文件**：`src/ui/StandaloneFloatingWidget.cpp` 构造函数

1. 列表列定义改为 8 列（宽度：60/200/130/80/90/80/60/80）。
2. 新增顶部控件区（在 panelSizer 中，list_ 之前）：

```cpp
// 状态文本
poolStatusText_ = new wxStaticText(this, wxID_ANY, L"代理池状态: 未运行");
// 第一行按钮
startStopBtn_ = new wxButton(this, wxID_ANY, L"启动池");
addBtn_ = new wxButton(this, wxID_ANY, L"添加代理");
refreshBtn_ = new wxButton(this, wxID_ANY, L"刷新");
// 第二行 checkbox
reportChk_ = new wxCheckBox(this, wxID_ANY, L"上报健康");
pruneChk_ = new wxCheckBox(this, wxID_ANY, L"自动剔除死亡");
optimizeChk_ = new wxCheckBox(this, wxID_ANY, L"自动优化");
```

3. Bind 新增：

```cpp
startStopBtn_->Bind(wxEVT_BUTTON, &StandaloneFloatingWidget::onStartStopPool, this);
addBtn_->Bind(wxEVT_BUTTON, &StandaloneFloatingWidget::onAddPoolMember, this);
refreshBtn_->Bind(wxEVT_BUTTON, &StandaloneFloatingWidget::onRefreshPool, this);
reportChk_->Bind(wxEVT_CHECKBOX, &StandaloneFloatingWidget::onToggleReport, this);
pruneChk_->Bind(wxEVT_CHECKBOX, &StandaloneFloatingWidget::onTogglePrune, this);
optimizeChk_->Bind(wxEVT_CHECKBOX, &StandaloneFloatingWidget::onToggleOptimize, this);
Bind(wxEVT_POOL_MEMBERS_UPDATED, &StandaloneFloatingWidget::onPoolMembersUpdated, this);
```

4. checkbox 初始值：`cfg_.standalone_pool.evaluate` 三字段回填。

### Task 2.4: refreshRows 改 getUnifiedMonitorRows

**文件**：`src/ui/StandaloneFloatingWidget.cpp`（refreshRows :682-722）

```cpp
void StandaloneFloatingWidget::refreshRows() {
    if (!controller_) return;
    list_->DeleteAllItems();
    rows_ = controller_->getUnifiedMonitorRows();
    for (size_t i = 0; i < rows_.size(); ++i) {
        const UnifiedMonitorRow& r = rows_[i];
        wxString typeText = (r.type == MonitorType::Pool) ? L"代理池" : L"独立";
        wxString stateText;
        if (r.type == MonitorType::Pool) {
            if (r.state == "active") stateText = L"运行中";
            else if (r.state == "remove-requested") stateText = L"待移除";
            else if (r.state == "draining") stateText = L"排空中";
            else stateText = wxString::FromUTF8(r.state);
        } else {
            stateText = L"运行中";
        }
        wxString delayText = (r.lastDelayMs > 0) ? wxString::Format(L"%lld", r.lastDelayMs) : L"-";
        wxString failText = (r.type == MonitorType::Pool) ? wxString::Format(L"%d", r.failStreak) : L"-";
        wxString pidText = (r.pid >= 0) ? wxString::Format(L"%lld", r.pid) : L"-";
        wxString portText = (r.socksPort > 0) ? wxString::Format(L"%d", r.socksPort) : L"-";
        wxString hostText = r.host.empty() ? L"-" : wxString::FromUTF8(r.host);
        wxString idText = r.indexId.empty() ? L"-" : wxString::FromUTF8(r.indexId);

        long idx = list_->InsertItem(static_cast<long>(i), typeText);
        list_->SetItem(idx, COL_INDEX_ID, idText);
        list_->SetItem(idx, COL_HOST, hostText);
        list_->SetItem(idx, COL_SOCKS_PORT, portText);
        list_->SetItem(idx, COL_STATE, stateText);
        list_->SetItem(idx, COL_DELAY_MS, delayText);
        list_->SetItem(idx, COL_FAIL_STREAK, failText);
        list_->SetItem(idx, COL_PID, pidText);
    }
    updatePoolStatusText();
    if (mode_ == Mode::Orb) Refresh();
}
```

### Task 2.5: onContextMenu 统一右键

**文件**：`src/ui/StandaloneFloatingWidget.cpp`（onContextMenu :533-561）

```cpp
void StandaloneFloatingWidget::onContextMenu(wxContextMenuEvent& event) {
    wxMenu menu;
    contextMenuSel_ = -1;
    if (mode_ == Mode::Panel) {
        long sel = list_->GetNextItem(-1, wxLIST_STATE_SELECTED);
        if (sel == -1) {
            wxPoint client = ScreenToClient(wxGetMousePosition());
            int flags = 0;
            sel = list_->HitTest(client, flags);
            if (sel != -1) list_->SetItemState(sel, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        }
        contextMenuSel_ = sel;
        if (sel >= 0 && sel < static_cast<long>(rows_.size())) {
            const UnifiedMonitorRow& r = rows_[sel];
            if (r.type == MonitorType::Standalone) {
                menu.Append(ID_MENU_CLOSE_PROXY, L"关闭代理");
                Bind(wxEVT_MENU, &StandaloneFloatingWidget::onMenuCloseProxy, this, ID_MENU_CLOSE_PROXY);
            } else {
                menu.Append(ID_MENU_REMOVE_POOL_MEMBER, L"删除成员");
                Bind(wxEVT_MENU, &StandaloneFloatingWidget::onMenuRemovePoolMember, this, ID_MENU_REMOVE_POOL_MEMBER);
            }
            menu.AppendSeparator();
            menu.Append(ID_MENU_LOCATE_PROXY, L"定位到代理列表");
            Bind(wxEVT_MENU, &StandaloneFloatingWidget::onMenuLocateProxy, this, ID_MENU_LOCATE_PROXY);
        }
    }
    menu.Append(ID_MENU_TEST_ONLINE, L"测试在线代理");
    Bind(wxEVT_MENU, &StandaloneFloatingWidget::onMenuTestOnline, this, ID_MENU_TEST_ONLINE);
    menu.AppendSeparator();
    menu.Append(wxID_EXIT, L"退出");
    Bind(wxEVT_MENU, &StandaloneFloatingWidget::onMenuExit, this, wxID_EXIT);
    PopupMenu(&menu);
}
```

> 新增 `ID_MENU_LOCATE_PROXY = wxID_HIGHEST + 503` 与 `onMenuLocateProxy`（复用 onItemSelected 的 LocateProxyEvent 逻辑）。

### Task 2.6: 新增池控件行为处理器

**文件**：`src/ui/StandaloneFloatingWidget.cpp`

```cpp
void StandaloneFloatingWidget::onStartStopPool(wxCommandEvent&) {
    if (!controller_) return;
    if (controller_->isProxyPoolRunning()) {
        controller_->stopProxyPool();
        startStopBtn_->SetLabel(L"启动池");
    } else {
        if (!controller_->startProxyPool()) {
            wxMessageBox(L"代理池启动失败", L"错误", wxOK | wxICON_ERROR, this);
            return;
        }
        startStopBtn_->SetLabel(L"停止池");
    }
    refreshRows();
}

void StandaloneFloatingWidget::onAddPoolMember(wxCommandEvent&) {
    if (!controller_) return;
    if (!controller_->isProxyPoolRunning()) {
        if (!controller_->startProxyPool()) {
            wxMessageBox(L"代理池启动失败，无法添加代理…", L"错误", wxOK | wxICON_ERROR, this);
            return;
        }
        startStopBtn_->SetLabel(L"停止池");
    }
    AddPoolMemberDialog dlg(this, controller_);
    if (dlg.ShowModal() != wxID_OK) return;
    const std::vector<std::string>& ids = dlg.getSelectedIndexIds();
    int added = 0;
    for (const std::string& id : ids) {
        if (controller_->injectProxyToPool(id)) ++added;
    }
    if (added > 0) {
        wxMessageBox(wxString::Format(L"已添加 %d 个代理到代理池。", added), L"提示", wxOK, this);
    } else {
        wxMessageBox(L"没有代理被成功加入代理池。", L"提示", wxOK, this);
    }
    refreshRows();
}

void StandaloneFloatingWidget::onRefreshPool(wxCommandEvent&) {
    if (!controller_) return;
    if (controller_->isProxyPoolRunning()) controller_->probeNow();
    refreshRows();
}

void StandaloneFloatingWidget::onToggleReport(wxCommandEvent&) {
    if (controller_) controller_->setPoolReportHealth(reportChk_->GetValue());
}
void StandaloneFloatingWidget::onTogglePrune(wxCommandEvent&) {
    if (controller_) controller_->setPoolAutoPruneDead(pruneChk_->GetValue());
}
void StandaloneFloatingWidget::onToggleOptimize(wxCommandEvent&) {
    if (controller_) controller_->setPoolAutoOptimize(optimizeChk_->GetValue());
}

void StandaloneFloatingWidget::onMenuRemovePoolMember(wxCommandEvent&) {
    if (!controller_ || contextMenuSel_ < 0 ||
        contextMenuSel_ >= static_cast<long>(rows_.size())) return;
    const UnifiedMonitorRow& r = rows_[contextMenuSel_];
    if (r.type != MonitorType::Pool) return;
    controller_->removePoolMember(r.indexId, true);
    refreshRows();
}

void StandaloneFloatingWidget::onMenuLocateProxy(wxCommandEvent&) {
    if (!controller_ || !locateTarget_ || contextMenuSel_ < 0 ||
        contextMenuSel_ >= static_cast<long>(rows_.size())) return;
    const UnifiedMonitorRow& r = rows_[contextMenuSel_];
    if (r.indexId.empty()) return;
    wxQueueEvent(locateTarget_, new LocateProxyEvent(r.indexId));
}

void StandaloneFloatingWidget::onPoolMembersUpdated(PoolMembersUpdatedEvent& event) {
    event.Skip();
    if (mode_ == Mode::Panel) refreshRows();
}

void StandaloneFloatingWidget::updatePoolStatusText() {
    if (!poolStatusText_) return;
    if (!controller_) return;
    bool running = controller_->isProxyPoolRunning();
    int memberCount = 0;
    int aliveCount = 0;
    for (const UnifiedMonitorRow& r : rows_) {
        if (r.type == MonitorType::Pool) {
            ++memberCount;
            if (r.lastAlive) ++aliveCount;
        }
    }
    wxString text = running ? L"代理池状态: 运行中" : L"代理池状态: 未运行";
    text += wxString::Format(L"  成员数: %d  在线代理: %d", memberCount, aliveCount);
    poolStatusText_->SetLabel(text);
}
```

> 注意：`AppController` 需暴露 `probeNow()`（池探活）。若不存在则新增转发方法（见 Task 2.7）。

### Task 2.7: AppController 暴露 probeNow

**文件**：`src/ui/AppController.h/.cpp`

```cpp
// AppController.h 公开区
void probePoolNow();

// AppController.cpp
void AppController::probePoolNow() {
    std::lock_guard<std::mutex> lock(poolMutex_);
    if (proxyPool_) proxyPool_->probeNow();
}
```

### Task 2.8: applyShape Panel 尺寸 600×480

**文件**：`src/ui/StandaloneFloatingWidget.cpp`（applyShape :751-752）

```cpp
w = FromDIP(600);
h = FromDIP(480);
```

### Task 2.9: applySettings 扩展

**文件**：`src/ui/StandaloneFloatingWidget.cpp`（applySettings :317）

在现有逻辑后追加：

```cpp
// 池控件启用状态（依 standalone_pool.enabled）
bool poolEnabled = cfg_.standalone_pool.enabled;
startStopBtn_->Enable(poolEnabled);
addBtn_->Enable(poolEnabled);
refreshBtn_->Enable(poolEnabled);
reportChk_->Enable(poolEnabled);
pruneChk_->Enable(poolEnabled);
optimizeChk_->Enable(poolEnabled);
// checkbox 初始值依新配置回填
reportChk_->SetValue(cfg_.standalone_pool.evaluate.report_health);
pruneChk_->SetValue(cfg_.standalone_pool.evaluate.auto_prune_dead);
optimizeChk_->SetValue(cfg_.standalone_pool.evaluate.auto_optimize);
```

> 需确认 `config::StandalonePoolConfig::evaluate` 字段名（report_health/auto_prune_dead/auto_optimize），以实际头文件为准。

### Task 2.10: MainFrame 清理

**文件**：`src/ui/MainFrame.h/.cpp`

1. 删 `ID_MENU_OPEN_POOL` 枚举（:40）。
2. 删 `EVT_MENU(ID_MENU_OPEN_POOL, onMenuOpenPool)`（:100）。
3. 删构造函数 `Bind(wxEVT_POOL_MEMBERS_UPDATED, ...)`（:136）。
4. 删析构 `poolDialog_->Destroy()`（:579-582）。
5. 删菜单 `proxyMenu_->Append(ID_MENU_OPEN_POOL, L"代理池…", ...)`（:728）。
6. 删 `onMenuOpenPool`（:1114-1126）与 `onPoolMembersUpdated`（:1128-1133）。
7. 删 `poolDialog_` 成员与前置声明 `class StandalonePoolDialog;`。
8. 删 `#include "StandalonePoolDialog.h"`（MainFrame.cpp:4）。
9. 删 `onMenuStandaloneMonitor` 中 `syncFloatingWidgetControls()` 调用（保留 toggleActive）。

### Task 2.11: 删除 StandalonePoolDialog

**文件**：`src/ui/StandalonePoolDialog.h`、`src/ui/StandalonePoolDialog.cpp`

删除两个文件，并从 `CMakeLists.txt` UI 源文件注册（:284-287）移除对应两行。

---

## Phase 3 — 测试与文档

### Task 3.1: UIIds.h 迁移

**文件**：`tests/ui/UIIds.h`

- 删 `PoolDialogName=L"独立代理池"`、`PoolOpenCmd=6000+115`。
- `PoolStartStopBtnName`/`PoolStopBtnName` 改名 `MonitorPanelPoolStartBtnName`/`MonitorPanelPoolStopBtnName`（按钮文本不变）。
- `PoolRefreshBtnName`/`PoolAddBtnName` 迁移 `MonitorPanel*` 前缀。
- 删 `PoolDeleteBtnName`（删除走右键）。
- 保留 `PoolPickerName=L"选择代理"`、`FloatingWidgetName=L"StandaloneFloatingWidget"`。

### Task 3.2: TestStandaloneProxyPool.cpp 改写

**文件**：`tests/ui/TestStandaloneProxyPool.cpp`

- 打开入口：由 `PostMessage(MAKEWPARAM(PoolOpenCmd,0))` 改为显示悬浮窗 Panel（复用 `FloatingWidgetName` + 展开/Show+Raise）。
- 用例语义保留：启动池→添加代理（PoolPickerName）→列表出现「代理池」类型行→右键删除成员→停止池。
- 断言增加类型列文本「独立」/「代理池」。

### Task 3.3: CMakeLists.txt 更新

- UI 源文件注册移除 StandalonePoolDialog 两行。
- UI 测试注册：`UI_POOL` 测试名与 tag 保持 `[pooldialog]`（或按实施结果调整）。

### Task 3.4: docs/INDEX.md 登记

- 更新 Spec-ProxyPoolMonitorUnify-v1.0 状态为「已实施」。
- 登记本 Plan 文档路径。

### Task 3.5: 构建与验证

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
ctest -V
ctest -R "UI_POOL|Dedup" -V
grep -r "StandalonePoolDialog\|ID_MENU_OPEN_POOL\|PoolOpenCmd\|PoolDialogName" --include="*.cpp" --include="*.h" .
```

---

## 验收清单（对应 Spec §8）

- [ ] Debug 构建 0 error
- [ ] ctest -V 全绿
- [ ] grep 无残留（docs/ 除外）
- [ ] 手工验证：类型列/启停池/添加/右键删除/刷新/3 checkbox/Orb 计数/禁用灰化/TerminateProcess 兜底
- [ ] INDEX 行 43 更新