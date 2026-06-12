# 2026-06-11-Bugfix-ProxyListPanel-RefreshFreeze-v1.0.md

## 摘要

修复批量测试完成后，代理列表在大数据量（如 53,837 条）场景下的 UI 冻结问题。

## 问题描述

- `ProxyListPanel::refreshResults()` 调用 `model_->notifyTestResultChanged()`。
- 原实现遍历全部记录并为每个 row × column 逐行发送 ValueChanged 事件，大数据量下产生十几万条事件 → GUI 线程暴政 → 假死。
- 仅在测试完结、结果入库被回调触发时复现（大 DB + 刷新操作叠加）。

## 修复方案

- 将逐行 ValueChanged 替换为单次 `listCtrl_->Refresh()`，把脏区重绘聚合到视图层，避免事件洪流。

## 覆盖范围

- `src/ui/ProxyListPanel.cpp` 中 `refreshResults()` 相关代码路径。

## 关联

- 结合零代理 guard 修复与网络字段回退，属于批量测试稳定化系列。

## QA

- 用全量测试库（约 53,837 profiles）完成一轮批量测试，观察冻结是否复现；
- 预期状态：结果显示正常，界面可操。

## 记录时间

2026-06-11
