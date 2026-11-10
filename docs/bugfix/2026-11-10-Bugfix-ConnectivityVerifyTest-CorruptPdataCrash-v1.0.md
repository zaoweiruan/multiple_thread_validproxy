---
title: "bugfix: ConnectivityVerifyTest 因 PE 异常目录(.pdata)损坏导致 catch 不命中而崩溃"
type: bugfix
status: fixed
date: 2026-11-10
updated: 2026-11-10
---

# ConnectivityVerifyTest.SocksProxyFail 崩溃（.pdata 异常目录损坏）

## 1. 症状

全量 `ctest --test-dir build` 中 `ConnectivityVerifyTest` 稳定失败：

```
[ RUN      ] ConnectivityVerifyTest.SocksProxyOk            → OK
[ RUN      ] ConnectivityVerifyTest.SocksProxyOk_NoContentBody → OK
[ RUN      ] ConnectivityVerifyTest.SocksProxyFail
（进程直接消失，无 FAILED 行、无 gtest XML 落盘）
EXIT = 541541187 = 0x20474343
```

`0x20474343` 是 MinGW/GCC 的 C++ 异常 SEH magic（`" GCC"`），即**未捕获 C++ 异常导致进程终止**。

调试器（gdb 与 CLion xdebug 一致）栈：

```
#0  RaiseException
#1  CurlEasyHandle::perform                include/CurlEasyHandle.h:228   res = CURLE_OPERATION_TIMEDOUT
#2  UrlFetcher::fetchViaProxyStatus        src/UrlFetcher.cpp:77
#3  std::function<bool()>::operator()      src/ProxyConnectivityVerifier.cpp:23
#4  proxy::ConnectivityVerifier::verifyWithProbe
```

矛盾点：`src/UrlFetcher.cpp:79` 处就有 `catch (const std::exception&)`，但**在 catch 行下断点从不命中**（`hits=0`），只留 catch 断点时进程依旧以 `0x20474343` 终止。即"同一条调用链上的 catch 没有接住同一条调用链上的 throw"。

## 2. 排查过程中的两个错误假设（已证伪，留档避免重复）

| 假设 | 证伪证据 |
|---|---|
| **A. vcpkg `libcurl-d.a`（debug 版）有问题** | 把链接命令里的 `debug/lib/libcurl-d.a` 换成 `lib/libcurl.a` 重链后测试通过；但**改回 `libcurl-d.a` 重链同样通过**。真正的变量是"重链"本身，不是 curl 库。 |
| **B. 工具链问题（w64devkit GCC 14.2 vs CLion 内置 GCC 15.2）** | CLion 内置工具链跑同一测试"同样崩溃"，但该 target 的 `RUNTIME_OUTPUT_DIRECTORY` 被硬编码为 `${CMAKE_SOURCE_DIR}/tests`（`CMakeLists.txt:563-566`），**两套构建树写同一个 exe 文件**；CLion 侧因对象文件陈旧报 `ninja: no work to do` 未重链，实际运行的仍是 `build/` 生成的同一份损坏产物。另用最小 `throw→catch` 探针在两条工具链上均正常捕获（EXIT=0），EH 运行时本身无问题。 |

## 3. 根因（RCA）

**损坏发生在链接产物，不在源码、不在编译、不在依赖库。** 证据链：

### 3.1 同一命令、同一批对象，链接出两种结果

`build/.ninja_log` 中该 exe 有两条链接记录，命令哈希完全相同：

```
614873	619550	8158913974866174	tests/test_connectivity_verify.exe	3df0d0793c0dd085   ← 损坏版（2026-11-09 09:36:41）
146	1412	8159796527581756	tests/test_connectivity_verify.exe	3df0d0793c0dd085   ← 正常版（2026-11-10 10:07:33）
```

期间 `build/CMakeFiles/test_connectivity_verify.dir/**/*.obj` 时间戳全部为 2026-11-09 09:21:1x，**对象文件未被重编**（exe 比 obj 新，ninja 因此也不会自动重链）。即：输入完全一致，链接器输出不一致。

### 3.2 逐段字节比对：只有 `.pdata` 不同

`temp/ctv_142_backup.exe`（崩溃版）vs 重链版（两者大小完全相同，均 25,376,650 字节）：

| 段 | 差异字节 |
|---|---|
| PE 头 | 2（仅 PE checksum，偏移 `0xD8`） |
| `.text` | **0** |
| `.data` | **0** |
| `.rdata` | **0** |
| `.pdata` | **63,308** |
| `.xdata` | **0** |
| `.idata` | **0** |
| `.reloc` | **0** |
| `.debug_*` | **0** |

机器码、只读数据、**展开信息本体（`.xdata`）全部逐字节相同**，唯一实质性差异是异常目录 `.pdata`。

### 3.3 `.pdata` 内容：904 条重复 + 904 条缺失

解析 `RUNTIME_FUNCTION`（12 字节/条，共 18,718 条）：

