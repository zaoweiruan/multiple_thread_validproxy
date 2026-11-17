---
title: "bugfix: Win7 启动即报丢失 api-ms-win-crt-convert-l1-1-0.dll（libwinpthread 依赖 UCRT）"
type: bugfix
status: completed
date: 2026-11-17
module: build-system
version: v1.0
---

# Win7 启动失败：丢失 api-ms-win-crt-convert-l1-1-0.dll

## 1. 症状

Windows 7 SP1（未安装 Universal CRT 更新 KB2999226 / VC++ 2015 运行库）双击运行后
弹出系统错误框：

```
validproxy.exe - 系统错误
无法启动此程序，因为计算机中丢失
api-ms-win-crt-convert-l1-1-0.dll。尝试重新安装该程序以解决此问题。
```

与前一份 `docs/bugfix/2026-11-17-Bugfix-Win7-UIEncoding-MenuAssertCrash-v1.0.md`
（乱码 / 菜单断言）**根因不同**：本次是**加载期**缺 DLL，程序根本起不来。

## 2. 证据

| 检查 | 结果 |
| --- | --- |
| `objdump -p bin\validproxy.exe` 导入表 | 仅 `msvcrt.dll` + 系统 DLL；**exe 本身不含 UCRT 依赖** |
| `objdump -p bin\libwinpthread-1.dll` 导入表 | `api-ms-win-crt-convert/heap/private/runtime/stdio/string` **→ UCRT API 集** |
| 两个工具链的 `libwinpthread-1.dll` | `E:\clion\bin\mingw`（72,032 B，sha `DD5D495F…`）与 `E:\mingw64`（56,832 B）**都导入 api-ms-win-crt-\\*** |
| `bin\validproxy-1.6.1-win64.zip` 内容 | 只有 `libwinpthread-1.dll` ⇒ 随包分发的就是这个 UCRT 版 DLL |
| 旧 dmp `lm` 段（现场） | 已加载 `api_ms_win_crt_convert_l1_1_0` / `ucrtbase` / `libwinpthread_1` ⇒ 与上述分析一致 |

> 工具链标签为 `clion-msvcrt`（编译器 `-dM` 不定义 `_UCRT`，libgcc/libstdc++ 走
> `msvcrt.dll`），但该发行版附带的 **`libwinpthread-1.dll` 是 UCRT 构建**——
> 这就是「exe 是 MSVCRT、却仍要求 UCRT」的原因。

## 3. 根因

`-static-libgcc -static-libstdc++` 只静态化了 libgcc/libstdc++，**libwinpthread 仍是
动态依赖**；而唯一的 `libwinpthread-1.dll` 需要 UCRT API 集，Win7 裸机没有这些转发 DLL。

## 4. 修复

### 4.1 失败的尝试（保留记录）

`add_link_options(-static)`（全静态）**不可用**：本工具链的 `libpthread.a` 同样是
UCRT 构建——它引用只在 `libucrt*.a` 中定义的 `__intrinsic_setjmpex`（已 `nm` 确认
该符号仅存在于 `libucrt.a`/`libucrtapp.a`/`libvcruntime*.a`），另出现
`__ms_vsnprintf` 链接序问题，实测 8 个 target 链接失败，已回滚。

### 4.2 实际方案

发布包随附 **MSVCRT 版 `libwinpthread-1.dll`**（`resources/win7/libwinpthread-1.dll`，
65,442 B，sha256 `B3AD954E…`）：导入表只有 `KERNEL32.dll` + `msvcrt.dll`，导出覆盖
exe 需要的全部 23 个符号（含 time64 三件套 `clock_gettime64` / `nanosleep64` /
`pthread_cond_timedwait64`）。

- `CMakeLists.txt`：`VALIDPROXY_STATIC_WX=ON` 时对 `validproxy` / `validproxy-cli`
  POST_BUILD 复制该 DLL（`-DVALIDPROXY_MSVCRT_WINPTHREAD_DLL` 可覆盖；缺文件 WARNING）；
  同时让既有「Copy MinGW runtime DLLs」步骤在该配置下**不再复制**工具链的 UCRT 版
  libwinpthread（否则会把刚部署的正确 DLL 覆盖回去——本次实测踩到，已修）。
- 新增部署闸门 `scripts/check-win7-deps.ps1`：扫描 exe（可选连同随包 DLL）导入表，
  命中 `api-ms-win-crt-*` / `ucrtbase` / 非系统 DLL 即返回 1。

## 5. 验收（2026-11-17 实测）

| 项 | 命令 / 方式 | 结果 |
| --- | --- | --- |
| 构建 | `cmake --build build-tc-clion-msvcrt-Release --parallel 8` | **EXIT 0**；configure 打印 `[win7] runtime: resources/win7/libwinpthread-1.dll (MSVCRT-only libwinpthread)`，POST_BUILD 打印 `Deploying MSVCRT-only libwinpthread-1.dll (Win7 SP1)` |
| 部署产物 | `Get-FileHash bin\libwinpthread-1.dll` | `B3AD954EC35305895E2370D0A1E3CA6A8203DB1D37C7C49CF5D06E72A5F0DB69`（= `resources/win7` 版本，未被 UCRT 版覆盖） |
| Win7 闸门 | `powershell -File scripts/check-win7-deps.ps1 -Path bin -IncludeDlls` | `[check-win7-deps] OK: libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll, validproxy-cli.exe, validproxy.exe`，EXIT 0 |
| GUI 冒烟 | 沙箱实例 `bin\validproxy.exe -c test\ui-sandbox\config.json`（`VALIDPROXY_ASSERT_LOG=1`） | 正常启动并进入事件循环（日志越过 `MainFrame` 构造 / `onFirstShow` / 独立代理 adoption）；`ui-assert-<pid>.log` 未生成 |
| 运行时模块 | 进程模块表 | `libwinpthread-1.dll` 来自 `bin\`；**无任何 `api-ms-win-crt-*` 模块**（`ucrtbase.dll` 由 Win10/11 系统组件加载，Win7 上系统组件不依赖它） |
| 全量回归 | `ctest --test-dir build-tc-clion-msvcrt-Release` | **74/75**（367 s，EXIT 8），唯一失败为既有 `LogStatisticsEventTest`（测试 exe 未嵌 Common Controls v6 manifest，与本改动无关；详见修复 #1 文档 §5.1） |

## 5.1 未验证项

- 真实 Win7 SP1 机器复验（需用户侧）。判定依据：exe + 唯一随包 DLL 的导入表已不含任何
  UCRT api-set，且全部系统依赖（msvcrt / comctl32 v6 / bcrypt / dbghelp / ws2_32 …）均为
  Win7 SP1 自带。

## 6. 关联

- `docs/bugfix/2026-11-17-Bugfix-Win7-UIEncoding-MenuAssertCrash-v1.0.md`（同批次 Win7 修复 #1）
- `resources/win7/README.md`（DLL 来源、SHA、替换条件）
- `docs/2026-11-16-Reference-MinGW-Toolchain-Usage-v1.0.md` §7（wx 运行时部署）
- `docs/DEV-PROCESS.md` 规则 5（禁止复制依赖 DLL）——本次仅随包一个 MSVCRT 运行时 DLL
