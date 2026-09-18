# ProxyListPanel「测试在线代理」结果弹窗单击定位设计

- **版本**: 1.1
- **日期**: 2026-09-02
- **状态**: Draft / 待评审
- **涉及模块**: `src/ui/TestOnlineResultDialog.h/.cpp`(新建)、`src/ui/ProxyListPanel.h/.cpp`、`src/ui/SubscriptionPanel.h/.cpp`、`src/ui/SubscriptionListModel.h/.cpp`、`src/ui/MainFrame.h/.cpp`、`src/ui/AppController.h/.cpp`、`src/ui/Events.h/.cpp`、`CMakeLists.txt`

## 1. 目标

在「测试在线代理」功能完成后，将失败代理列表的纯文本 `wxMessageBox` 弹窗改造为**可单击的列表对话框**：每条失败记录独占一行，用户单击某行时，**同时在订阅窗口（`SubscriptionPanel`）与代理列表面板（`ProxyListPanel`）定位**对应的代理：订阅窗口选中该代理所属的订阅行，代理列表选中并滚动到该代理记录，然后关闭弹窗。

## 2. 背景

当前 `onTestOnlineProxiesEvent`（`src/ui/ProxyListPanel.cpp` 约 490-516 行）在处理测试完成事件时：

- `total <= 0`：`wxMessageBox` 提示"当前没有正在运行的独立代理进程。"
- 有成功但有失败：将 `failedIndexIds` 拼成纯文本（最多 20 条 + 省略），`wxMessageBox(..., wxICON_WARNING)` 展示
- 全成功：`wxMessageBox(..., wxICON_INFORMATION)` 提示通过

缺点：失败列表为纯文本，无法与订阅窗口/代理列表面板联动。用户无法快速跳转到某个失败代理查看详情及其所属订阅。本需求通过引入一个可单击列表对话框解决，单击时在两侧面板同时定位。

## 3. 设计约束

| 约束 | 说明 |
|------|------|
| 禁 `auto` 类型推导 | 全栈 C++17 约束，完整写出类型 |
| 定位复用现有方法 | `ProxyListPanel::selectProxyByIndexId(const std::string&)` 已存在，公有，直接复用 |
| 订阅定位新建 | `SubscriptionPanel` 当前无「按 subId 定位行」方法，需新增 `selectSubBySubId(const std::string&)`，底用 `SubscriptionListModel::findRowBySubId` |
| 解耦定位链路 | 复用既有 `LocateProxyEvent`（`wxEVT_LOCATE_PROXY`）：对话框不直接持有订阅/代理面板，只发事件到顶层 `MainFrame` 统一处理 |
| MainFrame 统一指令 | 扩展 `wxEVT_LOCATE_PROXY` 处理器为**双定位**（订阅 + 代理），一处改动赋能所有定位入口 |
| 不阻塞 UI | 对话框为模态对话框，仅用于展示最终失败列表；工作线程测试逻辑不变 |
| 保持既有分支 | `total==0` 与全成功两个 `wxMessageBox` 分支保持原样 |
| 遵循对话框模式 | 复用 `AddPoolMemberDialog` 的 wxDialog + wxListCtrl 布局模式 |
| 新文件须入 CMake | `src/ui/UI_SOURCES` 显式列出所有源文件，新增 .h/.cpp 必须登记 |
| 先文档后代码 | 方案评审通过后再实施 |

## 4. 交互语义

- 「失败记录」= 测试在线代理后 `TestOnlineProxiesEvent.failedIndexIds` 中的每个 `indexId`（`std::vector<std::string>`）。
- 对话框首次打开时一次性展示全部失败记录；为避免列表过长，采用与原有同样的**上限截断**策略——仅当用户滚动或全部失败数较大时仍展示但对话框可滚动。设计决策：**全部失败记录都放入 wxListCtrl**（wxListCtrl 本身可滚动，无需截断），展示所有失败项，用户可滚动查看任意一条并定位。这与原纯文本弹窗的 20 条上限不同——列表天然支持滚动，故不截断。
- 交互：
  1. 单击某行（`EVT_LIST_ITEM_SELECTED`）→ 取该行对应的 `indexId` → 解析其所属订阅 `subId` → 发送标准 `LocateProxyEvent` 到顶层 `MainFrame` → `MainFrame` 统一处理：**订阅窗口** `selectSubBySubId(subId)` 选中该订阅行 + **代理列表** `selectProxyByIndexId(indexId)` 选中并滚动该代理 → 关闭对话框（`EndModal(wxID_OK)`）。
  2. 单击「关闭」按钮 → 仅关闭对话框，不定位。
  3. 关闭对话框后 `ProxyListPanel` 已停留在被定位的代理行、`SubscriptionPanel` 已停留在被定位的订阅行，用户可直接查看详情。
