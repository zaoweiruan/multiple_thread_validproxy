---
title: "spec: 多工具链构建矩阵（工具链选择 + Debug/Release 分离）"
type: spec
status: draft
date: 2026-11-11
module: build-system
version: v1.0
---

# 多工具链构建矩阵（Toolchain Selection & Debug/Release Matrix）v1.0

> 变更注记（2026-11-16）：实施落地后，工具链 ID 由 `clion`/`ucrt` **更名**为 `clion-msvcrt`/`mingw-ucrt`（原 `ucrt`→`mingw-ucrt`、原 `clion`→`clion-msvcrt`），以消除「编译器来源 + CRT 族」歧义。本文件保留原始设计命名；**当前行为以 `docs/2026-11-16-Reference-MinGW-Toolchain-Usage-v1.0.md` v1.1 为准**。

## 0. TL;DR

| 结论 | 内容 |
|---|---|
| **能做到什么** | 通过 `-DVALIDPROXY_TOOLCHAIN=<id>` 或环境变量 `VALIDPROXY_TOOLCHAIN`，配合 `CMAKE_BUILD_TYPE`，一条命令切换 3 套工具链 × Debug/Release；每套独立构建目录，互不污染 |
| **当前能真正产出可用二进制的只有 T1** | T1（CLion GCC 15.2.0 + 现有 vcpkg 树）已实测全量构建/回归通过 |
| **T2（w64devkit GCC 16.2.0）卡在链接** | 已复现：静态 `libstdc++.a` 的 locale 对象引用 `__imp_wctype`/`__imp_mbrtowc`/`__imp_wcrtomb` 未定义（与 vcpkg 无关，最小复现仅 1 个 boost 头文件） |
| **T3（llvm-mingw 23.1.5 UCRT）编译可过、ABI 不兼容** | 已实测 3 个代表性 TU 编译通过；但该发行版默认 **libc++ + UCRT**，**不含 libstdc++**，与现有 libstdc++/MSVCRT 的 wx DLL、静态依赖树不可混用 |
| **本方案的核心价值** | 把"选工具链"变成**声明式配置 + 配置期 ABI 闸门**：T1 零风险照跑；T2/T3 一旦依赖树具备即可启用，缺依赖时**配置期 fail-fast** 并给出可执行的修复命令，而不是让人在 1700 行 CMakeLists 里改路径 |

---

## 1. 目标与非目标

### 1.1 目标

1. **单一开关**：`VALIDPROXY_TOOLCHAIN` 环境变量或 `-D` 参数二选一即可完成工具链选择，默认仍为当前可用的 CLion GCC 15.2.0，不改变现有 CLion/`build` 目录行为。
2. **Debug/Release 正交**：工具链（3）× 配置（Debug/Release）任意组合，各自独立 `build/<profile>` 目录，配置缓存互不覆盖。
3. **依赖树可插拔**：vcpkg 安装树、triplet、Boost/SQLite 库路径全部由工具链描述文件推导；消除 17 处 `gcc14` 与 44+7 处 `x64-mingw-*` 硬编码对切换的阻断。
4. **配置期 fail-fast**：CRT（MSVCRT/UCRT）与 C++ 运行库（libstdc++/libc++）不一致时必须**在 configure 阶段**报错并给出修复命令，禁止产出"能链接但运行 UB"的二进制。
5. **零回归**：默认 profile（`clion-debug`）的构建、`ctest`、UI 门禁结果与 2026-11-11 基线一致。

### 1.2 非目标

- 不在本方案内**实际重建** T2/T3 的 vcpkg 依赖树（属 Phase 3，成本见 §8）。
- 不改业务代码、不引入新第三方依赖、不调整 C++17 标准。
- 不做跨工具链产物混链（例如用 T1 编译、T3 链接）。
- 不承诺 T3 在本期可用（见 §9 决策点 D2）。

---

## 2. 现状事实（2026-11-11 实测，非推断）

### 2.1 三套工具链

| ID | 编译器 | 版本 | CMake | Ninja | CRT | C++ 运行库 | 头文件根 |
|---|---|---|---|---|---|---|---|
| `clion` | `E:\clion\bin\mingw\bin\g++.exe` | GCC 15.2.0 | `E:\clion\bin\cmake\win\x64\bin\cmake.exe` 4.4.2 | `E:\clion\bin\ninja\win\x64\ninja.exe` 1.13.2 | MSVCRT（`__MSVCRT_VERSION__ 0x600`，导入 `msvcrt.dll`） | libstdc++（动态） | `<root>\x86_64-w64-mingw32\include` |
| `w64devkit` | `E:\w64devkit\bin\g++.exe` | GCC 16.2.0 | `E:\CMake\bin\cmake.exe` 4.4.5 | `E:\CMake\bin\ninja.exe` 1.13.2 | MSVCRT（`0x700`，导入 `msvcrt.dll`） | libstdc++（**仅静态**，`--disable-shared`） | `<root>\x86_64-w64-mingw32\include` |
| `llvm-mingw` | `E:\llvm-mingw-20261104-ucrt-x86_64\bin\clang++.exe` | clang 23.1.5（target `x86_64-w64-windows-gnu`） | 复用 `E:\CMake\bin\cmake.exe` | 复用 `E:\CMake\bin\ninja.exe` | **UCRT**（`_UCRT` + `0xE00`，导入 `api-ms-win-crt-*`） | **libc++**（`libc++.dll`/`libunwind.dll`），**无 libstdc++ 头** | `<root>\include`（注意：**不是** `<root>\x86_64-w64-mingw32\include`） |

