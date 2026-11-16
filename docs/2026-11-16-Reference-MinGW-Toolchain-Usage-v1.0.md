---
title: "reference: MinGW 工具链使用规范（工具链矩阵 + ABI 闸门 + 产物隔离）"
type: reference
status: maintained
date: 2026-11-16
module: build-system
version: v1.1
---

# MinGW 工具链使用规范 v1.1

> 目标读者：需要在本项目选择/切换 MinGW 工具链、配置依赖树、排查 ABI 混链的开发者和 AI Agent。
> 实现依据：`docs/specs/2026-11-11-Spec-MultiToolchainBuildMatrix-v1.0.md`（§12 落地记录）。
> 变更时同步更新本文件与 `docs/INDEX.md`。
> **v1.1（2026-11-16）**：工具链 ID 更名为 `clion-msvcrt` / `mingw-ucrt`（原 `clion` / `ucrt`）。旧 ID 不再被选择器接受；切换须用全新构建目录。

---

## 1. 工具链总览

项目通过 CMake 工具链文件声明式选择编译器。当前提供 **2 个** 工具链，均位于 `cmake/toolchains/`：

| ID | 工具链文件 | 编译器根 | 版本 | CRT | 说明 |
|---|---|---|---|---|---|
| `clion-msvcrt` | `cmake/toolchains/mingw-clion.cmake` | `E:\clion\bin\mingw` | GCC 15.2.0 | **MSVCRT** | 零风险基线；wx/Catch2 走 vcpkg msvcrt 树 |
| `mingw-ucrt` | `cmake/toolchains/mingw-ucrt.cmake` | `E:\mingw64` | GCC 16.2.0 | **UCRT** | GUI 闪退修复链；wx/Catch2 走 `temp\vcpkg-ucrt-root` UCRT 树（默认） |

> 默认工具链 = **`mingw-ucrt`**（保留 2026-11-13 已验证的 GUI 闪退修复状态）。默认值可由 `-DVALIDPROXY_TOOLCHAIN` 或 `$ENV{VALIDPROXY_TOOLCHAIN}` 覆盖（见 §2）。

## 2. 依赖策略（硬约束）

| 依赖 | 链接方式 | 说明 |
|---|---|---|
| **wxWidgets** | **动态** | 唯一动态依赖；DLL 由工具链对应 CRT 树提供 |
| 其余全部（boost / sqlite3 / gtest / Catch2 / curl / zlib / …） | **静态** | vcpkg 静态树 |

依赖根（每工具链默认，可在 `-D` 覆盖）：

| 变量 | `clion-msvcrt` 默认 | `mingw-ucrt` 默认 |
|---|---|---|
| `WX_VCPKG_DIR`（wx 动态） | `D:\vcpkg\installed\x64-mingw-dynamic` | `temp\vcpkg-ucrt-root\x64-mingw-dynamic-ucrt` |
| `CATCH2_VCPKG_DIR`（Catch2 静态） | `D:\vcpkg\installed\x64-mingw-static` | `temp\vcpkg-ucrt-root\x64-mingw-static-ucrt` |
| 其余静态依赖 | `D:\vcpkg\installed\x64-mingw-static`（项目内固定引用） | 同左 |

**CRT 一致性红线**：wx DLL 的 CRT 族必须与编译器 CRT 族一致（msvcrt↔msvcrt、ucrt↔ucrt）。混链（如 UCRT exe + msvcrt wx DLL）会导致 `0xC0000374` 堆损坏闪退，见 `docs/bugfix/2026-11-13-Bugfix-GuiFlashCrash-WxToolchainMismatch-v1.0.md`。ABI 闸门（§4）在 configure 期拦截。

## 3. 选择机制

`VALIDPROXY_TOOLCHAIN`，解析顺序（高→低）：

1. `-DVALIDPROXY_TOOLCHAIN=<id>`（CMake 缓存变量）
2. `$ENV{VALIDPROXY_TOOLCHAIN}=<id>`
3. 默认 `mingw-ucrt`

```powershell
# UCRT（默认，GUI 闪退修复链）
cmake -S . -B build-tc-mingw-ucrt -G "Ninja" -DCMAKE_BUILD_TYPE=Debug

# CLion 内置（MSVCRT 基线）
cmake -S . -B build-tc-clion-msvcrt -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DVALIDPROXY_TOOLCHAIN=clion-msvcrt

# 环境变量方式
$env:VALIDPROXY_TOOLCHAIN = "clion-msvcrt"
cmake -S . -B build-tc-clion-msvcrt -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
```

