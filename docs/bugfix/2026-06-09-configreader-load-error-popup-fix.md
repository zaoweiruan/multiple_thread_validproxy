# ConfigReader::load() 加载失败弹窗修复

- **日期**: 2026-06-09
- **修复人**: Agent
- **涉及文件**: `src/ConfigReader.cpp`, `src/main_gui.cpp`, `src/ui/UIApp.cpp`
- **类型**: Bugfix — 错误提示重复弹窗

---

## 问题描述

当数据库文件不存在时，GUI 模式下用户看到**两个弹窗**：

1. **"Database file not found"** — 从 `ConfigReader::load()` 新增的 `MessageBoxA`
2. **"Failed to load configuration file"** — 从 `main_gui.cpp` 调用方原有的通用 `MessageBoxA`

第二个弹窗信息过于笼统，用户无法区分是配置文件本身错误（文件不存在/JSON格式错）还是数据库文件不存在。

## 根因分析

`ConfigReader::load()` 对四种失败模式（文件打开失败、JSON 语法错误、JSON 根非 object、数据库文件不存在）均返回 `std::nullopt`，调用方无法区分具体失败原因，只能显示通用错误消息。

原有的代码设计：
- `load()` ：失败时仅记录 DEBUG 日志，返回 nullopt
- **2026-06-09 补充**：为 DB 文件不存在添加了 `MessageBoxA`，但调用方仍保留通用弹窗

导致 DB 文件不存在时发生弹窗重复。

## 修复方案

**原则**：`ConfigReader::load()` 作为最了解失败上下文的函数，对所有失败模式输出精确弹窗；调用方不再重复弹窗。

### 改动清单

#### 1. `src/ConfigReader.cpp` — 为所有失败模式添加弹窗

| 失败模式 | 弹窗标题 | 弹窗内容 |
|----------|----------|----------|
| 文件打开失败 | `"Configuration Error"` | "Failed to open configuration file.\n\nPath:\n{path}\n\nCheck that the file exists and is accessible." |
| JSON 语法错误 | `"Configuration Error"` | "Failed to parse configuration file.\n\nPath:\n{path}\n\nError:\n{exception.what()}" |
| JSON 根非 object | `"Configuration Error"` | "Invalid configuration file.\n\nPath:\n{path}\n\nThe root element must be a JSON object." |
| 数据库文件不存在 | `"Database Error"` | "Database file not found.\n\nConfig path:\n{configPath}\n\nDatabase path:\n{dbPath}\n\nThe application cannot start." |

所有弹窗使用 `MessageBoxA(NULL, ..., MB_ICONERROR | MB_OK)`。

#### 2. `src/main_gui.cpp` — 移除重复弹窗

```diff
-   if (!appConfig) {
-       std::string errMsg = "Failed to load configuration file.\n\n";
-       errMsg += "Config path:\n" + configPath;
-       MessageBoxA(NULL, errMsg.c_str(), "Configuration Error", MB_ICONERROR | MB_OK);
-       Logger::close();
-       return 1;
-   }
+   if (!appConfig) {
+       Logger::write("Failed to load configuration file: " + configPath, LogLevel::ERR);
+       Logger::close();
+       return 1;
+   }
```

#### 3. `src/ui/UIApp.cpp` — 移除重复弹窗

```diff
-               // Both config loads failed - show error and exit
-               wxMessageBox("Failed to load configuration file.\n\nTried:\n- " + configPath.string() + "\n- " + effectiveConfigPath + "\n\nThe application cannot start.",
-                            "Configuration Error", wxOK | wxICON_ERROR);
-               return false;
+               // Both config loads failed - specific error already shown by ConfigReader::load()
+               return false;
```

## 弹窗行为对比

| 失败场景 | 修复前 | 修复后 |
|----------|--------|--------|
| config.json 不存在 | 1 个弹窗: "Failed to load configuration file" | 1 个弹窗: "Failed to open configuration file" (含路径) |
| config.json 格式错误 | 1 个弹窗: "Failed to load configuration file" | 1 个弹窗: "Failed to parse configuration file" (含错误详情) |
| config.json 根非 object | 1 个弹窗: "Failed to load configuration file" | 1 个弹窗: "Invalid configuration file" (含路径) |
| 数据库文件不存在 | **2 个弹窗**: "Database file not found" + "Failed to load configuration file" | **1 个弹窗**: "Database file not found" (含配置路径和 DB 路径) |

## 测试验证

- 构建：`validproxy.exe` 和 `validproxy-cli.exe` 均链接成功
- 单元测试：6/6 全部通过（CurlEasyHandleTest, DedupTest, LoggerTest, ShareLinkTest, ConfigGeneratorTest, DeleteSubscriptionTest）