取证命令与现象：

```text
# CRT 判别（唯一可靠方式：必须 include <cstdio> 后再 -dM，直接 -dM 看不到 _UCRT）
echo '#include <cstdio>' | g++ -E -dM -x c++ -   # GCC15: __MSVCRT_VERSION__ 0x600
                                                  # GCC16: __MSVCRT_VERSION__ 0x700
                                                  # clang: _UCRT + __MSVCRT_VERSION__ 0xE00
# CRT 判别（二证：导入表）
objdump -p hello.exe | findstr "DLL Name"        # GCC: msvcrt.dll / clang: api-ms-win-crt-*
# clang 运行库二证
clang++ probe.cpp -o p.exe && objdump -p p.exe   # libc++.dll + libunwind.dll
clang++ -stdlib=libstdc++ probe.cpp              # fatal error: 'string' file not found
```

### 2.2 依赖树（vcpkg）

| 项 | 实测值 | 影响 |
|---|---|---|
| `D:\vcpkg\installed\x64-mingw-static` | Boost/Catch2/SQLite/CURL/gRPC 等静态库；`libboost_json-gcc14-mt-x64-1_91.a`，带编译器戳的库共 **10** 个，全部 `gcc14` | 由 **w64devkit-1.19.0（GCC 14）** 构建（`buildtrees/*/config-x64-mingw-*-CMakeCache.txt.log` 中 `CMAKE_CXX_COMPILER:STRING=E:/w64devkit-1.19.0/bin/...`） |
| `D:\vcpkg\installed\x64-mingw-dynamic` | wxWidgets 3.3.3、NanoSVG、abseil 等 DLL | 由 **`E:/w64devkit/bin`（GCC 16.2.0）** 构建（同上日志）；项目 `CMakeLists.txt:336/426/485-490` 直接从该目录取 wx 头与库 |
| 自定义 triplet | 仅 `community/x64-mingw-{static,dynamic}[-release].cmake`；**无 UCRT 变体** | T3 必须新增 overlay triplet，vcpkg 无现成支持 |
| `x64-mingw-static` 语义 | `VCPKG_CRT_LINKAGE dynamic` + `VCPKG_LIBRARY_LINKAGE static` | CRT 走 `msvcrt.dll` 动态导入、第三方库静态链接 ⇒ **跨 CRT 混链必炸**（`FILE*`/`malloc` 所有权跨界） |

### 2.3 现状阻塞点（可复现）

| # | 现象 | 复现 | 结论 |
|---|---|---|---|
| B1 | **T2 链接失败**：`undefined reference to __imp_wctype / __imp_mbrtowc / __imp_wcrtomb`，来源 `w64devkit/lib/gcc/x86_64-w64-mingw32/16.2.0/libstdc++.a(codecvt_members.o/ctype_members.o)` | `E:\w64devkit\bin\g++.exe -std=c++17 probe.cpp -ID:/vcpkg/installed/x64-mingw-static/include libboost_json-gcc14-....a -o p.exe`（控制组：同命令换 GCC 15.2.0 → `EXIT=0`） | 与 vcpkg 树无关，是 w64devkit 静态 libstdc++ 与 msvcrt 导入库的命名/版本不匹配；**待定位**（见 R1） |
| B2 | **T3 ABI 不兼容**：llvm-mingw 无 libstdc++ 头文件；默认 libc++/UCRT 与现有 wx DLL（libstdc++/MSVCRT）不可混用 | `clang++ -stdlib=libstdc++ probe.cpp` → `'string' file not found`；编译 3 个真实 TU（`Utils.cpp`/`XrayApi.cpp`/`MainFrame.cpp`）→ 成功（仅 1 个既有 `searchTargets` 未使用告警） | 编译面可用；**链接面必须重建全部依赖为 UCRT/libc++**，属 Phase 3 |
| B3 | T2/T3 configure 均成功（`Configuring done`，`wxWidgets found 3.3.3`） | `cmake -B temp/build-tc2|tc3 ... -DCMAKE_CXX_COMPILER=...` | 说明"选得到工具链"不是难点，**真正的闸门在 ABI/依赖树** |
| B4 | Boost 库名/依赖目录硬编码：`libboost_json-gcc14-mt-x64-1_91.a` 17 处、`installed/x64-mingw-static/lib/libsqlite3.a` 18 处、`x64-mingw-static` 44 处、`x64-mingw-dynamic` 7 处 | `Select-String CMakeLists.txt` | 不做变量化，切换工具链必然断链 |
| B5 | 产物无隔离：`CMAKE_RUNTIME_OUTPUT_DIRECTORY` 与逐 target `RUNTIME_OUTPUT_DIRECTORY` 共 **166** 行指向源树 `bin/`（13 行）与 `tests/`（156 行） | 同上 | Debug 与 Release、不同工具链会互相覆盖 `bin\validproxy.exe`、`tests\*.exe` |
| B6 | UI 测试硬编码被测程序路径 | `tests/ui/framework/Application.h:59`：`root() + L"\\bin\\validproxy.exe"` | 产物目录一旦隔离，UI 门禁需注入路径 |