- **行为统一说明**：扩展 `wxEVT_LOCATE_PROXY` 处理器后，既有 `StandaloneMonitorDialog` 双击定位（原仅定位代理列表）现在也会同时定位所属订阅行——这是**预期的统一行为**（同属「定位此代理」意图），非回归。

## 5. 类设计

### 5.1 新增 `TestOnlineResultDialog`（`src/ui/TestOnlineResultDialog.h` / `src/ui/TestOnlineResultDialog.cpp`）

`class TestOnlineResultDialog : public wxDialog`：

```cpp
class TestOnlineResultDialog : public wxDialog {
public:
    TestOnlineResultDialog(wxWindow* parent,
                           const std::vector<std::string>& failedIndexIds,
                           int total, int success, int failed);
private:
    void onItemSelected(wxListEvent& event);   // EVT_LIST_ITEM_SELECTED
    void onClose(wxCommandEvent& event);
    void buildList();

    std::vector<std::string> failedIndexIds_;  // 构造初始化列表存参，供 buildList
    std::vector<std::string> rowIndexIds_;     // 行 -> indexId 映射
};
```

**构造**：`wxDialog(parent, wxID_ANY, L"测试在线代理 - 失败列表", wxDefaultPosition, wxSize(520, 400))`

- 根 `wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);`
- 顶部提示 `wxStaticText`：`wxString::Format(L"在线代理测试完成：共 %d，成功 %d，失败 %d。单击下方记录可在订阅与代理列表中定位。", total, success, failed)`，`root->Add(hint, 0, wxALL, 8)`
- `wxListCtrl* list_`：`new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_HRULES | wxLC_VRULES | wxLC_SINGLE_SEL)`
  - `list_->InsertColumn(0, L"失败代理 (indexId)", wxLIST_FORMAT_LEFT, 480)`
  - `root->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8)`
  - `list_->Bind(wxEVT_LIST_ITEM_SELECTED, &TestOnlineResultDialog::onItemSelected, this);`
- 底部按钮行：`wxButton* closeBtn = new wxButton(this, wxID_OK, L"关闭");` → `#include <wx/button.h>`；`closeBtn->Bind(wxEVT_BUTTON, &TestOnlineResultDialog::onClose, this);` → `root->Add(btnRow, 0, wxALIGN_RIGHT | wxALL, 8)`
- `SetSizerAndFit(root)`
- 末尾 `buildList()`

**`buildList()`**：`list_->DeleteAllItems(); rowIndexIds_.clear();` → 对 `failedIndexIds_` 逐个 `long row = list_->InsertItem(list_->GetItemCount(), wxString::FromUTF8(id)); rowIndexIds_.push_back(id);`（单列，仅失败 indexId）

**`onItemSelected(wxListEvent& event)`**：`long idx = event.GetIndex(); if (idx < 0 || idx >= static_cast<long>(rowIndexIds_.size())) return; const std::string& indexId = rowIndexIds_[idx]; wxQueueEvent(wxGetTopLevelParent(this), new LocateProxyEvent(indexId)); EndModal(wxID_OK);`

即**发 `LocateProxyEvent`**（复用既有事件，含 `getIndexId()` 与 `Clone()`），由 `MainFrame` 统一做订阅 + 代理双定位。对话框不直接持有订阅/代理面板，保持解耦。需 `#include "Events.h"` 与 `<wx/toplevel.h>` 中的 `wxGetTopLevelParent`。

**`onClose(wxCommandEvent&)`**：`EndModal(wxID_OK);`

### 5.2 修改 `onTestOnlineProxiesEvent`（`src/ui/ProxyListPanel.cpp` 约 490-516 行）

`failed > 0` 分支由纯文本 `wxMessageBox` 改为：

```cpp
TestOnlineResultDialog dlg(this, failedIndexIds, total, success, failed);
dlg.ShowModal();
```

