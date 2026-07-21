# Git Tag Versioning — 基于 Git Tag 的统一版本管理方案

> **文档状态**: draft  
> **创建日期**: 2026-07-15  
> **目标版本**: v1.5.0  
> **文档类型**: Spec — 技术方案

---

## 1. 背景与现状分析

### 1.1 当前问题

当前项目的版本管理存在以下问题：

| 问题 | 位置 | 现状 |
|------|------|------|
| **版本硬编码** | `CMakeLists.txt:2` | `project(... VERSION 1.0.3)` 硬编码，与 Git tag 脱节 |
| **About 窗口版本** | `MainFrame.cpp:903-906` | 硬编码字符串 `"validproxy v1.0"`，从未更新 |
| **Git tag 与编译版本不一致** | 全局 | 最新 tag `v1.4.7`，但 CMake 版本仍为 `1.0.3` |
| **无版本头文件** | `include/` | 不存在 `version.h`，程序中无可直接引用的版本常量 |
| **Release 发布无自动化** | 发布流程 | 无自动化机制确保 tag 与编译产物版本一致 |

### 1.2 Git Tag 现状

```
v1.4.7 ← HEAD (latest)
v1.4.6
v1.4.5
...
v1.1.8
v1.0.0-multithread
```

Tag 命名规范：`v<major>.<minor>.<patch>`，符合语义化版本 (SemVer) 标准。

### 1.3 核心需求

1. **单一事实来源**: Git tag 是编译版本、发布 release 的唯一权威来源
2. **CMake 编译时自动获取**: 通过 `git describe` 获取当前 tag，注入 CMake 变量
3. **自动生成 `version.h`**: 编译时生成头文件，包含完整的版本信息
4. **About 窗口消费**: 读取 `version.h` 中的常量，在 About 对话框显示
5. **回退策略**: 当无法获取 Git tag 时（如从源码 tarball 编译），使用合理的默认值

---

## 2. 设计方案

### 2.1 总体架构

```
┌─────────────────────────────────────────────────────┐
│                   Git Tag                           │
│            git describe --tags --always             │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│              CMake configure 阶段                    │
│  execute_process(COMMAND git describe ...)           │
│  解析 → PROJECT_VERSION_MAJOR/MINOR/PATCH           │
│  configure_file(version.h.in → version.h)           │
└──────────────────────┬──────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────┐
│            include/version.h (编译时生成)            │
│  #define APP_VERSION         "1.4.7"                │
│  #define APP_VERSION_MAJOR   1                      │
│  #define APP_VERSION_MINOR   4                      │
│  #define APP_VERSION_PATCH   7                      │
│  #define APP_GIT_COMMIT      "abc1234"              │
│  #define APP_GIT_TAG         "v1.4.7"               │
│  #define APP_BUILD_TYPE      "Debug|Release"        │
│  #define APP_BUILD_TIME      "2026-07-15 09:00:00"  │
└──────────┬───────────────────────────┬──────────────┘
           │                           │
           ▼                           ▼
┌──────────────────────┐   ┌──────────────────────────┐
│    About 对话框       │   │    Logger 启动日志       │
│  "validproxy v1.4.7" │   │  "Version: 1.4.7"       │
│  "Commit: abc1234"   │   │  "Build: 2026-07-15 ..." │
└──────────────────────┘   └──────────────────────────┘
```

### 2.2 关键设计决策

| 决策 | 选项 | 选择 | 理由 |
|------|------|------|------|
| Tag 格式 | `v1.2.3` vs `1.2.3` | `v1.2.3` (保留 `v` 前缀) | 与现有 tag 命名一致，CMake 解析时去除 `v` |
| 获取方式 | `git describe` vs 纯 CMake | `git describe` | 更可靠，支持 dirty 检测 |
| 生成方式 | `configure_file()` vs 自定义 target | `configure_file()` | 标准 CMake 机制，在 configure 阶段执行，不依赖编译 |
| 版本号存储 | CMake `project()` VERSION 属性 | 自动覆盖 | 保持 `cmake --version` 等工具兼容 |
| 回退策略 | 无 tag → 使用 `0.0.0` | `0.0.0` + commit hash | 明确标识未标记版本 |