---

## 3. 配置矩阵

### 3.1 profile 命名

`<toolchain>-<config>`，其中 `toolchain ∈ {clion, w64devkit, llvm-mingw}`，`config ∈ {debug, release}`：

| profile | 编译器来源 | 构建目录 | 支持状态 |
|---|---|---|---|
| `clion-debug` | 默认 | `build/clion-debug`（`build/` 保留为兼容别名） | ✅ 全量可用 |
| `clion-release` | 同上 | `build/clion-release` | ✅ 预期可用（AC2） |
| `w64devkit-debug/release` | `E:\w64devkit` | `build/w64devkit-*` | ⚠️ 配置/编译可用，**链接阻塞 B1** |
| `llvm-mingw-debug/release` | `E:\llvm-mingw-...` | `build/llvm-mingw-*` | ⛔ 编译可用，**链接需 Phase 3** |

### 3.2 选择优先级（自高到低）

1. `-DVALIDPROXY_TOOLCHAIN=<id>`（CLI，最高）
2. `$ENV{VALIDPROXY_TOOLCHAIN}`
3. 兼容回退：`$ENV{VALIDPROXY_MINGW_ROOT}`（映射为自定义 root，沿用当前语义）
4. 默认 `clion`
5. 若 `clion` 根不存在 → 走 PATH 发现的编译器（保持旧行为，仅告警）

---

## 4. 方案设计

### 4.1 单一事实源：`cmake/toolchains/<id>.cmake`

新增 3 个描述文件（纯变量，不含逻辑），由 `CMakeLists.txt` 在 `project()` **之前** include：

```cmake
# cmake/toolchains/clion.cmake（示意）
set(VALIDPROXY_TC_ROOT        "E:/clion/bin/mingw")
set(VALIDPROXY_TC_CC          "${VALIDPROXY_TC_ROOT}/bin/gcc.exe")
set(VALIDPROXY_TC_CXX         "${VALIDPROXY_TC_ROOT}/bin/g++.exe")
set(VALIDPROXY_TC_RC_INCLUDE  "${VALIDPROXY_TC_ROOT}/x86_64-w64-mingw32/include")
set(VALIDPROXY_TC_CRT         "msvcrt")
set(VALIDPROXY_TC_CXX_RUNTIME "libstdc++")
set(VALIDPROXY_TC_CMAKE_HINT  "E:/clion/bin/cmake/win/x64/bin/cmake.exe")
set(VALIDPROXY_TC_VCPKG_INSTALLED     "D:/vcpkg/installed")
set(VALIDPROXY_TC_VCPKG_TRIPLET_STATIC  "x64-mingw-static")
set(VALIDPROXY_TC_VCPKG_TRIPLET_DYNAMIC "x64-mingw-dynamic")
```

`CMakeLists.txt` 现状第 3–24 行的 FORCE 块改为：**先解析 id → 若描述文件存在则加载 → 否则沿用旧的 `ENV > E:/clion/bin/mingw > PATH` 回退**。同时把 `WX64DEVKIT_ROOT` 改为由 `VALIDPROXY_TC_RC_INCLUDE` 推导（当前 `:395` 的 `-I${WX64DEVKIT_ROOT}/x86_64-w64-mingw32/include` 对 llvm-mingw 是**错路径**，见 §2.1）。

### 4.2 配置期 ABI 闸门（核心）

configure 阶段执行两件事，失败即 `FATAL_ERROR`：

1. **运行库族探测**：`execute_process(COMMAND ${CMAKE_CXX_COMPILER} -E -dM -x c++ - INPUT_FILE stdin)`，输入 `#include <cstdio>`；解析 `_UCRT` 与 `__MSVCRT_VERSION__`。得到 `detected_crt`。
2. **依赖树匹配**：扫描 `installed/<triplet>/lib/libboost_json-*.a` 的编译器戳（`gcc14` 等），与 `CMAKE_CXX_COMPILER_VERSION` 对照。

判定规则：

| 条件 | 动作 |
|---|---|
| `detected_crt != VALIDPROXY_TC_CRT` | **FATAL_ERROR**（描述文件与环境不符） |
| 依赖树 CRT ≠ 当前 CRT（T3 场景） | **FATAL_ERROR**，文案含 `vcpkg install --triplet <ucrt-triplet> --x-install-root <per-tc root>` |
| 依赖树 C++ 运行库族 ≠ 当前（libc++ vs libstdc++） | **FATAL_ERROR**（同上） |
| 仅编译器主版本戳不同（gcc14 库 + GCC15 编译器，即今天的 T1） | **WARNING**（`--log-level=WARNING` 仍可继续），提示重建依赖树 |
| 用户显式 `-DVALIDPROXY_ALLOW_ABI_MISMATCH=ON` | 降级为 WARNING（逃生舱，默认 OFF，CI 禁止使用） |

> 设计取舍：T1 今天依赖 `gcc14` 名字的静态库正常工作，若把"主版本不同"设为硬失败，默认 profile 会立刻不可构建；因此**硬失败只针对 CRT 与运行库族**（真正会导致运行时 UB 的两项）。

### 4.3 依赖路径变量化（消除 B4）

在 `CMakeLists.txt` 顶部一次性推导，后续全部引用变量：

