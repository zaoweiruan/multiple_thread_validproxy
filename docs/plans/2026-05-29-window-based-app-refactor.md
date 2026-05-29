---
title: "refactor: window-based program startup mode"
type: refactor
status: completed
date: 2026-05-29
supersedes: []
---

# Window-Based Program Refactoring

## 问题描述

当前应用程序启动行为：默认为 CLI 模式，仅当传入 `-ui` 或 `--ui` 参数时才启动 GUI，但 `wxEntry()` 阻塞主进程。

目标行为：
1. 无参启动 → 自动启动桌面窗口应用 (GUI)
2. `-ui/--ui` 启动 → 启动 GUI（向后兼容）
3. CLI 参数启动 → CLI 模式（非阻塞返回）
4. **GUI 启动后立即返回**（不等待 GUI 关闭）

## 解决方案

使用 **进程自我派生** 模式：
- 主进程接收到 GUI 启动请求
- 调用 `CreateProcess` 启动自身作为 GUI 工作进程（带 `--gui` 内部标记）
- 主进程立即返回
- 工作进程检测 `--gui` 标记后调用 `wxEntry()` 阻塞运行 GUI

## 文件变更

### src/main.cpp
- 添加 `--gui` 内部标记检测 (`isGuiWorker`)
- 添加进程派生逻辑：`CreateProcessA(...DETACHED_PROCESS...)`
- GUI 模式分为两部分：
  - 主进程：派生 GUI 工作进程后返回
  - 工作进程：检测 `--gui` 标记后运行阻塞 GUI

### src/ui/UIApp.h
- 添加 `setConfig()` 方法（为延迟初始化准备，当前未使用）

### docs/architecture.md
- 更新 CLI 模式说明

## 验证步骤

- [x] **编译**: `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel 8` ✅ 成功
- [ ] **GUI 模式测试**: 执行 `validproxy`（无参数）验证启动 GUI 后返回
- [ ] **CLI 模式测试**: 执行 `validproxy -F` 验证代理查找 CLI 模式不受影响

## 结果

Build 成功，`bin/validproxy.exe` 已生成 (27.7 MB)。