# CLI/GUI Binary Split Design

> **长期记忆规范文档** — 定义 validproxy 项目从单个混合二进制拆分为两个独立二进制（CLI + GUI）的架构设计。
> 此文档是后续所有相关开发工作的权威参考。

---

## 1. 背景与问题

### 1.1 问题描述

用户在 cmd.exe 和 PowerShell 中运行 `validproxy.exe -TA` 时观察到：
- cmd 立即返回提示符，不等待测试完成
- validproxy 进程在后台运行并执行测试
- 控制台无任何输出

### 1.2 根因

`CMakeLists.txt` 第 193 行：
```cmake
set_target_properties(validproxy PROPERTIES WIN32_EXECUTABLE TRUE)
```

这会将 PE 子系统设为 `WINDOWS_GUI`（值 2）。Windows 对 GUI 子系统的行为是：
- `cmd.exe` **立即返回**（如同运行 `start`），不阻塞等待
- `std::cout` **无处输出**（没有控制台句柄）
- 进程在后台运行，无控制台窗口

设置为 `WIN32_EXECUTABLE TRUE` 是为了 wxWidgets GUI 模式启动时不显示黑框控制台，但它错误地**影响了所有 CLI 模式**。

### 1.3 约束条件

- 一个可执行文件同时服务 GUI 和 CLI 两种模式不可行
- wxWidgets 的 GUI 二进制必须为 WIN32 子系统（避免控制台窗口）
- CLI 二进制必须为 CONSOLE 子系统（cmd 才能阻塞等待）
- 核心业务逻辑（代理测试、配置生成、订阅更新等）被 GUI 和 CLI 共享

---

## 2. 架构方案

### 2.1 方案选择：双二进制分离

将当前单个 `validproxy.exe` 拆分为两个独立的可执行文件：

| 二进制 | 子系统 | 入口 | 职责 |
|--------|--------|------|------|
| `validproxy.exe` | WINDOWS_GUI | `src/main_gui.cpp`（新） | GUI 用户界面 |
| `validproxy-cli.exe` | WINDOWS_CUI | `src/main_cli.cpp`（从 `main.cpp` 剥离） | CLI 命令行工具 |

### 2.2 架构图

```
┌─────────────────────────────────────────────────────────┐
│                   共享核心库 (Shared Core)                │
│  ConfigGenerator  ConfigReader  Logger  PortManager     │
│  ProxyBatchTester  ProxyFinder  ProxyTester             │
│  ShareLink  SubitemUpdaterV2  UrlFetcher  Utils         │
│  XrayApi  XrayInstance  XrayManager                     │
│  include/*.h                                            │
└────────────────────────┬────────────────────────────────┘
                         │ 编译到两个 target
         ┌───────────────┴───────────────┐
         ▼                               ▼
┌──────────────────┐          ┌──────────────────────┐
│  validproxy.exe  │          │ validproxy-cli.exe   │
│  (WINDOWS_GUI)   │          │ (WINDOWS_CUI)        │
├──────────────────┤          ├──────────────────────┤
│ main_gui.cpp     │          │ main_cli.cpp         │
│ (新, 简化入口)    │          │ (剥离GUI代码的main)   │
├──────────────────┤          ├──────────────────────┤
│ src/ui/*         │          │ 无 UI 源码            │
│ wxWidgets UI     │          │                       │
├──────────────────┤          ├──────────────────────┤
│ wx::wxbase       │          │ Threads::Threads      │
│ wx::wxcore       │          │ CURL::libcurl         │
│ wx::wxadv        │          │ SQLite3               │
│ wx::wxaui        │          │ boost_json            │
│ wx::wxhtml       │          │                       │
│ wx::wxpropgrid   │          │                       │
│ wx::wxnet        │          │                       │
│ + 共享核心库     │          │ + 共享核心库          │
└──────────────────┘          └──────────────────────┘
```

### 2.3 关键设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| GUI 二进制名 | `validproxy.exe` | 保持与现有构建输出一致，兼容已有脚本 |
| CLI 二进制名 | `validproxy-cli.exe` | 清晰标识 CLI 角色 |
| GUI 入口文件 | `src/main_gui.cpp`（新建） | 完全分离，无条件编译的污染 |
| CLI 入口文件 | `src/main_cli.cpp`（从 `main.cpp` 重命名） | 剥离 GUI 代码，保留全部 CLI 功能 |
| `-ui`/`--ui` 在 CLI 中的行为 | 报错退出 | 提示用户使用 `validproxy.exe` |
| `-H`/`--help` 在 GUI 中的行为 | 静默忽略 | GUI 二进制无控制台，无法输出帮助信息 |
| 共享核心代码 | 每个 target 独立编译同一份 `.cpp` | 避免 DLL/共享库的复杂度，链接时优化会消除冗余 |
| 构建输出目录 | 两个 target 都输出到 `bin/` | 保持现有 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 设置 |