| 新变量 | 默认来源 | 替换目标 |
|---|---|---|
| `VALIDPROXY_VCPKG_STATIC_DIR` | `${VALIDPROXY_TC_VCPKG_INSTALLED}/${VALIDPROXY_TC_VCPKG_TRIPLET_STATIC}` | 44 处 `installed/x64-mingw-static` |
| `VALIDPROXY_VCPKG_DYNAMIC_DIR` | `${VALIDPROXY_TC_VCPKG_INSTALLED}/${VALIDPROXY_TC_VCPKG_TRIPLET_DYNAMIC}` | 7 处 `installed/x64-mingw-dynamic` |
| `VALIDPROXY_BOOST_JSON_LIB` | `file(GLOB)` 探测（见下） | 17 处 `libboost_json-gcc14-mt-x64-1_91.a` |
| `VALIDPROXY_SQLITE3_LIB` | `${VALIDPROXY_VCPKG_STATIC_DIR}/lib/libsqlite3.a` | 18 处 `libsqlite3.a` 全路径 |

**Boost 探测规则（顺序）**：

1. 显式 `-DVALIDPROXY_BOOST_JSON_LIB=<path>` 最高优先级；
2. `file(GLOB ${dir}/lib/libboost_json-*.a)`，优先匹配 `*-gcc${CMAKE_CXX_COMPILER_VERSION_MAJOR}-*`；
3. 无精确匹配则取唯一 `*-gcc<other>-*` 并 WARN；
4. 命中 0 个或多个候选 → `FATAL_ERROR` 并**列出候选路径**（禁止静默猜）。

### 4.4 产物隔离（B5/B6）

现状：166 行 `RUNTIME_OUTPUT_DIRECTORY` 指向源树，隔离改造面过大。提供两档：

**Option A（推荐，Phase 1）— 目录不隔离 + 可选暂存**

- 保留 `bin/`、`tests/` 为**唯一活动产物目录**；profile 只隔离 `build/<profile>`。
- 新增 `VALIDPROXY_STAGE_DIR`（默认空=不暂存）：非空时经 `add_custom_command(TARGET ... POST_BUILD)` 把 `validproxy.exe` / `validproxy-cli.exe` / 运行时 DLL 复制到 `<stage>/<profile>/`。
- 语义：**任何时刻只有一个 profile 的产物在 `bin/`**；切换 profile = 重新 configure+build（默认行为），暂存目录用于并存比对。
- 成本：约 10 行 CMake + 0 处业务/测试改动；UI 门禁（读 `bin\validproxy.exe`）零影响。

**Option B（Phase 2，可选）— 目录真隔离**

- 令 `VALIDPROXY_BIN_DIR` / `VALIDPROXY_TEST_DIR` 两个 cache 变量，替换 13 + 156 行字面量（机械替换，值不变时行为等价）。
- `tests/ui/framework/Application.h:59` 改为读 `$ENV{VALIDPROXY_APP_EXE}`，回退 `<root>/bin/validproxy.exe`。
- `scripts/build-and-test.bat` 增加 `--profile` 参数，`prepare-ui-sandbox.bat` 同步。
- 成本：约 170 行机械 diff + 2 脚本 + 1 测试头；风险：UI 门禁路径回归（需 AC6 兜底）。**仅在需要多 profile 并存跑测试时启用。**

### 4.5 RC / windres / sysroot

- `CMAKE_RC_COMPILER`：T1/T2 由工具链自动推导（`windres`）；T3 必须显式 `E:\llvm-mingw-...\bin\windres.exe`（实测 `llvm-windres, compatible with GNU windres`）。
- `CMAKE_RC_FLAGS` 的 include 路径由 `VALIDPROXY_TC_RC_INCLUDE` 提供，覆盖三种布局（GCC 系 `<root>/<triple>/include`，llvm-mingw `<root>/include`）。
- 保留 `-I${CMAKE_SOURCE_DIR}`，不删除现有 include（`icons.rc` 依赖相对路径）。

### 4.6 Debug / Release 语义

| 配置 | 期望 | 现状核对点 |
|---|---|---|
| Debug | `-g3 -O0`，保留断言与 `VALIDPROXY_ASSERT_LOG` | `CMakeLists.txt:109-115` 已追加 `-g3 -O0` |
| Release | `-O3 -DNDEBUG`（CMake 默认），不复制调试 DLL | 需 AC2 验证：产物不依赖 Debug 版 wx DLL |
| 可选 | `VALIDPROXY_RELEASE_OPT`（默认取 CMake 默认 `-O3`） | 决策点 D3：是否改为 `-O2` 以缩短编译时间 |

`version.h` 追加 `VALIDPROXY_PROFILE`（`<toolchain>-<config>`）字符串，便于 AC 中"二进制自证身份"（可选 U7，涉及 `scripts/generate_version_h.py` 形参追加，属破坏性签名变更，需同步 `version.h.in`）。

### 4.7 预设与脚本

`CMakePresets.json` 新增（保留现有 `default` 为 `clion-debug` 的兼容别名）：

