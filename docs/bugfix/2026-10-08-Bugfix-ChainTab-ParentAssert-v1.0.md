---
title: "docs: bugfix — 组件链编辑 Tab 触发 wxWidgets parent 断言弹窗"
type: bugfix
status: completed
date: 2026-10-08
updated: 2026-10-08
version: v1.0
---

# Bugfix：组件链编辑 Tab 触发 wxWidgets Debug Alert

## 1. 现象

打开配置对话框 →「模板配置」→「组件链编辑」后弹出 **wxWidgets Debug Alert**：

```
A debugging check in this application has failed.
```

断言栈（从截图读取）：

```
[21] wxEvtHandler::TryHereOnly          ← 事件分发入口
[20] wxEventHashTable::HandleEvent
[19] wxEvtHandler::ProcessEventIfMatchesId
[12] <调用者>
[11] wxWindowBase::SetSizer            ← assert 点
[10] wxSizer::SetContainingWindow      （递归第 2 层）
[09] wxSizer::SetContainingWindow      （递归第 1 层）
[07] wxAbort
```

触发时机为事件处理过程中（栈底是 `ProcessEventIfMatchesId`），用户日志显示发生在启动后约 2 秒、`StandaloneProxyEvent` 到达前后。

## 2. 根因

上一轮为修复「JSON 预览框不可见」而重构布局时，**sizer 与控件的父子关系不匹配**。

`wxSizer::SetContainingWindow`（`wxWidgets/src/common/sizer.cpp:979`）在 Debug 下对每个子项执行：

```cpp
if (wxWindow* const w = item->GetWindow())
    ASSERT_WINDOW_PARENT_IS(w, m_containingWindow);
```

即「**sizer 管理的所有控件，其 parent 必须是该 sizer 所属的窗口**」。

重构后的代码把 parent 为 `chainTab` 的控件放进了 `editPanel` 的 sizer：

```cpp
// ✗ 错误：这些控件 parent = chainTab
wxStaticText* hint    = new wxStaticText(chainTab, ...);
wxButton*    saveBtn  = new wxButton(chainTab, ...);
chainStatus_          = new wxStaticText(chainTab, ...);
BundleChainEditor* chainEditor = new BundleChainEditor(chainTab, db_);
ComponentListPanel* componentPanel = new ComponentListPanel(chainTab, db_);

wxPanel* editPanel = new wxPanel(chainTab);
editSizer->Add(titleSizer, ...);      // ← titleSizer 里装的是 chainTab 的控件
editSizer->Add(bodySizer, ...);       // ← bodySizer 里也是 chainTab 的控件
editPanel->SetSizer(editSizer);       // ← 在此断言
```

`editPanel->SetSizer(editSizer)` 会递归 `SetContainingWindow(editPanel)`，遍历到 `chainEditor_` 时发现其 parent 是 `chainTab` ≠ `editPanel` → 断言失败。栈中两次 `SetContainingWindow` 递归帧正好对应 `editSizer → bodySizer` 两层嵌套。

## 3. 修复

`src/ui/ConfigDialog.cpp::createChainEditTab` 重构为「**先建 panel，再以 panel 为 parent 建控件**」：

| 控件 | 修复前 parent | 修复后 parent | 所属 sizer |
|---|---|---|---|
| `editHint` / `editSaveBtn` / `editStatus` | `chainTab` | **`editPanel`** | `editTitleSizer` / `editSizer` |
| `BundleChainEditor` | `chainTab` | **`editPanel`** | `editBodySizer` |
| `ComponentListPanel` | `chainTab` | **`editPanel`** | `editBodySizer` |
| `previewLabel` / `previewBox` | `previewPanel` | `previewPanel`（不变，本就正确） | `previewSizer` |

并删除旧的重复定义（原先在 `chainTab` 上建的那套控件/sizer 已整体移除），`chainEditor_` / `componentListPanel_` / `chainStatus_` 成员指针改为指向 `editPanel` 下的实例，`onResolved` lambda 只保留一份。

## 4. 验证

`tests/ui/TestChainPreview.cpp` 扩展为 2 个用例：

1. `ConfigDialog chain-edit tab renders a visible JSON preview box`（原有）
2. `ConfigDialog chain-edit tab resolves without wx assert`（新增）
   - `WM_COMMAND` 打开配置编辑器 → 断言无 Debug Alert
   - `Ctrl+PageDown` 逐页切到「组件链编辑」（以 UIA 检测「配置 JSON 预览」标签出现为判据）
   - `BM_CLICK` 点「解析」按钮
   - 枚举进程内窗口，断言标题不含 `Debug Alert` / `wxWidgets Debug`

| 项 | 结果 |
|---|---|
| `UI_CHAINPREVIEW`（2 用例） | **PASS**（25.35s） |
| UI_MAINWINDOW / SEARCH / CLEAR / POOL / EXITASSERT | 5/5 PASS |
| `test_config_generator` | 320 tests → 319 PASS + 1 SKIP + 0 FAIL |
| validproxy + UITests 构建 | 0 error |

## 5. 遗留说明

未能在本机外部自动化脚本中复现该断言：PowerShell 跨进程 `SendMessageTimeoutW` / `SendKeys` 被 UIPI 拦截（UI 测试二进制能成功是因为它在同一测试进程内用 `WM_COMMAND` 直接投递）。因此 RED 复现依赖代码审查定位（parent 与 sizer 所属窗口不一致），修复后的 GREEN 由 `UI_CHAINPREVIEW` 第二个用例守护——该用例在任何 parent 关系回退时都会因 Debug Alert 弹窗而失败。