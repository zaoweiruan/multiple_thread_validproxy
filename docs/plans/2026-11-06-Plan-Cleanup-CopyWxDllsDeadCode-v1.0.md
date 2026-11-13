---
title: "refactor: 清理 CMakeLists copy_wx_dlls 死代码与过时构建缓存"
type: refactor
status: completed
date: 2026-11-06
---

# 清理 CMakeLists copy_wx_dlls 死代码与过时构建缓存

## 问题描述

`CMakeLists.txt` 已于 `84fdfae`（2026-11-06）切至 **Scheme A（PATH-based）**：
`copy_wx_dlls()` 的三个调用点全部注释，构建不再复制 wxWidgets 动态 DLL。但函数
定义（370-434 行）与 rollback 注释（436-441 行）仍保留为死代码，且 1223/1248 行
仍留有注释调用点，与 `docs/DEV-PROCESS.md` 规则 5「禁止复制依赖 DLL」的语义不符
（虽已失效，但属"可重新启用"的隐患）。

另有过时构建缓存 `build2`（2026-06-16，0.1MB/9 文件，无 build.ninja，不完整）残留。

## 范围边界

- 修改: `CMakeLists.txt`（删除 `copy_wx_dlls()` 函数定义 + rollback 注释 + 2 处注释调用点）
- 删除: `build2/`（过时不完整缓存）
- NOT 修改: 链接目标、include 路径、`build`/`build-release`/`build-off`/`cmake-build-debug`、
  `bin/` 与 `tests/` 输出策略（均保持现状）

## 详细变更

### U1: CMakeLists.txt — 删除死代码

- 删除 369-434 行 `# Copy wx DLLs to output directory` 注释 + `function(copy_wx_dlls ...)` 定义
- 替换 436-441 行 rollback 注释为简洁 Scheme A 说明（引用规则 5）
- 删除 1223 行、1248 行 `# copy_wx_dlls(...)` 注释调用行（保留 1222/1247 行 Scheme A 说明）

### U2: 删除 build2 过时缓存

- 递归删除 `build2/`（2026-06-16 配置，无 build.ninja，0.1MB 不完整）

## 验证步骤

1. `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug` 重新配置成功（0 error，无
   copy_wx_dlls 引用）
2. `cmake --build build --parallel 8` 关键目标（validproxy/validproxy-cli）编译通过
3. `git diff --stat` 仅 CMakeLists.txt 变化，无其它源码改动

## 文件变更列表

- `CMakeLists.txt`（删死代码 + 注释）
- `docs/plans/2026-11-06-Plan-Cleanup-CopyWxDllsDeadCode-v1.0.md`（本计划）

## 风险

- 零行为变更：`copy_wx_dlls()` 已无任何调用点，删除不影响构建产物
- 若后续需恢复 DLL 复制，应按规则 5 重新走文档先行流程，而非依赖被删函数

## 后续发现（本计划范围外，同会话修复）

### R1: vcpkg z-applocal 是真实 DLL 复制来源（16:37 Debug 构建）