- 6 个 configurePreset：`clion-debug` / `clion-release` / `w64devkit-debug` / `w64devkit-release` / `llvm-mingw-debug` / `llvm-mingw-release`；各自 `binaryDir=${sourceDir}/build/${presetName}`、`cacheVariables.VALIDPROXY_TOOLCHAIN`、`environment.{VCPKG_ROOT,GTEST_ROOT}`。
- 6 个 buildPreset 同名；1 个 `testPresets`（`ctest --output-on-failure`）。
- 脚本：`scripts/build.bat [profile] [config]`、`scripts/build-and-test.bat [profile]`（缺省 `clion-debug`）、新增 `scripts/build-matrix.bat`（顺序跑受支持 profile 并汇总退出码）。

### 4.8 CMakeLists 兼容性约束

- `CMAKE_CXX_COMPILER` 为 cache 变量且 FORCE 设置 ⇒ **同一构建目录内不可切换工具链**（切换必须换 `build/<profile>`，预设已保证）。
- 现有 `build/`（`CMAKE_COMMAND=E:/clion/bin/cmake/win/x64/bin/cmake.exe`）保持可用：`default` 预设映射到 `build/` 不变。
- `VCPKG_TARGET_TRIPLET` 现值 `x64-mingw-dynamic`（仅影响 vcpkg 的 applocal，已被 `VCPKG_APPLOCAL_DEPS=OFF` 关闭），本方案将其显式写入预设，避免隐式漂移。

---

## 5. 输入 / 输出 / 前置条件 / 边界条件

| 项 | 内容 |
|---|---|
| 输入 | `VALIDPROXY_TOOLCHAIN`（env 或 -D）、`CMAKE_BUILD_TYPE`（Preset 提供）、`VALIDPROXY_VCPKG_INSTALLED`（可选覆盖）、`VALIDPROXY_BOOST_JSON_LIB`（可选覆盖）、`VALIDPROXY_STAGE_DIR`（可选） |
| 输出 | `build/<profile>/`（配置与中间产物）、按 Option 决定的 `bin/`+`tests/` 或 `bin/<profile>/`；不含任何业务行为变化 |
| 前置条件 | ① 工具链根目录与编译器存在；② vcpkg 安装树存在且 triplet 目录可读；③ CMake ≥ 3.20、Ninja 可用；④ `GTEST_ROOT` 目录存在（`E:/eclipse_workspace/googletest`） |
| 边界条件 | ① 未设置任何变量 → 与今日完全一致（`clion` + `build/`）；② 无效 id → FATAL_ERROR 并列出合法 id；③ CRT/运行库族不匹配 → FATAL_ERROR，除非显式逃生舱；④ 工具链根存在但 vcpkg 树缺失 → FATAL_ERROR（禁止静默降级到别的树）；⑤ 同一构建目录换工具链 → 明确报错提示使用新 `build/<profile>` |
| 不变量 | C++17、禁 `auto`、`-Wall -Wextra -pedantic` 不变、`bin/` 语义（默认 profile）不变、`VCPKG_APPLOCAL_DEPS=OFF`（DEV-PROCESS 规则 5）不变 |

---

## 6. 实施单元

| 单元 | 文件 | 内容 | 风险 |
|---|---|---|---|
| **U1** | `cmake/toolchains/{clion,w64devkit,llvm-mingw}.cmake`、`CMakeLists.txt:3-24` | 工具链描述 + id 解析 + 旧逻辑降级为回退分支；`WX64DEVKIT_ROOT` 由 RC include 推导 | 中：`project()` 前执行，注意不要引入未定义变量 |
| **U2** | `CMakeLists.txt`（顶部推导块） | `VALIDPROXY_VCPKG_{STATIC,DYNAMIC}_DIR`、`VALIDPROXY_BOOST_JSON_LIB`（glob 探测）、`VALIDPROXY_SQLITE3_LIB` | 低：纯替换，值不变时字节等价 |
| **U3** | `CMakeLists.txt` 44+7+17+18 处 | 替换为变量；`:395` RC flags 改用 `VALIDPROXY_TC_RC_INCLUDE` | 中：漏改一处即断链，需 AC1 兜底 |
| **U4** | 新增 `cmake/AbiGuard.cmake` + `CMakeLists.txt` include | CRT/运行库族探测 + 依赖树戳比对 + FATAL/WARN 文案 | 中：探测命令在 configure 期执行，需处理编译器为绝对/相对路径两种情形 |
| **U5** | `CMakePresets.json` | 6 configure + 6 build + test preset；`default` 兼容 | 低 |
| **U6** | `scripts/build.bat`、`scripts/build-and-test.bat`、新增 `scripts/build-matrix.bat` | profile 参数化、缺省值不变 | 低 |
| **U7（可选）** | `CMakeLists.txt` POST_BUILD + `scripts/generate_version_h.py` + `include/version.h.in` | `VALIDPROXY_STAGE_DIR` 暂存；profile 字符串入 `version.h` | 低 |

实施顺序：U2 → U1 → U4 → U3 → U5 → U6（每步后跑 AC1 子集），U7 视需求。

---

## 7. 验收标准