无效 id → `FATAL_ERROR`，列出合法值：

```
VALIDPROXY_TOOLCHAIN='bogus' is unknown.  Valid values: clion-msvcrt, mingw-ucrt.
```

### 3.1 关键约束：工具链 ↔ 构建目录强绑定

切换 `VALIDPROXY_TOOLCHAIN` 会改变 `CMAKE_TOOLCHAIN_FILE`，**同一构建目录内已缓存的编译器会胜出**。因此：

- **每个工具链使用独立构建目录**（如 `build-tc-mingw-ucrt` / `build-tc-clion-msvcrt`），绝不混用。
- 切换工具链 = 新建构建目录（或删除旧目录 `CMakeCache.txt` 后重配），而不是复用。
- 产物隔离（§6）默认关闭时，各工具链会覆盖源树 `bin/`/`tests/`；如需并存，用 `VALIDPROXY_ISOLATE_OUTPUTS=ON`。

### 3.2 会话内触发：命令行 vs CLion

**命令行 / CI / PowerShell 会话**——用 `VALIDPROXY_TOOLCHAIN`（§3）驱动本项目工具链文件，这是唯一能精确绑定编译器 + CRT + 依赖根的路径：

```powershell
# 当前会话固定为 clion-msvcrt（后续 configure 均继承）
$env:VALIDPROXY_TOOLCHAIN = "clion-msvcrt"
cmake -S . -B build-tc-clion-msvcrt -G "Ninja" -DCMAKE_BUILD_TYPE=Debug

# 或只对单次 configure 生效
cmake -S . -B build-tc-clion-msvcrt -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DVALIDPROXY_TOOLCHAIN=clion-msvcrt
```

**CLion IDE 会话**——CLion 的 CMake profile 自带 `-DCMAKE_TOOLCHAIN_FILE=...\vcpkg.cmake`（见 `.idea/workspace.xml` `GENERATION_OPTIONS`），此时 `VALIDPROXY_TOOLCHAIN` 选择器**跳过**（外部 toolchain 文件已显式给定），本项目工具链文件不加载，`VALIDPROXY_TC_CRT`/每工具链依赖根均未设置。编译器由 **CLion Toolchain 设置**（Settings → Build, Execution, Deployment → Toolchains）决定；配置期 ABI 闸门自动降级为「编译器 CRT 对照仅 WARN + 以 wx DLL CRT 为权威判据」（§4），保证仍能拦截 `0xC0000374` 混链。该路径默认依赖 `D:\vcpkg\installed\x64-mingw-*`（msvcrt）树，与 CLion 内置 GCC 15.2.0/MSVCRT 匹配。

> **CLion 内用 UCRT 修复链**：需把 CLion Toolchain 切到 `E:\mingw64`，并在 CMake profile 的 `GENERATION_OPTIONS` 追加 `-DVALIDPROXY_TOOLCHAIN=mingw-ucrt -DWX_VCPKG_DIR=<ucrt-wx-树> -DCATCH2_VCPKG_DIR=<ucrt-catch2-树>`；但由于 CLion 仍传入 `-DCMAKE_TOOLCHAIN_FILE`，选择器跳过、每工具链根不生效，因此 **UCRT 依赖根必须显式传 `-DWX_VCPKG_DIR`/`-DCATCH2_VCPKG_DIR`**，否则 wx 检查会因 msvcrt/UCRT 不一致而 FATAL。CLion 下优先保持默认 msvcrt 基线，UCRT 链建议命令行/CI 触发。

## 4. 配置期 ABI 闸门（`cmake/AbiGuard.cmake`）

`project()` 后自动 include。configure 期执行，失败即 `FATAL_ERROR`：

1. **编译器 CRT 探测**：`${CMAKE_CXX_COMPILER} -E -dM -x c++ <probe>`（`#include <cstdio>`），解析 `_UCRT` / `__MSVCRT_VERSION__` → `detected_crt`；与工具链文件 `VALIDPROXY_TC_CRT` 对照。
2. **wx DLL CRT 探测**：取 `${WX_VCPKG_DIR}/debug/bin/wx*.dll` 首个，`objdump -p` 导入表判定（`api-ms-win-crt-*` → UCRT；`msvcrt.dll` → MSVCRT）；与 `detected_crt` 对照。

