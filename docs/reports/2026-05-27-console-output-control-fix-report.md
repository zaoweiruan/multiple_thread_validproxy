---
title: "修复: UI 模式下禁用控制台日志输出，全部路由到 LogPanel"
type: report
status: completed
date: 2026-05-27
origin: "Logger 控制台输出控制增强"
---

# 技术报告: UI 模式控制台日志输出控制

## 问题描述

在 GUI 模式下运行 `validproxy.exe -ui` 时，所有日志消息同时输出到：
1. **控制台**（`std::cout`）— 通过 `consoleLevel_` 级别门控
2. **文件**（`.log`）— 通过 `fileEnabled_` + `fileLevel_` 门控
3. **LogPanel**（wxTextCtrl）— 通过 Logger 回调机制

控制台输出在 GUI 模式下是多余的，且会污染终端输出，降低调试效率。

## 根因分析

Logger 类没有控制控制台输出通断的开关。现有的 `consoleLevel_` 仅控制级别阈值（默认 `INFO`），不能完全关闭控制台输出。

三个输出路径在 `Logger::write()` 中**同时触发**，没有互斥切换机制：

```
Logger::write()
  ├── std::cout              ← 无开关，仅级别门控
  ├── *outFile_              ← 有 fileEnabled_ 开关
  └── logCallback_()         ← 有回调注册机制
```

## 解决方案

### 修改一: Logger.h — 添加 setConsoleEnabled 声明和成员变量

```cpp
// include/Logger.h:46 — 新增声明
static void setConsoleEnabled(bool enabled);

// include/Logger.h:63 — 新增成员
static bool consoleEnabled_;
```

### 修改二: Logger.cpp — 实现控制台开关逻辑

**初始化**（与 `fileEnabled_` 一致，默认开启保证 CLI 兼容）：

```cpp
// src/Logger.cpp:13
bool Logger::consoleEnabled_ = true;
```

**`write()` 方法** — 控制台输出增加 `consoleEnabled_` 判断：

```cpp
// 旧: if (static_cast<int>(level) >= static_cast<int>(consoleLevel_)) {
// 新: if (consoleEnabled_ && static_cast<int>(level) >= static_cast<int>(consoleLevel_)) {
```

**`writeTimestamp()` 方法** — 相同修改：

```cpp
// 旧: if (static_cast<int>(level) >= static_cast<int>(consoleLevel_)) {
// 新: if (consoleEnabled_ && static_cast<int>(level) >= static_cast<int>(consoleLevel_)) {
```

**实现 `setConsoleEnabled()`**（与 `setFileEnabled()` 模式一致）：

```cpp
void Logger::setConsoleEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    consoleEnabled_ = enabled;
}
```

### 修改三: main.cpp — UI 模式禁用控制台输出

```cpp
// src/main.cpp:259
Logger::setConsoleEnabled(false);  // UI mode: route all output to LogPanel only
```

## 变更清单

| 文件 | 行 | 变更类型 | 说明 |
|------|-----|----------|------|
| `include/Logger.h` | 46 | 新增声明 | `setConsoleEnabled(bool)` |
| `include/Logger.h` | 63 | 新增成员 | `consoleEnabled_` 静态变量 |
| `src/Logger.cpp` | 13 | 新增初始化 | `consoleEnabled_ = true` |
| `src/Logger.cpp` | 65 | 修改判断 | `write()` 中 console 输出加标志检查 |
| `src/Logger.cpp` | 135 | 修改判断 | `writeTimestamp()` 中 console 输出加标志检查 |
| `src/Logger.cpp` | 207-212 | 新增实现 | `setConsoleEnabled()` 方法体 |
| `src/main.cpp` | 259 | 新增调用 | UI 分支 `setConsoleEnabled(false)` |

## 输出流影响矩阵

| 输出路径 | CLI 模式 | GUI 模式 | 开关机制 |
|----------|----------|----------|----------|
| `std::cout` | ✅ 输出 | ❌ 禁用 | `consoleEnabled_` |
| 文件 (.log) | ✅ 按配置 | ✅ 按配置 | `fileEnabled_` + `fileLevel_` |
| LogPanel/TestPanel | N/A | ✅ 接收 | 回调注册机制 |

## 验证结果

```
Build: cmake --build build --parallel 8
Result: ✅ SUCCESS (0 errors)

Tests: ctest -V
Result: ✅ 3/3 passed
  - CurlEasyHandleTest: ✅
  - DedupTest: ✅ (11/11)
  - ProfileitemTest: ✅ (3/3)
```

## 文件变更统计

| 文件 | 新增行 | 删除行 |
|------|--------|--------|
| `include/Logger.h` | 2 | 0 |
| `src/Logger.cpp` | 7 | 2 |
| `src/main.cpp` | 1 | 0 |
| **合计** | **10** | **2** |

净变化: **+8 行**