### 2.3 CMake Git 版本检测策略

按优先级尝试以下策略：

```
策略 1: git describe --tags --dirty --always
  → 输出样例: v1.4.7, v1.4.7-2-gabc1234, v1.4.7-dirty, abc1234
  → 成功: 解析 tag 中的版本号

策略 2: git tag --list 'v*' --sort=-creatordate | head -1
  → 当 describe 失败时的回退（如 detached HEAD 无 tag 祖先时）

策略 3: GIT_TAG_FILE 环境变量
  → 从 CI 环境（GitHub Actions 等）注入 tag 值

策略 4: 默认值 0.0.0
  → 纯源码 tarball 编译时的最终回退
```

---

## 3. 详细实现

### 3.1 CMake 脚本 — `cmake/GetGitVersion.cmake`

新建模块文件，封装版本检测逻辑。

```cmake
# cmake/GetGitVersion.cmake
# 从 Git tag 获取版本信息，回退链保证始终有有效值

function(get_git_version)
    # 策略 1: git describe
    find_package(Git QUIET)
    if(Git_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_DESCRIBE
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()

    if(GIT_DESCRIBE MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)")
        # 干净 tag: v1.4.7 → 1.4.7
        set(VERSION_MAJOR "${CMAKE_MATCH_1}" PARENT_SCOPE)
        set(VERSION_MINOR "${CMAKE_MATCH_2}" PARENT_SCOPE)
        set(VERSION_PATCH "${CMAKE_MATCH_3}" PARENT_SCOPE)
        set(VERSION_TAG "${GIT_DESCRIBE}" PARENT_SCOPE)
        set(VERSION_DIRTY "0" PARENT_SCOPE)
    elseif(GIT_DESCRIBE MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)-([0-9]+)-g([0-9a-f]+)(-dirty)?$")
        # tag 之后有提交: v1.4.7-2-gabc1234
        set(VERSION_MAJOR "${CMAKE_MATCH_1}" PARENT_SCOPE)
        set(VERSION_MINOR "${CMAKE_MATCH_2}" PARENT_SCOPE)
        set(VERSION_PATCH "${CMAKE_MATCH_3}" PARENT_SCOPE)
        set(VERSION_TAG "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}-dev.${CMAKE_MATCH_4}" PARENT_SCOPE)
        set(VERSION_COMMIT "${CMAKE_MATCH_5}" PARENT_SCOPE)
        if(CMAKE_MATCH_6)
            set(VERSION_DIRTY "1" PARENT_SCOPE)
        else()
            set(VERSION_DIRTY "0" PARENT_SCOPE)
        endif()
        set(VERSION_AHEAD "${CMAKE_MATCH_4}" PARENT_SCOPE)
    elseif(GIT_DESCRIBE MATCHES "^[0-9a-f]+(-dirty)?$")
        # 只有 commit hash: abc1234
        set(VERSION_MAJOR "0" PARENT_SCOPE)
        set(VERSION_MINOR "0" PARENT_SCOPE)
        set(VERSION_PATCH "0" PARENT_SCOPE)
        set(VERSION_TAG "${GIT_DESCRIBE}" PARENT_SCOPE)
        set(VERSION_COMMIT "${GIT_DESCRIBE}" PARENT_SCOPE)
        if(CMAKE_MATCH_1)
            set(VERSION_DIRTY "1" PARENT_SCOPE)
        else()
            set(VERSION_DIRTY "0" PARENT_SCOPE)
        endif()
    else()
        # 所有策略失败，使用默认值
        set(VERSION_MAJOR "0" PARENT_SCOPE)
        set(VERSION_MINOR "0" PARENT_SCOPE)
        set(VERSION_PATCH "0" PARENT_SCOPE)
        set(VERSION_TAG "unknown" PARENT_SCOPE)
        set(VERSION_DIRTY "0" PARENT_SCOPE)
    endif()

    # 没有 dirty 标记时确保 VERSION_DIRTY 有值
    if(NOT DEFINED VERSION_DIRTY)
        set(VERSION_DIRTY "0" PARENT_SCOPE)
    endif()
endfunction()
```

