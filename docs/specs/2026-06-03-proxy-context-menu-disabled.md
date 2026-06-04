---
title: "feat: 批量操作时禁止右键菜单功能"
type: spec
status: completed

## 验证结果
- 编译通过
- 6 个测试套件全部通过

### 修改文件列表
- `src/ui/SubscriptionPanel.cpp` - 更新/测试/编辑/删除订阅菜单项检查运行状态
- `src/ui/ProxyListPanel.cpp` - 测试此代理/有效代理分享菜单项检查运行状态

## 提示信息统一
"操作进行中，请等待完成后再试"
date: 2026-06-03
origin: "UI 交互安全"
---

# 批量操作右键菜单禁用

## 问题
批量更新/测试代理过程中，点击代理列表右键菜单"测试此代理"会干扰当前操作。

## 设计
在 ProxyListPanel 的右键菜单和菜单项处理函数中添加运行状态检查：

```cpp
if (controller_ && controller_->isRunning()) {
    return;
}
```

## 变更文件
- `src/ui/ProxyListPanel.cpp`

## 变更内容
- `onContextMenu` - 检查运行状态后再显示菜单
- `onTestProxy` - 检查运行状态
- `onExportShareLink` - 检查运行状态

## 验证
1. 开始批量测试 → 右键点击代理列表 → 菜单不响应或被禁用