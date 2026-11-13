# 调查报告：编译工具链环境核对、CMakeLists 配置评估与全量回归验证

**日期**: 2026-11-11
**类型**: 环境核对 + 配置评估 + 验证报告
**模块**: 构建系统（`CMakeLists.txt` / `CMakePresets.json` / `build/CMakeCache.txt`）、回归与 UI 门禁
**版本**: v1.0
**状态**: 已完成（**零代码、零配置、零文档改动**；未修复任何既有失败项）

---

## 1. 结论先行

| # | 结论 | 证据 |
|---|---|---|
| 1 | **`CMakeLists.txt` 无需修改**。第 3–24 行工具链选择块的优先级 `$ENV{VALIDPROXY_MINGW_ROOT}` → `E:/clion/bin/mingw` → PATH，与本机环境变量实际解析结果完全一致 | §2 实测 + §3 |
| 2 | 构建链路健康：重新配置后识别 `GNU 15.2.0`，`scripts\build.bat` 返回 `[PASS] Application + UITests`，`bin\validproxy.exe` 重新链接且 3 个 MinGW 运行时 DLL 同步刷新 | §4.1 |
| 3 | 全量回归 **70/71 通过**（302.65 s），唯一失败 `#46 XrayManagerStartupTest`，属**真实 xray 在受限环境下 API 端口不就绪**的既有环境基线，非回归 | §4.2 / §4.4 |
| 4 | UI 门禁 `scripts\build-and-test.bat` **15/16 通过**（128.93 s），唯一失败 `#59 UI_FLOATINGWIDGET`；单跑 6 次 4 过 2 败，属仓库多次登记过的**位置采样竞态 flaky**，非回归 | §4.3 / §4.4 |
| 5 | 两个后续风险已确认但**本轮不动**：① Boost 静态库名硬编码 `gcc14`（17 处）与当前 GCC 15.2.0 靠文件同名侥幸链接；② `UI_FLOATINGWIDGET` 的自动收回保位断言偶发漂移 | §5 |

---

## 2. 环境实测（按环境变量解析，非历史缓存）

| 工具 | 版本 | 实际路径 | 校验命令 |
|---|---|---|---|
| `gcc` | 15.2.0 | `E:\clion\bin\mingw\bin\gcc.exe` | `(Get-Command gcc).Source` / `gcc --version` |
| `g++` | 15.2.0 | `E:\clion\bin\mingw\bin\g++.exe` | `(Get-Command g++.Source)` / `g++ --version` |
| `cmake` | 4.4.2 | `E:\clion\bin\cmake\win\x64\bin\cmake.exe` | `(Get-Command cmake).Source` / `cmake --version` |
| `ninja` | 1.13.2 | `E:\clion\bin\ninja\win\x64\ninja.exe` | `(Get-Command ninja).Source` / `ninja --version` |
| `VCPKG_ROOT` | `d:\vcpkg` | 环境变量已设置 | `$env:VCPKG_ROOT` |
| `VALIDPROXY_MINGW_ROOT` | **未设置** | — | `$env:VALIDPROXY_MINGW_ROOT` |

生效缓存（`build/CMakeCache.txt`，重配置后）：

```
CMAKE_BUILD_TYPE:STRING=Debug
CMAKE_CXX_COMPILER:STRING=E:/clion/bin/mingw/bin/g++.exe
CMAKE_MAKE_PROGRAM:FILEPATH=E:/CMake/bin/ninja.exe
CMAKE_TOOLCHAIN_FILE:FILEPATH=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
CMAKE_COMMAND:INTERNAL=E:/clion/bin/cmake/win/x64/bin/cmake.exe
```

> 说明：`CMAKE_COMMAND` 在本次 `scripts\build.bat` 重配置之前为 `E:/CMake/bin/cmake.exe`（4.4.5），之后变为 PATH 中的 `E:/clion/bin/cmake/win/x64/bin/cmake.exe`（4.4.2）。仅驱动版本变化，配置与构建均成功，无需干预。

---

## 3. `CMakeLists.txt` 评估

### 3.1 现有实现（`CMakeLists.txt:10-24`）

```cmake
if(NOT DEFINED CMAKE_CXX_COMPILER OR "${CMAKE_CXX_COMPILER}" STREQUAL "")
    set(_MINGW_ROOT "$ENV{VALIDPROXY_MINGW_ROOT}")
    if(NOT _MINGW_ROOT AND EXISTS "E:/clion/bin/mingw/bin/g++.exe")
        set(_MINGW_ROOT "E:/clion/bin/mingw")
    endif()
    if(_MINGW_ROOT)
        set(CMAKE_CXX_COMPILER "${_MINGW_ROOT}/bin/g++.exe" CACHE FILEPATH "C++ compiler" FORCE)
        set(CMAKE_C_COMPILER   "${_MINGW_ROOT}/bin/gcc.exe" CACHE FILEPATH "C compiler" FORCE)
        set(WX64DEVKIT_ROOT    "${_MINGW_ROOT}" CACHE PATH "MinGW root for windres includes" FORCE)
    endif()
endif()
```

### 3.2 判定依据

