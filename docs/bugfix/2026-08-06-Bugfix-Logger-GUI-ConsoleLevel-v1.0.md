# Bugfix: GUI 入口 Logger 启动级别未从配置应用 console_level

**日期**: 2026-08-06
**模块**: Logger / main_gui.cpp
**严重程度**: 低（功能完整性）
**状态**: 已修复

---

## 问题描述

`config.json` 的 `log` 段已支持 `file_level` 和 `console_level` 两个独立配置项。CLI 入口 (`main_cli.cpp`) 在启动时正确读取并应用这两个值，但 GUI 入口 (`main_gui.cpp`) 仅应用了 `file_level`，遗漏了 `console_level`。

## 影响范围

- GUI 模式下 `console_level` 配置实际无效（GUI 强制 `setConsoleEnabled(false)`）
- CLI 模式不受影响
- 运行时配置热更新（`MainFrame`）已正确应用两个级别

## 根因

`main_gui.cpp` 第 70-72 行：

```cpp
// Apply config-specified log levels
Logger::setFileEnabled(appConfig->log_enabled);
Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
// ← 缺少 setConsoleLevel 调用
```

对比 CLI 入口（`main_cli.cpp`）的正确实现：

```cpp
Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
```

## 修复内容

在 `main_gui.cpp` 添加一行 `setConsoleLevel` 调用，与 CLI 入口保持一致：

```cpp
// Apply config-specified log levels
Logger::setFileEnabled(appConfig->log_enabled);
Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
```

## 验证

- 构建通过，21/21 测试全绿
- `config.json` 已有 `"log":{"enabled":true,"network_failures":true,"console_level":"ERROR","file_level":"DEBUG"}`
- `LogConfigParser`、`ConfigJsonSerializer`、`ConfigDialog`、`MainFrame` 均已正确支持两个字段

## 配置文件结构

```json
{
  "log": {
    "enabled": true,
    "network_failures": true,
    "console_level": "ERROR",
    "file_level": "DEBUG"
  }
}
```

| 配置项 | 默认值 | 有效值 |
|--------|--------|--------|
| `log.enabled` | `true` | `true` / `false` |
| `log.network_failures` | `false` | `true` / `false` |
| `log.console_level` | `"INFO"` | `"TRACE"` / `"DEBUG"` / `"INFO"` / `"REPORT"` / `"WARN"` / `"ERR"` |
| `log.file_level` | `"DEBUG"` | `"TRACE"` / `"DEBUG"` / `"INFO"` / `"REPORT"` / `"WARN"` / `"ERR"` |
