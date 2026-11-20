---
title: "bugfix: ctest 测试进程跨工具链 DLL 混用（0xC0000139 模态框 / 0xC0000374 堆损坏）"
type: bugfix
status: completed
date: 2026-11-20
module: build-system
version: v1.0
---

# ctest 测试进程跨工具链 DLL 混用（0xC0000139 / 0xC0000374）

## 1. 现象

在 `build-ucrt/`（`-DVALIDPROXY_TOOLCHAIN=mingw-ucrt`，`E:\mingw64` = GCC 16.2 + UCRT，
`-DVALIDPROXY_ISOLATE_OUTPUTS=ON` → 产物在 `build-ucrt/out/{bin,tests}`）跑全量回归时：

| 阶段 | 结果 |
|---|---|
| 首次 `ctest` | 78 项中 **16 个 `UI_*` 全部失败**，每个耗 15 s（GUI 起不来） |
| 给 UI 用例前置本工具链 wx bin 后 | **77/78**，仅 `ProxyListModelTest` = `0xC0000374`（堆损坏） |
| 把 PATH 前置推广到全部测试后 | `LogStatisticsEventTest` = `0xC0000139`，耗时 62 s（系统模态框卡住，靠外层超时才结束） |

`0xC0000139` = `STATUS_ENTRYPOINT_NOT_FOUND`，即加载器弹「无法定位程序输入点 … 于动态链接库」的
模态框；`0xC0000374` = `STATUS_HEAP_CORRUPTION`。

**注意**：这与同名的「wx 测试 exe 缺 Common Controls v6 清单」不是一回事
（见 `2026-08-18-Bugfix-TestExe-Comctl32-Manifest-v1.0.md`）。本次 exe **已含清单**，
`scripts/check-test-manifests.ps1 -Dir build-ucrt/out/tests` 返回 OK（2 个 wx exe 全合格）。

## 2. 证据

1. 直接运行 exe，使用**继承的**环境 PATH：

   ```powershell
   $env:PATH = "<ucrt wx bin>;" + $env:PATH      # 只加 wx，不加编译器 bin
   & build-ucrt\out\tests\test_log_statistics_event.exe
   # exit = -1073741511  (0xC0000139)
   ```

   同一 exe 在 PATH 里补上 `E:\mingw64\bin` 后：`--list-tests` 与正常执行均 0 ms 通过。

2. 开发机 PATH 同时挂着多条工具链 bin：

   ```
   E:\clion\bin\mingw\bin                        (GCC 15.2 msvcrt)
   d:\vcpkg\installed\x64-mingw-dynamic\debug\bin (msvcrt)
   E:\mingw64\bin                                 (GCC 16.2 UCRT)
   ```

3. `objdump -p build-ucrt\out\tests\test_log_statistics_event.exe` 的导入表只有
   `wxbase333ud_*` / `wxmsw333ud_core_*` / `libwinpthread-1.dll` + `api-ms-win-crt-*`；
   即 `libwinpthread-1.dll` 必须从 PATH 解析，而 PATH 上第一个命中的是 MSVCRT 树那份
   （`2026-11-17-Bugfix-Win7-UCRT-libwinpthread-v1.0.md` 已记录该 DLL 在各工具链间版本不同），
   入口点与 GCC 16.2 UCRT 编出的 exe 不匹配 → `0xC0000139`。

4. `0xC0000374` 同理：`ProxyListModelTest`（exe 在 `bin/`）从 PATH 捡到 **msvcrt 的 wx DLL**
   配 UCRT exe → wx 内部跨 CRT 堆操作 → 堆损坏。

## 3. 根因

测试 exe 所在目录（`build-ucrt/out/tests`、`bin/`）**只放 exe 与少数依赖 DLL**，
MinGW 运行时（`libwinpthread-1.dll` 等）与 wx DLL 全部依赖 PATH 解析；而开发机 PATH
是多工具链混合的，命中顺序与「本次构建使用的工具链」无关：

- 同一个 `ctest` 里，msvcrt 与 ucrt 两套 wx/运行时 DLL 都可能被不同测试进程捡到；
- 症状取决于命中的是哪一份：入口点不匹配 → `0xC0000139`（弹模态框，卡住套件）；
  跨 CRT 边界 → `0xC0000374`；GUI 起不来 → UI 用例 15 s 超时。

## 4. 修复