1. 环境优先级链与实时环境一致：`VALIDPROXY_MINGW_ROOT` 未设置 → 命中 `E:/clion/bin/mingw/bin/g++.exe` 存在性检查 → 最终 `CMAKE_CXX_COMPILER` 落在同一路径（见 §2 缓存值）。
2. 同目录 `x86_64-w64-mingw32\include` 存在，`WX64DEVKIT_ROOT` 的 FORCE 赋值保证 `windres` 能找到 `windows.h` 等 RC 依赖（`CMakeLists.txt:395` 附近使用 RC flags）。
3. `CMakePresets.json` 的 `default` 预设仍写 `WX64DEVKIT_ROOT=E:/w64devkit`，但会被上述 FORCE 覆盖，属预期行为，不构成冲突。
4. 重配置实测输出与推断一致：`The CXX compiler identification is GNU 15.2.0`、`Check for working CXX compiler: E:/clion/bin/mingw/bin/g++.exe - skipped`。

**结论：不需要修改 `CMakeLists.txt`。** 若未来把 MinGW 迁出 `E:\clion`，只需设置 `VALIDPROXY_MINGW_ROOT` 环境变量，无需改 CMake 文件。

---

## 4. 构建与回归验证证据

### 4.1 构建（含重配置）

| 项 | 值 |
|---|---|
| 命令 | `scripts\build-and-test.bat` 内部 `call scripts\build.bat`（等价 `cmake -B build -G Ninja` + `cmake --build build`） |
| 配置结果 | `-- Configuring done (8.3s)` / `-- Generating done (1.0s)` |
| 构建结果 | `[PASS] build`、`[PASS] Application + UITests` |
| 产物 | `bin\validproxy.exe`（17:09:44 重新链接）、`libgcc_s_seh-1.dll` / `libstdc++-6.dll` / `libwinpthread-1.dll`（17:09:09，POST_BUILD 拷入正常） |

### 4.2 全量回归

| 项 | 值 |
|---|---|
| 命令 | `ctest --test-dir build --output-on-failure` |
| 退出码 | **8** |
| 结果 | **70/71 通过（99%）**，`Total Test time (real) = 302.65 sec` |
| 唯一失败 | `#46 XrayManagerStartupTest` |
| 完整日志 | `temp/ctest_full_20261111.log`（本地证据，未入库） |

> 参照：同一构建下更早一次全量运行为 69/71，多出的一项 `#9 CurlEasyHandleTest` 系外网依赖抖动（该用例断言 `https://www.example.com` 返回 200/204，源码 `tests/test_curl_easy_handle.cpp`），其后 3 次单跑均 `EXIT=0`（HTTP 200，约 0.84 s），本次全量中亦通过。

### 4.3 UI 门禁

| 项 | 值 |
|---|---|
| 命令 | `scripts\build-and-test.bat`（`build.bat` + `prepare-ui-sandbox.bat` + `ctest -R "^UI_"`） |
| 退出码 | **8** |
| 结果 | **15/16 通过**，`Total Test time (real) = 128.93 sec` |
| 唯一失败 | `#59 UI_FLOATINGWIDGET`（22.76 s） |
| 沙盒安全性 | `prepare-ui-sandbox.bat` 只重置 `test/ui-sandbox/`（`guiNDB_empty.db` → `ui-test.db`），脚本第 3–4 行明确声明不触碰 `bin\worker\guindb.db`、`test\guindb.db` |
| 完整日志 | `temp/ui_gate_20261111.log`（本地证据，未入库） |

`UI_FLOATINGWIDGET` 单跑复验统计（`temp/ui_fw_run1..3.log` + 前两次）：

| 次序 | 结果 | 耗时 | 失败断言 |
|---|---|---|---|
| 1 | Passed | 17.19 s | — |
| 2 | Failed | 20.29 s | `:491/492`（1222 vs 672、335 vs 232）、`:805`（orbCx 308 > 80） |
| 3 | Passed | 15.95 s | — |
| 4 | Failed | 18.29 s | `:638/639`（漂移 191 px / 244 px，阈值 10） |
| 5 | Passed | — | — |
| 6 | Passed | — | — |

即 **4/6 通过**，失败断言均为「拖拽后 / 自动收回后的悬浮球位置」采样，非确定性。

### 4.4 失败项归因

**4.4.1 `#46 XrayManagerStartupTest`（既有环境基线）**

```
[XrayInstance] Executing: E:\v2rayN-windows-64\bin\xray\xray.exe run -c ...xray_config_29501.json
[ERROR] XrayManager: API port 29601 not ready within timeout for instance 0
[XrayInstance][stop] Terminated..., exit=1
[REPORT] XrayManager::start: actualCount=0
tests/test_xray_manager_startup.cpp:137: Expected equality: 1 vs 0
tests/test_xray_manager_startup.cpp:139: Expected (elapsedMs) < (3000), actual: 6219 vs 3000
```

同一用例内的 `FlashCrashDoesNotHang` 通过（7413 ms）。失败点是真实 xray 进程未能让 gRPC API 端口在 5 s 内就绪（进而 `actualCount=0`），与历史多次记录同签名（6185 / 6146 / 6472 / 6182 ms）。**属运行环境（真实 xray 二进制 + 本机端口/策略）问题，不是本次任何改动的回归。**