---

## 3. 文件清单

### 3.1 CLI target (`validproxy-cli.exe`)

```
src/main_cli.cpp            # 入口（从 main.cpp 重命名，剥离 GUI 代码）
src/ConfigGenerator.cpp     # 共享
src/ConfigReader.cpp        # 共享
src/Logger.cpp              # 共享
src/PortManager.cpp         # 共享
src/ProxyBatchTester.cpp    # 共享
src/ProxyFinder.cpp         # 共享
src/ProxyTester.cpp         # 共享
src/ShareLink.cpp           # 共享
src/SubitemUpdaterV2.cpp    # 共享
src/UrlFetcher.cpp          # 共享
src/Utils.cpp               # 共享
src/XrayApi.cpp             # 共享
src/XrayInstance.cpp        # 共享
src/XrayManager.cpp         # 共享
```

**链接库**：`Threads::Threads`, `CURL::libcurl`, `libsqlite3.a`, `libboost_json-*.a`

**无** wxWidgets、**无** UI 源码、**无** `WIN32_EXECUTABLE`、**无** `HAS_WXWIDGETS`。

### 3.2 GUI target (`validproxy.exe`)

**入口**：
```
src/main_gui.cpp             # 新建：简化 GUI 入口
```

**UI 源码**：
```
src/ui/UIApp.cpp            src/ui/UIApp.h
src/ui/MainFrame.cpp        src/ui/MainFrame.h
src/ui/AppController.cpp    src/ui/AppController.h
src/ui/Events.cpp           src/ui/Events.h
src/ui/SubscriptionPanel.cpp  src/ui/SubscriptionPanel.h
src/ui/ProxyListPanel.cpp   src/ui/ProxyListPanel.h
src/ui/ProxyListModel.cpp   src/ui/ProxyListModel.h
src/ui/ProxyDetailPanel.cpp src/ui/ProxyDetailPanel.h
src/ui/TestPanel.cpp        src/ui/TestPanel.h
src/ui/LogPanel.cpp         src/ui/LogPanel.h
src/ui/ConfigDialog.cpp     src/ui/ConfigDialog.h
src/ui/TrayIcon.cpp         src/ui/TrayIcon.h
src/ui/TestLogMediator.cpp  src/ui/TestLogMediator.h
src/ui/ToolbarIcons.h
src/ui/Icons.h
src/ui/icons.rc
src/ui/resources.h
```

**共享核心源码**（与 CLI target 相同的文件列表）：
```
src/ConfigGenerator.cpp     src/ConfigReader.cpp
src/Logger.cpp              src/PortManager.cpp
src/ProxyBatchTester.cpp    src/ProxyFinder.cpp
src/ProxyTester.cpp         src/ShareLink.cpp
src/SubitemUpdaterV2.cpp    src/UrlFetcher.cpp
src/Utils.cpp               src/XrayApi.cpp
src/XrayInstance.cpp        src/XrayManager.cpp
```

**链接库**：`wx::wxbase`, `wx::wxcore`, `wx::wxadv`, `wx::wxaui`, `wx::wxhtml`, `wx::wxpropgrid`, `wx::wxnet`, `Threads::Threads`, `CURL::libcurl`, `libsqlite3.a`, `libboost_json-*.a`

**编译定义**：`HAS_WXWIDGETS=1`
**目标属性**：`WIN32_EXECUTABLE TRUE`
**额外包含路径**：`E:/vcpkg/installed/x64-mingw-dynamic/include`, `${CMAKE_SOURCE_DIR}/src/ui`
**wx DLL 复制**：`copy_wx_dlls(validproxy)`

### 3.3 测试 target

所有测试 target（`test_curl_easy_handle`、`test_dedup`、`test_logger`、`test_sharelink`、`test_config_generator`）**保持不变**。它们不链接 wxWidgets，也不需要 `WIN32_EXECUTABLE`。

---

## 4. main_cli.cpp 规范

### 4.1 来源

将现有 `src/main.cpp` **重命名**为 `src/main_cli.cpp`，然后进行以下修改。

### 4.2 需移除的内容

1. **移除** `#include "UIApp.h"` 和 `#include <wx/event.h>`（原 `main.cpp:32-34`）
2. **移除** `consoleCtrlHandler` 函数中 `#ifdef HAS_WXWIDGETS` 块内的 wxWidgets 代码（`wxApp::GetInstance()->GetTopWindow()->Close()`），只保留通用的 `g_xrayManager->stopAll()`
3. **移除** 整个 `shouldLaunchGui` 分支（原 `main.cpp:328-428`，约 100 行代码）：
   - `isGuiWorker` 检测和 `wxEntry` 路径
   - `CreateProcessA` fork/spawn 子进程路径（GUI worker 模式）
   - `#else` 无 wxWidgets 的 fallback
