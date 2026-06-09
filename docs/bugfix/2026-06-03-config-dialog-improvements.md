---
title: "fix: 禁止运行中切换数据库 & 日志级别保存"
type: spec
status: completed
date: 2026-06-03
origin: "配置编辑问题反馈"
---

# 配置编辑改进

## 问题描述

### Bug 1: 运行中切换数据库
- **现象**: 批量测试或更新订阅过程中可以打开配置编辑窗口并切换数据库
- **原因**: `onMenuConfig` 没有检查 `isRunning_` 状态
- **影响**: 可能导致数据库句柄混乱，数据丢失

### Bug 2: 日志级别未保存
- **现象**: 在配置编辑中修改 console/file 日志级别，保存后重启日志级别未改变
- **原因**: 保存配置后未调用 `Logger::setFileLevel` 和 `Logger::setConsoleLevel`
- **影响**: 配置变更不生效

## 设计

### D1: 禁止运行中切换数据库
```cpp
// 在 onMenuConfig 添加检查
if (isRunning_) {
    wxMessageBox("请在操作完成后再编辑配置", "禁止操作", wxOK | wxICON_WARNING);
    return;
}
```

### D2: 日志级别应用
```cpp
// 在 onMenuConfig 保存后添加
Logger::setFileLevel(Logger::stringToLevel(cfg.log_file_level));
Logger::setConsoleLevel(Logger::stringToLevel(cfg.log_console_level));
```

### D3: 日志级别加载（补充修复）
```cpp
// 在 ConfigDialog::loadConfig 添加:
propGrid_->SetPropertyValue("log_console_level", wxString(cfg.log_console_level));
propGrid_->SetPropertyValue("log_file_level", wxString(cfg.log_file_level));
propGrid_->SetPropertyValue("priority_mode", wxString(cfg.priority_mode));
```

## 实施计划

| 步骤 | 文件 | 变更 |
|------|------|------|
| 1 | src/ui/MainFrame.cpp | 在 onMenuConfig 添加 isRunning_ 检查和日志级别应用 |

## 验证步骤
1. 开始批量测试 → 点击配置菜单 → 应提示禁止操作
2. 修改日志级别 → 保存 → 重启 → 日志级别应生效