**4.4.2 `#59 UI_FLOATINGWIDGET`（既有 flaky）**

失败集中在两个用例：

- `Floating widget keeps free-drop position after re-show`（`tests/ui/TestStandaloneFloatingWidget.cpp:455-493`）：`dragWindow(hwnd, -200, -100)` 后位置未稳定即被隐藏/重显，读出 `p3` 仍为默认右缘（1222/335）。
- `Click-expand then auto-hide keeps free-drop position`（`:638-639`）：自动收回后球心漂移 191 px / 244 px。该用例已有 `stayedCollapsed` 连续 4 次采样门禁（`:608-625`），但仍存在合成鼠标点击/悬停消息在途的时序窗口。
- `Edge orb returns to original position after expand-and-collapse`（`:805`）：`dragWindow(hwnd, -1500, 0)` 六次重试后球心仍为 308，即拖拽被外部覆盖窗口截获（用例注释 `:779-784` 已记录"console/main frame 遮挡导致合成点击落点偏移"）。

该用例在仓库中已被多次登记为已知 flaky，例如 `docs/architecture.md:237`、`docs/specs/2026-10-15-Spec-SubscriptionBase64UrlFix-v1.2.md`（"已知基线失败 … `UI_FLOATINGWIDGET`（并行 flaky）"）、`docs/bugfix/2026-10-21-Bugfix-StandaloneAdoption-*.md`（"单跑 2/3 通过，判定 `WindowFromPoint` z-order 竞态非回归"）。本次观测与既有记录一致。

---

## 5. 观察项与风险（本轮未处理）

| # | 观察 | 影响 | 建议 |
|---|---|---|---|
| R1 | `CMakeLists.txt` 中 **17 处** Boost 静态库名硬编码 `gcc14`（如 `libboost_json-gcc14-mt-x64-1_91.a`，行 288/408/628/748/770/817/936/1010/1083/1114/1147/1195/1342/1362/1429/1511/1677） | 当前 GCC 15.2.0 能链接，仅因 `D:\vcpkg\installed\x64-mingw-static\lib` 下确实存在该文件名；一旦 vcpkg 按 `gcc15` 重建，17 处链接全部会断 | 配置期用 `file(GLOB)`/`find_library` 探测实际库名后统一注入变量；需先出设计文档 |
| R2 | `UI_FLOATINGWIDGET` 位置保位断言在共享桌面下不稳定（本次 4/6） | UI 门禁存在偶发红灯，需人工复跑甄别 | 立项 bugfix：把"位置采样"改为基于 `dockEdge_`/`savedOrbCenter_` 的可注入状态判定，或把自动收回路径改为消息队列同步点后再断言 |
| R3 | `#46 XrayManagerStartupTest` 依赖真实 `E:\v2rayN-windows-64\bin\xray\xray.exe` 与固定端口段 29xxx | 本机环境下恒定失败，掩盖真实回归信号 | 提供"真实 xray 不可用则 `GTEST_SKIP`"的前置探测（与 `NetworkMonitorTest` 的环境代理用例做法一致） |
| R4 | `cmake` 存在双版本（PATH 4.4.2 / 旧缓存 4.4.5） | 已观测到 `CMAKE_COMMAND` 随重配置切换，未引发问题 | 保持现状；若出现生成差异再统一锁定版本 |

---

## 6. 本轮改动与未做事项

| 项 | 状态 |
|---|---|
| 源码 / 头文件 / 测试 | **零改动** |
| `CMakeLists.txt` / `CMakePresets.json` / `CMakeCache.txt` | **零手改**（缓存变化仅由 `scripts\build.bat` 重配置产生） |
| 文档 | 仅新增本报告，并登记 `docs/INDEX.md` 与 `docs/plans/project-plans-tracker.md` |
| 既有失败修复 | **未做**（`#46` 与 `#59` 均为既有项，需独立立项，见 §5） |
| Git 分支 | 未操作。当前 `main`，HEAD `eecb314`；`git reflog` 显示 15:25 由 `tasks-and-test-queue-management` 快进合并、15:42 与 15:56 两次文档提交，均非本次会话操作 |

本地证据文件（`temp/`，不纳入版本库）：`ctest_full_20261111.log`、`ui_gate_20261111.log`、`ui_fw_run1.log` ~ `ui_fw_run3.log`。

---

## 7. 复现步骤

```powershell
# 1) 环境核对
(Get-Command gcc).Source; gcc --version | Select-Object -First 1
(Get-Command cmake).Source; cmake --version | Select-Object -First 1
(Get-Command ninja).Source; ninja --version

# 2) 构建（含重配置）
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8

# 3) 全量回归（约 300 s）
ctest --test-dir build --output-on-failure

# 4) UI 门禁（约 130 s，仅重置 test/ui-sandbox）
scripts\build-and-test.bat

# 5) 单跑甄别 flaky
ctest --test-dir build --output-on-failure -R "^UI_FLOATINGWIDGET$"
```
