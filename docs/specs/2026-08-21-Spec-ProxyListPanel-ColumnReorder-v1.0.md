---
title: "spec: ProxyListPanel 列显示顺序调整（用户指定顺序）"
type: spec
module: ProxyListPanel
date: 2026-08-21
status: completed
verification: "构建 0 error + ctest 32/32"
---

# ProxyListPanel 列显示顺序调整

## 1. 背景与目标

用户要求将代理列表（`ProxyListPanel`）的列显示顺序调整为：

```
Region | Latency ↕ | Health ↕ | Type | Host ↕ | Port | Message ↕ |
Starts ↕ | Runtime ↕ | # | IndexId | Failures ↕ | Remarks
```

（原顺序为 `# | Region | Latency ↕ | Type | Host ↕ | Port | Failures ↕ | Remarks | Message ↕ | IndexId | Starts ↕ | Runtime ↕ | Health ↕`。）

## 2. 变更范围（仅视图层）

- **唯一改动文件**：`src/ui/ProxyListPanel.cpp` 的 `onColumnsInit()` 方法。
- **唯一改动内容**：重排 13 个 `listCtrl_->AppendTextColumn(...)` 调用的顺序。
- **每一列的 `(label, 模型列索引 COL_*, 宽度, 可编辑标志)` 完全保持不变**，仅视觉位置改变。

## 3. 不变的部分（关键安全论证）

`ProxyListModel` 完全未改动：

- `COL_*` 是**模型列索引**（固定常量），与视觉位置无关；
- `GetValueByRow(int row, unsigned int col)` 按 `col`（模型列）取数；
- `SetValueByRow(...)` 写回仅在 `col == COL_REMARKS` 时生效（模型列判定，不依赖视觉顺序）；
- `Compare(item1, item2, col, ascending)` 的 `col` 是**模型列索引**。

### 排序正确性论证

点击列头触发 `wxDataViewColumn::SetSortOrder()` → `Resort()`。框架在排序时调用
`Compare` 传入的 `column` 来自 `wxDataViewColumn::GetModelColumn()`
（见 `wxWidgets/src/generic/datavgen.cpp:1833-1838`：
`return m_model->Compare(..., m_sortOrder.GetColumn(), ...)`，而
`m_sortOrder` 在 `SetAsSortKey`/`SetSortOrder` 时由该列的 `GetModelColumn()` 设定）。

因此 `Compare` 收到的永远是**模型列索引**，与视觉位置**解耦**。
仅重排 `AppendTextColumn` 的调用顺序——即仅改变视觉位置——**不会影响任何列的排序行为**。

`onColumnHeaderClick` 中 `event.GetColumn()` 返回视觉位置，仅用于：
1. 在 `sortState_.column` 记录视觉位置（刷新时复用）；
2. 经 `GetColumn(visualPos)` 取回被点击的 `wxDataViewColumn` 以设置排序指示箭头。

这两处"视觉进、视觉出"逻辑自洽，不依赖具体列号，重排后依然正确。

## 4. 验证

- 构建：`cmake --build build --parallel 8` → 0 error（仅一处既有 `unused parameter 'event'` 警告，与本次无关）。
- 回归：`ctest` → **32/32 全部通过**（含 `ProxyListModelTest`、`ProfileExItemDAOTest` 等）。
- 排序路径未被触及，既有 `COL_*` 排序单测仍覆盖。

## 5. 边界与限制

- 未新增、删除或重命名任何列；`Remarks` 列仍为唯一 `wxDATAVIEW_CELL_EDITABLE` 列，编辑逻辑不受影响。
- 持久化/导出逻辑（`ShareLink`、DB 读写）均基于 `ProfileItem`/`ProfileExItem` 字段，与列表视觉顺序无关。
