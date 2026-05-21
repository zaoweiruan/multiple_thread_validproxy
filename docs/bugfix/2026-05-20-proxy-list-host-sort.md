---
title: "fix: ProxyListPanel Host column header click sorting"
type: fix
status: completed
date: 2026-05-20
---

# ProxyListPanel: Host Column Header Sorting Fix

## 问题描述
点击Host列进行排序时，代理列表没有重新排列。日志显示排序函数被正确调用，但视图没有更新。

## 现象
- 点击Host列头（column=2）
- 控制台输出：`onColumnHeaderClick: column=2`、`sortProxiesByColumn: col=2, dir=1, proxies_ size=915`、`onColumnHeaderClick done: col=2, dir=1`
- 但代理列表没有任何变化

## 根本原因
1. `EVT_DATAVIEW_COLUMN_HEADER_CLICK` 未在事件表中绑定
2. Column indices 定义错误（IndexId 在前，RowNum 在后）
3. `loadProxies()` 在排序后调用时会重置已排序的 `proxies_`

## 修复方案
1. 将 `EVT_DATAVIEW_COLUMN_HEADER_CLICK` 移出注释，正确绑定事件
2. 重新排序 Column indices：RowNum(0) → Type(1) → Address(2) → ... → IndexId(8)
3. 添加 `subIdChanged` 检查，只在订阅切换时重置 `proxies_`

## 文件变更
- `src/ui/ProxyListPanel.cpp`：
  - Line 32: 移除注释符启用事件绑定
  - Line 14-25: 重排 Column indices 定义
  - Line 55-62: 调整列创建顺序
  - Line 107-115: 添加 subIdChanged 检查逻辑

## 状态
✅ 已修复，构建通过，测试通过

## 验证步骤
1. 构建：`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel 8`
2. 运行GUI，点击Host列头验证排序功能
3. 排序后点击行号列验证恢复原始顺序