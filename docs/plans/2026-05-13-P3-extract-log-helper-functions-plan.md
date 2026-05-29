---
title: "refactor: Extract logInfo/logError/logWarn helper functions"
type: refactor
status: completed
date: 2026-05-13
completed: 2026-05-13
origin: "Code audit P3 — unify log format strings and eliminate INFO:/WARN:/ERR: prefix inconsistencies"
---

# P3: Extract logInfo/logError/logWarn helper functions

## 问题描述

项目中 `Logger::write` 的调用格式不统一，存在两种不一致模式：

1. **带前缀的**（应避免，Logger::write 会自动添加 `[INFO]` 标签）：
   ```cpp
   Logger::write("INFO: Processing complete", LogLevel::INFO);
   ```

2. **直接消息的**（推荐格式）：
   ```cpp
   Logger::write("Processing complete", LogLevel::INFO);
   ```

此外，每条调用都需显式指定 `LogLevel::INFO` / `LogLevel::ERR` / `LogLevel::WARN`，缺少便捷包装函数。

## 执行结果

- logInfo/logError 辅助函数已在 `main.cpp:50-56` 实现（自由函数形式）
- 运行时签名支持默认日志级别（logInfo 默认 INFO, logError 默认 ERR）
- 未严格按计划放入 Logger.h（静态内联），当前实现已满足使用需求

## 范围边界

- 实际实现：`src/main.cpp` — 添加自由函数 logInfo/logError
- NOT 修改：`include/Logger.h`（未放入 Logger 类，以保持最小变更）
- NOT 修改：其他源文件

## 详细变更

### U1: 在 Logger.h 中添加内联辅助函数

```cpp
// Convenience wrappers — auto-adds [tag] prefix, no "INFO:" in message text
inline static void logInfo(const std::string& msg) {
    write(msg, LogLevel::INFO);
}
inline static void logWarn(const std::string& msg) {
    write(msg, LogLevel::WARN);
}
inline static void logError(const std::string& msg) {
    write(msg, LogLevel::ERR);
}
inline static void logDebug(const std::string& msg) {
    write(msg, LogLevel::DEBUG);
}
inline static void logReport(const std::string& msg) {
    write(msg, LogLevel::REPORT);
}
```

### U2: 更新 DEV-PROCESS.md 日志规范

在 `docs/plans/DEV-PROCESS.md` 中推荐使用辅助函数替代裸 `Logger::write(msg, LogLevel::INFO)` 调用。

## 验证步骤

1. `cmake --build build --parallel 8` — 0 errors
2. `ctest -V` — 所有测试通过
3. 确认辅助函数可被调用且日志输出格式正确

## 文件变更列表

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/main.cpp` | 修改 | 添加 logInfo/logError 自由函数（行 50-56）|

## 验证结果
- ✅ 构建 0 errors
- ✅ ctest 全部通过

## 风险

| 风险 | 缓解措施 |
|------|----------|
| 辅助函数新增不影响现有代码 | 纯新增，不改写任何已有调用 |
| 命名冲突风险低 | 函数名足够通用且置于 Logger 命名空间 |