`CMakeLists.txt`（`# -- 运行时 DLL 的 PATH 前置（所有测试）`）为**每个** ctest 条目追加
`ENVIRONMENT_MODIFICATION`，把本次构建自己的两段路径前置到测试子进程 PATH：

```cmake
get_filename_component(_vp_cc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
foreach(_vp_test IN LISTS _vp_all_tests)
    set_property(TEST ${_vp_test} APPEND PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${_vp_cc_bin_native}")     # 编译器 bin（运行时 DLL）
endforeach()
if(wxWidgets_FOUND)
    foreach(_vp_test IN LISTS _vp_all_tests)
        set_property(TEST ${_vp_test} APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_vp_wx_bin}")         # 本工具链 wx bin
    endforeach()
endif()
```

要点：

- 用 `ENVIRONMENT_MODIFICATION` 的 `path_list_prepend`，**不要**塞进 `ENVIRONMENT`
  列表项（PATH 自身含 `;`，会被切开）；
- 覆盖所有测试而非只覆盖 `UI_*`：`ProxyListModel` / `LogStatisticsEvent` /
  `UnifiedMonitorRows` 同样链接动态 wx；
- 不改主机 PATH、不复制 DLL 到 exe 目录，两种工具链各自自洽，互不污染。

## 5. 验证

| 构建 | 结果 |
|---|---|
| `build-ucrt`（mingw-ucrt，隔离输出） | 构建 1192/1192；`ctest` **78/78 通过**（290.14 s），含 17 个 UI 用例与 `UI_LANGUAGEMENU` |
| `build-ucrt` 单点复验 | `LogStatisticsEventTest` 由 62.05 s / `0xC0000139` → **0.24 s Passed** |
| 清单门禁 | `check-test-manifests.ps1 -Dir build-ucrt\out\tests` → OK（2 个 wx exe 均含 Common Controls v6） |
| `clion-msvcrt`（`build`） | 同一改动后 `cmake --build build --parallel 6` 成功；`ctest --test-dir build` **78/78 通过**（276.48 s） |
| 生产配置隔离 | 回归期间 `bin/config.json` 曾被某次 UI 测试运行重新序列化（一行 dump，`ui.language` 变 `en-US`），已 `git checkout` 还原；新增 `scripts/config-guard.ps1`（`-Snapshot`/`-Verify`）接入 `build-and-test.bat` 第 6 步，跑完后比对 sha256，改动即失败 |

## 6. 边界与教训

- 只有「exe 目录里没有该 DLL」时才会走 PATH；把运行时 DLL 复制到 exe 旁同样是有效修法，
  但会随输出目录增多而重复，CMake 前置 PATH 更集中。
- `ctest` 的 `TIMEOUT 600` 兜底仍然保留：任何加载器模态框都不能让套件无限挂住。
- 换工具链（`-DVALIDPROXY_TOOLCHAIN=...`）后首次跑测试前，先确认
  `CTestTestfile.cmake` 里的 `ENVIRONMENT_MODIFICATION` 指向**本次**工具链目录。
- 跨工具链产物不要混放同一 exe 目录；隔离输出（`VALIDPROXY_ISOLATE_OUTPUTS`）可避免
  两套构建写同一个 `tests/`、`bin/` 的老问题（参见 `2026-11-10-Bugfix-ConnectivityVerifyTest-CorruptPdataCrash-v1.0.md`）。
- **UI 用例对主机负载敏感**：`build-ucrt` 的一次 324 s 运行中，主机同时在向虚机复制
  153 MB 测试库，`UI_FLOATINGWIDGET`「点击展开后自动收起保持自由落点」出现 19 px / 18 px
  偏移（容差 10 px）；同一用例单独复跑 21.15 s 通过。属调度抖动，不是 DLL/PATH 回归——
  全量回归期间不要让主机跑大 I/O 任务。

## 7. 关联文档

- `docs/bugfix/2026-08-18-Bugfix-TestExe-Comctl32-Manifest-v1.0.md`（清单缺失，另一根因）
- `docs/bugfix/2026-11-13-Bugfix-GuiFlashCrash-WxToolchainMismatch-v1.0.md`（wx DLL CRT 混配）
- `docs/bugfix/2026-11-17-Bugfix-Win7-UCRT-libwinpthread-v1.0.md`（libwinpthread 版本差异）
- `docs/specs/2026-11-18-Spec-BilingualUI-v1.2.md`（本次回归所属变更）
