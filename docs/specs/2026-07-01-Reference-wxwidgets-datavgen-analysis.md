---
title: wxWidgets datavgen.cpp 双击事件分析
date: 2026-07-01
type: reference
version: v1.0
---

## 问题背景

Windows (MSW) 平台下，wxWidgets `wxDataViewCtrl` 的双击事件无效。经源码分析发现 `wxDataViewMainWindow` 在创建时缺失 `CS_DBLCLKS` 窗口类样式。

## 源码定位

- **文件**: `E:\eclipse_workspace\wxWidgets\src\generic\datavgen.cpp`
- **行号**: 2091
- **代码片段**:
  ```cpp
  // 问题代码（省略 CS_DBLCLKS）
  WNDCLASSEX wndclass;
  wndclass.style = CS_HREDRAW | CS_VREDRAW;
  // 应为:
  // wndclass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  ```

## 解决方案

采用 selection-change-based 双击检测 Workaround：
- 跟踪 `lastSelItem` 和 `lastSelTime`
- 同一项在 500ms 内二次选中 → 触发双击事件
- 见: `docs/bugfix/2026-07-01-Bugfix-DoubleClick-V1.0.md`

## 引用方式

本项目中使用的 wxWidgets 版本: 3.2+ (wxMSW)