# Bugfix: 点击 Latency/Health/Starts/Runtime 表头排序失效

| 项目 | 内容 |
| :--- | :--- |
| 日期 | 2026-08-21 |
| 模块 | `ProxyListPanel::onColumnHeaderClick`（`src/ui/ProxyListPanel.cpp:306`）+ `SubscriptionPanel::onColumnHeaderClick`（`src/ui/SubscriptionPanel.cpp:497`） |
| 关联 | 前序改动 `ProxyListPanel` 表头列顺序重排（`ProxyListPanel::initUI`）——本缺陷由该重排触发，根因在此之前已潜伏 |

## 1. 现象

点击代理列表（`ProxyListPanel`）的 **Latency / Health / Starts / Runtime** 四列任意表头，
列表不发生预期的重新排序（或排序到错误的列），排序箭头指示错位。其余列（Type/Host/Port/
Region/Remarks/IndexId/#）及 Failures/Message 在重排后仍可正常排序。

`SubscriptionPanel` 的列点击处理存在同一潜在缺陷（仅在其列被重排后会暴露），本次一并修复。

## 2. 根因（RCA）

模型层排序逻辑本身正确——`ProxyListModel::Compare` 对 Latency/Health/Starts/Runtime 的
数值比较在单元测试中全部通过。缺陷位于 **UI 表头点击处理** 中对「列索引」语义的混淆：

1. `wxDataViewCtrlGenericBase::OnClick`（`src/generic/datavgen.cpp:378`）在发送
   `wxEVT_DATAVIEW_COLUMN_HEADER_CLICK` 时构造事件为
   `wxDataViewEvent(type, owner, owner->GetColumn(n))`，随后
   `wxDataViewEvent::Init`（`src/common/datavcmn.cpp:1789`）设置
   `m_col = column->GetModelColumn()`。
   ⇒ **`wxDataViewEvent::GetColumn()` 返回的是「模型列」索引，而非「视觉位置」。**

2. 处理函数却把该值当作视觉位置传给 `wxDataViewCtrl::GetColumn(int pos)`：
   - `ProxyListPanel.cpp:320` 原 `wxDataViewColumn* dvCol = listCtrl_->GetColumn(col);`
   - `ProxyListPanel.cpp:683`（数据重载后保留排序）同理
   - `SubscriptionPanel.cpp:510` 同理

3. **关键触发点**：重排前，各列按 `COL_*` 枚举顺序 `AppendTextColumn`，视觉位置 == 模型列，
   巧合下可用；重排后二者发散，`GetColumn(模型列)` 取到错误的视觉列（甚至越界返回
   `nullptr`），`SetSortOrder` + `Resort()` 排序到错误/空的列 → 表现为「点击排序失效」。

4. 为何恰好是 Latency/Health/Starts/Runtime 四列：它们是重排中**移动过**的「计算列」，
   移动后模型列与视觉位置差最大，映射错得最明显；未移动的列差值为 0/小，故仍看似正常。

## 3. 修复方案

新增私有辅助函数 `resolveColumnByModel(int modelCol)`，将「模型列」正确解析为
`wxDataViewColumn*`（遍历视觉位置比较 `GetModelColumn()`）。该方式**不依赖任何 wxWidgets
内部 API、与列顺序无关**，对任意重排均健壮。

- `ProxyListPanel` / `SubscriptionPanel` 各新增：
  ```cpp
  wxDataViewColumn* XxxPanel::resolveColumnByModel(int modelCol) const {
      if (modelCol < 0) return nullptr;
      const unsigned int count = listCtrl_->GetColumnCount();
      for (unsigned int i = 0; i < count; ++i) {
          wxDataViewColumn* col = listCtrl_->GetColumn(i);
          if (col != nullptr && static_cast<int>(col->GetModelColumn()) == modelCol) {
              return col;
          }
      }
      return nullptr;
  }
  ```
- 将三处 `listCtrl_->GetColumn(col)` / `GetColumn(sortState_.column)` 全部替换为
  `resolveColumnByModel(...)`：
  - `ProxyListPanel.cpp:334`（点击 → 设置排序指示 + `Resort`）
  - `ProxyListPanel.cpp:697`（数据重载后恢复排序指示 + `Resort`）
  - `SubscriptionPanel.cpp:524`（点击 → 设置排序指示 + `Resort`）

`sortState_.column` 仍存储模型列（与 `event.GetColumn()` 语义一致），比较/循环方向逻辑不变。

## 4. 变更清单

| 文件 | 变更 |
| :--- | :--- |
| `src/ui/ProxyListPanel.h` | 新增私有声明 `wxDataViewColumn* resolveColumnByModel(int modelCol) const;`（:67） |
| `src/ui/ProxyListPanel.cpp` | 实现 `resolveColumnByModel`（:292）；:334、:697 两处改用该辅助函数 |
| `src/ui/SubscriptionPanel.h` | 新增私有声明 `wxDataViewColumn* resolveColumnByModel(int modelCol) const;`（:46） |
| `src/ui/SubscriptionPanel.cpp` | 实现 `resolveColumnByModel`（:483）；:524 改用该辅助函数 |

## 5. 验证

- [x] 构建 0 error：`./build/validproxy.exe` 与 `./build/validproxy-cli.exe` 链接通过
      （仅有 1 条与本次无关的历史 `unused parameter` 告警，位于 `ProxyListPanel.cpp:230`）
- [x] 单元测试回归：`test_proxy_list_model` **15/15 通过**，含
      `CompareRuntimeIncludesRunningDuration`、`CompareRuntimeEqualWhenTotalsEqual`、
      `BaselineWithoutRunningSessions` 等——证明模型层对 Latency/Health/Starts/Runtime 的
      排序比较逻辑本身正确，缺陷确在 UI 列映射层
- [ ] 手动 GUI 验证（CI 无法点击表头，建议人工确认）：启动 `bin/validproxy.exe`，载入含
      差异延迟/运行时长的订阅，依次点击 Latency/Health/Starts/Runtime，确认列表按该列
      重排且排序箭头显示在**被点击的列**上；并验证点不同列切换、三态循环
      （None→Asc→Desc→None）正常

## 6. 未覆盖场景 / 建议

1. **建议补充 UI 级回归测试**：构造 `wxDataViewCtrl` + 列，模拟
   `wxEVT_DATAVIEW_COLUMN_HEADER_CLICK`，断言
   `GetSortingColumn()->GetModelColumn()` 等于被点击列的模型列。可将本类映射错误长期锁死。
2. 编辑器 LSP 报 `'wx/wx.h' file not found` 为沙箱 include 路径缺失的**伪错误**，非真实
   编译错误（CMake 提供正确 include 目录，实际构建干净）。
3. 该修复同时消除了 `SubscriptionPanel` 的同源隐患，避免其列未来被重排时复现同类问题。