4. **移除** `g_commandMode` 全局变量中与 GUI 相关的逻辑
5. **移除** 所有 `#ifdef HAS_WXWIDGETS` 条件编译块

### 4.3 需新增的内容

在 CLI 的 `-ui`/`--ui` 处理中（原 `main.cpp` 解析 `-ui` 的位置），改为：
```cpp
if (arg == "-ui" || arg == "--ui") {
    std::cerr << "Error: GUI mode not available in CLI build.\n"
              << "Use validproxy.exe for GUI mode.\n";
    return 1;
}
```

### 4.4 保持不变的内容

- 所有 CLI 命令处理（`-TA`、`-F`、`-FMIN`、`-T`、`-TU`、`-U`、`-UA`、`-S`、`-D`、`-IS`、`-show-sub`、`-G`）
- `consoleCtrlHandler` 的通用部分（`xrayManager->stopAll()`）
- `g_xrayManager` 全局变量
- `runDefaultTest()` 函数
- 同步阻塞的 `ProxyBatchTester::run()` → `testProxiesMultiThreaded()` → `thread::join` 调用链
- Logger 初始化、ConfigReader 加载、SQLite 数据库操作

---

## 5. main_gui.cpp 规范

### 5.1 文件位置

新建 `src/main_gui.cpp`，作为 GUI 二进制的唯一入口。

### 5.2 伪代码规范

```cpp
#include "UIApp.h"
#include <wx/app.h>

int main(int argc, char* argv[]) {
    // 仅解析 -c/--config 配置路径
    std::string configPath = "config.json";
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "-c" || arg == "--config") {
            if (++i < argc) configPath = argv[i];
        }
        // -ui, --ui, --gui: 静默接受（向前兼容）
        // -H, --help: 静默忽略（无控制台可输出）
        // 其他未知参数: 静默忽略
    }

    // 设置 GUI 全局状态（配置路径、日志等）

    // 启动 wxWidgets
    wxApp::SetInstance(new UIApp());
    return wxEntry(argc, argv);
}
```

### 5.3 禁止的行为

- ❌ 不解析任何 CLI 命令（`-TA`, `-F`, `-T`, `-U` 等）
- ❌ 不调用 `runDefaultTest()`、`runCliCommand()` 等 CLI 运行函数
- ❌ 不输出到 `std::cout`（可能无处可去）
- ❌ 不 fork/spawn 子进程（GUI 进程即最终进程）

---

## 6. CMakeLists.txt 构建规范

### 6.1 基本原则