| ID | 命令 | 期望 |
|---|---|---|
| **AC1** | `cmake --preset clion-debug && cmake --build --preset clion-debug --parallel 8` | EXIT 0；与今日构建产物等价（`bin\validproxy.exe` 可启动） |
| **AC2** | `cmake --preset clion-release && cmake --build --preset clion-release --parallel 8`，随后 `bin\validproxy-cli.exe -h` | EXIT 0；Release 产物不引用 Debug 版 wx DLL（`objdump -p` 检查导入表） |
| **AC3** | `ctest --test-dir build/clion-debug --output-on-failure` | 70/71（仅 `#46 XrayManagerStartupTest` 既有环境基线） |
| **AC4** | `scripts\build-and-test.bat clion-debug` | 15/16（`UI_FLOATINGWIDGET` 既有 flaky；单跑通过） |
| **AC5** | `cmake -B temp/x -DVALIDPROXY_TOOLCHAIN=bogus` | 非 0；报错列出合法 id |
| **AC6** | `cmake --preset w64devkit-debug`（Phase 1 状态） | 非 0，且失败点在 **ABI 闸门/链接**，文案含具体修复建议；**不得**出现"配置成功但产物不可用"的静默通过 |
| **AC7** | `cmake --preset llvm-mingw-debug` | 同 AC6（CRT/运行库族硬失败） |
| **AC8** | 连续执行 AC1→AC2→AC1，检查 `git status` 与 `bin/` | 仅声明文件变化；`bin/` 内容符合"最后构建 profile"语义（Option A） |
| **AC9** | 新增代码 `git diff` 中 `auto` 计数 | 0 |
| **AC10** | 二次 configure（同 preset，无改动） | 不重建、不报错（幂等） |

---

## 8. 风险与未决问题

| ID | 风险 | 现状 | 处置 |
|---|---|---|---|
| **R1** | T2 链接失败根因未定位（w64devkit 静态 libstdc++ 引用 `__imp_wctype` 等） | 已最小复现（无 vcpkg 也失败） | 立项 timebox（≤2h）spike：候选排查 `-public` 符号归属、`libmsvcrt.a` 版本、`-static-libstdc++`/`-shared-libstdc++`、`__MSVCRT_VERSION__` 定义。若不可解 ⇒ T2 降级为"仅编译 profile" |
| **R2** | T3 需全量重建依赖（boost/curl/sqlite/grpc/protobuf/wx/catch2）到 UCRT+libc++ | llvm-mingw 无 libstdc++，wx 由 GCC/libstdc++ 构建 | 成本估 2–6h vcpkg + 可能的 port 补丁；wx 是否支持 clang/libc++ 未验证 |
| **R3** | vcpkg 无 `x64-mingw-ucrt-*` triplet | 已确认 | 新增 `cmake/triplets/x64-mingw-ucrt-{static,dynamic}.cmake`（`VCPKG_CMAKE_SYSTEM_NAME MinGW` + UCRT CRT linkage） |
| **R4** | 硬编码替换漏改 → 链接失败 | 44/7/17/18 处 | 机械替换 + AC1/AC2 双 profile 兜底；可加 `configure` 期断言：CMakeLists 中不应再出现 `x64-mingw-` 字面量（grep 门禁） |
| **R5** | 闸门误伤 T1（gcc14 库 + GCC15 编译器） | 已知 | 硬失败仅限 CRT/运行库族；主版本差异降为 WARN |
| **R6** | Option A 下 Debug/Release 互相覆盖 `bin/` | 设计取舍 | 文档明确 + `VALIDPROXY_STAGE_DIR` 暂存；若需并存再评估 Option B |
| **R7** | preset 与 CLion reload 冲突（CLion 使用 `build/`） | 现状 CLion 指向 `build/` | `default` 预设保持 `binaryDir=build/`，CLion 无感 |

---

## 9. 分阶段计划与决策点

| 阶段 | 范围 | 产出 | 可验证结果 |
|---|---|---|---|
| **Phase 1**（本方案实施范围） | U1–U6（不含 T2/T3 依赖重建） | 工具链开关、ABI 闸门、变量化、预设、脚本 | AC1/AC3/AC4/AC5/AC6/AC7/AC10 全绿；T1 零回归 |
| **Phase 2**（条件触发） | Option B 产物真隔离 + U7 | 多 profile 并存 | 能在同一台机同时保留 3 套产物并分别跑 ctest |
| **Phase 3**（按决策点执行） | T2 根因修复 / T3 依赖重建 | 新增可用 profile | AC2 扩展到对应 profile |

**决策点（需 owner 裁决）**

- **D1**：Phase 1 是否采用 Option A（不隔离产物，推荐）还是直接上 Option B（170 行机械 diff）？
- **D2**：T3（llvm-mingw UCRT/libc++）是否值得投入 Phase 3？若目标只是"多编译器交叉验证语法"，可只保留"编译 profile"（`--target` 编译检查，不链接）。
- **D3**：Release 优化级别固定 CMake 默认 `-O3` 还是改 `-O2`（本项目曾因 `-O3` 体积/调试体验讨论过）？
- **D4**：ABI 闸门对"主版本不同"只 WARN 是否可接受（替代方案：要求 vcpkg 树按当前编译器重建，代价最高）？

---

## 10. 参考

- `CMakeLists.txt:3-24`（工具链选择块）、`:157-158`（vcpkg include/link 目录）、`:164-166`（输出目录）、`:287-288/408/628/...`（boost 硬编码 17 处）、`:395`（RC flags）、`:1695`（Catch2 路径）
- `CMakePresets.json`（现有 3 preset）
- `scripts/build.bat`、`scripts/build-and-test.bat`、`scripts/prepare-ui-sandbox.bat`
- `tests/ui/framework/Application.h:59`（UI 被测程序路径）
- `docs/DEV-PROCESS.md` 规则 5（禁止复制依赖 DLL）
- `docs/reports/2026-11-11-Report-Toolchain-Verification-v1.0.md`（T1 基线：全量 70/71、UI 15/16）
- `docs/plans/2026-11-06-Plan-Cleanup-CopyWxDllsDeadCode-v1.0.md`（Scheme A / PATH-based 运行时）

