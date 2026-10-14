---
title: "docs: bugfix — 配置编辑器事件越界与多选残留（组件列表被清空 / 保存报 ID 已存在）"
type: bugfix
status: completed
date: 2026-10-13
updated: 2026-10-13
version: v1.0
---

# Bugfix：wx 事件越界捕获 + 列表多选残留

本篇记录本会话修复的两个同源缺陷，都发生在「配置编辑器」的列表事件与选中态处理上，
根因都指向 **wx 的 ID_ANY 作用域 / 选中位按行独立** 这两个易被误解的底层行为。

---

## 缺陷 A：解析后单击「组件类型」显示框，列表被清空

### 现象

「组件链编辑」页点「解析」，中间组件列表正确显示 4 个组件；随后**单击该列表任意一行**，
列表被清空、配置预览也一并消失。

### 根因：事件表里的 `wxID_ANY` 跨控件捕获

`ConfigDialog` 的事件表（`ConfigDialog.cpp:99`）原有一行：

```cpp
EVT_LIST_ITEM_SELECTED(wxID_ANY, ConfigDialog::onTemplateSelection)
```

`wxID_ANY` 的作用域是**整棵窗口树**，列表选中事件会沿父窗口冒泡上溯到 `ConfigDialog`。
因此点击「组件包列表」「组件类型列表」也会进入本该只服务「模板列表」的
`onTemplateSelection` → `syncChainEditorFromSelection()` → `componentListPanel_->clear()`，
把刚解析出来的列表清空；同时还顺带重刷模板配置预览，语义完全错误。

注意：`onBundleSelection` 本身没有绑定选中事件（单击列表不改变数据），
被劫持的是**模板列表**的处理函数。

### 修复

1. 删除事件表中的 `wxID_ANY` 条目；
2. 改为在 `createTemplateTab()` 内对 `templateList_` 单独
   `Bind(wxEVT_LIST_ITEM_SELECTED, ...)`，作用域收敛到本控件。

另两个列表（`bundleList_` 在 `createBundleTab` 内 lambda 绑定、`BundleChainEditor`
内部自带绑定）本就是各自绑到本控件，不受影响——只是此前被事件表的 `wxID_ANY` 额外劫持。

### 教训

> 在 wx 中，事件表级别的 `wxID_ANY` 会跨控件捕获冒泡事件。
> 多列表对话框必须**逐控件 Bind**，不能用事件表 `wxID_ANY` 一把梭。
> 同类历史：`docs/bugfix/2026-08-21-Bugfix-StandaloneMonitorDialog-v1.0.md`
> （wxBoxSizer 构造期断言）也属「对 wx 默认作用域/默认值的误解」这一类。

---

## 缺陷 B：新增组件包后点「保存」，报「组件包 ID 已存在」

### 现象

「组件包配置」页点「新增」创建 `xray_env`，随即点「保存」——弹
「校验失败：组件包 ID 已存在: xray_env，请换一个未被占用的 ID。未保存。」，
但这个包**就是刚建的自己**。截图里列表**首行 `xray_LOG` 与末行 `xray_env` 同时高亮**。

### 根因：`SetItemState` 不清除其它行，多选状态残留

`onBundleNew` 保存后选中新建行用的是：
```cpp
bundleList_->SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
```

而 `wxListCtrl::SetItemState` 在 MSW 下走 `LVM_SETITEMSTATE` **逐行**下发
（`wxWidgets/src/msw/listctrl.cpp`），只设置**本行**的状态位，**不会清除其它行**。
新增前 `loadBundles()` 已把第 0 行 `xray_LOG` 选中，于是两行并存。

随后 `onBundleSave` 用 `GetNextItem(-1, ..., wxLIST_STATE_SELECTED)` 取的是
**第一个**选中行 = `xray_LOG`，而编辑器里显示的是 `xray_env` 的 JSON ⇒
`bundle.id != bundles_[sel].id` ⇒ 误判为「改名」⇒ 撞上「ID 已存在」检查。

即这不是 ID 逻辑问题，而是**行定位错了**。

### 修复（三层）

1. `onBundleNew`：选中新建行前先遍历**清空所有行**的
   `wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED`，再选中目标行并 `EnsureVisible`。
2. `loadBundles`：选中第一项前同样先清空（同类隐患）。
3. `onBundleSave` **纵深防御**：用 `GetNextItem` 链式遍历统计选中行数，
   ≠1 时弹窗「当前有 N 个组件包处于选中状态，请只选中一个再保存」并返回。
   这样即便用户 Ctrl/Shift 多选，也不会把 JSON 存到错误的包上。

（统计选中数用 `GetNextItem` 链式遍历；wx 3.3 的 `wxListCtrl` 无 `GetItemState`，
首次编译报错后已改。）

### 教训

> `wxListCtrl::SetItemState` 是「按掩码设置本行状态」，不是「设置唯一选中行」。
> 任何「确保选中某一行」的代码都必须先显式清除其它行，否则
> `GetNextItem` 会返回第一个选中行，造成行定位错乱。

---

## 验证

- 构建 validproxy / validproxy-cli / UITests：0 error
- `test_config_generator`：338 passed + 1 skipped + 0 failed（修复 A/B 均不涉及单测主体）
- `UI_CHAINPREVIEW` + `UI_MAINWINDOW`：PASS
- ctest 47/49：失败项均为既有（`RegionBatchResolverTest` 链接缺源、
  `XrayManagerStartupTest` / `UI_PORTCLOSE` 需真实 xray）
- **未目视确认**：沙箱内无法驱动 GUI 模拟鼠标单击/多选（SendKeys / WM_COMMAND 均失效），
  两个缺陷的最终交互效果需用户实测。缺陷 A 可用「解析后单击组件列表」复现，
  缺陷 B 可用「新增组件包后直接保存」复现。