- CLI target 和 GUI target 共享同一份核心 `.cpp` 文件（各自独立编译）
- 所有测试 target 保持不变
- `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 保持 `bin/`

### 6.2 CLI target 定义

```cmake
add_executable(validproxy-cli
    src/main_cli.cpp
    # 共享核心文件列表（见 3.1 节）
)
target_link_libraries(validproxy-cli PRIVATE
    Threads::Threads
    CURL::libcurl
    E:/vcpkg/installed/x64-mingw-static/lib/libsqlite3.a
    D:/boost_1_88_0/lib/libboost_json-mgw14-mt-x64-1_88.a
)
# 不设置 WIN32_EXECUTABLE → 默认 CONSOLE 子系统
```

### 6.3 GUI target 定义

```cmake
add_executable(validproxy
    src/main_gui.cpp
    # UI 文件列表（见 3.2 节）
    # 共享核心文件列表（见 3.2 节）
)
target_link_libraries(validproxy PRIVATE
    wx::wxbase wx::wxcore wx::wxadv wx::wxaui wx::wxhtml wx::wxpropgrid wx::wxnet
    Threads::Threads
    CURL::libcurl
    E:/vcpkg/installed/x64-mingw-static/lib/libsqlite3.a
    D:/boost_1_88_0/lib/libboost_json-mgw14-mt-x64-1_88.a
)
set_target_properties(validproxy PROPERTIES WIN32_EXECUTABLE TRUE)
target_compile_definitions(validproxy PRIVATE HAS_WXWIDGETS=1)
target_include_directories(validproxy PRIVATE
    "E:/vcpkg/installed/x64-mingw-dynamic/include"
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)
copy_wx_dlls(validproxy)
```

### 6.4 可选的共享源列表变量

为减少重复，可以将共享核心文件列表定义为 CMake 变量：

```cmake
set(CORE_SOURCES
    src/ConfigGenerator.cpp
    src/ConfigReader.cpp
    src/Logger.cpp
    src/PortManager.cpp
    src/ProxyBatchTester.cpp
    src/ProxyFinder.cpp
    src/ProxyTester.cpp
    src/ShareLink.cpp
    src/SubitemUpdaterV2.cpp
    src/UrlFetcher.cpp
    src/Utils.cpp
    src/XrayApi.cpp
    src/XrayInstance.cpp
    src/XrayManager.cpp
)
```

---

## 7. 向后兼容性

### 7.1 构建脚本

现有构建命令不受影响：
```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
```

构建完成后，`bin/` 目录下会同时出现 `validproxy.exe`（GUI）和 `validproxy-cli.exe`（CLI）。

### 7.2 用户使用习惯

| 使用场景 | 旧命令 | 新命令 | 兼容？ |
|---------|--------|--------|--------|
| GUI 模式 | `validproxy.exe` | `validproxy.exe` | ✅ 完全兼容 |
| GUI 模式 | `validproxy.exe -ui` | `validproxy.exe -ui` | ✅ 完全兼容 |
| GUI 模式 | `validproxy.exe --ui` | `validproxy.exe --ui` | ✅ 完全兼容 |
| 测试所有 | `validproxy.exe -TA` | `validproxy-cli.exe -TA` | ⚠️ 需改用 CLI 二进制 |
| 查找代理 | `validproxy.exe -F` | `validproxy-cli.exe -F` | ⚠️ 需改用 CLI 二进制 |

> 建议：创建批处理脚本 `validproxy.bat` 自动根据参数选择：
> - 无参数或 `-ui` → 启动 `validproxy.exe`
> - 其他参数 → 转发到 `validproxy-cli.exe`

### 7.3 测试兼容性

所有现有测试命令完全不受影响：
```bash
ctest -V        # 全量测试
ctest -R DedupTest -V  # 单测
```

---

## 8. 验证标准

| 验收项 | 验证方法 | 预期结果 |
|--------|---------|---------|
| GUI 二进制子系统 | `python3 check_subsystem.py` 或 `link /dump validproxy.exe` | `Subsystem: 2 (WINDOWS_GUI)` |
| CLI 二进制子系统 | 同上 | `Subsystem: 3 (WINDOWS_CUI)` |
| CLI 二进制阻塞 | `echo START && validproxy-cli.exe -TA && echo END` | `END` 在测试完成后才出现 |
| CLI 二进制无 wx 链接 | `ldd validproxy-cli.exe` 或 `dumpbin /imports` | 无 wx DLL 依赖 |
| GUI 二进制控制台 | 双击运行 | 无控制台窗口闪现 |
| GUI 二进制阻塞 | `validproxy.exe -ui` | cmd 返回提示符（GUI 子系统行为） |
| GUI 二进制 -H/--help | `validproxy.exe -H` | 无任何反应（静默忽略） |
| CLI 二进制 -ui | `validproxy-cli.exe -ui` | 打印错误并退出 |
| 全量测试 | `cmake --build build && ctest -V` | 所有测试通过 |
| GUI 功能 | 运行 `validproxy.exe` | GUI 窗口正常显示，功能完整 |
| CLI -F | `validproxy-cli.exe -F` | 找到可用代理 |

---

## 9. 长期维护规范

1. **禁止在共享核心代码中引入 wxWidgets 依赖**：任何 `src/*.cpp` 或 `include/*.h` 文件不得包含 wx 头文件、使用 wx 类型或 wx API。
2. **禁止在 UI 代码中引入 CLI 特定逻辑**：`src/ui/*` 文件不应引用 CLI 命令的全局变量、函数或常量。
3. **新功能添加时，必须评估属于哪个 target**：
   - 仅控制台交互功能 → `main_cli.cpp` + CLI target
   - 仅窗口界面功能 → `main_gui.cpp` + `src/ui/` + GUI target
   - 业务逻辑 → 共享核心代码（`src/*.cpp`）
4. **`WIN32_EXECUTABLE TRUE` 只能用于 GUI target**，CLI target 绝不能设置此属性。
5. **`HAS_WXWIDGETS` 编译定义只能用于 GUI target**，CLI target 绝不能定义此宏。

---

## 10. 参考

- 问题根因：`CMakeLists.txt:193` `set_target_properties(validproxy PROPERTIES WIN32_EXECUTABLE TRUE)`
- 现有 main.cpp：`src/main.cpp:909` 行（将被拆分为 `main_cli.cpp` + `main_gui.cpp`）
- CMake WIN32_EXECUTABLE 文档：https://cmake.org/cmake/help/latest/prop_tgt/WIN32_EXECUTABLE.html
- PE 子系统值：2 = WINDOWS_GUI, 3 = WINDOWS_CUI