### 3.2 版本头文件模板 — `include/version.h.in`

```c
// include/version.h.in
// 自动生成 — 请勿手动编辑
// 源文件: version.h.in
// 生成时间: @BUILD_TIMESTAMP@

#ifndef VALIDPROXY_VERSION_H
#define VALIDPROXY_VERSION_H

// 版本号组件（整数，可用于编译时条件判断）
#define APP_VERSION_MAJOR   @VERSION_MAJOR@
#define APP_VERSION_MINOR   @VERSION_MINOR@
#define APP_VERSION_PATCH   @VERSION_PATCH@

// 版本号字符串（显示用）
#define APP_VERSION         "@VERSION_MAJOR@.@VERSION_MINOR@.@VERSION_PATCH@"
#define APP_VERSION_FULL    "v@VERSION_MAJOR@.@VERSION_MINOR@.@VERSION_PATCH@"

// Git 元信息
#define APP_GIT_TAG         "@VERSION_TAG@"
#define APP_GIT_COMMIT      "@VERSION_COMMIT@"
#define APP_GIT_DIRTY       @VERSION_DIRTY@

// 构建信息
#define APP_BUILD_TYPE      "@CMAKE_BUILD_TYPE@"
#define APP_BUILD_TIME      "@BUILD_TIMESTAMP@"

// 项目名称
#define APP_NAME            "validproxy"

#endif // VALIDPROXY_VERSION_H
```

**字段说明**:

| 宏 | 含义 | 示例值 |
|----|------|--------|
| `APP_VERSION_MAJOR` | 主版本号 | `1` |
| `APP_VERSION_MINOR` | 次版本号 | `4` |
| `APP_VERSION_PATCH` | 修订号 | `7` |
| `APP_VERSION` | 简洁版本 | `"1.4.7"` |
| `APP_VERSION_FULL` | 完整版本（含 v 前缀） | `"v1.4.7"` |
| `APP_GIT_TAG` | Git tag 原始输出 | `"v1.4.7"` 或 `"v1.4.7-2-gabc1234"` |
| `APP_GIT_COMMIT` | Git commit hash | `"abc1234"` |
| `APP_GIT_DIRTY` | 工作区是否含未提交修改 | `0` 或 `1` |
| `APP_BUILD_TYPE` | CMake 构建类型 | `"Debug"` 或 `"Release"` |
| `APP_BUILD_TIME` | CMake configure 时间戳 | `"2026-07-15 09:00:00"` |

### 3.3 CMakeLists.txt 修改

在现有 `project(... VERSION ...)` 行之后插入版本检测逻辑：

```cmake
# 当前第 2 行 → 保持 project() 声明
project(multiple_thread_validproxy VERSION 1.0.3 LANGUAGES CXX)

# ↓↓↓ 新增：Git 版本覆写 ↓↓↓
include(cmake/GetGitVersion.cmake)
get_git_version()

# 用 Git tag 版本号覆写 project() 的 VERSION
# 仅在成功获取 Git tag 时覆写，保留 CMake 默认值作为回退
if(DEFINED VERSION_MAJOR)
    set(PROJECT_VERSION_MAJOR "${VERSION_MAJOR}")
    set(PROJECT_VERSION_MINOR "${VERSION_MINOR}")
    set(PROJECT_VERSION_PATCH "${VERSION_PATCH}")
    set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
endif()

# 生成版本头文件
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/version.h.in
    ${CMAKE_CURRENT_SOURCE_DIR}/include/version.h
    @ONLY
)
```