---

## 11. 2026-11-13 增补：跨 CRT 混链根因 + UCRT wx 重建（已验证）

> 本增补记录一次真实发生的静默混链事故及其修复，作为 §4.2「ABI 闸门」的实证。

**事故**：`validproxy.exe`（GUI）启动闪退 `0xC0000374`（STATUS_HEAP_CORRUPTION）。

**根因（已确认）**：exe 由 `E:\mingw64`（GCC 16.2.0 / **UCRT**）构建，而 wxWidgets DLL 来自 vcpkg `x64-mingw-dynamic`（GCC 14.2.0 / **msvcrt.dll**）。唯一跨 CRT 边界 = 动态 wx DLL；跨边界分配/释放导致堆损坏。旁证：CLI（无 wx，运行相同引导代码）正常 → 项目引导代码干净。

**修复（已验证）**：
- 新增自定义 triplet `temp/vcpkg-triplets/x64-mingw-dynamic-ucrt.cmake`（拷贝 `community/x64-mingw-dynamic.cmake`，工具链经 PATH 解析 → `E:\mingw64`）。
- 隔离重建 `vcpkg install wxwidgets[debug-support] --triplet x64-mingw-dynamic-ucrt --x-install-root=temp/vcpkg-ucrt-root`（PATH 前置 `E:\mingw64\bin`；特性集 `[debug-support]` 匹配现有 wx 无 sound/sdl2）。
- `CMakeLists.txt` 新增缓存变量 `WX_VCPKG_DIR`（默认 `${VCPKG_ROOT}/installed/x64-mingw-dynamic`，wx 的 find_package/include 全部改用它）；UCRT 用 `-DWX_VCPKG_DIR=<ucrt root>`。
- **部署级**：加载器**先搜 exe 目录**；默认 PATH 上的 `D:\vcpkg\installed\x64-mingw-dynamic\debug\bin` 仍是 msvcrt wx，双击会先命中它而重演 `0xC0000374`。故 `VALIDPROXY_COPY_WX_RUNTIME=ON`（默认 OFF 保持 Rule 5）经 POST_BUILD 把 `${WX_VCPKG_DIR}/debug/bin/*.dll` 复制到 `bin\`（exe 目录），使 GUI 双击自包含、加载 CRT 匹配的 UCRT wx。
- 重链 `validproxy.exe`（`build-tc-mingw64-Debug`），运行时 exe 目录内的 UCRT wx/编解码 DLL 被加载器优先命中。

**验收（证据）**：wx DLL 及传递依赖（jpeg/png/tiff/webp/pcre2/zlib）全部导入 `api-ms-win-crt-*`（UCRT）、无 `msvcrt.dll`；exe 导入 12 UCRT / 0 msvcrt；`-c` 测试配置运行 ≥12s 存活（进程加载 wx 全来自 UCRT 根，`Get-Process .Modules` 实证）；**双击/默认 PATH（无 UCRT 覆盖）**复测：bin\ 置入 UCRT wx 前 8 s 内 `0xC0000374`，置入后 ≥10 s 存活且 wx 全从 `bin\`（UCRT）加载；GUI 日志越过旧崩溃点进入事件循环；生产库无活动连接。

**教训 / 后续**：`bin\` 曾滞留 46 个 msvcrt wx/编解码 DLL（违反 Rule 5 且被加载器优先选中会重演崩溃）——已移出备份；`UITests.exe` 在 GCC 16.2 下预存 `std::basic_streambuf::seekpos` 未定义（Catch2 静态库，与 wx 无关）——属环境性已知问题。D4「ABI 闸门」仍待实施，应覆盖「exe 编译器版本/CRT 与 wx 动态 DLL 一致」这一实际失效维度。

---

## 12. 2026-11-13 增补：工具链文件 + 配置期 ABI 闸门落地（实现）

> 本增补记录 §4「方案设计」的实际落地：两套真实工具链（CLion GCC15/msvcrt 基线 + `E:\mingw64` GCC16/UCRT 修复链）、声明式工具链文件、配置期 ABI 闸门（wx DLL CRT 为权威判据）、按工具链产物隔离，以及全新构建目录暴露并修复的两个既有缺陷。

### 12.1 实际工具链矩阵（相对 §2.1 收敛为两条已验证链）

| ID | 编译器根 | 版本 | CRT | C++ 运行库 | wx 依赖根（动态） | Catch2 依赖根（静态） |
|---|---|---|---|---|---|---|
| `clion` | `E:\clion\bin\mingw` | GCC 15.2.0 | MSVCRT | libstdc++（动态） | `D:\vcpkg\installed\x64-mingw-dynamic` | `D:\vcpkg\installed\x64-mingw-static` |
| `ucrt` | `E:\mingw64` | GCC 16.2.0 | UCRT | libstdc++（动态） | `temp/vcpkg-ucrt-root/x64-mingw-dynamic-ucrt` | `temp/vcpkg-ucrt-root/x64-mingw-static-ucrt` |

依赖策略（用户指令 2026-11-13）：**wxWidgets = 动态**；**其余全部静态**（boost / sqlite3 / gtest / Catch2 / curl / zlib / ...）。

### 12.2 已落地文件

| 文件 | 内容 |
|---|---|
| `cmake/toolchains/mingw-clion.cmake` | 工具链文件：编译器/RC、CRT 标签 `VALIDPROXY_TC_CRT=msvcrt`、每工具链 wx/Catch2 依赖根默认值 |
| `cmake/toolchains/mingw-ucrt.cmake` | 同上，`VALIDPROXY_TC_CRT=ucrt`；wx/Catch2 默认指向 `temp/vcpkg-ucrt-root`（UCRT 隔离根） |
| `cmake/AbiGuard.cmake` | 配置期 ABI 闸门（见 12.3） |
| `CMakeLists.txt` | ① `project()` 前工具链选择器（`VALIDPROXY_TOOLCHAIN` 缓存变量 > 环境变量 > 默认 `ucrt`）；② `project()` 后每工具链依赖根 + AbiGuard include + 输出隔离变量；③ 全部 `RUNTIME_OUTPUT_DIRECTORY*` 字面量 → `VALIDPROXY_BIN_DIR`/`VALIDPROXY_TEST_DIR`；④ wx/MinGW 运行时 POST_BUILD 复制目标 → `VALIDPROXY_BIN_DIR`；⑤ UI 测试统一注入 `VALIDPROXY_APP_EXE`；⑥ 全新目录修复：SQLite3 搜索路径提示 + version.h `BYPRODUCTS` |
| `tests/ui/framework/Application.h` | `exePath()` 读 `$ENV{VALIDPROXY_APP_EXE}`，回退 `<repo>/bin/validproxy.exe` |

### 12.3 ABI 闸门实现（相对 §4.2 判据收敛）

真实混链失效维度是 **exe 与 wx 动态 DLL 的 CRT 族**（§11 实证 `0xC0000374`），故 AbiGuard 以 wx DLL 为权威判据：

1. **编译器 CRT 探测**：`${CMAKE_CXX_COMPILER} -E -dM -x c++ <probe>`（`#include <cstdio>`），解析 `_UCRT` / `__MSVCRT_VERSION__` → `detected_crt`；与工具链文件 `VALIDPROXY_TC_CRT` 对照，不等 → FATAL。
2. **wx DLL CRT 探测**：取 `${WX_VCPKG_DIR}/debug/bin/wx*.dll` 首个，`objdump -p` 导入表判定（`api-ms-win-crt-*` → UCRT；`msvcrt.dll` → MSVCRT）；与 `detected_crt` 不等 → **FATAL**（文案含 `0xC0000374` 失效模式 + 修复指引）。
3. **Catch2 主版本漂移**：best-effort，仅 WARN（链接失败由链接期暴露，非运行 UB）。
4. **逃生舱**：`-DVALIDPROXY_ALLOW_ABI_MISMATCH=ON` 把上述 FATAL 降为 WARNING（默认 OFF，CI 禁用）。

