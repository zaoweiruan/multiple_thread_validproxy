# Bugfix: GUI 入口 Logger 启动级别与 LogPanel 筛选器同步配置值

**日期**: 2026-08-06
**模块**: Logger / LogPanel / MainFrame / main_gui.cpp
**严重程度**: 低（功能完整性）
**状态**: 已修复

---

## 问题描述

`config.json` 的 `log` 段已支持 `file_level` 和 `console_level` 两个独立配置项。存在两个不一致点：

1. **`main_gui.cpp`** 启动时仅调用 `setFileLevel()`，遗漏 `setConsoleLevel()`，与 CLI 入口不一致。
2. **`LogPanel`** 初始化时将筛选下拉框硬编码为 `INFO`，不读取 `config.json` 的 `log_console_level`，导致 GUI 启动时日志筛选级别与配置文件不同步。

## 影响范围

- GUI 模式下 `console_level` 配置实际无效（GUI 强制 `setConsoleEnabled(false)`）
- CLI 模式不受影响
- 运行时配置热更新（`MainFrame`）已正确应用两个级别

## 根因

### 根因 1：main_gui.cpp 遗漏 setConsoleLevel

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

### 根因 2：LogPanel 筛选器硬编码 INFO

`LogPanel.cpp` 第 40-41 行：

```cpp
levelFilter_->SetSelection(2); // default: INFO  ← 硬编码
minLevel_ = LogLevel::INFO;   // ← 硬编码
```

构造函数不读取配置，用户修改 `config.json` 后重启 GUI 不会生效。

## 修复内容

### 修复 1：main_gui.cpp 补全 console_level

```cpp
// Apply config-specified log levels
Logger::setFileEnabled(appConfig->log_enabled);
Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
Logger::setConsoleLevel(Logger::stringToLevel(appConfig->log_console_level));
```

### 修复 2：LogPanel 新增 setInitialLogLevel()，构造函数调用 config 值

**LogPanel.h** 新增公共方法：
```cpp
void setInitialLogLevel(LogLevel level);
```

**LogPanel.cpp** 新增实现（同步 wxChoice 下拉框选项）：
```cpp
void LogPanel::setInitialLogLevel(LogLevel level) {
    minLevel_ = level;
    switch (level) {
        case LogLevel::TRACE: levelFilter_->SetSelection(0); break;
        case LogLevel::DEBUG: levelFilter_->SetSelection(1); break;
        case LogLevel::INFO:  levelFilter_->SetSelection(2); break;
        case LogLevel::REPORT: levelFilter_->SetSelection(3); break;
        case LogLevel::WARN:  levelFilter_->SetSelection(4); break;
        case LogLevel::ERR:   levelFilter_->SetSelection(5); break;
    }
}
```

**MainFrame.cpp** 构造函数中调用：
```cpp
logPanel_ = new LogPanel(centerPanel);
logPanel_->setInitialLogLevel(Logger::stringToLevel(config_.log_console_level));
```

## 验证

- 构建通过，21/21 测试全绿（37.68s）
- `config.json` 已有 `"log":{"enabled":true,"network_failures":true,"console_level":"ERROR","file_level":"DEBUG"}`
- `LogConfigParser`、`ConfigJsonSerializer`、`ConfigDialog`、`MainFrame` 均已正确支持两个字段
- GUI 启动时 LogPanel 筛选下拉框自动显示配置值（ERROR）
- 用户在 `config.json` 中修改 `log_console_level` 后重启 GUI，LogPanel 筛选器与配置同步

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