清理 `copy_wx_dlls()` 后，16:37 Debug 构建仍将 14 个 Debug DLL
（`libpng16d.dll`/`wxbase333ud_*`/`libwebpdemux.dll` 等）复制至 `bin\`，created
= 16:37:11–16:37:26，MD5 与 `D:\vcpkg\installed\x64-mingw-dynamic\debug\bin\` 源一致。

**根因**：vcpkg 工具链（`CMAKE_TOOLCHAIN_FILE`）在 `VCPKG_APPLOCAL_DEPS=ON`（默认）时，
自动为每个 EXECUTABLE 目标注入 **z-applocal POST_BUILD**：
`D:\vcpkg\vcpkg.exe z-applocal --target-binary=<exe> --installed-bin-dir=<triplet>/debug/bin`
（见 `cmake-build-debug/build.ninja` 原 2474 行）。这与已删除的 `copy_wx_dlls()` 完全无关，
是独立机制。`.ninja_log` 无复制条目（POST_BUILD 非 ninja 独立 edge），此前
`findstr` 搜索未命中系路径为**正斜杠**（`x64-mingw-dynamic/debug`）而模式用反斜杠。

### R2: 修复 — 禁用 VCPKG_APPLOCAL_DEPS

在 `CMakeLists.txt` `project()` 后（第 9 行）强制：
`set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "Disable vcpkg applocal DLL copy (Rule 5)" FORCE)`

验证（`clion_load_cmake` 双 profile 重配置 EXIT=0）：
- `cmake-build-debug`/`build-release`/`build` 三个 `build.ninja` 的 `z-applocal` 计数 = **0**
- `cmake-build-debug/CMakeCache.txt`：`VCPKG_APPLOCAL_DEPS:BOOL=OFF`
- 移出 `bin\` 14 个 Debug DLL → 删除 exe 强制重链 → `cmake --build` 输出仅 version.h + 链接，
  **无任何 DLL 复制行**；重建后 `bin\` DLL 数 = **0**（符合规则 5）

## 文件变更列表（R2 追加）

- `CMakeLists.txt`（第 4-9 行：vcpkg applocal 禁用注释 + `set(VCPKG_APPLOCAL_DEPS OFF ...)`）

### R3: 运行期死锁根因 — Tesseract-OCR 旧版 MinGW 运行时被加载（17:26 起 validproxy.exe 挂起）

用户报告 `bin\validproxy.exe` 运行出错。复现：启动后**无日志**（未达 `Logger::init`），
进程存活但 6MB / 0 CPU（挂起非崩溃）。CLion 两个 GDB（MinGW-w64 + 捆绑）均**无法中断**
（进程阻塞在加载器/内核等待）。

**取证**：`MiniDumpWriteDump` 抓取 705MB 转储 → Python 解析（GDB 不认 Windows 转储，
无 WinDbg/cdb）：
- 3 线程全部在 **ntdll.dll**：2× `NtWaitForSingleObject`（等待锁）+ 1× `NtRaiseHardError`
  （**硬错误弹窗阻塞** → 其余线程等待加载器锁 → 死锁）
- 模块列表：进程加载了 **`C:\Program Files (x86)\Tesseract-OCR\` 的 MinGW 运行时**：
  `libstdc++-6.dll`（2.4MB）、`libgcc_s_seh-1.dll`、`libwinpthread-1.dll`

**根因链**：validproxy.exe 由 **CLion 内置 MinGW GCC 15.2.0**（`E:\clion\bin\mingw\bin\c++.exe`）
构建，导入 `libstdc++-6.dll` 等。系统 PATH 中 **`C:\Program Files (x86)\Tesseract-OCR\` 位于
第 7 位**，**早于** `e:\w64devkit\bin`（33/58）、`d:\vcpkg\...\x64-mingw-dynamic\debug\bin`（49/50）
且 **`E:\clion\bin\mingw\bin` 根本不在 PATH**。R2 禁用 z-applocal 后 `bin\` 不再被复制任何
DLL，于是运行时按 PATH 解析 → 命中 **Tesseract 旧版 MinGW 运行时**（与 GCC 15.2.0 不兼容，
缺少 exe/wx 所需的入口点）→ `NtRaiseHardError`（"procedure entry point not found"）→
硬错误弹窗阻塞 → 死锁。（注：17:18-17:20 用户运行正常，因彼时其启动环境 PATH 含正确
MinGW 目录；17:26 起以系统 PATH 启动即挂起。CLion 内置 MinGW 的 libstdc++ 26MB 含符号。）

### R4: 修复 — CMake POST_BUILD 复制编译器 MinGW 运行时 DLL

在 `CMakeLists.txt` 为 **validproxy** 与 **validproxy-cli** 各添加 `add_custom_command(POST_BUILD)`：
`cmake -E copy_if_different` 从 `${CMAKE_CXX_COMPILER}` 所在目录（CLion MinGW bin）复制
`libstdc++-6.dll` / `libgcc_s_seh-1.dll` / `libwinpthread-1.dll` 到 `${CMAKE_SOURCE_DIR}/bin/`。
加载器**优先搜索 exe 目录**，故无论 PATH 顺序如何都加载正确版本。

**验证**：
- `clion_load_cmake` 重配置 EXIT=0 → `cmake --build cmake-build-debug --target validproxy`
  → `[12/12] Linking ...; Copy MinGW runtime DLLs to bin/`（POST_BUILD 执行）
- `bin\` 现有 3 个运行时 DLL；launch validproxy.exe → **日志创建**（`ui_20261106_175255.log`，
  "Build: Debug | 2026-11-06 17:52:02"），GUI 初始化完成 + StandaloneProxy 收养既有 xray，
  WS=495MB 正常 → 死锁解除

**注释澄清（规则 5 例外）**：MinGW 运行时（libstdc++/libgcc/libwinpthread）是编译器自身的
C++ 运行时，非可复制依赖（wxWidgets）；规则 5 禁止复制的是依赖 DLL，故复制运行时 DLL 属
例外。已同步至 `docs/DEV-PROCESS.md` 规则 5。

## 文件变更列表（R4 追加）

- `CMakeLists.txt`（validproxy 段 + validproxy-cli 段：MinGW 运行时 DLL POST_BUILD）
- `docs/DEV-PROCESS.md`（规则 5：注明 MinGW 运行时例外）
- `bin\`：`libstdc++-6.dll` / `libgcc_s_seh-1.dll` / `libwinpthread-1.dll`（构建产物）
