# Bugfix: SubscriptionPanel / ProxyListPanel 双击无反应

## Problem

订阅面板（`SubscriptionPanel`）和代理列表面板（`ProxyListPanel`）的双击操作无效。用户双击订阅项或代理项时无任何响应：
- 双击订阅 → 应打开编辑对话框（`showEditDialog()`）
- 双击代理 → 应启动独立代理测试（`onStartProxy()`）

## Root Cause

wxWidgets 3.2 MSW 移植（`datavgen.cpp:2091`）在注册窗口类时**未设置 `CS_DBLCLKS`** 类样式：

```cpp
// E:\eclipse_workspace\wxWidgets\src\generic\datavgen.cpp ~line 2091
RegisterClass(&wc);  // wc.style = 0, 缺少 CS_DBLCLKS
```

没有 `CS_DBLCLKS`，Windows 系统永远不会向该窗口发送 `WM_LBUTTONDBLCLK` 消息，因此：
- `wxDataViewMainWindow` 中的 `LeftDClick()` 处理函数永远不会被调用
- `wxEVT_DATAVIEW_ITEM_ACTIVATED` 事件仅在键盘 Enter 键时触发（`OnChar` handler）
- `Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, ...)` 方式在双击时完全无效

这是 wxWidgets 3.2 generic DataViewCtrl 的已知 MSW 平台限制。wx ≥ 3.3 已修复。

## Fix

使用 selection-change based double-click detection 方案，绕过底层窗口消息限制。

**原理**：在单选择模式下（`wxDV_SINGLE`），DataViewCtrl 在用户点击已选中行时会重新发送 `SendSelectionChangedEvent`。通过追踪上一次选中的 item 和时间，在 500ms 内对同一行的第二次选中即可判定为双击。

### 修改文件

| File | Changes |
|------|---------|
| `include/SubscriptionPanel.h` | 新增 `wxDataViewItem lastSelItem_`、`wxLongLong lastSelTime_{0}` |
| `src/ui/SubscriptionPanel.cpp` | 新增 `DBLCLICK_TIMEOUT_MS` 常量（500ms）；`onSelectionChanged()` 中添加双击检测逻辑，检测到双击时调用 `onEditSubscription()` |
| `include/ProxyListPanel.h` | 新增 `wxDataViewItem lastSelItem_`、`wxLongLong lastSelTime_{0}` |
| `src/ui/ProxyListPanel.cpp` | 新增 `DBLCLICK_TIMEOUT_MS` 常量（500ms）；`onSelectionChanged()` 中添加双击检测逻辑，检测到双击时调用 `onStartProxy()` |

### 关键代码

```cpp
wxLongLong now = wxGetLocalTimeMillis();
if (lastSelItem_.IsOk() && item.IsOk() &&
    lastSelItem_.GetID() == item.GetID() &&
    now - lastSelTime_ < DBLCLICK_TIMEOUT_MS) {
    // 双击检测命中 — 触发操作
    wxCommandEvent dummy;
    onEditSubscription(dummy);  // 或 onStartProxy(dummy)
    // Reset 防止三次点击重复触发
    lastSelItem_ = wxDataViewItem();
    lastSelTime_ = 0;
} else {
    lastSelItem_ = item;
    lastSelTime_ = now;
}
```

## Scope

- 仅影响 UI 双击检测逻辑
- 右键菜单功能不受影响，保持原样
- 面板原有功能（左键选中、右键菜单）保持不变

## Verification

- **编译**: 纯新增代码，零错误零警告
- **LSP 诊断**: 两个修改文件均无错误
- **运行时**: 双击订阅项 → 弹出编辑对话框；双击代理项 → 弹出端口检测/启动流程