| 指标 | 崩溃版 | 正常版 |
|---|---|---|
| 条目总数 | 18,718 | 18,718 |
| 唯一 `BeginAddress` | 17,814 | 18,718 |
| 重复 `BeginAddress` | **904**（范围 RVA `0x108A50`–`0x1B46E0`） | 0 |
| 两版共有条目的 `(EndAddress, UnwindInfo)` 不一致数 | **0** | — |
| 未排序条目 | 0 | 0 |
| `UnwindInfoAddress` 越界 | 0 | 0 |

即崩溃版把 904 条记录原样重复了一遍（重复项与原项 `(End,UnwindInfo)` 完全相同），同时**丢掉了另外 904 个函数的记录**；缺失项全部落在 RVA `0x1B5610`–`0x1C3810`（该区间符号全部是 `std::*` / `testing::*` 模板实例化）。

### 3.4 崩溃机制（推断，与证据一致）

x64 Windows 的 C++ 异常展开由 `RtlVirtualUnwind` 依据 `.pdata` 逐帧查表完成。展开链上的 `std::function<bool()>::operator()` 帧（`ProxyConnectivityVerifier.cpp:23`）正落在**丢失记录的区间**：该帧查不到 `RUNTIME_FUNCTION` 会被当作叶子函数处理，不恢复非易失寄存器/栈帧，展开链随之错乱，最终 `RtlUnwind` 找不到 `UrlFetcher::fetchViaProxyStatus` 的 handler，异常升级为未捕获异常 → `RaiseException(0x20474343)` → 进程终止。这解释了"catch 明明存在却从不命中"。

> 说明：3.1–3.3 为可直接复现的实测证据；3.4 的寄存器/栈帧级机理为与证据一致的推断，未做逐帧内存取证。

## 4. 修复

**重新链接**（对象与源码均无需改动）：

```powershell
$env:Path = "E:\w64devkit\bin;E:\CMake\bin;" + $env:Path
cmake --build build --target test_connectivity_verify
```

## 5. 验证证据

| 项 | 结果 |
|---|---|
| 单用例 `--gtest_filter=ConnectivityVerifyTest.SocksProxyFail` | 重链后连续 3 次 `EXIT=0`（崩溃版连续 3 次 `541541187`） |
| 全量 11 用例 | `[  PASSED  ] 11 tests.`（15.6 s），`FULL_EXIT=0` |
| 旧产物对照 | `temp/ctv_142_backup.exe` 仍稳定复现 `541541187`（证明差异来自产物而非环境漂移） |
| 全量 `ctest --test-dir build -V` | 见 `temp/ctest_final_1110.log` |
| 源码影响面 | `git status` 对 `src/UrlFetcher.cpp` / `src/ProxyConnectivityVerifier.cpp` / `include/CurlEasyHandle.h` / `tests/test_connectivity_verify.cpp` 全部干净；最后一次相关提交为 `a5c7069`（2026-10-23），本次任务未触及 |

## 6. 残余风险与建议

1. **链接非确定性未定位到具体 ld 缺陷**：本次只能证明"同一命令 + 同一批对象 → 两种 `.pdata`"，未定位 binutils 侧触发条件（`.pdata` 在 COMDAT/重复符号合并下的排序与去重路径）。**影响**：理论上可能再次出现同类"catch 不命中"的诡异崩溃。
2. **判别手法（建议纳入排障惯例）**：遇到"源码有 catch 却崩 0x20474343 / 0xC0000409"时，先做**逐段字节比对 + `.pdata` 条目解析**（本文 3.2/3.3 的方法），若 `.text`/`.xdata` 相同而 `.pdata` 异常，直接判定为链接产物损坏，重链即可，不要先怀疑源码、库版本或工具链。
3. **构建树产物别名（重要隐患）**：`test_connectivity_verify` 等 target 把 `RUNTIME_OUTPUT_DIRECTORY` 硬编码到 `${CMAKE_SOURCE_DIR}/tests`，导致 `build/`（w64devkit）与 `cmake-build-debug/`（CLion 内置工具链）写同一个 exe。切换工具链时必须先确认实际产物来源，否则会得出"两条工具链都崩"的错误结论（本次已踩）。后续可考虑改为按构建树区分输出目录，或在 CLion 侧禁用该 target 的 run 输出复用。
4. **陈旧产物判定**：`bin/worker/validproxy.exe` 一类的陈旧副本禁用于验证（见 `AGENTS.md` §2.3）；本次同类教训是"`ninja: no work to do` 不代表产物新鲜"，跨构建树运行时需核对 mtime 与命令哈希。

## 7. 关联

- 发现场景：spec v1.2（`docs/specs/2026-11-06-Spec-TaskQueueAdmissionAndFinishedHistory-v1.2.md`）实施后的全量 `ctest` 甄别。
- 同类既有记录：`docs/bugfix/2026-11-06-Bugfix-ValidproxyMinGWRuntimeDll-Hang-v1.0.md`（同为"构建/运行时产物"层面的问题）。