**需要注意的关键点**：

1. `include/version.h` 是 **生成文件**，应加入 `.gitignore`
2. `include/version.h.in` 是源文件，受版本控制
3. `configure_file()` 在每次 CMake configure 时执行，不依赖编译

### 3.4 About 对话框改造 — `src/ui/MainFrame.cpp`

```cpp
// 文件顶部添加
#include "version.h"

// 替换现有 onMenuAbout
void MainFrame::onMenuAbout(wxCommandEvent&) {
    wxString info;
    info.Printf(
        L"%hs v%hs\n\n"
        L"版本: %hs\n"
        L"Git Tag: %hs\n"
        L"Git Commit: %hs\n"
        L"构建类型: %hs\n"
        L"构建时间: %hs\n"
        L"编译器: %hs\n\n"
        L"管理并测试您的代理订阅。",
        APP_NAME,
        APP_VERSION,
        APP_VERSION_FULL,
        APP_GIT_TAG,
        APP_GIT_COMMIT,
        APP_BUILD_TYPE,
        APP_BUILD_TIME,
        __VERSION__
    );

    wxMessageBox(info,
                 L"关于 validproxy",
                 wxOK | wxICON_INFORMATION,
                 this);
}
```

**预期 About 窗口展示效果**:

```
validproxy v1.4.7

版本: v1.4.7
Git Tag: v1.4.7
Git Commit: abc1234
构建类型: Debug
构建时间: 2026-07-15 09:00:00
编译器: GNU GCC 13.2.0

管理并测试您的代理订阅。
```

### 3.5 启动日志输出

在 `main_gui.cpp`（GUI 入口）和 `main_cli.cpp`（CLI 入口）添加版本日志：

```cpp
#include "version.h"
#include "Logger.h"

// 在程序初始化早期
Logger::write(REPORT, "----------------------------------------");
Logger::write(REPORT, APP_NAME, " v", APP_VERSION, " (", APP_GIT_TAG, ")");
Logger::write(REPORT, "Build: ", APP_BUILD_TYPE, " | ", APP_BUILD_TIME);
Logger::write(REPORT, "Compiler: ", __VERSION__);
Logger::write(REPORT, "----------------------------------------");
```

日志输出示例：
```
[R] ----------------------------------------
[R] validproxy v1.4.7 (v1.4.7)
[R] Build: Debug | 2026-07-15 09:00:00
[R] Compiler: GNU GCC 13.2.0
[R] ----------------------------------------
```

### 3.6 `.gitignore` 更新

```
# 版本头文件（自动生成）
include/version.h
```

---

## 4. 测试策略

| 测试场景 | 前置条件 | 预期行为 | 验证方式 |
|---------|---------|---------|---------|
| 干净 tag 编译 | `git checkout v1.4.7` | `APP_VERSION = "1.4.7"` | 编译后检查 `include/version.h` |
| tag 后开发中编译 | `git checkout v1.4.7 && git commit --allow-empty` | `APP_VERSION = "1.4.7"`, `APP_GIT_TAG = "v1.4.7-1-gxxx"` | 同上 |
| dirty 工作区 | 有未提交文件 | `APP_GIT_DIRTY = 1` | 同上 |
| 无 tag 环境 | `git tag | xargs git tag -d` 或 tarball | `APP_VERSION = "0.0.0"` | 同上 |
| 跨构建类型 | `-DCMAKE_BUILD_TYPE=Release` | `APP_BUILD_TYPE = "Release"` | 同上 |
| About 窗口 | GUI 模式启动 | 窗口内容与 `version.h` 一致 | 目测 |
| 启动日志 | CLI/GUI 启动 | 日志行包含版本信息 | 检查日志文件 |