判定：

| 条件 | 动作 |
|---|---|
| 编译器 CRT ≠ 工具链声明 CRT | `FATAL_ERROR` |
| wx DLL CRT ≠ 编译器 CRT | `FATAL_ERROR`（文案含 `0xC0000374` 失效模式 + 修复指引） |
| 外部 `-DCMAKE_TOOLCHAIN_FILE`（`VALIDPROXY_TC_CRT` 未设置，CLion 原生路径） | 编译器 CRT 对照降为 `WARNING`；**wx DLL CRT 对照仍为权威**（能拦截混链） |
| Catch2 主版本漂移 | `WARNING`（best-effort） |
| `-DVALIDPROXY_ALLOW_ABI_MISMATCH=ON` | 上述 FATAL 降为 WARNING（**逃生舱，默认 OFF，CI 禁用**） |

> 外部 toolchain 文件路径下，`WX_VCPKG_DIR`/`CATCH2_VCPKG_DIR` 未由工具链文件提供时，回退到项目既定 vcpkg 根（`VCPKG_ROOT`，未定义则 `D:\vcpkg`）的 `installed/x64-mingw-{dynamic,static}`（msvcrt 树），与 CLion 内置 GCC 15.2.0/MSVCRT 匹配。

失败示例（UCRT 编译器 + msvcrt wx）：

```
CMake Error at cmake/AbiGuard.cmake:91 (message):
  [AbiGuard] wx DLL CRT 'msvcrt' != compiler CRT 'ucrt';
  (wx=D:/vcpkg/installed/x64-mingw-dynamic/debug/bin/wxbase32ud_gcc_custom.dll).
  This is the GUI flash-crash (0xC0000374) failure mode; — a UCRT exe must
  load UCRT wx DLLs, an msvcrt exe must load msvcrt wx DLLs.  Reconfigure
  with the matching VALIDPROXY_TOOLCHAIN, or pass
  -DVALIDPROXY_ALLOW_ABI_MISMATCH=ON to override.
```

## 5. 构建 / 测试

```powershell
# 配置 + 构建（--parallel 8）
cmake -S . -B build-tc-ucrt -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tc-ucrt --parallel 8

# 测试（PATH 前置：工具链运行时 + 隔离 bin）
$env:PATH = "E:\eclipse_workspace\multiple_thread_validproxy\build-tc-ucrt\out\bin;E:\mingw64\bin;" + $env:PATH
ctest --test-dir build-tc-ucrt
```

> PATH 前置顺序：**exe 目录（隔离 bin）> 工具链运行时**。若未隔离，wx DLL 需由 `VALIDPROXY_COPY_WX_RUNTIME=ON` 置入 exe 目录或由 PATH 提供（见 §7）。

## 6. 产物隔离（`VALIDPROXY_ISOLATE_OUTPUTS`）

| 值 | 产物目录 | 语义 |
|---|---|---|
| `OFF`（默认） | 源树 `bin/`、`tests/` | 零回归，UI 门禁/`build-and-test.bat` 不变；多工具链会互相覆盖 |
| `ON` | `<build>/out/bin`、`<build>/out/tests` | 每工具链独立产物，互不污染；UI 测试经 `VALIDPROXY_APP_EXE`/`VALIDPROXY_APP_ROOT` 指向隔离 exe |

## 7. wx 运行时部署（`VALIDPROXY_COPY_WX_RUNTIME`）

默认 `OFF`（遵守 DEV-PROCESS 规则 5「禁止复制依赖 DLL」）。置 `ON` 时经 POST_BUILD 把 `${WX_VCPKG_DIR}/debug/bin/*.dll` 复制到 exe 目录——**Windows 加载器先搜 exe 目录**，保证 GUI 双击时加载到与 exe CRT 匹配的 wx（根治默认 PATH 上 msvcrt wx 被优先命中的 `0xC0000374` 重演）。

## 8. 故障排查

