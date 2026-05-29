---
title: "fix: Fix LogLevel enum ordering so REPORT is below ERR, enabling ERR output when console level is REPORT"
type: fix
status: completed
date: 2026-05-13
origin: "Bug: when log_console_level is set to REPORT, ERR messages are silently filtered out because REPORT(5) > ERR(4) in enum, but `level >= threshold` filtering blocks ERR(4) since 4 < 5."
---

# P5: Fix LogLevel enum ordering — REPORT below ERR

## 问题描述

**当前行为**：当 `bin/config.json` 中 `console_level: "REPORT"` 时，`LogLevel::ERR` 消息**不会输出到 console**。

**根因**：`include/Logger.h` 中 LogLevel 枚举的数值顺序错误：

```cpp
enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERR = 4,
    REPORT = 5    // ❌ 定义在 ERR 之上
};
```

`Logger::write()` 的过滤逻辑（`src/Logger.cpp:60`）使用 `level >= threshold` 比较：

```
consoleLevel_ = REPORT (5)
ERR 消息 level = ERR (4)
4 >= 5 → false → ❌ 不输出
```

**语义预期**："设日志级别为 REPORT" 应表示"显示 REPORT 及以上严重级别的消息"，其中 ERR 比 REPORT 更严重，因此应被包含。正确的枚举排序应将 REPORT 置于 INFO 和 WARN 之间。

**影响范围**：任何设置 `console_level: "REPORT"` 的用户都会丢失所有 ERR 日志输出。

## 范围边界

- 修改: `include/Logger.h` — 仅重排 LogLevel 枚举值
- NOT 修改: `src/Logger.cpp` — 所有函数（write/stringToLevel/levelToString）使用不依赖数值的 switch/if-else 逻辑，无需改动
- NOT 修改: 任何 `.cpp` 文件 — `level >= threshold` 比较逻辑在新排序下自动正确工作
- NOT 修改: 任何 `.json` 配置文件 — stringToLevel 使用字符串匹配
- NOT 修改: 任何测试文件 — 无测试依赖枚举数值

## 详细变更

### U1: 重排 `include/Logger.h` 中 LogLevel 枚举

```cpp
// Before (错误的顺序)
enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERR = 4,
    REPORT = 5
};

// After (正确的顺序: REPORT 介于 INFO 和 WARN 之间)
enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    REPORT = 3,
    WARN = 4,
    ERR = 5
};
```

### 变更后行为对照

| consoleLevel | 显示的级别 | 逻辑 |
|-------------|-----------|------|
| `INFO` (2) | INFO(2), REPORT(3), WARN(4), ERR(5) | >= 2 |
| `REPORT` (3) | **REPORT(3), WARN(4), ERR(5)** | >= 3 ✅ ERR 正常显示 |
| `WARN` (4) | WARN(4), ERR(5) | >= 4 |

### 影响零改动分析

| 组件 | 依赖数值？ | 是否需要修改 | 原因 |
|------|-----------|-------------|------|
| `Logger::write()` | ✅ `>=` 比较 | **否** | 比较逻辑不变，仅数值变化，结果正确 |
| `Logger::writeTimestamp()` | ✅ `>=` 比较 | **否** | 同上 |
| `Logger::stringToLevel()` | ❌ 字符串匹配 | **否** | `if (s == "report") return LogLevel::REPORT;` — 按名返回 |
| `Logger::levelToString()` | ❌ switch/case | **否** | `case LogLevel::REPORT: return "REPORT";` — 按标签匹配 |
| `Logger::setConsoleLevel()` | ❌ 直接赋值 | **否** | 只是存储 enum 值 |
| `Logger::setFileLevel()` | ❌ 直接赋值 | **否** | 同上 |
| `Logger::disableFile()` | ⚠️ `static_cast<LogLevel>(100)` | **否** | 魔数 100 > 所有级别，与具体枚举值无关 |
| 配置文件 | ❌ 字符串 | **否** | JSON 中用字符串 "REPORT"/"ERROR"/"INFO" |
| `main.cpp` | ❌ 间接调用 | **否** | 仅调用 `setConsoleLevel(stringToLevel(...))` |

## 验证步骤

1. `cmake --build build --parallel 8` — 0 errors（仅修改头文件枚举值）
2. `ctest -V` — 所有测试通过
3. **手动验证修复**：
   - `bin/config.json` 中设 `"console_level": "REPORT"`
   - 运行 `.\bin\validproxy.exe -D`（或其他触发 ERR 路径的功能）
   - 确认 console 可见 ERR 级别日志

## 文件变更列表

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/Logger.h` | 修改 | 1 行变更：REPORT 从 5 → 3 |
| `docs/plans/project-plans-tracker.md` | 修改 | 更新进度 |

## 风险

| 风险 | 缓解措施 |
|------|----------|
| 任何隐式依赖枚举数值的外部代码 | 枚举为项目内部定义，无外部 API 暴露 |
| 序列化/反序列化依赖数值 | 项目中全部使用 stringToLevel/levelToString 字符串转换 |
| `disableFile()` 使用魔数 100 | 100 仍然大于所有级别（最大 5），不受影响 |