即新建 `TestOnlineResultDialog` 并 `ShowModal()`。关闭后 `ProxyListPanel` 已在测试完成时 `refreshResults()` 刷新完毕，`MainFrame` 的定位回调基于当前模型查找。

保持 `total == 0` 与全成功分支不变。

### 5.3 扩展 `MainFrame` 的 `wxEVT_LOCATE_PROXY` 处理（`src/ui/MainFrame.cpp` 约 247-251 行）

现有处理器仅 `proxyPanel_->selectProxyByIndexId(evt.getIndexId())`。扩为**双定位**：

```cpp
// MainFrame.cpp 现有 Bind 处理器 onLocateProxy（或等价命名）改为：
const std::string indexId = evt.getIndexId();
proxyPanel_->selectProxyByIndexId(indexId);               // 代理列表定位
const std::string subId = controller_->getSubIdByProxyIndexId(indexId);
subPanel_->selectSubBySubId(subId);                       // 订阅窗口定位
```

`getSubscriptionPanel()`/`getProxyListPanel()` 已在 `MainFrame.h:47-48` 提供。一处改动同时赋能 `TestOnlineResultDialog` 与既有 `StandaloneMonitorDialog` 两处定位入口。

### 5.4 新增 `SubscriptionPanel::selectSubBySubId`（`src/ui/SubscriptionPanel.h/.cpp`）

公有方法：

```cpp
void SubscriptionPanel::selectSubBySubId(const std::string& subId) {
    if (!model_) return;
    const long row = model_->findRowBySubId(subId);
    if (row < 0) return;
    wxDataViewItem item = model_->GetItem(static_cast<unsigned int>(row));
    listCtrl_->Select(item);
    listCtrl_->EnsureVisible(item);
}
```

`findRowBySubId` 返回 -1（未找到）即静默跳过，安全。`GetItem(unsiged)` 返回视图行对应的 `wxDataViewItem`（`wxDataViewIndexListModel` 的 item id = 视图行 + idOffset）。

### 5.5 新增 `SubscriptionListModel::findRowBySubId`（`src/ui/SubscriptionListModel.h/.cpp`）

对标 `ProxyListModel::findRowByIndexId`（`ProxyListModel.cpp:442-454`）实现，遍历**视图行** viewRow（0..count-1），用 `getDataIndex(viewRow)` 补偿排序/idOffset 得到 dataIdx，比较 `(*subscriptions_)[dataIdx].id == subId`，命中返回 viewRow，否则 -1。同时新增私有辅助 `getDataIndex(viewRow)`（镜像 `ProxyListModel::getDataIndex`，`ProxyListModel.cpp:457+`：`unsigned int dataIdx = GetItem(viewRow).GetID() - idOffset_;` 并做边界校验后返回）。

`Subitem.id` 即订阅标识（与 subId 同义），`GetValueByRow`/`Compare` 均基于 `sub.id`。

### 5.6 新增 `AppController::getSubIdByProxyIndexId`（`src/ui/AppController.h/.cpp`）

公有或私有方法（UI 线程查询）：

```cpp
std::string AppController::getSubIdByProxyIndexId(const std::string& indexId) {
    const std::optional<db::models::Profileitem> item = ProfileitemDAO::getByIndexId(indexId);
    return item ? item->subid : std::string();
}
```

返回该代理所属订阅 `subid`；未找到返回空串（`selectSubBySubId("")` 将 `findRowBySubId` 未命中静默跳过）。需包含 `Profileitem.h`/`ProfileitemDAO`。

### 5.7 新增/复用 `LocateProxyEvent`（`src/ui/Events.h/cpp`，复用不改）

既有 `LocateProxyEvent`（`Events.h:344-355` + `wxEVT_LOCATE_PROXY`）：携带 `indexId`，提供 `getIndexId()` 与 `Clone()`。`TestOnlineResultDialog` 直接复用，无需改动。

### 5.8 既有 `selectProxyByIndexId`（不改动）

已在 `ProxyListPanel.cpp:319-326` 实现，公有，完成选中 + `EnsureVisible` 滚动。直接复用。

## 6. 文件清单

