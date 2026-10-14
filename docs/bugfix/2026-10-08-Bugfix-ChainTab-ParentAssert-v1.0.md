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

---

## 6. 追加：2026-10-13 同一断言被**再次**引入（三栏布局改造）

### 6.1 现象

按需求把「组件链编辑」页由「左列表 / 右列表 + 下方预览」改为**三栏水平排列**后，
**单击「配置」按钮即弹 wxWidgets Debug Alert**（断言发生在**对话框构造期**，非切 Tab 时）：

```
assert "!m_containingSizer" failed in SetContainingSizer():
Adding a window already in a sizer, detach it first!
栈：wxWindowBase::SetContainingSizer → wxSizer::SetContainingWindow(×2) → wxSizerItem::Insert → wxAbort
```

截图 `bin/log/802aa432-449c-4372-a010-b18b68e4d2a3.png`。

### 6.2 根因：SetContainingWindow 会**递归改写嵌套 sizer 的 containing window**

三栏改造时把 `previewPanel` 的 parent 从 `chainTab` 改成 `editPanel`（以便放进
`editBodySizer`），**但保留了它内部的 `previewPanel->SetSizer(previewSizer)`**。

`wxSizer::SetContainingWindow`（`wxWidgets/src/common/sizer.cpp:979-1007`）的实现是：

```cpp
void wxSizer::SetContainingWindow(wxWindow *win) {
    if ( win == m_containingWindow ) return;
    m_containingWindow = win;
    for ( const wxSizerItem* item : m_children ) {
        wxSizer *const sizer = item->GetSizer();
        if ( sizer ) sizer->SetContainingWindow(win);          // ← 递归进子 panel 的 sizer
        if ( m_containingWindow ) {
            if ( wxWindow* const w = item->GetWindow() )
                ASSERT_WINDOW_PARENT_IS(w, m_containingWindow);
        }
    }
}
```

于是 `editPanel->SetSizer(editSizer)` 会**递归进入 `previewSizer` 并把它的
containing window 改写成 `editPanel`**；而 `previewLabel` / `previewBox` 的 parent
仍是 `previewPanel` ≠ `editPanel` → 断言失败。

对比可正常工作的形态（同一次改造前的 HEAD）：`previewPanel` parent=`chainTab`，
进 `chainTab` 的 `mainSizer`，此时 `previewSizer` 的 containing window = `chainTab`
= 其内部控件的 parent ✅。

**与 §2 的关系**：§2 是「控件 parent ≠ 所在 sizer 所属窗口」；
本次是「递归改写后，sizer 所属窗口 ≠ 其内部控件 parent」。两者表象同源
（`SetContainingWindow` 断言），但触发路径不同——本次由**嵌套 sizer 的递归**引起，
仅检查控件自身 parent 无法发现，这也是上一轮静态审查漏判的原因。

### 6.3 修复

去掉 `previewPanel` 这层包装，`previewLabel` / `previewBox` 直接以 `editPanel` 为 parent
加入一个**不挂任何窗口**的纵向 sizer `previewColSizer`：

```cpp
wxStaticText* previewLabel = new wxStaticText(editPanel, ...);
wxTextCtrl*   previewBox   = new wxTextCtrl(editPanel, ...);
wxBoxSizer* previewColSizer = new wxBoxSizer(wxVERTICAL);   // 纯布局，不 SetSizer
previewColSizer->Add(previewLabel, 0, wxEXPAND | wxALL, 4);
previewColSizer->Add(previewBox, 1, wxEXPAND | wxALL, 4);
editBodySizer->Add(previewColSizer, 1, wxEXPAND, 0);        // 容纳 sizer 而非 panel
```

因 `previewColSizer` 从未 `SetSizer` 到任何窗口，递归时 `item->GetSizer()` 取不到它，
containing window 不会被改写，断言不再触发。三栏结构与
`chainEditor` / `componentPanel`（二者虽也 `SetSizer`，但其内部控件的 parent 就是
该 panel 本身，与 containing window 一致）保持一致。

### 6.4 验证：A/B 对照实验（已证实修复有效）

`UI_CHAINPREVIEW` / `[chainassert]` 自身**对本问题无判别力**——把 bug 故意改回去后，
这两个用例仍然 `All tests passed (21 assertions)`。但它们**会生成断言捕获文件**：

- `UIApp::OnAssertFailure` 在 `VALIDPROXY_ASSERT_LOG=1` 时把断言写入
  **stderr** + `bin/ui-assert-<pid>.log`（`src/ui/UIApp.cpp:160-176`）
- `AppProcess::start` 已把子进程 stdout/stderr 重定向到文件（`tests/ui/framework/Application.cpp:95`）

据此做 A/B 对照：

| 组 | 代码 | 21 断言用例 | `bin/ui-assert-*.log` |
|---|---|---|---|
| **RED**（bug 故意重现） | 预览框塞回带自有 sizer 的 `previewPanel` | 全 PASS | **生成**，内容正是截图那两条：<br>`SetContainingSizer: !m_containingSizer` +<br>`DoInsert: CheckExpectedParentIs` |
| **GREEN**（修复后） | 预览框直接加入不挂窗口的 `previewColSizer` | 全 PASS | **零文件产生** |

⇒ 断言确由本次改动引入、且已被移除。`21 assertions 全 PASS` 只能说明未引入新回归，
真正的判据是「断言捕获文件为空」。

> 补充：验证前需先清空 `bin/ui-assert-*.log`，否则历史遗留文件会造成误判。

### 6.5 教训

排查 wx parent 断言时，**必须检查嵌套 sizer 的递归改写**，
不能只逐个核对「控件 parent == 直接所在 sizer 的窗口」。
判据：`SetContainingWindow` 会向下递归，任何「带自有 sizer 的子 panel」被加入
另一 sizer 时，其内部控件的 parent 与被改写后的 containing window 就可能不一致。

（另：`AddStretchSpacer` 传 window=nullptr（`sizer.h:1237` → `Add(0,0,prop)`），
`gbsizer.cpp:218` 有 `if (item->GetWindow())` 判空，**不会**触发本断言——
排查时曾误判它为元凶，勿再重犯。）