---
title: "修复: ProxyListPanel 调试日志从 std::cout 迁移到 Logger"
type: report
status: completed
date: 2026-05-27
origin: "完成控制台输出控制 — 残留调试日志清理"
---

# 技术报告: 残留 `std::cout` 调试日志迁移

## 问题描述

在完成 UI 模式下禁用控制台输出（`Logger::setConsoleEnabled(false)`）后，`ProxyListPanel.cpp` 中仍有三处使用 `std::cout` 的调试日志。这些日志绕过 Logger 系统，即使设置了 `setConsoleEnabled(false)`，仍会输出到控制台。

## 根因

`ProxyListPanel.cpp` 中列排序的调试日志直接使用 `std::cout`：

```cpp
// src/ui/ProxyListPanel.cpp:229
std::cout << "onColumnHeaderClick: column=" << col << std::endl;

// src/ui/ProxyListPanel.cpp:249
std::cout << "onColumnHeaderClick done: col=" << sortState_.column
          << ", dir=" << (int)sortState_.direction << std::endl;

// src/ui/ProxyListPanel.cpp:253
std::cout << "sortProxiesByColumn: col=" << col << ", dir=" << (int)dir
          << ", proxies_ size=" << proxies_.size() << std::endl;
```

这些日志的作用：
- 记录用户点击列头进行排序的触发事件
- 记录排序操作的参数（列索引、方向、代理总数）
- 用于开发和调试阶段追踪排序行为

## 解决方案

三处全部替换为 `Logger::write()` 并使用 `LogLevel::DEBUG` 级别：

```cpp
// 新增 Logger.h 包含
#include "Logger.h"

// 替换后:
Logger::write("[ProxyListPanel] Column header click: column=" + std::to_string(col), LogLevel::DEBUG);

Logger::write("[ProxyListPanel] Column header click done: col=" + std::to_string(sortState_.column)
              + ", dir=" + std::to_string(static_cast<int>(sortState_.direction)), LogLevel::DEBUG);

Logger::write("[ProxyListPanel] Sort: col=" + std::to_string(col) + ", dir=" + std::to_string(static_cast<int>(dir))
              + ", proxies_ size=" + std::to_string(proxies_.size()), LogLevel::DEBUG);
```

## 变更清单

| 文件 | 行 | 变更 |
|------|-----|------|
| `src/ui/ProxyListPanel.cpp` | 4 | 新增 `#include "Logger.h"` |
| `src/ui/ProxyListPanel.cpp` | 229 | `std::cout` → `Logger::write(..., LogLevel::DEBUG)` |
| `src/ui/ProxyListPanel.cpp` | 249 | `std::cout` → `Logger::write(..., LogLevel::DEBUG)` |
| `src/ui/ProxyListPanel.cpp` | 253 | `std::cout` → `Logger::write(..., LogLevel::DEBUG)` |

## 效果

| 场景 | 修改前 | 修改后 |
|------|--------|--------|
| GUI 模式运行（无 console） | 仍有三行输出到 `stdout` | **无输出**（Logger 已禁用 console） |
| GUI 模式 LogPanel | 不显示 | ✅ 可通过 LogPanel 查看（DEBUG 级别） |
| CLI 模式 | 输出到 stdout | ✅ 通过 Logger 输出（符合控制台输出控制） |
| 日志文件 (.log) | 不记录 | ✅ 记录到日志文件 |

## 验证结果

```
Build: cmake --build build --parallel 8
Result: ✅ SUCCESS (0 errors)

Tests: ctest -V
Result: ✅ 3/3 passed
```