| 文件 | 操作 |
|------|------|
| `src/ui/TestOnlineResultDialog.h` | 新建（发 `LocateProxyEvent`） |
| `src/ui/TestOnlineResultDialog.cpp` | 新建 |
| `src/ui/ProxyListPanel.cpp` | 修改：onTestOnlineProxiesEvent 的 failed>0 分支改用对话框；新增 include |
| `src/ui/SubscriptionPanel.h` | 修改：新增公有 `selectSubBySubId(const std::string&)` 声明 |
| `src/ui/SubscriptionPanel.cpp` | 修改：实现 `selectSubBySubId` |
| `src/ui/SubscriptionListModel.h/.cpp` | 修改：新增 `findRowBySubId` 与私有 `getDataIndex` |
| `src/ui/MainFrame.cpp` | 修改：扩展 `wxEVT_LOCATE_PROXY` 处理器为订阅 + 代理双定位 |
| `src/ui/AppController.h` | 修改：新增 `getSubIdByProxyIndexId` 声明 |
| `src/ui/AppController.cpp` | 修改：实现 `getSubIdByProxyIndexId`；含 ProfileitemDAO |
| `CMakeLists.txt` | 修改：`UI_SOURCES` 登记 TestOnlineResultDialog.h/.cpp |
| `docs/design/2026-09-02-Design-ProxyListPanel-TestOnlineResultDialog-v1.0.md` | 新建（本文档） |

注：`Events.h/cpp`、`ProxyListPanel.h` **无需改动**——`LocateProxyEvent` 已存在，事件、菜单、测试链路均已在上一个功能完成。

## 7. 测试策略

- **单测（新增）**：
  - `SubscriptionListModel::findRowBySubId`：按 subId 命中返回视图行、未命中返回 -1（覆盖排序/过滤后视图行偏移正确性）。
  - `AppController::getSubIdByProxyIndexId`：按 indexId 返回所属订阅、未找到返回空（TDD，需 DB 测试库）。
- **手动验证**：
  1. 启动程序 → 开启若干独立代理（有连通的与不连通的）→ 右键菜单「测试在线代理」→ 失败列表对话框弹出，展示全部失败 indexId。
  2. 单击某条失败记录 → 对话框关闭，**订阅窗口**选中该代理所属订阅行，**代理列表**对应行被选中并滚动到可见。
  3. 「关闭」按钮 → 仅关闭，不定位。
  4. 全成功：仍为原信息弹窗。
  5. 无在线代理：仍为原"当前没有正在运行的独立代理进程。"弹窗。
  6. 回归既有 `StandaloneMonitorDialog` 双击：现也应同时定位订阅行（统一行为）。
- **回归**：`ctest --test-dir build -V` 全绿。

## 8. 风险

| 风险 | 缓解 |
|------|------|
| 失败列表很长导致弹窗过大 | 使用 `wxListCtrl`，天然滚动，不截断；对话框固定大小 `wxSize(520,400)` |
| `selectProxyByIndexId` 在 `refreshResults()` 后行索引变化 | `onTestOnlineProxiesEvent` 先 `refreshResults()` 再弹窗，模型已重建，`findRowByIndexId` 基于当前模型查找 |
| `failedIndexIds` 中的 indexId 在列表中不存在 | `selectProxyByIndexId` 内部 `findRowByIndexId` 返回 -1 时静默跳过，安全 |
| indexId 无对应订阅（查不到 subId） | `getSubIdByProxyIndexId` 返回空串，`selectSubBySubId("")` 静默跳过，不影响代理定位 |
| `FindRowBySubId` 未命中当前过滤子集 | `selectSubBySubId` 内部 `findRowBySubId` 返回 -1 时静默跳过，安全（订阅窗口当前过滤不包含该订阅） |
| 新文件漏登记 CMake | 明确加入 `UI_SOURCES` |
| 禁 `auto` | 完整写出类型（`std::vector<std::string>`、`long`、指针等） |

## 9. 验收标准

1. 失败列表以可滚动列表对话框展示（非纯文本）。
2. 单击某失败记录 → 订阅窗口选中所属订阅行 + `ProxyListPanel` 选中并滚动到该代理，对话框关闭。
3. 「关闭」按钮仅关闭不定位。
4. `total==0` 与全成功分支保持原有 `wxMessageBox`。
5. 既有 `StandaloneMonitorDialog` 双击定位行为升级为双定位且无回归。
6. 构建 0 error；`ctest --test-dir build -V` 全绿。