> 设计取舍（承 §4.2）：**硬失败只针对 CRT 族**（`0xC0000374` 根因）；编译器主版本差异（gcc14 库 + GCC15/16 编译器）只 WARN，避免默认 profile 立刻不可构建。

### 12.4 输出隔离（相对 §4.4，落地 Option B 但默认关）

- `VALIDPROXY_ISOLATE_OUTPUTS`（默认 OFF）：OFF → 产物仍在源树 `bin/`、`tests/`（零回归，UI 门禁 / `build-and-test.bat` 不变）；ON → 产物到 `<build>/out/bin`、`<build>/out/tests`。
- `VALIDPROXY_BIN_DIR` / `VALIDPROXY_TEST_DIR` 替换全部 `RUNTIME_OUTPUT_DIRECTORY*` 字面量（值不变时行为等价）。
- UI 门禁路径注入：UI 测试通过 `VALIDPROXY_APP_EXE` 指向 `VALIDPROXY_BIN_DIR/validproxy.exe`（`Application.h` 兜底 `<repo>/bin`）。
- 每工具链独立构建目录（`build-tc-<tc>-gated`），配置缓存不混链。

### 12.5 全新构建目录发现并修复的既有缺陷

| 缺陷 | 症状（全新目录） | 修复 |
|---|---|---|
| SQLite3 `find_package` 失败 | `Could NOT find SQLite3 (missing: SQLite3_LIBRARY SQLite3_INCLUDE_DIR)` | `list(APPEND CMAKE_LIBRARY_PATH / CMAKE_INCLUDE_PATH <vcpkg-static>)`，免手动 `-D` |
| `include/version.h` 无生成规则 | `ninja: error: 'include/version.h' ... no known rule to make it` | `add_custom_target(update_version_h ... BYPRODUCTS ${VERSION_H})` 声明输出 |

### 12.6 选择器优先级（实际落地）

`-DVALIDPROXY_TOOLCHAIN=<tc>`（缓存变量，最高）> `$ENV{VALIDPROXY_TOOLCHAIN}` > 默认 `ucrt`。无效 id → FATAL 并列出合法值（`clion`、`ucrt`）。切换工具链必须用全新构建目录（`CMAKE_TOOLCHAIN_FILE` 变更，同目录缓存编译器会胜出）。