---

## 5. 风险评估

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|---------|
| `configure_file()` 写入 `include/` 目录可能触发 IDE 重新索引 | 轻微 | 中 | 确认将 `version.h` 加入 `.gitignore` |
| Git tag 格式变更导致解析失败 | 版本回退为 `0.0.0` | 低 | 正则匹配支持 `v` 前缀 + `-rc`/`-beta` 后缀 |
| 多人协作时 tag 不一致 | 各开发者编译版本不同 | 低 | Tag 应由维护者在 CI 上统一创建 |
| CI 环境中无 Git 历史 | 版本降级为 `0.0.0` | 中 | CI 应通过 `git fetch --tags --unshallow` 获取完整历史 |
| `version.h` 被误提交 | 过时版本污染仓库 | 低 | `.gitignore` + code review 预防 |

---

## 6. 实施计划

### Phase 1 — 核心机制（3 个任务）

| 任务 | 文件 | 工作量 |
|------|------|--------|
| 1. 创建 `cmake/GetGitVersion.cmake` | 新建 | ~30 行 |
| 2. 创建 `include/version.h.in` 模板 | 新建 | ~30 行 |
| 3. 修改 `CMakeLists.txt` 集成版本检测 | 修改 +8 行 | 2 行 include + 生成调用 |

### Phase 2 — 消费端（3 个任务）

| 任务 | 文件 | 工作量 |
|------|------|--------|
| 4. 改造 About 对话框引用 `version.h` | `MainFrame.cpp` | ~20 行 |
| 5. 启动日志添加版本输出 | `main_gui.cpp` + `main_cli.cpp` | ~10 行 |
| 6. `.gitignore` 更新 | `.gitignore` | +1 行 |

### Phase 3 — CI 集成（可选）

| 任务 | 说明 |
|------|------|
| 7. Release CI 自动创建并推送 tag | GitHub Actions workflow |
| 8. CI 构建产物命名含版本号 | 如 `validproxy-v1.4.7.zip` |

---

## 7. 文件变更清单

| 操作 | 文件 | 说明 |
|------|------|------|
| **新建** | `cmake/GetGitVersion.cmake` | Git 版本检测模块 |
| **新建** | `include/version.h.in` | 版本头文件 CMake 模板 |
| **修改** | `CMakeLists.txt` | 集成 `get_git_version()` 和 `configure_file()` |
| **修改** | `include/.gitignore`（或根 `.gitignore`） | 排除自动生成的 `version.h` |
| **修改** | `src/ui/MainFrame.cpp` | About 对话框使用 `APP_VERSION` 等宏 |
| **修改** | `src/main_gui.cpp` | 启动日志输出版本信息 |
| **修改** | `src/main_cli.cpp` | 启动日志输出版本信息 |

---

## 8. 附录

### 8.1 `git describe` 输出格式

```
# 场景 1: HEAD 正好在 tag 上
v1.4.7

# 场景 2: Tag 之后有新的提交
v1.4.7-2-gabc1234
  ↑tag    ↑提交数 ↑commit hash

# 场景 3: 工作区有未提交修改
v1.4.7-dirty
v1.4.7-2-gabc1234-dirty

# 场景 4: 无 tag
abc1234
abc1234-dirty
```

### 8.2 CMake `execute_process` 使用规范

- `WORKING_DIRECTORY` 必须指定为 `${CMAKE_SOURCE_DIR}`，确保在 git 仓库根目录执行
- `ERROR_QUIET` 避免在非 git 环境中产生 CMake 警告
- `OUTPUT_STRIP_TRAILING_WHITESPACE` 去除换行符

### 8.3 与现有 `configversion` 字段的关系

`Profileitem::configversion`（对应 ShareLink 导出的 ConfigVersion 字段）是代理配置项的版本标识，与本方案的**应用版本号**无关。两者互不影响。

---

> **End of Spec**
