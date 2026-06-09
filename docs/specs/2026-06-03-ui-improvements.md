---
title: "feat: 弹窗居中 + ProxyDetail 可选择拷贝"
type: spec
status: completed

## 验证结果

- 编译通过
- 6 个测试套件全部通过

### 修改文件列表
- `src/ui/ConfigDialog.cpp` - 添加 `CentreOnScreen()` 居中显示
- `src/ui/ProxyDetailPanel.h` - 改用 `wxTextCtrl` 数组
- `src/ui/ProxyDetailPanel.cpp` - 使用 `SetValue` 设置只读文本控件内容
---

# UI 体验改进

## 需求

1. 所有弹出窗口、对话框居中显示
2. ProxyDetailPanel 内容支持鼠标选择和拷贝

## 设计

### D1: 对话框居中显示

```cpp
// 在对话框构造函数后添加:
CentreOnScreen();
// 或在 ShowModal 前添加:
CentreOnParent();
```

### D2: ProxyDetail 可选择拷贝

```cpp
// 将 wxStaticText 改为 wxTextCtrl 单行，只读模式
fieldLabels_[i] = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxDefaultSize,
                                 wxTE_READONLY | wxTE_MULTILINE);
```

## 实施计划

| 步骤 | 文件 | 变更 |
|------|------|------|
| 1 | src/ui/ConfigDialog.cpp | 添加 CentreOnScreen() 调用 |
| 2 | src/ui/ConfigDialog.h | 同 |
| 3 | src/ui/ProxyDetailPanel.cpp | 改用 wxTextCtrl 只读模式 |

## 验证步骤

1. 打开配置编辑器 → 居中显示
2. 点击代理行 → ProxyDetail 内容可选拷贝