| 症状 | 排查 / 处置 |
|---|---|
| `Could NOT find SQLite3` | 全新目录需 `CMAKE_LIBRARY_PATH`/`CMAKE_INCLUDE_PATH` 种子（已内置）；手动 `-DSQLite3_LIBRARY/-DSQLite3_INCLUDE_DIR` 会因 `find_library` 覆盖而失效 |
| `ninja: error: 'include/version.h' ... no known rule` | `update_version_h` 的 `BYPRODUCTS` 声明解决（已内置）；仅全新目录触发 |
| ABI 闸门 FATAL | 用匹配工具链重新 configure（`clion-msvcrt`↔`mingw-ucrt`），或重建对应 CRT 的依赖树 |
| GUI 双击闪退 `0xC0000374` | 用 `mingw-ucrt` 工具链 + `VALIDPROXY_COPY_WX_RUNTIME=ON`，确保 exe 目录 UCRT wx |
| UITests 链接 `seekpos(fpos<int>)` 未定义 | Catch2 静态库 CRT 与编译器不符 → 用匹配工具链的 `CATCH2_VCPKG_DIR` |

## 9. 验证状态（2026-11-13 实测；2026-11-16 补充外部路径修复）

- `mingw-ucrt`：configure + full build EXIT 0（1109/1109）、全量 ctest 71/72（唯一失败 `UI_FLOATINGWIDGET` = 既有已知 flaky，单跑复验通过）、UI 16/16。
- `clion-msvcrt`：configure EXIT 0、build EXIT 0、ctest 71/72（同 flaky 单跑复验过）。
- ABI 闸门：正向通过 / 反向（clion-msvcrt 编译器 + UCRT wx）FATAL / 无效 id FATAL / 逃生舱降级，全部按预期。
- 改动文件无 `auto`（项目硬约束）。

**2026-11-16 补充（外部 `-DCMAKE_TOOLCHAIN_FILE` 路径回归修复）**：CLion 原生路径（profile 自带 vcpkg toolchain 文件，无 `VALIDPROXY_TOOLCHAIN`）曾被 ABI 闸门误 FATAL（`VALIDPROXY_TC_CRT` 未设置 → `compiler CRT 'msvcrt' != ''`）。修复：
1. `cmake/AbiGuard.cmake`：`VALIDPROXY_TC_CRT` 未设置（外部 toolchain 文件）时，编译器 CRT 对照降为 `WARNING`，wx DLL CRT 对照仍为权威判据。
2. `CMakeLists.txt`：AbiGuard 之前为 `WX_VCPKG_DIR`/`CATCH2_VCPKG_DIR` 提供回退默认（`VCPKG_ROOT` 未定义则 `D:\vcpkg` 的 msvcrt 树），否则 wx 权威检查被静默跳过。

复验（2026-11-16，全新目录）：CLion 原生路径 EXIT 0（`compilerCRT=msvcrt wxCRT=msvcrt`）；`mingw-ucrt` 选择器路径 EXIT 0（`wxCRT=ucrt`）；`clion-msvcrt` 选择器路径 EXIT 0（`wxCRT=msvcrt`）；反向 ABI（clion-msvcrt 编译器 + UCRT wx）仍 FATAL（含 `0xC0000374`）。

**2026-11-16 追加（ID 更名）**：`VALIDPROXY_TOOLCHAIN` 取值由 `clion`/`ucrt` 改为 `clion-msvcrt`/`mingw-ucrt`；旧 id 不再接受（FATAL 列出新合法值）。验证（全新目录）：`clion-msvcrt` configure EXIT 0（`wxCRT=msvcrt`）、`mingw-ucrt` configure EXIT 0（`wxCRT=ucrt`）、旧 id `ucrt`/`clion` 均 FATAL 并提示新合法值。

## 10. 关联文档

- `docs/specs/2026-11-11-Spec-MultiToolchainBuildMatrix-v1.0.md`（设计 + §11 混链事故实证 + §12 落地）
- `docs/bugfix/2026-11-13-Bugfix-GuiFlashCrash-WxToolchainMismatch-v1.0.md`（`0xC0000374` 根因）
- `docs/bugfix/2026-11-13-Bugfix-XrayManagerWinsockInit-v1.0.md`（Winsock，同批回归）
- `docs/reports/2026-11-11-Report-Toolchain-Verification-v1.0.md`（工具链环境基线）
- `docs/DEV-PROCESS.md`（规则 5：禁止复制依赖 DLL）
