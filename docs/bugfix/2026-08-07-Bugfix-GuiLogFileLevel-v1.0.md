# Bugfix: GUI 启动日志未按 config.json file_level 过滤写入文件

**日期**: 2026-08-07
**模块**: main_gui.cpp / Logger
**严重程度**: 中（日志污染，违反配置语义）
**状态**: 已修复

---

## 问题描述

`config.json` 的 `log.file_level` 配置（生产配置为 `"ERROR"`）在 GUI 启动阶段不生效，导致低于配置级别的 INFO/DEBUG 日志仍被写入日志文件。

实际复现（修复前 `bin/log/ui_20260806_173705.log`）：

```
[2026-08-06 17:37:05] [INFO] gui entry: Logger::init completed
[2026-08-06 17:37:05] [DEBUG] SQL query: ...
[2026-08-06 17:37:05] [DEBUG] SQL by_subid: ...
```

配置 `file_level=ERROR` 时，INFO/DEBUG 行不应出现在文件中。

## 影响范围

- 仅 GUI 入口（`src/main_gui.cpp`）受影响；CLI 入口（`src/main_cli.cpp`）12 处为相同模式，本次不修改（后续一致性项）。
- `LoggerInstance::write()` 的文件过滤逻辑本身正确（`level >= fileLevel_` 才写），问题仅在初始化时机。

## 根因

`main_gui.cpp` 的 Logger 初始化顺序错误：

```cpp
// L58: 默认 fileLevel=DEBUG, consoleLevel=INFO
Logger::init(logDir.string(), "ui");
Logger::write("gui entry: Logger::init completed", LogLevel::INFO);   // L59: INFO 写入文件
Logger::setConsoleEnabled(false);
std::optional<config::AppConfig> appConfig = config::ConfigReader::load(configPath);  // L63: 内部 DEBUG SQL 日志写入文件
// L70-73: 此时才从 config 应用 file_level —— 已太晚
Logger::setFileLevel(Logger::stringToLevel(appConfig->log_file_level));
```

**核心矛盾**：`ConfigReader::load()` 内部会调用 `Logger::write()`（Step5 的 DEBUG SQL 诊断日志），因此 Logger 必须先于 load 初始化；但文件级别又来自 config.json。解决方案：在 `Logger::init` 之前先用 ConfigFileStore + ConfigJsonParser + LogConfigParser 预解析 log 段，用真实配置级别初始化 Logger。

**安全性**：预解析路径中 `LogConfigParser::parse()` 仅在字段类型错误时调用 `Logger::write(..., WARN)`；Logger 未 init 时 `Logger::defaultInstance()` 会创建 `outFile_=nullptr` 的实例，写文件条件短路跳过，控制台输出在 WIN32 子系统 GUI 下被丢弃——因此预解析在 init 之前调用无副作用，且后续 `Logger::init` 配置的是同一个 defaultInstance_。

## 修复内容

`src/main_gui.cpp` 在 `Logger::init` 前插入预解析块（全栈禁止 `auto`，全部显式类型）：

```cpp
// Pre-parse log section BEFORE Logger::init so the file level from
// config.json is applied from the very first log write.
// (ConfigReader::load() itself writes DEBUG SQL diagnostics, so the level
// must already be in effect before load runs.)
try {
    config::ConfigFileStore fileStore;
    std::string jsonStr = fileStore.read(configPath);
    config::ConfigJsonParser jsonParser;
    boost::json::value root = jsonParser.parse(jsonStr);
    if (!root.is_null() && root.is_object()) {
        config::AppConfig preConfig;
        config::LogConfigParser logParser;
        logParser.parse(root, preConfig, exeDir);
        Logger::init(logDir.string(), "ui",
                     Logger::stringToLevel(preConfig.log_file_level),
                     Logger::stringToLevel(preConfig.log_console_level));
        Logger::setFileEnabled(preConfig.log_enabled);
    } else {
        Logger::init(logDir.string(), "ui");
    }
} catch (...) {
    // Config file missing/unreadable: fall back to default levels (DEBUG/INFO)
    Logger::init(logDir.string(), "ui");
}
```

保留原有 L59 INFO 入口日志、L60 `setConsoleEnabled(false)`、L63 `ConfigReader::load`、L70-73 加载后应用级别（幂等，作为权威兜底保留）。

新增 include：

```cpp
#include "config/ConfigFileStore.h"
#include "config/ConfigJsonParser.h"
#include "config/sections/LogConfigParser.h"
```

## 验证

- 构建通过（Debug/Ninja，`cmake --build build --parallel 8`）
- `ctest -V` 全量测试通过（基线 21/21）
- 功能验证：`file_level="ERROR"` 时启动 GUI 数秒后结束，`bin/log/ui_*.log` 不再出现 INFO/DEBUG 行；`file_level="DEBUG"` 时恢复 DEBUG SQL 日志（与 2026-08-06-Bugfix-Logger-GUI-ConsoleLevel 文档中配置示例兼容）

## 配置文件结构（不变）

```json
{
  "log": {
    "enabled": true,
    "network_failures": true,
    "console_level": "WARN",
    "file_level": "ERROR"
  }
}
```

| 配置项 | 默认值 | 有效值 |
|--------|--------|--------|
| `log.enabled` | `true` | `true` / `false` |
| `log.network_failures` | `false` | `true` / `false` |
| `log.console_level` | `"INFO"` | `"TRACE"` / `"DEBUG"` / `"INFO"` / `"REPORT"` / `"WARN"` / `"ERR"` |
| `log.file_level` | `"DEBUG"` | `"TRACE"` / `"DEBUG"` / `"INFO"` / `"REPORT"` / `"WARN"` / `"ERR"` |

## 后续项（本次不实施）

- `src/main_cli.cpp` 12 处 `Logger::init` → load → setLevel 模式存在相同时机问题，后续可抽公共预解析工具函数统一两入口。
