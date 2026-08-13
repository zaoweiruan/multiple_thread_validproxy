# Bugfix: About 窗口编译时间不准确 + 配置窗口 console 日志级别不同步 LogPanel

**日期**: 2026-08-07
**模块**: CMakeLists.txt / version.h 生成链 / MainFrame.cpp
**严重程度**: 低（功能完整性与一致性）
**状态**: 已修复

---

## 问题描述

两个 GUI 相关问题：

1. **About 窗口编译时间不准确** — 菜单「关于」弹窗与启动日志中 `APP_BUILD_TIME` 显示的是上次 `cmake configure` 的时刻，而非本次实际编译时刻。增量构建（未重新 configure）后版本头不再重新生成，时间戳陈旧。
2. **配置窗口修改 console 日志级别不同步日志面板** — 通过「配置」对话框修改控制台日志级别并保存后，`Logger` 全局级别已热更新，但 `LogPanel` 界面下拉框与过滤阈值 `minLevel_` 保持旧值，界面筛选与配置不一致。

## 影响范围

- 问题 1：About 弹窗（`MainFrame::onMenuAbout`）、GUI/CLI 启动 Build 日志（`main_gui.cpp:106`、`main_cli.cpp:130,239`）显示陈旧时间戳；版本头内容陈旧会影响调试定位。
- 问题 2：仅 GUI 配置对话框运行时保存路径受影响（启动路径已正确，见 2026-08-06 修复）；CLI 不受影响。

## 根因

### 根因 1：version.h 仅在 configure 时生成

`CMakeLists.txt` 原有实现：

```cmake
add_custom_command(
    OUTPUT ${VERSION_H}                      # build/include/version.h
    COMMAND python .../generate_version_h.py <9 参数>
    DEPENDS ${VERSION_H_IN} ... generate_version_h.py ${CMAKE_BINARY_DIR}/build.ninja
    VERBATIM
)
add_custom_target(update_version_h DEPENDS ${VERSION_H})
```

`add_custom_command` 的依赖中包含 `${CMAKE_BINARY_DIR}/build.ninja`，而 `build.ninja` 仅在 configure 时重写 → **增量构建（不重新 configure）时 version.h 不重新生成**，`@BUILD_TIMESTAMP@` 停留于上次 configure 时刻（实测：version.h 08:50:46，而含增量修复的 exe 09:30:24 构建）。

### 根因 2：MainFrame 保存回调未同步 LogPanel

`MainFrame::onMenuConfig` 保存回调仅更新 Logger 全局级别：

```cpp
// Apply log level changes
Logger::setFileLevel(Logger::stringToLevel(cfg.log_file_level));
Logger::setConsoleLevel(Logger::stringToLevel(cfg.log_console_level));
```

未调用 `logPanel_` 的任何同步方法。启动路径（构造时 `logPanel_->setInitialLogLevel(...)`）已有正确实现，但运行时配置对话框保存路径遗漏。2026-08-06 的修复文档验证标准仅覆盖「修改 config.json 后重启 GUI」启动路径，未覆盖配置对话框热保存路径。

## 修复方案

### 修复 1：CMakeLists.txt — version.h 改为每次构建生成

将 `add_custom_command` + `add_custom_target(update_version_h DEPENDS ...)` 组合替换为单一 **ALL target**，命令内联：

```cmake
# Build-time version header custom target — an ALL target that runs the
# generator on every `cmake --build`, so APP_BUILD_TIME reflects the actual
# build moment instead of the last configure time.
add_custom_target(update_version_h ALL
    COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_version_h.py
        ${VERSION_H_IN}
        ${VERSION_H}
        ${CMAKE_BUILD_TYPE}
        ${VERSION_MAJOR}
        ${VERSION_MINOR}
        ${VERSION_PATCH}
        ${VERSION_TAG}
        ${VERSION_COMMIT}
        ${VERSION_DIRTY}
    COMMENT "[version.h] Regenerating build-time version header..."
    VERBATIM
)
```

- 每次 `cmake --build`（含默认 ALL 与指定 target 构建）都会执行脚本，`APP_BUILD_TIME` = 本次构建时刻。
- 依赖 version.h 的 TU 仅 3 个（`main_gui.cpp`、`main_cli.cpp`、`MainFrame.cpp`），Ninja 通过 depfile 感知 version.h mtime 变化触发重编，开销毫秒级。
- `add_dependencies(validproxy-cli update_version_h)`（:208）与 `add_dependencies(validproxy update_version_h)`（:291）保留不变。

### 修复 2：MainFrame.cpp — 保存回调同步 LogPanel 筛选

`src/ui/MainFrame.cpp:867-869` 追加：

```cpp
        // Apply log level changes
        Logger::setFileLevel(Logger::stringToLevel(cfg.log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(cfg.log_console_level));

        // Keep the log panel's visible level filter in sync with the new console level
        if (logPanel_) {
            logPanel_->setInitialLogLevel(Logger::stringToLevel(cfg.log_console_level));
        }
```

复用现有 `setInitialLogLevel(LogLevel)`——该方法同时更新 `minLevel_` 与下拉框 selection（0-5=TRACE..ERR），无需新增 API。

## 验证

