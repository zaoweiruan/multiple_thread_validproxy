---
title: "feat: Xray executable 配置值校验"
type: feat
status: implemented
date: 2026-06-09
---

# Xray executable 配置值校验

## 问题描述

`config.json` 中 `xray.executable` 字段指向 xray 可执行文件路径，是应用程序的核心配置项。原有校验存在以下不足：

1. **ConfigReader::load()** — 文件不存在时仅记录 WARN 级别日志，无用户可见提示
2. **ConfigDialog::validateConfig()** — 仅校验非空，不检查文件是否存在或路径格式是否正确
3. **无扩展名校验** — 非 `.exe` 路径静默接受，后续运行时才暴露错误

与此对比，`database.path` 已有严格的文件存在性校验（ERROR 日志 + 弹窗 + 中断加载），而 `xray.executable` 作为同样关键的核心路径，校验力度不足。

## 范围边界

- **修改:**
  - `src/ConfigReader.cpp:418-433` — 加载时校验升级
  - `src/ui/ConfigDialog.cpp:9, 209-222` — GUI 编辑器校验扩展
- **不修改:**
  - 数据库结构
  - xray_executable 的使用方（XrayManager、XrayInstance、XrayApi 等）
  - 配置文件格式

## 设计决策

### 决策 1：加载时不中断 vs 中断

| 方案 | 优点 | 缺点 |
|------|------|------|
| **选中：不中断加载** | 兼容 CLI-only 操作（`-show-sub` 等无需 xray）；用户可在 GUI 中修正 | 缺少 xray 的应用启动后功能受限 |
| 中断加载（返回 nullopt） | 与 database.path 行为一致 | 破坏 CLI-only 使用场景 |

**结论：** 弹出错误提示框但不中断加载。用户看到错误后可以选择修正路径或继续使用受限功能。

### 决策 2：双层级校验策略

| 层级 | 触发时机 | 校验内容 | 失败行为 |
|------|----------|----------|----------|
| ConfigReader::load() | 应用启动/配置文件加载 | ① 文件存在性 ② .exe 扩展名 | ① ERROR 日志 + 弹窗（不中断） ② WARN 日志 |
| ConfigDialog::validateConfig() | GUI 配置保存 | ① 非空 ② 文件存在性 ③ .exe 扩展名 | ① 错误弹窗 + 阻止保存 ② 错误弹窗 + 阻止保存 ③ 警告弹窗（允许继续） |

### 决策 3：扩展名校验策略

- 仅当文件扩展名存在且非 `.exe` 时触发警告
- 无扩展名的路径（如 `xray_binary`）静默通过
- 警告不阻止操作继续（允许非标准可执行文件名称）

## 详细变更

### C1: `src/ConfigReader.cpp` — 加载时校验

**位置：** `ConfigReader::load()` 末尾，database.path 校验之后

```cpp
// 修改前 — 仅 WARN 日志
if (!config.xray_executable.empty()) {
    std::filesystem::path xrayPath(config.xray_executable);
    if (!std::filesystem::exists(xrayPath)) {
        Logger::write("WARNING: xray executable not found: " + config.xray_executable, LogLevel::WARN);
    }
}

// 修改后 — ERROR 日志 + 弹窗 + 扩展名校验
if (!config.xray_executable.empty()) {
    std::filesystem::path xrayPath(config.xray_executable);
    if (!std::filesystem::exists(xrayPath)) {
        Logger::write("ERROR: xray executable not found: " + config.xray_executable, LogLevel::ERR);
        std::string errMsg = "Xray executable not found.\n\n";
        errMsg += "Config path:\n" + configPath + "\n\n";
        errMsg += "Xray path:\n" + config.xray_executable + "\n\n";
        errMsg += "Please check the path in the configuration file.";
        errorReporter_("Configuration Error", errMsg);
    }
    // Check file extension on Windows — .exe is expected for xray executable
    std::string ext = xrayPath.extension().string();
    if (!ext.empty() && ext != ".exe") {
        Logger::write("WARNING: xray executable should have .exe extension: " + config.xray_executable, LogLevel::WARN);
    }
}
```

**设计要点：**
- `errorReporter_` 的默认实现是 `MessageBoxA`，在单元测试中被替换为 no-op
- `std::filesystem::path::extension()` 对无扩展名路径返回空字符串，安全处理

### C2: `src/ui/ConfigDialog.cpp` — GUI 编辑器校验

**位置：** `validateConfig()` 方法，xray_executable 非空检查之后

```cpp
// 新增 #include <filesystem>

// 修改前 — 仅非空检查
if (editedConfig_.xray_executable.empty()) {
    wxMessageBox("Xray executable path cannot be empty", "Validation Error", wxOK | wxICON_ERROR);
    return false;
}

// 修改后 — 非空 + 文件存在 + 扩展名警告
if (editedConfig_.xray_executable.empty()) {
    wxMessageBox("Xray executable path cannot be empty", "Validation Error", wxOK | wxICON_ERROR);
    return false;
}
if (!std::filesystem::exists(editedConfig_.xray_executable)) {
    wxMessageBox("Xray executable file not found.\n\nPath:\n" + editedConfig_.xray_executable,
                 "Validation Error", wxOK | wxICON_ERROR);
    return false;
}
{
    std::filesystem::path xrayPath(editedConfig_.xray_executable);
    std::string ext = xrayPath.extension().string();
    if (!ext.empty() && ext != ".exe") {
        wxMessageBox("Xray executable should have .exe extension.\n\nCurrent:\n" + editedConfig_.xray_executable,
                     "Validation Warning", wxOK | wxICON_WARNING);
        // Continue — allow non-standard extensions
    }
}
```

**设计要点：**
- 文件不存在是硬错误 — 阻止保存，用户必须修正
- 扩展名不是 `.exe` 是软警告 — 允许用户继续（兼容非标准可执行文件）
- 使用独立作用域 `{}` 限定扩展名检查的临时变量生命周期

## 兼容性分析

| 场景 | 修改前行为 | 修改后行为 | 影响 |
|------|------------|------------|------|
| xray.exe 路径正确 | 正常加载 | 正常加载 | 无变化 |
| xray.exe 路径错误（加载时） | WARN 日志 | ERROR 日志 + 弹窗 | 用户可见错误提示 |
| xray.exe 路径错误（GUI 保存） | 保存成功 | 错误弹窗 + 阻止保存 | 用户必须修正路径 |
| CLI 模式无 xray | 静默加载空路径 | 静默加载空路径（路径为空时不触发校验） | 无变化 |
| xray 配置段不存在 | 静默加载空路径 | 静默加载空路径 | 无变化 |
| xray.exe 路径扩展名不是 .exe | 静默接受 | WARN 日志（加载时）/ 警告弹窗（GUI） | 用户知晓可能的配置问题 |

## 测试覆盖

| 测试 | 文件 | 验证 |
|------|------|------|
| FullConfigRoundTrip | `tests/test_config_reader_load.cpp:83` | xray 路径解析正确，不存在的文件触发 ERROR 日志但不中断加载 |
| ConfigReaderTest | `tests/test_config_reader.cpp:30` | save()/load() 往返不丢失 xray_executable |

- **构建验证：** `cmake --build build --parallel 8` — 3 个目标全部通过
- **全量测试：** `ctest -V` — 7/7 通过
- **关键日志输出：** `[ERROR] ERROR: xray executable not found: <path>` （单元测试中 `errorReporter_` 被替换为 no-op）
