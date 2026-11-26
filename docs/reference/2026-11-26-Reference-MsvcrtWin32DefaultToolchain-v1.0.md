# 构建目录与默认工具链切换：msvcrt-win32 静态发布 v1.0

- 日期：2026-11-26
- 类型：reference（工程配置决策）
- 关联：`docs/specs/2026-11-11-Spec-MultiToolchainBuildMatrix-v1.0.md`、`docs/bugfix/2026-11-17-Bugfix-Win7-UCRT-libwinpthread-v1.0.md`

## 1. 目标与决策

将默认工具链切换为 **MinGW-Builds GCC 16.2.0 / MSVCRT / win32 threads**（`D:/mingw64-win32-msvcrt`），使 Release 的 `validproxy.exe` / `validproxy-cli.exe` 成为**全静态 EXE**：导入表只有 KERNEL32/msvcrt 与系统 DLL，无 `libwinpthread-1.dll`、无 `libstdc++-6.dll`、无 `libgcc_s_seh-1.dll`、无 wx DLL。

## 2. 变更清单

| 文件 | 变更 |
|---|---|
| `cmake/toolchains/mingw-msvcrt-win32.cmake`（新增） | 编译器指向 `D:/mingw64-win32-msvcrt`；CRT=msvcrt；wx/Catch2 根=`D:/vcpkg/installed/x64-mingw-static` |
| `CMakeLists.txt` | `VALIDPROXY_TOOLCHAIN` 默认 `msvcrt-win32`；新分支加载新 toolchain；仅 GUI/CLI target 加 `-static`（测试目标保持动态 libstdc++）；LTO 默认关闭（`VALIDPROXY_LTO=ON` 显式开启）；旧 libwinpthread 部署逻辑仅对 posix 线程工具链生效 |
| `src/platform/pthread_win32_shim.cpp`（新增） | `pthread_mutex_*` / `pthread_once` 兼容 shim：vcpkg 预编译静态库（Boost.JSON/Catch2）由 posix 线程工具链编出，需这些符号；win32 线程无 libwinpthread。按本工具链 `<pthread.h>` 的布局（`pthread_mutex_t=intptr_t` 句柄、一次性分配 `CRITICAL_SECTION`；`pthread_once_t=long`）实现 |
| `CMakePresets.json` | `default/debug/release` 统一指向新工具链；构建目录改为 `build-msvcrt-win32-debug` / `build-msvcrt-win32-release`；移除旧 `release-static` 预设 |
| `scripts/build.bat` / `scripts/test-ui.bat` / `scripts/build-and-test.bat` | 改用 `cmake --preset debug` 与 `ctest --test-dir build-msvcrt-win32-debug` |
| 构建目录 | 删除全部 8 个旧缓存（`build`、`build-rel-clion-dyn/static`、`build-release`、`build-tc-clion-msvcrt-Debug/Release`、`build-tc-mingw64-Debug`、`build-tc-wxstatic-probe`）；只保留 debug/release 两个 |

## 3. 关键事实

1. 切换工具链必须全新构建目录（CMake 缓存编译器）；本次已清理。
2. 预编译 wx/Catch2/Boost 静态库由 GCC 15.2 编出，但与 GCC 16.2 win32 线程 ABI 兼容（实测全量构建+测试通过）。
3. 测试 exe **不能**加 `-static`：Catch2/GTest 归档含不同 GCC 的 libstdc++ inline 定义，需 `--allow-multiple-definition` 且链接动态 libstdc++；GUI/CLI 的 `-static` 只作用于 shipped EXE。
4. Catch2 的 `pthread_once_t` 是 `long`（PTHREAD_ONCE_INIT=0），shim 用 `InterlockedCompareExchange` 实现 4 字节 once；曾误用 40 字节 `INIT_ONCE` 导致 0xC0000374，已按 `<pthread.h>` 布局纠正。
5. 产物按项目规范落 `bin/`（GUI/CLI/测试 exe），无需再复制到 `bin/worker/`。

## 4. 验证（2026-11-26）

| 项 | 命令 | 结果 |
|---|---|---|
| Release 配置 | `cmake --preset release --fresh` | EXIT 0；编译器 GCC 16.2.0 / msvcrt；LTO 默认关闭 |
| Release 构建 | `cmake --build build-msvcrt-win32-release --parallel 6` | EXIT 0（1350/1350） |
| Debug 配置+构建 | `cmake --preset debug --fresh` + `cmake --build --preset debug --target validproxy validproxy-cli UITests` | EXIT 0 |
| 产物依赖 | `objdump -p bin/validproxy.exe` | 仅系统 DLL；GUI 31,395,338 B / CLI 9,967,371 B |
| 非 UI 回归（两配置） | `ctest --test-dir build-msvcrt-win32-{debug,release} -E "^UI_"` | 均 67/67 EXIT 0 |
| UI 门禁 | `scripts/build-and-test.bat` | EXIT 0 / ALL TESTS PASSED，17/17（234.13 s，config-guard OK） |
| 工作区 | `git status` | 无未授权改动；旧 build 缓存已清理 |

## 5. 备注

- `-DNDEBUG` Release 编译与 `-static` 链接组合在 LTO 关闭时稳定；如需恢复 LTO 必须重建全部依赖以保证同编译器 ABI。
- 旧 `VALIDPROXY_STATIC_WX=ON` 的 Win7 libwinpthread 部署路径保留给 posix 线程工具链，msvcrt-win32 不触发。
