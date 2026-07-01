# Spec: 对话框屏幕居中

## Problem

部分对话框（编辑订阅、确认删除、端口占用提示等）默认显示在父窗口（面板）的左上角区域，而不是屏幕中央。对于宽屏显示器而言，弹窗出现在屏幕角落的用户体验较差。

## Solution

在所有关键的 `wxDialog` / `wxMessageDialog` / `wxTextEntryDialog` 调用处，在 `ShowModal()` 前添加 `dlg.CentreOnScreen()`，使对话框在屏幕正中央显示。

### 改动范围

| File | Dialogs | Changes |
|------|---------|---------|
| `src/ui/SubscriptionPanel.cpp` | 编辑订阅对话框 (`wxDialog`) | `showEditDialog()` 中 `dlg.SetMinSize()` 后添加 `dlg.CentreOnScreen()` |
| | 删除代理确认 (`wxMessageDialog`) | `onDeleteProxies()` 中 `dlg.ShowModal()` 前添加 `dlg.CentreOnScreen()` |
| | 导入订阅 (`wxTextEntryDialog`) | `onImportSubscription()` 中 `dlg.ShowModal()` 前添加 `dlg.CentreOnScreen()` |
| | 删除订阅确认 (`wxMessageDialog`) | `confirmDelete()` 中 `dlg.ShowModal()` 前添加 `dlg.CentreOnScreen()` |
| `src/ui/ProxyListPanel.cpp` | 端口被占用警告 (`wxMessageDialog`) | 由 `wxMessageBox()` 静态调用改为 `wxMessageDialog` + `dlg.CentreOnScreen()` + `dlg.ShowModal()` |
| | 端口冲突询问 (`wxMessageDialog`) | 同上，`wxMessageBox()` → `wxMessageDialog` + `CentreOnScreen()` |

### 实现模式

```cpp
// 模式 A: 已有的 wxDialog/wxMessageDialog
wxMessageDialog dlg(this, msg, title, style);
dlg.CentreOnScreen();
if (dlg.ShowModal() == wxID_YES) { ... }

// 模式 B: 原 wxMessageBox 静态调用（需要展开）
wxMessageDialog dlg(this, msg, title, wxOK | wxICON_WARNING);
dlg.CentreOnScreen();
dlg.ShowModal();
```

## Scope

- 仅影响 UI 弹出层的显示位置
- 不影响对话框的内容、行为或返回值
- `wxMessageBox()` 静态调用被替换为等价的 `wxMessageDialog` + `ShowModal()` 模式以支持居中

## Verification

- **编译**: 零错误零警告
- **运行时**: 所有弹窗均在屏幕正中央显示