- `cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`：配置通过，`Git version: 1.4.9 (tag: v1.4.9-7-gdc2bf8a-dirty)`。
- `cmake --build build --parallel 8`：输出首步 `[1/3] [version.h] Regenerating build-time version header...`，`build/include/version.h` 刷新为 `APP_BUILD_TIME "2026-08-07 10:33:49"`（本次构建时刻），`bin/validproxy.exe` 重新链接。
- `cmake --build build --target validproxy-cli --parallel 8`：version.h 再次刷新 → `main_cli.cpp` 重编 → `bin/validproxy-cli.exe` 重新链接（验证 depfile 重编链生效）。
- `ctest -V`：21 项中 19 项通过；`CurlEasyHandleTest` 与 `NetworkMonitorTest` 失败原因为 `curl_error=28 (Timeout was reached)` 网络环境性超时，与本修复无关。
- GUI 运行时验证（需人工）：菜单「关于」显示本次构建时间；「配置」窗口修改控制台级别保存后日志面板下拉框与过滤同步。

## 二次修复（2026-08-07）：Ninja unscanned 规则不跟踪头文件依赖

首轮修复后 `bin\worker\log\ui_20260807_105729.log` 的 Build 行仍显示 08:50:46（旧 configure 时刻）。

### 根因（二次）

`add_custom_target(update_version_h ALL)` 无输出文件，不构成任何编译边的依赖；且本项目编译规则为 Ninja `_unscanned` 系列（如 `CXX_COMPILER__validproxy_unscanned_Debug`），**不生成/不读取 depfile**，ninja 无法感知 version.h 被哪些 TU include。因此 version.h 虽每次构建刷新，但未改源码的 TU 不重编：

- `main_gui.cpp.obj`（08:50:59 从未重编）→ 内嵌旧时间戳 08:50:46，贡献启动 Build 日志；
- `MainFrame.cpp.obj`（10:33:10 重编，因修复 2 源码改动）→ 内嵌 10:32:59，贡献 About 弹窗；
- 链接产物同时含两个时间戳（`rg -a` 于 exe 实测确认），时间戳不一致。

### 修复（二次）：CMakeLists.txt — OBJECT_DEPENDS 钉死依赖

`CMakeLists.txt`（update_version_h ALL target 定义之后）追加：

```cmake
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include)
set_property(SOURCE
    src/main_gui.cpp
    src/main_cli.cpp
    src/ui/MainFrame.cpp
    APPEND PROPERTY OBJECT_DEPENDS ${VERSION_H})
```

`OBJECT_DEPENDS` 将 version.h 声明为这 3 个 TU（唯一内嵌 APP_BUILD_TIME 的编译单元）的显式对象依赖，无视 `_unscanned` 规则——version.h mtime 变化即触发重编。

### 验证（二次）

- 重新 configure + `cmake --build build --parallel 8`：`[1/4] version.h Regenerating` → `[2/4] Building main_gui.cpp.obj` → `[3/4] Building MainFrame.cpp.obj` → `[4/4] Linking bin/validproxy.exe`（main_gui 重编 ✅）。
- `rg -a -o "2026-08-07 [0-9:]+" bin/validproxy.exe`：仅剩唯一时间戳 `2026-08-07 11:02:10`（旧 08:50:46 消失）。
- `cmake --build build --target validproxy-cli --parallel 8`：main_cli.cpp.obj 重编，cli 内嵌唯一 `2026-08-07 11:02:38`；version.h 最终 `APP_BUILD_TIME "2026-08-07 11:02:38"`。
- `ctest --test-dir build`：20/21 通过，仅 NetworkMonitorTest 失败（环境性：curl probe http://127.0.0.1:1 超时，与基线一致）。

## 三次修复（2026-08-12）：源码树 stale `include/version.h` 遮蔽生成头

尽管 `update_version_h` ALL target + `OBJECT_DEPENDS` 机制正确，`bin/validproxy.exe` 在 2026-08-12 17:30:54 重建后仍内嵌旧时间戳 `2026-08-07 16:36:03`。

### 根因（三次）

`CMakeLists.txt` 含 `include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)`，编译器在源码树 `E:\eclipse_workspace\multiple_thread_validproxy\include\version.h` 找到 stale 文件（08/07/2026 16:36:03），优先于 `build/include/version.h`（08/12/2026 16:25:43）。虽然 `update_version_h` 每次构建刷新 `build/include/version.h`，但编译单元 include 的是源码树的 stale 副本，导致嵌入旧时间戳。

### 修复（三次）

删除源码树中的 stale 生成文件：

```powershell
Remove-Item "E:\eclipse_workspace\multiple_thread_validproxy\include\version.h"
```

`.gitignore` 已含 `/include/version.h`（line 21），删除不产生工作区噪声。`update_version_h` ALL target 每次构建继续在 `build/include/` 生成 fresh 头文件；`OBJECT_DEPENDS` 钉死 3 个 TU 重编机制保持不变。

### 验证（三次）

- `include/version.h` 不存在（源码树已清理）。
- `build/include/version.h` 为 fresh 生成文件：`APP_BUILD_TIME "2026-08-12 17:39:19"`。
- `bin/validproxy.exe` 重新链接（`2026/8/12 17:45:10`），内嵌唯一时间戳 `2026-08-12 17:39:19`，旧时间戳 `2026-08-07 16:36:03` 消失。
- `ctest --output-on-failure`：24/24 通过。
- `cmake --build build --parallel 8`：347/347 targets 完成，无错误。

## 涉及文件

| 文件 | 变更 |
|------|------|
| `include/version.h` | **已删除**（stale 生成文件，源码树残留遮蔽 `build/include/version.h`；`.gitignore` 已覆盖） |
| `CMakeLists.txt` | 无变更（ALL target + OBJECT_DEPENDS 机制保持正确） |
| `src/ui/MainFrame.cpp` | onMenuConfig 保存回调追加 logPanel_->setInitialLogLevel 同步 |
